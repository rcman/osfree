/*
 * osFree SMP (Symmetric Multi-Processing) Support Header
 * Copyright (c) 2024 osFree Project
 * 
 * Multi-core CPU support for modern hardware
 */

#ifndef _OS3_SMP_H_
#define _OS3_SMP_H_

#include <os3/types.h>
#include <os3/atomic.h>

/* Maximum supported CPUs */
#define MAX_CPUS            256
#define MAX_NUMA_NODES      64

/* CPU States */
#define CPU_STATE_OFFLINE   0
#define CPU_STATE_STARTING  1
#define CPU_STATE_ONLINE    2
#define CPU_STATE_HALTED    3

/* IPI (Inter-Processor Interrupt) Types */
#define IPI_RESCHEDULE      0x01
#define IPI_TLB_FLUSH       0x02
#define IPI_CALL_FUNC       0x03
#define IPI_STOP            0x04
#define IPI_NMI             0x05

/* CPU Feature Flags */
#define CPU_FEATURE_FPU     (1 << 0)
#define CPU_FEATURE_SSE     (1 << 1)
#define CPU_FEATURE_SSE2    (1 << 2)
#define CPU_FEATURE_SSE3    (1 << 3)
#define CPU_FEATURE_SSSE3   (1 << 4)
#define CPU_FEATURE_SSE4_1  (1 << 5)
#define CPU_FEATURE_SSE4_2  (1 << 6)
#define CPU_FEATURE_AVX     (1 << 7)
#define CPU_FEATURE_AVX2    (1 << 8)
#define CPU_FEATURE_AVX512  (1 << 9)
#define CPU_FEATURE_AES     (1 << 10)
#define CPU_FEATURE_XSAVE   (1 << 11)
#define CPU_FEATURE_RDRAND  (1 << 12)
#define CPU_FEATURE_INVARIANT_TSC (1 << 13)
#define CPU_FEATURE_X2APIC  (1 << 14)
#define CPU_FEATURE_PCID    (1 << 15)
#define CPU_FEATURE_INVPCID (1 << 16)

/* Cache line size for alignment */
#define CACHE_LINE_SIZE     64

/* Per-CPU alignment macro */
#define __percpu __attribute__((section(".percpu")))
#define PERCPU_ALIGNED __attribute__((aligned(CACHE_LINE_SIZE)))

/* Forward declarations */
struct cpu_info;
struct numa_node;

/*
 * Per-CPU data structure
 * Aligned to cache line to prevent false sharing
 */
typedef struct cpu_info {
    /* Identification */
    uint32_t cpu_id;            /* Logical CPU ID */
    uint32_t apic_id;           /* Local APIC ID */
    uint32_t acpi_id;           /* ACPI Processor ID */
    uint8_t  numa_node;         /* NUMA node this CPU belongs to */
    uint8_t  package_id;        /* Physical package/socket */
    uint8_t  core_id;           /* Core within package */
    uint8_t  thread_id;         /* Thread within core (SMT) */
    
    /* State */
    volatile uint32_t state;    /* CPU_STATE_* */
    volatile uint32_t flags;
    
    /* Features */
    uint64_t features;          /* CPU_FEATURE_* flags */
    
    /* Frequency info (in kHz) */
    uint32_t base_freq;
    uint32_t max_freq;
    uint32_t current_freq;
    
    /* Statistics */
    uint64_t idle_time;
    uint64_t busy_time;
    uint64_t irq_count;
    uint64_t context_switches;
    
    /* Current execution context */
    struct thread *current_thread;
    struct thread *idle_thread;
    
    /* Scheduler run queue (per-CPU) */
    struct run_queue *runqueue;
    
    /* Local APIC info */
    void *lapic_base;
    uint32_t lapic_timer_freq;
    
    /* TSC calibration */
    uint64_t tsc_freq;
    uint64_t tsc_offset;
    
    /* Padding to cache line */
    uint8_t _pad[CACHE_LINE_SIZE - 
        ((sizeof(uint32_t)*7 + sizeof(uint8_t)*4 + sizeof(uint64_t)*7 +
          sizeof(void*)*4) % CACHE_LINE_SIZE)];
          
} PERCPU_ALIGNED cpu_info_t;

/*
 * NUMA Node information
 */
typedef struct numa_node {
    uint32_t node_id;
    uint32_t cpu_count;
    uint64_t mem_start;
    uint64_t mem_size;
    uint64_t mem_free;
    uint32_t cpu_mask[MAX_CPUS / 32];
    uint8_t  distance[MAX_NUMA_NODES];  /* Distance to other nodes */
} numa_node_t;

/*
 * SMP System information
 */
typedef struct smp_info {
    uint32_t cpu_count;             /* Total online CPUs */
    uint32_t cpu_possible;          /* Total possible CPUs */
    uint32_t numa_nodes;            /* Number of NUMA nodes */
    uint32_t bsp_id;                /* Bootstrap processor ID */
    
    cpu_info_t *cpus[MAX_CPUS];     /* Per-CPU info pointers */
    numa_node_t *nodes[MAX_NUMA_NODES];
    
    /* CPU masks */
    volatile uint32_t online_mask[MAX_CPUS / 32];
    volatile uint32_t active_mask[MAX_CPUS / 32];
    
    /* Synchronization */
    atomic_t startup_count;
    atomic_t ready_count;
    
    /* IPI function call */
    void (*ipi_func)(void *arg);
    void *ipi_arg;
    atomic_t ipi_pending;
    
} smp_info_t;

/* Global SMP info structure */
extern smp_info_t smp_info;

/* Get current CPU ID (fast path using GS segment or APIC) */
static inline uint32_t smp_processor_id(void) {
    uint32_t id;
#if defined(__x86_64__)
    __asm__ volatile("movl %%gs:0, %0" : "=r"(id));
#elif defined(__i386__)
    __asm__ volatile("movl %%fs:0, %0" : "=r"(id));
#else
    /* Fallback: read from APIC ID */
    id = apic_read_id();
#endif
    return id;
}

/* Get per-CPU data pointer */
static inline cpu_info_t *get_cpu_info(void) {
    return smp_info.cpus[smp_processor_id()];
}

/* Check if we're on BSP */
static inline int smp_is_bsp(void) {
    return smp_processor_id() == smp_info.bsp_id;
}

/* CPU mask operations */
static inline void cpu_set(uint32_t cpu, uint32_t *mask) {
    mask[cpu / 32] |= (1U << (cpu % 32));
}

static inline void cpu_clear(uint32_t cpu, uint32_t *mask) {
    mask[cpu / 32] &= ~(1U << (cpu % 32));
}

static inline int cpu_isset(uint32_t cpu, const uint32_t *mask) {
    return (mask[cpu / 32] & (1U << (cpu % 32))) != 0;
}

/* SMP initialization functions */
int smp_init(void);
int smp_boot_cpu(uint32_t cpu_id);
void smp_halt_cpu(uint32_t cpu_id);
int smp_online_cpu(uint32_t cpu_id);
int smp_offline_cpu(uint32_t cpu_id);

/* Inter-Processor Interrupts */
void smp_send_ipi(uint32_t cpu_id, uint32_t ipi_type);
void smp_send_ipi_all(uint32_t ipi_type);
void smp_send_ipi_others(uint32_t ipi_type);
void smp_send_ipi_mask(const uint32_t *mask, uint32_t ipi_type);

/* Cross-CPU function calls */
typedef void (*smp_call_func_t)(void *arg);
int smp_call_function(smp_call_func_t func, void *arg, int wait);
int smp_call_function_single(uint32_t cpu, smp_call_func_t func, 
                             void *arg, int wait);

/* TLB management */
void smp_flush_tlb_all(void);
void smp_flush_tlb_page(void *addr);
void smp_flush_tlb_range(void *start, void *end);

/* CPU hotplug notifiers */
typedef int (*cpu_hotplug_callback_t)(uint32_t cpu_id, int online);
int register_cpu_hotplug_callback(cpu_hotplug_callback_t cb);
void unregister_cpu_hotplug_callback(cpu_hotplug_callback_t cb);

/* CPU topology helpers */
int cpu_to_node(uint32_t cpu_id);
int cpu_sibling_mask(uint32_t cpu_id, uint32_t *mask);
int cpu_core_mask(uint32_t cpu_id, uint32_t *mask);

/* Frequency scaling */
int cpu_set_frequency(uint32_t cpu_id, uint32_t freq_khz);
uint32_t cpu_get_frequency(uint32_t cpu_id);

#endif /* _OS3_SMP_H_ */
