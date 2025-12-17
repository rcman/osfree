/*
 * osFree Atomic Operations
 * Copyright (c) 2024 osFree Project
 * 
 * Lock-free atomic primitives for SMP synchronization
 */

#ifndef _OS3_ATOMIC_H_
#define _OS3_ATOMIC_H_

#include <os3/types.h>

/*
 * Atomic integer type
 */
typedef struct {
    volatile int32_t counter;
} atomic_t;

typedef struct {
    volatile int64_t counter;
} atomic64_t;

/* Static initializers */
#define ATOMIC_INIT(i)      { (i) }
#define ATOMIC64_INIT(i)    { (i) }

/*
 * Read atomic value
 */
static inline int32_t atomic_read(const atomic_t *v) {
    return __atomic_load_n(&v->counter, __ATOMIC_RELAXED);
}

static inline int64_t atomic64_read(const atomic64_t *v) {
    return __atomic_load_n(&v->counter, __ATOMIC_RELAXED);
}

/*
 * Set atomic value
 */
static inline void atomic_set(atomic_t *v, int32_t i) {
    __atomic_store_n(&v->counter, i, __ATOMIC_RELAXED);
}

static inline void atomic64_set(atomic64_t *v, int64_t i) {
    __atomic_store_n(&v->counter, i, __ATOMIC_RELAXED);
}

/*
 * Add to atomic value
 */
static inline void atomic_add(int32_t i, atomic_t *v) {
    __atomic_fetch_add(&v->counter, i, __ATOMIC_RELAXED);
}

static inline void atomic64_add(int64_t i, atomic64_t *v) {
    __atomic_fetch_add(&v->counter, i, __ATOMIC_RELAXED);
}

/*
 * Subtract from atomic value
 */
static inline void atomic_sub(int32_t i, atomic_t *v) {
    __atomic_fetch_sub(&v->counter, i, __ATOMIC_RELAXED);
}

static inline void atomic64_sub(int64_t i, atomic64_t *v) {
    __atomic_fetch_sub(&v->counter, i, __ATOMIC_RELAXED);
}

/*
 * Increment atomic value
 */
static inline void atomic_inc(atomic_t *v) {
    __atomic_fetch_add(&v->counter, 1, __ATOMIC_RELAXED);
}

static inline void atomic64_inc(atomic64_t *v) {
    __atomic_fetch_add(&v->counter, 1, __ATOMIC_RELAXED);
}

/*
 * Decrement atomic value
 */
static inline void atomic_dec(atomic_t *v) {
    __atomic_fetch_sub(&v->counter, 1, __ATOMIC_RELAXED);
}

static inline void atomic64_dec(atomic64_t *v) {
    __atomic_fetch_sub(&v->counter, 1, __ATOMIC_RELAXED);
}

/*
 * Add and return new value
 */
static inline int32_t atomic_add_return(int32_t i, atomic_t *v) {
    return __atomic_add_fetch(&v->counter, i, __ATOMIC_SEQ_CST);
}

static inline int64_t atomic64_add_return(int64_t i, atomic64_t *v) {
    return __atomic_add_fetch(&v->counter, i, __ATOMIC_SEQ_CST);
}

/*
 * Subtract and return new value
 */
static inline int32_t atomic_sub_return(int32_t i, atomic_t *v) {
    return __atomic_sub_fetch(&v->counter, i, __ATOMIC_SEQ_CST);
}

static inline int64_t atomic64_sub_return(int64_t i, atomic64_t *v) {
    return __atomic_sub_fetch(&v->counter, i, __ATOMIC_SEQ_CST);
}

/*
 * Increment and return new value
 */
static inline int32_t atomic_inc_return(atomic_t *v) {
    return atomic_add_return(1, v);
}

static inline int64_t atomic64_inc_return(atomic64_t *v) {
    return atomic64_add_return(1, v);
}

/*
 * Decrement and return new value
 */
static inline int32_t atomic_dec_return(atomic_t *v) {
    return atomic_sub_return(1, v);
}

static inline int64_t atomic64_dec_return(atomic64_t *v) {
    return atomic64_sub_return(1, v);
}

/*
 * Decrement and test if zero
 */
static inline int atomic_dec_and_test(atomic_t *v) {
    return atomic_dec_return(v) == 0;
}

static inline int atomic64_dec_and_test(atomic64_t *v) {
    return atomic64_dec_return(v) == 0;
}

/*
 * Increment and test if zero
 */
static inline int atomic_inc_and_test(atomic_t *v) {
    return atomic_inc_return(v) == 0;
}

/*
 * Add and test if negative
 */
static inline int atomic_add_negative(int32_t i, atomic_t *v) {
    return atomic_add_return(i, v) < 0;
}

/*
 * Compare and exchange
 * Returns old value
 */
static inline int32_t atomic_cmpxchg(atomic_t *v, int32_t old, int32_t new) {
    int32_t prev = old;
    __atomic_compare_exchange_n(&v->counter, &prev, new, 0,
                                __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
    return prev;
}

static inline int64_t atomic64_cmpxchg(atomic64_t *v, int64_t old, int64_t new) {
    int64_t prev = old;
    __atomic_compare_exchange_n(&v->counter, &prev, new, 0,
                                __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
    return prev;
}

/*
 * Exchange (swap) value
 * Returns old value
 */
static inline int32_t atomic_xchg(atomic_t *v, int32_t new) {
    return __atomic_exchange_n(&v->counter, new, __ATOMIC_SEQ_CST);
}

static inline int64_t atomic64_xchg(atomic64_t *v, int64_t new) {
    return __atomic_exchange_n(&v->counter, new, __ATOMIC_SEQ_CST);
}

/*
 * Fetch and OR
 */
static inline int32_t atomic_fetch_or(int32_t i, atomic_t *v) {
    return __atomic_fetch_or(&v->counter, i, __ATOMIC_SEQ_CST);
}

static inline int64_t atomic64_fetch_or(int64_t i, atomic64_t *v) {
    return __atomic_fetch_or(&v->counter, i, __ATOMIC_SEQ_CST);
}

/*
 * Fetch and AND
 */
static inline int32_t atomic_fetch_and(int32_t i, atomic_t *v) {
    return __atomic_fetch_and(&v->counter, i, __ATOMIC_SEQ_CST);
}

static inline int64_t atomic64_fetch_and(int64_t i, atomic64_t *v) {
    return __atomic_fetch_and(&v->counter, i, __ATOMIC_SEQ_CST);
}

/*
 * Fetch and XOR
 */
static inline int32_t atomic_fetch_xor(int32_t i, atomic_t *v) {
    return __atomic_fetch_xor(&v->counter, i, __ATOMIC_SEQ_CST);
}

/*
 * Try to increment if not zero
 * Returns true if incremented
 */
static inline int atomic_inc_not_zero(atomic_t *v) {
    int32_t c = atomic_read(v);
    while (c != 0) {
        int32_t old = atomic_cmpxchg(v, c, c + 1);
        if (old == c)
            return 1;
        c = old;
    }
    return 0;
}

/*
 * Atomic pointer operations
 */
static inline void *atomic_read_ptr(void * volatile *ptr) {
    return __atomic_load_n(ptr, __ATOMIC_RELAXED);
}

static inline void atomic_set_ptr(void * volatile *ptr, void *val) {
    __atomic_store_n(ptr, val, __ATOMIC_RELAXED);
}

static inline void *atomic_xchg_ptr(void * volatile *ptr, void *new) {
    return __atomic_exchange_n(ptr, new, __ATOMIC_SEQ_CST);
}

static inline void *atomic_cmpxchg_ptr(void * volatile *ptr, 
                                        void *old, void *new) {
    void *prev = old;
    __atomic_compare_exchange_n(ptr, &prev, new, 0,
                                __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
    return prev;
}

/*
 * Atomic bit operations
 */
static inline void atomic_set_bit(int nr, volatile unsigned long *addr) {
    unsigned long mask = 1UL << (nr % (sizeof(unsigned long) * 8));
    __atomic_fetch_or(&addr[nr / (sizeof(unsigned long) * 8)], 
                      mask, __ATOMIC_SEQ_CST);
}

static inline void atomic_clear_bit(int nr, volatile unsigned long *addr) {
    unsigned long mask = 1UL << (nr % (sizeof(unsigned long) * 8));
    __atomic_fetch_and(&addr[nr / (sizeof(unsigned long) * 8)], 
                       ~mask, __ATOMIC_SEQ_CST);
}

static inline int atomic_test_bit(int nr, const volatile unsigned long *addr) {
    unsigned long mask = 1UL << (nr % (sizeof(unsigned long) * 8));
    return (__atomic_load_n(&addr[nr / (sizeof(unsigned long) * 8)], 
                            __ATOMIC_RELAXED) & mask) != 0;
}

static inline int atomic_test_and_set_bit(int nr, volatile unsigned long *addr) {
    unsigned long mask = 1UL << (nr % (sizeof(unsigned long) * 8));
    unsigned long old = __atomic_fetch_or(
        &addr[nr / (sizeof(unsigned long) * 8)], mask, __ATOMIC_SEQ_CST);
    return (old & mask) != 0;
}

static inline int atomic_test_and_clear_bit(int nr, volatile unsigned long *addr) {
    unsigned long mask = 1UL << (nr % (sizeof(unsigned long) * 8));
    unsigned long old = __atomic_fetch_and(
        &addr[nr / (sizeof(unsigned long) * 8)], ~mask, __ATOMIC_SEQ_CST);
    return (old & mask) != 0;
}

#endif /* _OS3_ATOMIC_H_ */
