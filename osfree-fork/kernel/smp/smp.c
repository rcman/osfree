/*
 * osFree SMP Implementation
 * Copyright (c) 2024 osFree Project
 * 
 * CPU discovery, AP bootstrap, and IPI handling
 */

#include <os3/smp.h>
#include <os3/apic.h>
#include <os3/acpi.h>
#include <os3/scheduler.h>
#include <os3/memory.h>
#include <os3/spinlock.h>
#include <os3/debug.h>

/* Global SMP information */
smp_info_t smp_info;

/* AP (Application Processor) boot synchronization */
static volatile int ap_boot_cpu_id;
static volatile int ap_boot_done;
static spinlock_t ap_boot_lock = SPINLOCK_INIT("ap_boot");

/* AP trampoline code address (must be in low memory < 1MB) */
#define AP_TRAMPOLINE_ADDR  0x8000
extern char ap_trampoline_start[];
extern char ap_trampoline_end[];

/* Per-CPU GDT/IDT pointers for AP startup */
extern void setup_cpu_gdt(uint32_t cpu_id);
extern void setup_cpu_idt(void);
extern void setup_cpu_tss(uint32_t cpu_id);

/*
 * Initialize SMP subsystem
 */
int smp_init(void) {
    uint32_t i;
    int ret;
    
    kprintf("SMP: Initializing multi-processor support\n");
    
    /* Clear SMP info structure */
    memset(&smp_info, 0, sizeof(smp_info));
    atomic_set(&smp_info.startup_count, 0);
    atomic_set(&smp_info.ready_count, 0);
    
    /* Parse ACPI tables to discover CPUs */
    ret = acpi_parse_madt();
    if (ret < 0) {
        kprintf("SMP: Failed to parse ACPI MADT, falling back to UP\n");
        smp_info.cpu_count = 1;
        smp_info.cpu_possible = 1;
        return 0;
    }
    
    /* Current CPU (BSP) is always CPU 0 */
    smp_info.bsp_id = 0;
    
    /* Count and allocate CPU info structures */
    smp_info.cpu_possible = acpi_info.num_cpus;
    kprintf("SMP: Found %d processor(s)\n", smp_info.cpu_possible);
    
    /* Allocate per-CPU info for BSP */
    smp_info.cpus[0] = kmalloc(sizeof(cpu_info_t));
    if (!smp_info.cpus[0]) {
        return -1;
    }
    memset(smp_info.cpus[0], 0, sizeof(cpu_info_t));
    
    /* Initialize BSP info */
    smp_info.cpus[0]->cpu_id = 0;
    smp_info.cpus[0]->apic_id = acpi_info.cpus[0].apic_id;
    smp_info.cpus[0]->acpi_id = acpi_info.cpus[0].acpi_id;
    smp_info.cpus[0]->state = CPU_STATE_ONLINE;
    smp_info.cpus[0]->numa_node = acpi_get_numa_node(smp_info.cpus[0]->apic_id);
    
    /* Detect CPU features */
    detect_cpu_features(smp_info.cpus[0]);
    
    /* Mark BSP as online */
    cpu_set(0, smp_info.online_mask);
    cpu_set(0, smp_info.active_mask);
    smp_info.cpu_count = 1;
    
    /* Initialize BSP's Local APIC */
    ret = lapic_init();
    if (ret < 0) {
        kprintf("SMP: Failed to initialize Local APIC\n");
        return ret;
    }
    
    /* Initialize I/O APICs */
    ret = ioapic_init();
    if (ret < 0) {
        kprintf("SMP: Failed to initialize I/O APIC(s)\n");
        return ret;
    }
    
    /* Initialize BSP scheduler */
    sched_init();
    sched_init_cpu(0);
    
    /* Setup per-CPU segment (GS/FS) for fast CPU ID access */
    setup_percpu_segment(0);
    
    /* Copy AP trampoline to low memory */
    memcpy((void*)AP_TRAMPOLINE_ADDR, ap_trampoline_start,
           ap_trampoline_end - ap_trampoline_start);
    
    /* Boot Application Processors */
    for (i = 1; i < smp_info.cpu_possible; i++) {
        if (acpi_info.cpus[i].flags & MADT_LAPIC_ENABLED) {
            ret = smp_boot_cpu(i);
            if (ret == 0) {
                smp_info.cpu_count++;
            }
        }
    }
    
    kprintf("SMP: %d of %d CPUs online\n", 
            smp_info.cpu_count, smp_info.cpu_possible);
    
    return 0;
}

/*
 * Detect CPU features via CPUID
 */
static void detect_cpu_features(cpu_info_t *cpu) {
    uint32_t eax, ebx, ecx, edx;
    
    /* Basic CPUID */
    __asm__ volatile("cpuid" 
        : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) 
        : "a"(1));
    
    if (edx & (1 << 0))  cpu->features |= CPU_FEATURE_FPU;
    if (edx & (1 << 25)) cpu->features |= CPU_FEATURE_SSE;
    if (edx & (1 << 26)) cpu->features |= CPU_FEATURE_SSE2;
    if (ecx & (1 << 0))  cpu->features |= CPU_FEATURE_SSE3;
    if (ecx & (1 << 9))  cpu->features |= CPU_FEATURE_SSSE3;
    if (ecx & (1 << 19)) cpu->features |= CPU_FEATURE_SSE4_1;
    if (ecx & (1 << 20)) cpu->features |= CPU_FEATURE_SSE4_2;
    if (ecx & (1 << 25)) cpu->features |= CPU_FEATURE_AES;
    if (ecx & (1 << 26)) cpu->features |= CPU_FEATURE_XSAVE;
    if (ecx & (1 << 28)) cpu->features |= CPU_FEATURE_AVX;
    if (ecx & (1 << 30)) cpu->features |= CPU_FEATURE_RDRAND;
    if (ecx & (1 << 21)) cpu->features |= CPU_FEATURE_X2APIC;
    if (ecx & (1 << 17)) cpu->features |= CPU_FEATURE_PCID;
    
    /* Extended CPUID for AVX2, AVX512, invariant TSC */
    __asm__ volatile("cpuid"
        : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
        : "a"(7), "c"(0));
    
    if (ebx & (1 << 5))  cpu->features |= CPU_FEATURE_AVX2;
    if (ebx & (1 << 16)) cpu->features |= CPU_FEATURE_AVX512;
    if (ebx & (1 << 10)) cpu->features |= CPU_FEATURE_INVPCID;
    
    /* Check for invariant TSC */
    __asm__ volatile("cpuid"
        : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
        : "a"(0x80000007));
    
    if (edx & (1 << 8)) cpu->features |= CPU_FEATURE_INVARIANT_TSC;
    
    /* Get frequency info if available */
    __asm__ volatile("cpuid"
        : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
        : "a"(0x16));
    
    if (eax) cpu->base_freq = eax;  /* MHz */
    if (ebx) cpu->max_freq = ebx;
}

/*
 * Boot an Application Processor
 */
int smp_boot_cpu(uint32_t cpu_id) {
    cpu_info_t *cpu;
    uint32_t apic_id;
    int timeout;
    
    if (cpu_id >= MAX_CPUS || cpu_id >= smp_info.cpu_possible) {
        return -1;
    }
    
    apic_id = acpi_info.cpus[cpu_id].apic_id;
    
    kprintf("SMP: Booting CPU %d (APIC ID %d)\n", cpu_id, apic_id);
    
    /* Allocate CPU info structure */
    cpu = kmalloc_node(sizeof(cpu_info_t), 
                       acpi_get_numa_node(apic_id));
    if (!cpu) {
        return -1;
    }
    memset(cpu, 0, sizeof(cpu_info_t));
    
    cpu->cpu_id = cpu_id;
    cpu->apic_id = apic_id;
    cpu->acpi_id = acpi_info.cpus[cpu_id].acpi_id;
    cpu->state = CPU_STATE_STARTING;
    cpu->numa_node = acpi_get_numa_node(apic_id);
    
    smp_info.cpus[cpu_id] = cpu;
    
    /* Setup boot parameters for AP */
    spin_lock(&ap_boot_lock);
    ap_boot_cpu_id = cpu_id;
    ap_boot_done = 0;
    
    /* Allocate stack for this CPU */
    cpu->stack_base = kmalloc_node(KERNEL_STACK_SIZE, cpu->numa_node);
    if (!cpu->stack_base) {
        spin_unlock(&ap_boot_lock);
        kfree(cpu);
        return -1;
    }
    
    /* Setup GDT, IDT, TSS for this CPU */
    setup_cpu_gdt(cpu_id);
    setup_cpu_tss(cpu_id);
    
    /* Send INIT IPI */
    lapic_send_init(apic_id);
    
    /* Wait 10ms */
    udelay(10000);
    
    /* Send STARTUP IPI (twice, as per Intel spec) */
    lapic_send_startup(apic_id, AP_TRAMPOLINE_ADDR >> 12);
    udelay(200);
    
    lapic_send_startup(apic_id, AP_TRAMPOLINE_ADDR >> 12);
    
    /* Wait for AP to signal it's running */
    timeout = 1000;  /* 1 second timeout */
    while (!ap_boot_done && timeout > 0) {
        mdelay(1);
        timeout--;
    }
    
    spin_unlock(&ap_boot_lock);
    
    if (!ap_boot_done) {
        kprintf("SMP: CPU %d failed to start\n", cpu_id);
        cpu->state = CPU_STATE_OFFLINE;
        kfree(cpu->stack_base);
        return -1;
    }
    
    /* Mark CPU as online */
    cpu->state = CPU_STATE_ONLINE;
    cpu_set(cpu_id, smp_info.online_mask);
    cpu_set(cpu_id, smp_info.active_mask);
    
    kprintf("SMP: CPU %d online\n", cpu_id);
    
    return 0;
}

/*
 * AP entry point (called from trampoline)
 */
void ap_entry(void) {
    uint32_t cpu_id = ap_boot_cpu_id;
    cpu_info_t *cpu = smp_info.cpus[cpu_id];
    
    /* Initialize Local APIC */
    lapic_init_ap();
    
    /* Setup per-CPU segment */
    setup_percpu_segment(cpu_id);
    
    /* Detect features */
    detect_cpu_features(cpu);
    
    /* Calibrate TSC */
    cpu->tsc_freq = calibrate_tsc();
    
    /* Initialize scheduler for this CPU */
    sched_init_cpu(cpu_id);
    
    /* Signal BSP we're ready */
    atomic_inc(&smp_info.ready_count);
    ap_boot_done = 1;
    
    /* Enable interrupts and enter scheduler */
    local_irq_enable();
    
    /* This CPU is now ready to schedule threads */
    kprintf("SMP: CPU %d entering scheduler\n", cpu_id);
    
    /* Enter idle loop */
    while (1) {
        if (smp_info.cpus[cpu_id]->runqueue->nr_running > 0) {
            schedule();
        } else {
            /* Idle - try to pull work from other CPUs */
            idle_balance(cpu_id);
            
            /* Enable interrupts and halt until next interrupt */
            __asm__ volatile("sti; hlt" ::: "memory");
        }
    }
}

/*
 * Send IPI to specific CPU
 */
void smp_send_ipi(uint32_t cpu_id, uint32_t ipi_type) {
    uint32_t vector;
    
    if (cpu_id >= smp_info.cpu_count) return;
    
    switch (ipi_type) {
        case IPI_RESCHEDULE: vector = VECTOR_IPI_RESCHED; break;
        case IPI_TLB_FLUSH:  vector = VECTOR_IPI_TLB; break;
        case IPI_CALL_FUNC:  vector = VECTOR_IPI_CALL; break;
        case IPI_STOP:       vector = VECTOR_IPI_STOP; break;
        default: return;
    }
    
    lapic_send_ipi(smp_info.cpus[cpu_id]->apic_id, vector);
}

/*
 * Send IPI to all CPUs
 */
void smp_send_ipi_all(uint32_t ipi_type) {
    uint32_t i;
    for (i = 0; i < smp_info.cpu_count; i++) {
        if (cpu_isset(i, smp_info.online_mask)) {
            smp_send_ipi(i, ipi_type);
        }
    }
}

/*
 * Send IPI to all other CPUs (excluding self)
 */
void smp_send_ipi_others(uint32_t ipi_type) {
    uint32_t self = smp_processor_id();
    uint32_t i;
    
    for (i = 0; i < smp_info.cpu_count; i++) {
        if (i != self && cpu_isset(i, smp_info.online_mask)) {
            smp_send_ipi(i, ipi_type);
        }
    }
}

/*
 * IPI handler for reschedule
 */
void ipi_reschedule_handler(void) {
    set_need_resched();
    lapic_eoi();
}

/*
 * IPI handler for TLB flush
 */
void ipi_tlb_flush_handler(void) {
    /* Invalidate entire TLB */
    __asm__ volatile(
        "movq %%cr3, %%rax\n\t"
        "movq %%rax, %%cr3"
        ::: "rax", "memory"
    );
    lapic_eoi();
}

/*
 * IPI handler for cross-CPU function call
 */
void ipi_call_handler(void) {
    if (smp_info.ipi_func) {
        smp_info.ipi_func(smp_info.ipi_arg);
    }
    atomic_dec(&smp_info.ipi_pending);
    lapic_eoi();
}

/*
 * Call function on all CPUs
 */
int smp_call_function(smp_call_func_t func, void *arg, int wait) {
    irqflags_t flags;
    
    spin_lock_irqsave(&smp_info.global_lock, &flags);
    
    smp_info.ipi_func = func;
    smp_info.ipi_arg = arg;
    atomic_set(&smp_info.ipi_pending, smp_info.cpu_count - 1);
    
    smp_send_ipi_others(IPI_CALL_FUNC);
    
    /* Also call on this CPU */
    func(arg);
    
    if (wait) {
        while (atomic_read(&smp_info.ipi_pending) > 0) {
            cpu_relax();
        }
    }
    
    spin_unlock_irqrestore(&smp_info.global_lock, flags);
    
    return 0;
}

/*
 * Flush TLB on all CPUs
 */
void smp_flush_tlb_all(void) {
    /* Flush local TLB */
    __asm__ volatile(
        "movq %%cr3, %%rax\n\t"
        "movq %%rax, %%cr3"
        ::: "rax", "memory"
    );
    
    /* Send TLB flush IPI to others */
    smp_send_ipi_others(IPI_TLB_FLUSH);
}

/*
 * Get CPU's NUMA node
 */
int cpu_to_node(uint32_t cpu_id) {
    if (cpu_id < MAX_CPUS && smp_info.cpus[cpu_id]) {
        return smp_info.cpus[cpu_id]->numa_node;
    }
    return 0;
}
