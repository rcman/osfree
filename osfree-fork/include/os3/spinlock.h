/*
 * osFree Spinlock Implementation
 * Copyright (c) 2024 osFree Project
 * 
 * SMP-safe locking primitives with ticket locks for fairness
 */

#ifndef _OS3_SPINLOCK_H_
#define _OS3_SPINLOCK_H_

#include <os3/types.h>
#include <os3/atomic.h>
#include <os3/smp.h>

/* Spinlock debugging */
#ifdef CONFIG_DEBUG_SPINLOCK
#define SPINLOCK_DEBUG 1
#else
#define SPINLOCK_DEBUG 0
#endif

/*
 * Ticket spinlock - provides fairness (FIFO ordering)
 * Prevents starvation that can occur with simple test-and-set locks
 */
typedef struct spinlock {
    union {
        uint32_t head_tail;
        struct {
            uint16_t head;  /* Next ticket to be served */
            uint16_t tail;  /* Next ticket to be issued */
        };
    };
#if SPINLOCK_DEBUG
    const char *name;
    uint32_t owner_cpu;
    void *lock_addr;
    uint64_t lock_time;
#endif
} spinlock_t;

/* Static initializer */
#if SPINLOCK_DEBUG
#define SPINLOCK_INIT(n) { .head_tail = 0, .name = n, .owner_cpu = -1 }
#define DEFINE_SPINLOCK(x) spinlock_t x = SPINLOCK_INIT(#x)
#else
#define SPINLOCK_INIT(n) { .head_tail = 0 }
#define DEFINE_SPINLOCK(x) spinlock_t x = SPINLOCK_INIT(#x)
#endif

/* Initialize a spinlock dynamically */
static inline void spin_lock_init(spinlock_t *lock) {
    lock->head_tail = 0;
#if SPINLOCK_DEBUG
    lock->name = "dynamic";
    lock->owner_cpu = -1;
    lock->lock_addr = NULL;
    lock->lock_time = 0;
#endif
}

/* CPU pause instruction for spin loops */
static inline void cpu_relax(void) {
#if defined(__x86_64__) || defined(__i386__)
    __asm__ volatile("pause" ::: "memory");
#elif defined(__aarch64__)
    __asm__ volatile("yield" ::: "memory");
#else
    __asm__ volatile("" ::: "memory");
#endif
}

/* Memory barriers */
static inline void mb(void) {
    __asm__ volatile("mfence" ::: "memory");
}

static inline void rmb(void) {
    __asm__ volatile("lfence" ::: "memory");
}

static inline void wmb(void) {
    __asm__ volatile("sfence" ::: "memory");
}

/* Compiler barrier */
#define barrier() __asm__ volatile("" ::: "memory")

/*
 * Acquire spinlock (ticket lock algorithm)
 */
static inline void spin_lock(spinlock_t *lock) {
    uint16_t ticket;
    
    /* Atomically fetch and increment tail to get our ticket */
    ticket = __atomic_fetch_add(&lock->tail, 1, __ATOMIC_RELAXED);
    
    /* Spin until our ticket is served */
    while (__atomic_load_n(&lock->head, __ATOMIC_ACQUIRE) != ticket) {
        cpu_relax();
    }
    
#if SPINLOCK_DEBUG
    lock->owner_cpu = smp_processor_id();
    lock->lock_addr = __builtin_return_address(0);
#endif
}

/*
 * Release spinlock
 */
static inline void spin_unlock(spinlock_t *lock) {
#if SPINLOCK_DEBUG
    lock->owner_cpu = -1;
    lock->lock_addr = NULL;
#endif
    /* Increment head to serve next ticket */
    __atomic_fetch_add(&lock->head, 1, __ATOMIC_RELEASE);
}

/*
 * Try to acquire spinlock (non-blocking)
 * Returns: 1 if lock acquired, 0 if not
 */
static inline int spin_trylock(spinlock_t *lock) {
    uint32_t old, new;
    
    old = __atomic_load_n(&lock->head_tail, __ATOMIC_RELAXED);
    
    /* Check if lock is free (head == tail) */
    if ((old >> 16) != (old & 0xFFFF)) {
        return 0;
    }
    
    new = old + 0x10000;  /* Increment tail */
    
    if (__atomic_compare_exchange_n(&lock->head_tail, &old, new,
                                     0, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED)) {
#if SPINLOCK_DEBUG
        lock->owner_cpu = smp_processor_id();
#endif
        return 1;
    }
    
    return 0;
}

/*
 * Check if spinlock is held
 */
static inline int spin_is_locked(spinlock_t *lock) {
    uint32_t val = __atomic_load_n(&lock->head_tail, __ATOMIC_RELAXED);
    return (val >> 16) != (val & 0xFFFF);
}

/*
 * Spinlock with IRQ disable
 * Use these when lock may be taken in interrupt context
 */
typedef unsigned long irqflags_t;

static inline irqflags_t local_irq_save(void) {
    irqflags_t flags;
#if defined(__x86_64__) || defined(__i386__)
    __asm__ volatile(
        "pushf\n\t"
        "pop %0\n\t"
        "cli"
        : "=r"(flags)
        :
        : "memory"
    );
#endif
    return flags;
}

static inline void local_irq_restore(irqflags_t flags) {
#if defined(__x86_64__) || defined(__i386__)
    __asm__ volatile(
        "push %0\n\t"
        "popf"
        :
        : "r"(flags)
        : "memory", "cc"
    );
#endif
}

static inline void local_irq_disable(void) {
#if defined(__x86_64__) || defined(__i386__)
    __asm__ volatile("cli" ::: "memory");
#endif
}

static inline void local_irq_enable(void) {
#if defined(__x86_64__) || defined(__i386__)
    __asm__ volatile("sti" ::: "memory");
#endif
}

static inline void spin_lock_irqsave(spinlock_t *lock, irqflags_t *flags) {
    *flags = local_irq_save();
    spin_lock(lock);
}

static inline void spin_unlock_irqrestore(spinlock_t *lock, irqflags_t flags) {
    spin_unlock(lock);
    local_irq_restore(flags);
}

static inline void spin_lock_irq(spinlock_t *lock) {
    local_irq_disable();
    spin_lock(lock);
}

static inline void spin_unlock_irq(spinlock_t *lock) {
    spin_unlock(lock);
    local_irq_enable();
}

/*
 * Read-Write Spinlock
 * Allows multiple readers or single writer
 */
typedef struct rwlock {
    atomic_t count;     /* 0 = unlocked, >0 = readers, -1 = writer */
    spinlock_t wait;    /* Serializes writers */
#if SPINLOCK_DEBUG
    const char *name;
#endif
} rwlock_t;

#if SPINLOCK_DEBUG
#define RWLOCK_INIT(n) { .count = ATOMIC_INIT(0), \
                         .wait = SPINLOCK_INIT(n), .name = n }
#else
#define RWLOCK_INIT(n) { .count = ATOMIC_INIT(0), .wait = SPINLOCK_INIT(n) }
#endif
#define DEFINE_RWLOCK(x) rwlock_t x = RWLOCK_INIT(#x)

static inline void rwlock_init(rwlock_t *rw) {
    atomic_set(&rw->count, 0);
    spin_lock_init(&rw->wait);
}

static inline void read_lock(rwlock_t *rw) {
    while (1) {
        int count = atomic_read(&rw->count);
        if (count >= 0) {
            if (atomic_cmpxchg(&rw->count, count, count + 1) == count)
                break;
        }
        cpu_relax();
    }
}

static inline void read_unlock(rwlock_t *rw) {
    atomic_dec(&rw->count);
}

static inline void write_lock(rwlock_t *rw) {
    spin_lock(&rw->wait);
    while (atomic_cmpxchg(&rw->count, 0, -1) != 0) {
        cpu_relax();
    }
}

static inline void write_unlock(rwlock_t *rw) {
    atomic_set(&rw->count, 0);
    spin_unlock(&rw->wait);
}

static inline int read_trylock(rwlock_t *rw) {
    int count = atomic_read(&rw->count);
    if (count >= 0 && atomic_cmpxchg(&rw->count, count, count + 1) == count)
        return 1;
    return 0;
}

static inline int write_trylock(rwlock_t *rw) {
    if (spin_trylock(&rw->wait)) {
        if (atomic_cmpxchg(&rw->count, 0, -1) == 0)
            return 1;
        spin_unlock(&rw->wait);
    }
    return 0;
}

/*
 * Sequence lock for read-mostly data
 * Writers don't block readers; readers detect concurrent writes
 */
typedef struct seqlock {
    uint32_t sequence;
    spinlock_t lock;
} seqlock_t;

#define SEQLOCK_INIT(n) { .sequence = 0, .lock = SPINLOCK_INIT(n) }
#define DEFINE_SEQLOCK(x) seqlock_t x = SEQLOCK_INIT(#x)

static inline void seqlock_init(seqlock_t *sl) {
    sl->sequence = 0;
    spin_lock_init(&sl->lock);
}

static inline uint32_t read_seqbegin(const seqlock_t *sl) {
    uint32_t seq;
    do {
        seq = __atomic_load_n(&sl->sequence, __ATOMIC_ACQUIRE);
    } while (seq & 1);  /* Wait if writer is active */
    return seq;
}

static inline int read_seqretry(const seqlock_t *sl, uint32_t start) {
    rmb();
    return sl->sequence != start;
}

static inline void write_seqlock(seqlock_t *sl) {
    spin_lock(&sl->lock);
    __atomic_fetch_add(&sl->sequence, 1, __ATOMIC_RELEASE);
    wmb();
}

static inline void write_sequnlock(seqlock_t *sl) {
    wmb();
    __atomic_fetch_add(&sl->sequence, 1, __ATOMIC_RELEASE);
    spin_unlock(&sl->lock);
}

#endif /* _OS3_SPINLOCK_H_ */
