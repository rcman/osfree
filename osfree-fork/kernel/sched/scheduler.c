/*
 * osFree SMP Scheduler Implementation
 * Copyright (c) 2024 osFree Project
 * 
 * Per-CPU run queues with O(1) scheduling and load balancing
 */

#include <os3/scheduler.h>
#include <os3/smp.h>
#include <os3/spinlock.h>
#include <os3/memory.h>
#include <os3/time.h>
#include <os3/debug.h>

/* Global scheduler instance */
scheduler_t scheduler;

/* Per-CPU preemption counter */
static __percpu int preempt_counter;

/*
 * Bitmap operations for O(1) priority lookup
 */
static inline int find_first_bit(uint32_t word) {
    if (word == 0) return -1;
#if defined(__GNUC__)
    return __builtin_ffs(word) - 1;
#else
    int pos = 0;
    if (!(word & 0xFFFF)) { pos += 16; word >>= 16; }
    if (!(word & 0xFF)) { pos += 8; word >>= 8; }
    if (!(word & 0xF)) { pos += 4; word >>= 4; }
    if (!(word & 0x3)) { pos += 2; word >>= 2; }
    if (!(word & 0x1)) pos += 1;
    return pos;
#endif
}

/*
 * Initialize a single run queue
 */
static void init_run_queue(run_queue_t *rq, uint32_t cpu_id) {
    int c, p;
    
    spin_lock_init(&rq->lock);
    rq->cpu_id = cpu_id;
    rq->nr_running = 0;
    rq->nr_switches = 0;
    rq->load = 0;
    rq->class_bitmap = 0;
    rq->clock = 0;
    rq->tick_count = 0;
    
    /* Initialize all priority queues */
    for (c = 0; c < NUM_SCHED_CLASSES; c++) {
        rq->active_bitmap[c] = 0;
        for (p = 0; p < PRIO_LEVELS_PER_CLASS; p++) {
            INIT_LIST_HEAD(&rq->queues[c][p].queue);
            rq->queues[c][p].count = 0;
        }
    }
    
    rq->current = NULL;
    rq->idle = NULL;
    rq->last_balance = 0;
}

/*
 * Initialize scheduler subsystem
 */
int sched_init(void) {
    uint32_t i;
    
    spin_lock_init(&scheduler.global_lock);
    atomic_set(&scheduler.total_threads, 0);
    atomic_set(&scheduler.need_balance, 0);
    
    scheduler.balance_interval = LOAD_BALANCE_INTERVAL;
    scheduler.rt_period_us = 1000000;   /* 1 second */
    scheduler.rt_runtime_us = 950000;   /* 95% max RT */
    
    /* Initialize run queue pointers */
    for (i = 0; i < MAX_CPUS; i++) {
        scheduler.runqueues[i] = NULL;
    }
    
    return 0;
}

/*
 * Initialize scheduler for a specific CPU
 */
int sched_init_cpu(uint32_t cpu_id) {
    run_queue_t *rq;
    thread_t *idle;
    
    if (cpu_id >= MAX_CPUS) return -1;
    
    /* Allocate run queue (NUMA-aware if possible) */
    rq = kmalloc_node(sizeof(run_queue_t), cpu_to_node(cpu_id));
    if (!rq) return -1;
    
    init_run_queue(rq, cpu_id);
    
    /* Create idle thread for this CPU */
    idle = thread_create(NULL, idle_thread_func, NULL, 
                         THREAD_FLAG_KERNEL | THREAD_FLAG_IDLE);
    if (!idle) {
        kfree(rq);
        return -1;
    }
    
    idle->sched_class = SCHED_CLASS_IDLE;
    idle->base_priority = 0;
    idle->dynamic_priority = 0;
    idle->cpu_affinity = (1ULL << cpu_id);  /* Bound to this CPU */
    idle->flags |= THREAD_FLAG_BOUND;
    idle->preferred_cpu = cpu_id;
    
    rq->idle = idle;
    scheduler.runqueues[cpu_id] = rq;
    
    /* Link to CPU info */
    smp_info.cpus[cpu_id]->runqueue = rq;
    smp_info.cpus[cpu_id]->idle_thread = idle;
    
    return 0;
}

/*
 * Find highest priority runnable thread
 */
static thread_t *pick_next_thread(run_queue_t *rq) {
    int c, p;
    prio_queue_t *pq;
    thread_t *next;
    
    if (rq->nr_running == 0) {
        return rq->idle;
    }
    
    /* Find highest priority class with threads */
    for (c = NUM_SCHED_CLASSES - 1; c >= 0; c--) {
        if (!(rq->class_bitmap & (1 << c)))
            continue;
            
        /* Find highest priority within class */
        p = find_first_bit(rq->active_bitmap[c]);
        if (p < 0) continue;
        
        /* Scan from highest priority down */
        for (; p < PRIO_LEVELS_PER_CLASS; p++) {
            if (!(rq->active_bitmap[c] & (1 << (31 - p))))
                continue;
                
            pq = &rq->queues[c][31 - p];
            if (!list_empty(&pq->queue)) {
                next = list_first_entry(&pq->queue, thread_t, run_list);
                return next;
            }
        }
    }
    
    return rq->idle;
}

/*
 * Add thread to run queue
 */
void enqueue_thread(thread_t *thread) {
    run_queue_t *rq;
    prio_queue_t *pq;
    uint32_t cpu;
    uint8_t c, p;
    irqflags_t flags;
    
    /* Select CPU for this thread */
    cpu = thread->preferred_cpu;
    if (!(thread->cpu_affinity & (1ULL << cpu))) {
        /* Find first allowed CPU */
        for (cpu = 0; cpu < smp_info.cpu_count; cpu++) {
            if (thread->cpu_affinity & (1ULL << cpu))
                break;
        }
    }
    
    rq = scheduler.runqueues[cpu];
    c = thread->sched_class;
    p = thread->dynamic_priority % PRIO_LEVELS_PER_CLASS;
    pq = &rq->queues[c][p];
    
    spin_lock_irqsave(&rq->lock, &flags);
    
    list_add_tail(&thread->run_list, &pq->queue);
    pq->count++;
    rq->nr_running++;
    
    /* Update bitmaps */
    rq->active_bitmap[c] |= (1 << (31 - p));
    rq->class_bitmap |= (1 << c);
    
    thread->state = THREAD_STATE_READY;
    
    spin_unlock_irqrestore(&rq->lock, flags);
    
    /* Check if we should preempt current thread */
    if (rq->current && 
        thread->dynamic_priority > rq->current->dynamic_priority) {
        set_need_resched();
        if (cpu != smp_processor_id()) {
            smp_send_ipi(cpu, IPI_RESCHEDULE);
        }
    }
}

/*
 * Remove thread from run queue
 */
void dequeue_thread(thread_t *thread) {
    run_queue_t *rq;
    prio_queue_t *pq;
    uint8_t c, p;
    irqflags_t flags;
    
    rq = scheduler.runqueues[thread->last_cpu];
    c = thread->sched_class;
    p = thread->dynamic_priority % PRIO_LEVELS_PER_CLASS;
    pq = &rq->queues[c][p];
    
    spin_lock_irqsave(&rq->lock, &flags);
    
    list_del_init(&thread->run_list);
    pq->count--;
    rq->nr_running--;
    
    /* Update bitmaps if queue is empty */
    if (pq->count == 0) {
        rq->active_bitmap[c] &= ~(1 << (31 - p));
        if (rq->active_bitmap[c] == 0) {
            rq->class_bitmap &= ~(1 << c);
        }
    }
    
    spin_unlock_irqrestore(&rq->lock, flags);
}

/*
 * Main scheduler - select and switch to next thread
 */
void schedule(void) {
    run_queue_t *rq;
    thread_t *prev, *next;
    uint32_t cpu;
    irqflags_t flags;
    uint64_t now;
    
    /* Cannot schedule with preemption disabled (except for blocking) */
    if (preempt_count() > 0 && current_thread()->state == THREAD_STATE_RUNNING) {
        return;
    }
    
    cpu = smp_processor_id();
    rq = scheduler.runqueues[cpu];
    
    spin_lock_irqsave(&rq->lock, &flags);
    
    now = get_time_ns();
    rq->clock = now;
    
    prev = rq->current;
    
    /* Clear reschedule flag */
    if (prev) {
        prev->flags &= ~THREAD_FLAG_NEED_RESCHED;
        
        /* Account time */
        if (prev->last_run) {
            prev->total_runtime += now - prev->last_run;
        }
        
        /* Re-enqueue if still runnable */
        if (prev->state == THREAD_STATE_RUNNING) {
            prev->state = THREAD_STATE_READY;
            uint8_t c = prev->sched_class;
            uint8_t p = prev->dynamic_priority % PRIO_LEVELS_PER_CLASS;
            prio_queue_t *pq = &rq->queues[c][p];
            
            list_add_tail(&prev->run_list, &pq->queue);
            pq->count++;
            rq->nr_running++;
            rq->active_bitmap[c] |= (1 << (31 - p));
            rq->class_bitmap |= (1 << c);
        }
    }
    
    /* Pick next thread */
    next = pick_next_thread(rq);
    
    /* Dequeue next if not idle */
    if (next != rq->idle && next->state == THREAD_STATE_READY) {
        uint8_t c = next->sched_class;
        uint8_t p = next->dynamic_priority % PRIO_LEVELS_PER_CLASS;
        prio_queue_t *pq = &rq->queues[c][p];
        
        list_del_init(&next->run_list);
        pq->count--;
        rq->nr_running--;
        
        if (pq->count == 0) {
            rq->active_bitmap[c] &= ~(1 << (31 - p));
            if (rq->active_bitmap[c] == 0) {
                rq->class_bitmap &= ~(1 << c);
            }
        }
    }
    
    next->state = THREAD_STATE_RUNNING;
    next->last_run = now;
    next->last_cpu = cpu;
    next->timeslice = next->timeslice_max;
    
    rq->current = next;
    get_cpu_info()->current_thread = next;
    
    if (prev != next) {
        rq->nr_switches++;
        next->context_switches++;
        
        if (prev && prev->state == THREAD_STATE_RUNNING) {
            prev->involuntary_switches++;
        } else if (prev) {
            prev->voluntary_switches++;
        }
        
        spin_unlock_irqrestore(&rq->lock, flags);
        
        /* Actual context switch */
        context_switch(prev, next);
    } else {
        spin_unlock_irqrestore(&rq->lock, flags);
    }
}

/*
 * Timer tick handler
 */
void sched_tick(void) {
    run_queue_t *rq;
    thread_t *curr;
    uint32_t cpu;
    
    cpu = smp_processor_id();
    rq = scheduler.runqueues[cpu];
    curr = rq->current;
    
    if (!curr || curr == rq->idle)
        return;
    
    rq->tick_count++;
    rq->clock = get_time_ns();
    
    /* Decrement time slice */
    if (curr->timeslice > 0) {
        curr->timeslice--;
    }
    
    /* Decay priority boost */
    if (curr->boost_ticks > 0) {
        curr->boost_ticks--;
        if (curr->boost_ticks == 0) {
            curr->priority_boost = 0;
            curr->dynamic_priority = curr->base_priority;
        }
    }
    
    /* Time slice expired - need reschedule */
    if (curr->timeslice == 0) {
        set_need_resched();
    }
    
    /* Periodic load balancing check */
    if ((rq->tick_count % scheduler.balance_interval) == 0) {
        atomic_set(&scheduler.need_balance, 1);
    }
}

/*
 * Voluntary yield
 */
void sched_yield(void) {
    thread_t *curr = current_thread();
    
    /* Reset timeslice to trigger immediate reschedule */
    curr->timeslice = 0;
    curr->voluntary_switches++;
    
    schedule();
}

/*
 * Load balancing - pull threads from busy CPUs
 */
void trigger_load_balance(void) {
    uint32_t cpu, busiest_cpu, this_cpu;
    uint64_t max_load, this_load, load;
    run_queue_t *busiest_rq, *this_rq;
    thread_t *thread;
    int moved = 0;
    irqflags_t flags;
    
    this_cpu = smp_processor_id();
    this_rq = scheduler.runqueues[this_cpu];
    this_load = this_rq->nr_running;
    
    /* Find busiest CPU */
    max_load = this_load;
    busiest_cpu = this_cpu;
    
    for (cpu = 0; cpu < smp_info.cpu_count; cpu++) {
        if (cpu == this_cpu) continue;
        if (!cpu_isset(cpu, smp_info.online_mask)) continue;
        
        load = scheduler.runqueues[cpu]->nr_running;
        if (load > max_load + 1) {  /* Imbalance threshold */
            max_load = load;
            busiest_cpu = cpu;
        }
    }
    
    if (busiest_cpu == this_cpu)
        return;
    
    busiest_rq = scheduler.runqueues[busiest_cpu];
    
    /* Try to pull threads */
    spin_lock_irqsave(&busiest_rq->lock, &flags);
    
    /* Find a migratable thread */
    for (int c = 0; c < NUM_SCHED_CLASSES && !moved; c++) {
        for (int p = 0; p < PRIO_LEVELS_PER_CLASS && !moved; p++) {
            prio_queue_t *pq = &busiest_rq->queues[c][p];
            
            list_for_each_entry(thread, &pq->queue, run_list) {
                /* Check if thread can run on this CPU */
                if (!(thread->cpu_affinity & (1ULL << this_cpu)))
                    continue;
                /* Don't migrate bound threads */
                if (thread->flags & THREAD_FLAG_BOUND)
                    continue;
                /* Don't migrate threads that just ran on that CPU */
                if (get_time_ns() - thread->last_run < 1000000) /* 1ms */
                    continue;
                
                /* Found one - migrate it */
                list_del_init(&thread->run_list);
                pq->count--;
                busiest_rq->nr_running--;
                
                if (pq->count == 0) {
                    busiest_rq->active_bitmap[c] &= ~(1 << (31 - p));
                }
                
                thread->preferred_cpu = this_cpu;
                thread->flags |= THREAD_FLAG_MIGRATING;
                
                spin_unlock_irqrestore(&busiest_rq->lock, flags);
                
                /* Enqueue on this CPU */
                enqueue_thread(thread);
                thread->flags &= ~THREAD_FLAG_MIGRATING;
                
                moved = 1;
                break;
            }
        }
    }
    
    if (!moved) {
        spin_unlock_irqrestore(&busiest_rq->lock, flags);
    }
}

/*
 * Idle balance - called when CPU goes idle
 */
void idle_balance(uint32_t cpu_id) {
    /* More aggressive balancing when idle */
    trigger_load_balance();
}

/*
 * Thread blocking
 */
void thread_block(void *channel) {
    thread_t *curr = current_thread();
    
    preempt_disable();
    
    curr->state = THREAD_STATE_BLOCKED;
    curr->wait_channel = channel;
    curr->voluntary_switches++;
    
    preempt_enable();
    
    schedule();
}

/*
 * Wake threads waiting on channel
 */
void thread_wake(void *channel) {
    uint32_t cpu;
    run_queue_t *rq;
    /* In practice, you'd have a hash table of wait channels */
    /* This is simplified - real implementation needs wait queues */
    
    for (cpu = 0; cpu < smp_info.cpu_count; cpu++) {
        /* Search for blocked threads on this channel */
        /* ... implementation depends on wait queue structure ... */
    }
}

/*
 * Preemption control
 */
void preempt_disable(void) {
    preempt_counter++;
    barrier();
}

void preempt_enable(void) {
    barrier();
    preempt_counter--;
    
    if (preempt_counter == 0 && need_resched()) {
        schedule();
    }
}

int preempt_count(void) {
    return preempt_counter;
}

/*
 * Set thread CPU affinity
 */
int set_thread_affinity(thread_t *thread, uint64_t mask) {
    irqflags_t flags;
    
    /* Validate mask has at least one online CPU */
    int valid = 0;
    for (uint32_t i = 0; i < smp_info.cpu_count; i++) {
        if ((mask & (1ULL << i)) && cpu_isset(i, smp_info.online_mask)) {
            valid = 1;
            break;
        }
    }
    if (!valid) return -1;
    
    spin_lock_irqsave(&scheduler.global_lock, &flags);
    thread->cpu_affinity = mask;
    
    /* Check if current CPU is still valid */
    if (!(mask & (1ULL << thread->last_cpu))) {
        /* Need to migrate */
        if (thread->state == THREAD_STATE_READY) {
            dequeue_thread(thread);
            thread->preferred_cpu = find_first_bit(mask & 0xFFFFFFFF);
            enqueue_thread(thread);
        }
    }
    
    spin_unlock_irqrestore(&scheduler.global_lock, flags);
    return 0;
}

/*
 * Priority boost for interactive threads
 */
void boost_thread_priority(thread_t *thread, int8_t boost, uint8_t duration) {
    thread->priority_boost = boost;
    thread->boost_ticks = duration;
    thread->dynamic_priority = thread->base_priority + boost;
    
    if (thread->dynamic_priority > MAX_PRIORITY)
        thread->dynamic_priority = MAX_PRIORITY;
    if (thread->dynamic_priority < MIN_PRIORITY)
        thread->dynamic_priority = MIN_PRIORITY;
}
