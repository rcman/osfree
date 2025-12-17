/*
 * osFree SMP-Aware Scheduler
 * Copyright (c) 2024 osFree Project
 * 
 * Multi-queue scheduler with per-CPU run queues,
 * load balancing, and CPU affinity support
 */

#ifndef _OS3_SCHEDULER_H_
#define _OS3_SCHEDULER_H_

#include <os3/types.h>
#include <os3/smp.h>
#include <os3/spinlock.h>
#include <os3/list.h>

/* Scheduling classes (OS/2 compatible priority classes) */
#define SCHED_CLASS_IDLE        0   /* Idle time only (class 1) */
#define SCHED_CLASS_REGULAR     1   /* Regular (class 2) */
#define SCHED_CLASS_TIMECRIT    2   /* Time critical (class 3) */  
#define SCHED_CLASS_SERVER      3   /* Fixed high (class 4) */
#define SCHED_CLASS_REALTIME    4   /* Real-time (internal) */
#define NUM_SCHED_CLASSES       5

/* Priority levels within each class */
#define PRIO_LEVELS_PER_CLASS   32
#define MAX_PRIORITY            ((NUM_SCHED_CLASSES * PRIO_LEVELS_PER_CLASS) - 1)
#define MIN_PRIORITY            0

/* Default time slice in milliseconds */
#define DEFAULT_TIMESLICE_MS    31  /* OS/2 default */
#define MIN_TIMESLICE_MS        1
#define MAX_TIMESLICE_MS        1000

/* Load balancing intervals (in scheduler ticks) */
#define LOAD_BALANCE_INTERVAL   100
#define IDLE_BALANCE_INTERVAL   1

/* CPU affinity */
#define CPU_AFFINITY_ALL        ((uint64_t)-1)  /* Can run on any CPU */

/* Thread states */
#define THREAD_STATE_READY      0
#define THREAD_STATE_RUNNING    1
#define THREAD_STATE_BLOCKED    2
#define THREAD_STATE_ZOMBIE     3
#define THREAD_STATE_SUSPENDED  4

/* Thread flags */
#define THREAD_FLAG_KERNEL      (1 << 0)
#define THREAD_FLAG_IDLE        (1 << 1)
#define THREAD_FLAG_NEED_RESCHED (1 << 2)
#define THREAD_FLAG_MIGRATING   (1 << 3)
#define THREAD_FLAG_BOUND       (1 << 4)  /* Hard CPU affinity */

/* Forward declarations */
struct thread;
struct process;
struct run_queue;

/*
 * Thread structure (TCB - Thread Control Block)
 */
typedef struct thread {
    /* Linkage */
    struct list_head run_list;      /* Link in run queue */
    struct list_head thread_list;   /* Link in process thread list */
    
    /* Identity */
    uint32_t tid;                   /* Thread ID */
    struct process *process;        /* Owning process */
    char name[32];                  /* Debug name */
    
    /* Scheduling */
    uint8_t sched_class;            /* SCHED_CLASS_* */
    uint8_t base_priority;          /* Base priority within class */
    uint8_t dynamic_priority;       /* Current priority (with boost) */
    uint8_t state;                  /* THREAD_STATE_* */
    uint32_t flags;                 /* THREAD_FLAG_* */
    
    /* Time accounting */
    uint32_t timeslice;             /* Remaining time slice (ticks) */
    uint32_t timeslice_max;         /* Maximum time slice */
    uint64_t total_runtime;         /* Total CPU time (ns) */
    uint64_t last_run;              /* Last time scheduled (ns) */
    uint64_t wait_time;             /* Time spent waiting */
    
    /* CPU affinity */
    uint64_t cpu_affinity;          /* Bitmask of allowed CPUs */
    uint32_t last_cpu;              /* Last CPU this ran on */
    uint32_t preferred_cpu;         /* Preferred CPU (cache hot) */
    
    /* Priority boost tracking */
    int8_t priority_boost;          /* Current boost value */
    uint8_t boost_ticks;            /* Ticks until boost expires */
    
    /* Sleep/wake */
    uint64_t wake_time;             /* Absolute wake time for sleeps */
    void *wait_channel;             /* What we're waiting on */
    int wait_result;                /* Result of wait */
    
    /* Context */
    void *stack_base;               /* Kernel stack base */
    uint32_t stack_size;            /* Stack size */
    void *saved_context;            /* Saved CPU context */
    
    /* FPU/SIMD state */
    void *fpu_state;                /* FPU/SSE/AVX state */
    uint32_t fpu_flags;
    
    /* Statistics */
    uint64_t context_switches;
    uint64_t voluntary_switches;    /* Yielded/blocked */
    uint64_t involuntary_switches;  /* Preempted */
    
} thread_t;

/*
 * Priority queue within a run queue
 */
typedef struct prio_queue {
    struct list_head queue;         /* List of threads at this priority */
    uint32_t count;                 /* Number of threads */
} prio_queue_t;

/*
 * Per-CPU run queue
 */
typedef struct run_queue {
    spinlock_t lock;                /* Protects this run queue */
    uint32_t cpu_id;                /* CPU this belongs to */
    
    /* Priority queues for each scheduling class */
    prio_queue_t queues[NUM_SCHED_CLASSES][PRIO_LEVELS_PER_CLASS];
    
    /* Active priority bitmap (for O(1) highest priority lookup) */
    uint32_t active_bitmap[NUM_SCHED_CLASSES];
    uint32_t class_bitmap;          /* Which classes have threads */
    
    /* Statistics */
    uint32_t nr_running;            /* Total runnable threads */
    uint64_t nr_switches;           /* Context switches */
    uint64_t load;                  /* CPU load estimate */
    
    /* Load balancing */
    uint64_t last_balance;          /* Last balance timestamp */
    uint32_t push_cpu;              /* CPU to push tasks to */
    uint32_t pull_cpu;              /* CPU to pull tasks from */
    
    /* Current thread */
    thread_t *current;              /* Currently running */
    thread_t *idle;                 /* Idle thread for this CPU */
    
    /* Time tracking */
    uint64_t clock;                 /* Run queue clock (ns) */
    uint64_t tick_count;            /* Scheduler tick count */
    
} PERCPU_ALIGNED run_queue_t;

/*
 * Global scheduler data
 */
typedef struct scheduler {
    /* Per-CPU run queues */
    run_queue_t *runqueues[MAX_CPUS];
    
    /* Global scheduling state */
    spinlock_t global_lock;         /* For global operations */
    atomic_t total_threads;         /* System-wide thread count */
    
    /* Load balancing domains */
    uint32_t balance_interval;
    atomic_t need_balance;
    
    /* Real-time bandwidth control */
    uint32_t rt_period_us;          /* RT period (default 1s) */
    uint32_t rt_runtime_us;         /* Max RT runtime per period */
    
} scheduler_t;

/* Global scheduler instance */
extern scheduler_t scheduler;

/* Convert OS/2 priority class to internal representation */
static inline uint8_t os2_to_internal_priority(uint8_t prtyclass, uint8_t prtylevel) {
    uint8_t sched_class;
    
    switch (prtyclass) {
        case 1: sched_class = SCHED_CLASS_IDLE; break;
        case 2: sched_class = SCHED_CLASS_REGULAR; break;
        case 3: sched_class = SCHED_CLASS_TIMECRIT; break;
        case 4: sched_class = SCHED_CLASS_SERVER; break;
        default: sched_class = SCHED_CLASS_REGULAR; break;
    }
    
    /* OS/2 prtylevel is -31 to +31, map to 0-31 */
    uint8_t level = (prtylevel + 31) / 2;
    if (level > 31) level = 31;
    
    return (sched_class * PRIO_LEVELS_PER_CLASS) + level;
}

/* Scheduler initialization */
int sched_init(void);
int sched_init_cpu(uint32_t cpu_id);

/* Thread management */
thread_t *thread_create(struct process *proc, void (*entry)(void*), 
                        void *arg, uint32_t flags);
void thread_destroy(thread_t *thread);
void thread_exit(int exit_code);

/* Scheduling operations */
void schedule(void);                        /* Main scheduler entry */
void sched_tick(void);                      /* Timer tick handler */
void sched_yield(void);                     /* Voluntary yield */

/* Run queue operations */
void enqueue_thread(thread_t *thread);
void dequeue_thread(thread_t *thread);
void requeue_thread(thread_t *thread);

/* Priority management */
void set_thread_priority(thread_t *thread, uint8_t sched_class, uint8_t prio);
void boost_thread_priority(thread_t *thread, int8_t boost, uint8_t duration);

/* CPU affinity */
int set_thread_affinity(thread_t *thread, uint64_t mask);
uint64_t get_thread_affinity(thread_t *thread);
int migrate_thread(thread_t *thread, uint32_t dest_cpu);

/* Blocking/waking */
void thread_block(void *channel);
void thread_unblock(thread_t *thread);
void thread_wake(void *channel);            /* Wake all on channel */
void thread_wake_one(void *channel);        /* Wake one on channel */
int thread_sleep(uint64_t nanoseconds);
int thread_sleep_until(uint64_t abs_time);

/* Load balancing */
void trigger_load_balance(void);
void idle_balance(uint32_t cpu_id);
uint64_t calc_cpu_load(uint32_t cpu_id);

/* Preemption control */
void preempt_disable(void);
void preempt_enable(void);
int preempt_count(void);

/* Current thread access */
static inline thread_t *current_thread(void) {
    return get_cpu_info()->current_thread;
}

static inline void set_need_resched(void) {
    current_thread()->flags |= THREAD_FLAG_NEED_RESCHED;
}

static inline int need_resched(void) {
    return (current_thread()->flags & THREAD_FLAG_NEED_RESCHED) != 0;
}

/* Statistics */
void sched_get_stats(uint32_t cpu_id, uint64_t *switches, 
                     uint64_t *load, uint32_t *nr_running);

#endif /* _OS3_SCHEDULER_H_ */
