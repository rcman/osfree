/*
 * osFree OS/2 Thread API Implementation (DOSCALLS)
 * Copyright (c) 2024 osFree Project
 *
 * SMP-aware implementation of OS/2 threading APIs
 */

#include <os3/doscalls.h>
#include <os3/scheduler.h>
#include <os3/smp.h>
#include <os3/process.h>
#include <os3/spinlock.h>
#include <os3/memory.h>

/*
 * DosCreateThread - Create a new thread
 *
 * OS/2 API compatible, enhanced for SMP
 */
APIRET APIENTRY DosCreateThread(PTID ptid, PFNTHREAD pfn, 
                                 ULONG param, ULONG flag, ULONG cbStack)
{
    thread_t *thread;
    process_t *proc;
    APIRET rc = NO_ERROR;
    
    if (!ptid || !pfn) {
        return ERROR_INVALID_PARAMETER;
    }
    
    if (cbStack < 4096) {
        cbStack = 4096;  /* Minimum stack size */
    }
    
    /* Round up to page boundary */
    cbStack = (cbStack + 4095) & ~4095;
    
    proc = current_process();
    
    /* Create the thread structure */
    thread = thread_create(proc, (void(*)(void*))pfn, 
                           (void*)(uintptr_t)param, 0);
    if (!thread) {
        return ERROR_NOT_ENOUGH_MEMORY;
    }
    
    /* Set stack size */
    thread->stack_size = cbStack;
    
    /* Set OS/2 compatible priority (Regular class, delta 0) */
    thread->sched_class = SCHED_CLASS_REGULAR;
    thread->base_priority = 16;  /* Middle of Regular class */
    thread->dynamic_priority = 16;
    thread->timeslice_max = DEFAULT_TIMESLICE_MS;
    thread->timeslice = thread->timeslice_max;
    
    /* SMP: Allow thread to run on any CPU by default */
    thread->cpu_affinity = CPU_AFFINITY_ALL;
    thread->preferred_cpu = smp_processor_id();  /* Start on current CPU */
    
    /* Handle creation flags */
    if (flag & CREATE_READY) {
        /* Thread starts immediately */
        thread->state = THREAD_STATE_READY;
        enqueue_thread(thread);
    } else if (flag & CREATE_SUSPENDED) {
        /* Thread starts suspended */
        thread->state = THREAD_STATE_SUSPENDED;
    }
    
    *ptid = thread->tid;
    
    return rc;
}

/*
 * DosKillThread - Terminate a thread
 */
APIRET APIENTRY DosKillThread(TID tid)
{
    thread_t *thread;
    process_t *proc = current_process();
    
    /* Find thread in current process */
    thread = find_thread_by_tid(proc, tid);
    if (!thread) {
        return ERROR_INVALID_THREADID;
    }
    
    /* Cannot kill self this way */
    if (thread == current_thread()) {
        return ERROR_INVALID_THREADID;
    }
    
    /* Mark thread for termination */
    thread->flags |= THREAD_FLAG_TERMINATING;
    
    /* If thread is blocked, wake it up */
    if (thread->state == THREAD_STATE_BLOCKED) {
        thread_unblock(thread);
    }
    
    /* If thread is on another CPU, send IPI */
    if (thread->state == THREAD_STATE_RUNNING) {
        uint32_t cpu = thread->last_cpu;
        if (cpu != smp_processor_id()) {
            smp_send_ipi(cpu, IPI_RESCHEDULE);
        }
    }
    
    return NO_ERROR;
}

/*
 * DosSuspendThread - Suspend a thread
 */
APIRET APIENTRY DosSuspendThread(TID tid)
{
    thread_t *thread;
    process_t *proc = current_process();
    irqflags_t flags;
    
    thread = find_thread_by_tid(proc, tid);
    if (!thread) {
        return ERROR_INVALID_THREADID;
    }
    
    /* Increment suspend count */
    spin_lock_irqsave(&thread->lock, &flags);
    thread->suspend_count++;
    
    if (thread->state == THREAD_STATE_READY) {
        dequeue_thread(thread);
        thread->state = THREAD_STATE_SUSPENDED;
    } else if (thread->state == THREAD_STATE_RUNNING) {
        thread->state = THREAD_STATE_SUSPENDED;
        if (thread == current_thread()) {
            spin_unlock_irqrestore(&thread->lock, flags);
            schedule();
            return NO_ERROR;
        } else {
            /* Thread running on another CPU - send IPI */
            smp_send_ipi(thread->last_cpu, IPI_RESCHEDULE);
        }
    }
    
    spin_unlock_irqrestore(&thread->lock, flags);
    return NO_ERROR;
}

/*
 * DosResumeThread - Resume a suspended thread
 */
APIRET APIENTRY DosResumeThread(TID tid)
{
    thread_t *thread;
    process_t *proc = current_process();
    irqflags_t flags;
    
    thread = find_thread_by_tid(proc, tid);
    if (!thread) {
        return ERROR_INVALID_THREADID;
    }
    
    spin_lock_irqsave(&thread->lock, &flags);
    
    if (thread->suspend_count == 0) {
        spin_unlock_irqrestore(&thread->lock, flags);
        return ERROR_NOT_FROZEN;
    }
    
    thread->suspend_count--;
    
    if (thread->suspend_count == 0 && 
        thread->state == THREAD_STATE_SUSPENDED) {
        thread->state = THREAD_STATE_READY;
        spin_unlock_irqrestore(&thread->lock, flags);
        enqueue_thread(thread);
        return NO_ERROR;
    }
    
    spin_unlock_irqrestore(&thread->lock, flags);
    return NO_ERROR;
}

/*
 * DosSetPriority - Set thread/process priority
 *
 * OS/2 Priority Classes:
 *   1 = Idle-time (PRTYC_IDLETIME)
 *   2 = Regular (PRTYC_REGULAR)
 *   3 = Time-critical (PRTYC_TIMECRITICAL)
 *   4 = Fixed-high (PRTYC_FOREGROUNDSERVER)
 */
APIRET APIENTRY DosSetPriority(ULONG scope, ULONG ulClass, 
                                LONG delta, ULONG id)
{
    thread_t *thread;
    process_t *proc;
    uint8_t sched_class;
    int8_t prio_delta;
    
    /* Validate class */
    if (ulClass > 4) {
        return ERROR_INVALID_PCLASS;
    }
    
    /* Validate delta (-31 to +31) */
    if (delta < -31 || delta > 31) {
        return ERROR_INVALID_PDELTA;
    }
    
    /* Map OS/2 class to internal class */
    switch (ulClass) {
        case 0: /* No change */
            sched_class = 0xFF;
            break;
        case PRTYC_IDLETIME:
            sched_class = SCHED_CLASS_IDLE;
            break;
        case PRTYC_REGULAR:
            sched_class = SCHED_CLASS_REGULAR;
            break;
        case PRTYC_TIMECRITICAL:
            sched_class = SCHED_CLASS_TIMECRIT;
            break;
        case PRTYC_FOREGROUNDSERVER:
            sched_class = SCHED_CLASS_SERVER;
            break;
        default:
            return ERROR_INVALID_PCLASS;
    }
    
    prio_delta = (int8_t)delta;
    
    switch (scope) {
        case PRTYS_PROCESS:
            /* Set priority for all threads in process */
            proc = id ? find_process_by_pid(id) : current_process();
            if (!proc) {
                return ERROR_INVALID_PROCID;
            }
            
            list_for_each_entry(thread, &proc->thread_list, thread_list) {
                apply_priority_change(thread, sched_class, prio_delta);
            }
            break;
            
        case PRTYS_PROCESSTREE:
            /* Set priority for process tree - simplified here */
            proc = id ? find_process_by_pid(id) : current_process();
            if (!proc) {
                return ERROR_INVALID_PROCID;
            }
            /* TODO: Recurse through child processes */
            list_for_each_entry(thread, &proc->thread_list, thread_list) {
                apply_priority_change(thread, sched_class, prio_delta);
            }
            break;
            
        case PRTYS_THREAD:
            /* Set priority for specific thread */
            proc = current_process();
            thread = find_thread_by_tid(proc, id);
            if (!thread) {
                return ERROR_INVALID_THREADID;
            }
            apply_priority_change(thread, sched_class, prio_delta);
            break;
            
        default:
            return ERROR_INVALID_SCOPE;
    }
    
    return NO_ERROR;
}

/*
 * Helper to apply priority change
 */
static void apply_priority_change(thread_t *thread, uint8_t new_class, 
                                   int8_t delta)
{
    irqflags_t flags;
    int was_queued = 0;
    
    spin_lock_irqsave(&thread->lock, &flags);
    
    /* Remove from run queue if queued */
    if (thread->state == THREAD_STATE_READY) {
        dequeue_thread(thread);
        was_queued = 1;
    }
    
    /* Apply class change */
    if (new_class != 0xFF) {
        thread->sched_class = new_class;
    }
    
    /* Apply delta to base priority */
    int new_prio = thread->base_priority + delta;
    if (new_prio < 0) new_prio = 0;
    if (new_prio > 31) new_prio = 31;
    thread->base_priority = new_prio;
    thread->dynamic_priority = os2_to_internal_priority(
        thread->sched_class + 1, new_prio - 16);
    
    spin_unlock_irqrestore(&thread->lock, flags);
    
    /* Re-queue if was queued */
    if (was_queued) {
        enqueue_thread(thread);
    }
}

/*
 * DosGetInfoBlocks - Get thread and process info blocks
 */
APIRET APIENTRY DosGetInfoBlocks(PTIB *pptib, PPIB *pppib)
{
    thread_t *thread = current_thread();
    process_t *proc = current_process();
    
    if (pptib) {
        *pptib = &thread->tib;
    }
    
    if (pppib) {
        *pppib = &proc->pib;
    }
    
    return NO_ERROR;
}

/*
 * DosSleep - Sleep for specified milliseconds
 */
APIRET APIENTRY DosSleep(ULONG msec)
{
    if (msec == 0) {
        /* Yield to other threads of same priority */
        sched_yield();
        return NO_ERROR;
    }
    
    /* Convert to nanoseconds and sleep */
    uint64_t ns = (uint64_t)msec * 1000000ULL;
    thread_sleep(ns);
    
    return NO_ERROR;
}

/*
 * DosEnterCritSec - Disable thread switching for this process
 * 
 * Note: In SMP, this only affects threads on this CPU
 * Use DosSetProcessAffinity for true single-CPU operation
 */
APIRET APIENTRY DosEnterCritSec(void)
{
    process_t *proc = current_process();
    
    spin_lock(&proc->critsec_lock);
    proc->critsec_count++;
    preempt_disable();
    
    return NO_ERROR;
}

/*
 * DosExitCritSec - Re-enable thread switching
 */
APIRET APIENTRY DosExitCritSec(void)
{
    process_t *proc = current_process();
    
    if (proc->critsec_count == 0) {
        return ERROR_CRITSEC_UNDERFLOW;
    }
    
    proc->critsec_count--;
    preempt_enable();
    spin_unlock(&proc->critsec_lock);
    
    return NO_ERROR;
}

/*
 * NEW API: DosSetThreadAffinity - Set CPU affinity (SMP extension)
 *
 * This is a new API for SMP support, not in original OS/2
 */
APIRET APIENTRY DosSetThreadAffinity(TID tid, ULONG64 affinity_mask)
{
    thread_t *thread;
    process_t *proc = current_process();
    
    thread = tid ? find_thread_by_tid(proc, tid) : current_thread();
    if (!thread) {
        return ERROR_INVALID_THREADID;
    }
    
    /* Validate mask - at least one online CPU must be set */
    ULONG64 valid_mask = 0;
    for (uint32_t i = 0; i < smp_info.cpu_count; i++) {
        if (cpu_isset(i, smp_info.online_mask)) {
            valid_mask |= (1ULL << i);
        }
    }
    
    if ((affinity_mask & valid_mask) == 0) {
        return ERROR_INVALID_PARAMETER;
    }
    
    return set_thread_affinity(thread, affinity_mask);
}

/*
 * NEW API: DosGetThreadAffinity - Get CPU affinity (SMP extension)
 */
APIRET APIENTRY DosGetThreadAffinity(TID tid, PULONG64 paffinity_mask)
{
    thread_t *thread;
    process_t *proc = current_process();
    
    if (!paffinity_mask) {
        return ERROR_INVALID_PARAMETER;
    }
    
    thread = tid ? find_thread_by_tid(proc, tid) : current_thread();
    if (!thread) {
        return ERROR_INVALID_THREADID;
    }
    
    *paffinity_mask = thread->cpu_affinity;
    return NO_ERROR;
}

/*
 * NEW API: DosQuerySysInfo extension for SMP
 *
 * QSV_NUMPROCESSORS - Number of CPUs
 * QSV_PROCESSOR_ID  - Current CPU ID
 */
APIRET APIENTRY DosQuerySysInfo(ULONG iStart, ULONG iLast,
                                 PVOID pBuf, ULONG cbBuf)
{
    PULONG pul = (PULONG)pBuf;
    ULONG i;
    
    if (!pBuf || cbBuf < (iLast - iStart + 1) * sizeof(ULONG)) {
        return ERROR_INVALID_PARAMETER;
    }
    
    for (i = iStart; i <= iLast; i++, pul++) {
        switch (i) {
            case QSV_NUMPROCESSORS:
                *pul = smp_info.cpu_count;
                break;
                
            case QSV_PROCESSOR_ID:
                *pul = smp_processor_id();
                break;
                
            case QSV_MAXPRMEM:
                *pul = 512 * 1024 * 1024;  /* 512MB - example */
                break;
                
            case QSV_MAXSHMEM:
                *pul = 256 * 1024 * 1024;  /* 256MB - example */
                break;
                
            case QSV_VERSION_MAJOR:
                *pul = 20;  /* OS/2 Warp 4 compatible */
                break;
                
            case QSV_VERSION_MINOR:
                *pul = 45;
                break;
                
            /* Add more as needed */
            default:
                *pul = 0;
                break;
        }
    }
    
    return NO_ERROR;
}
