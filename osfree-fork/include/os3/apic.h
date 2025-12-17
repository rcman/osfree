/*
 * osFree APIC (Advanced Programmable Interrupt Controller) Support
 * Copyright (c) 2024 osFree Project
 * 
 * Local APIC and I/O APIC support for SMP interrupt handling
 */

#ifndef _OS3_APIC_H_
#define _OS3_APIC_H_

#include <os3/types.h>

/* Local APIC Register Offsets */
#define LAPIC_ID            0x020   /* Local APIC ID */
#define LAPIC_VERSION       0x030   /* Local APIC Version */
#define LAPIC_TPR           0x080   /* Task Priority Register */
#define LAPIC_APR           0x090   /* Arbitration Priority */
#define LAPIC_PPR           0x0A0   /* Processor Priority */
#define LAPIC_EOI           0x0B0   /* End of Interrupt */
#define LAPIC_RRD           0x0C0   /* Remote Read */
#define LAPIC_LDR           0x0D0   /* Logical Destination */
#define LAPIC_DFR           0x0E0   /* Destination Format */
#define LAPIC_SVR           0x0F0   /* Spurious Interrupt Vector */
#define LAPIC_ISR           0x100   /* In-Service Register (8 regs) */
#define LAPIC_TMR           0x180   /* Trigger Mode Register (8 regs) */
#define LAPIC_IRR           0x200   /* Interrupt Request Register (8 regs) */
#define LAPIC_ESR           0x280   /* Error Status Register */
#define LAPIC_ICR_LO        0x300   /* Interrupt Command Register Low */
#define LAPIC_ICR_HI        0x310   /* Interrupt Command Register High */
#define LAPIC_TIMER_LVT     0x320   /* Timer Local Vector Table */
#define LAPIC_THERMAL_LVT   0x330   /* Thermal LVT */
#define LAPIC_PERF_LVT      0x340   /* Performance Counter LVT */
#define LAPIC_LINT0_LVT     0x350   /* Local Interrupt 0 LVT */
#define LAPIC_LINT1_LVT     0x360   /* Local Interrupt 1 LVT */
#define LAPIC_ERROR_LVT     0x370   /* Error LVT */
#define LAPIC_TIMER_ICR     0x380   /* Timer Initial Count */
#define LAPIC_TIMER_CCR     0x390   /* Timer Current Count */
#define LAPIC_TIMER_DCR     0x3E0   /* Timer Divide Configuration */

/* x2APIC MSRs (when in x2APIC mode) */
#define MSR_X2APIC_BASE     0x800
#define MSR_X2APIC_ID       0x802
#define MSR_X2APIC_VERSION  0x803
#define MSR_X2APIC_TPR      0x808
#define MSR_X2APIC_PPR      0x80A
#define MSR_X2APIC_EOI      0x80B
#define MSR_X2APIC_LDR      0x80D
#define MSR_X2APIC_SVR      0x80F
#define MSR_X2APIC_ISR0     0x810
#define MSR_X2APIC_TMR0     0x818
#define MSR_X2APIC_IRR0     0x820
#define MSR_X2APIC_ESR      0x828
#define MSR_X2APIC_ICR      0x830
#define MSR_X2APIC_TIMER    0x832
#define MSR_X2APIC_TIMER_ICR 0x838
#define MSR_X2APIC_TIMER_CCR 0x839
#define MSR_X2APIC_TIMER_DCR 0x83E

/* APIC Base MSR */
#define MSR_APIC_BASE       0x1B
#define APIC_BASE_BSP       (1 << 8)    /* Bootstrap Processor */
#define APIC_BASE_X2APIC    (1 << 10)   /* x2APIC Enable */
#define APIC_BASE_ENABLE    (1 << 11)   /* APIC Global Enable */
#define APIC_BASE_ADDR_MASK 0xFFFFF000

/* SVR Register bits */
#define APIC_SVR_ENABLE     (1 << 8)
#define APIC_SVR_FOCUS      (1 << 9)
#define APIC_SVR_EOI_BC     (1 << 12)

/* LVT bits */
#define APIC_LVT_MASKED     (1 << 16)
#define APIC_LVT_LEVEL      (1 << 15)
#define APIC_LVT_REMOTE_IRR (1 << 14)
#define APIC_LVT_ACTIVE_LOW (1 << 13)
#define APIC_LVT_PENDING    (1 << 12)

/* Timer modes */
#define APIC_TIMER_ONESHOT      0
#define APIC_TIMER_PERIODIC     (1 << 17)
#define APIC_TIMER_TSC_DEADLINE (2 << 17)

/* Timer divider values */
#define APIC_TIMER_DIV_1    0x0B
#define APIC_TIMER_DIV_2    0x00
#define APIC_TIMER_DIV_4    0x01
#define APIC_TIMER_DIV_8    0x02
#define APIC_TIMER_DIV_16   0x03
#define APIC_TIMER_DIV_32   0x08
#define APIC_TIMER_DIV_64   0x09
#define APIC_TIMER_DIV_128  0x0A

/* ICR Delivery Mode */
#define APIC_DM_FIXED       (0 << 8)
#define APIC_DM_LOWEST      (1 << 8)
#define APIC_DM_SMI         (2 << 8)
#define APIC_DM_NMI         (4 << 8)
#define APIC_DM_INIT        (5 << 8)
#define APIC_DM_STARTUP     (6 << 8)

/* ICR Destination Mode */
#define APIC_DEST_PHYSICAL  (0 << 11)
#define APIC_DEST_LOGICAL   (1 << 11)

/* ICR Delivery Status */
#define APIC_DS_IDLE        (0 << 12)
#define APIC_DS_PENDING     (1 << 12)

/* ICR Level */
#define APIC_LEVEL_DEASSERT (0 << 14)
#define APIC_LEVEL_ASSERT   (1 << 14)

/* ICR Trigger Mode */
#define APIC_TM_EDGE        (0 << 15)
#define APIC_TM_LEVEL       (1 << 15)

/* ICR Destination Shorthand */
#define APIC_DEST_SELF      (1 << 18)
#define APIC_DEST_ALL       (2 << 18)
#define APIC_DEST_ALL_EX    (3 << 18)   /* All excluding self */

/* I/O APIC Registers */
#define IOAPIC_ID           0x00
#define IOAPIC_VERSION      0x01
#define IOAPIC_ARB          0x02
#define IOAPIC_REDTBL_BASE  0x10

/* I/O APIC Redirection Entry */
typedef union {
    struct {
        uint64_t vector     : 8;
        uint64_t delvmode   : 3;    /* Delivery mode */
        uint64_t destmode   : 1;    /* 0=physical, 1=logical */
        uint64_t delvstatus : 1;    /* Delivery status */
        uint64_t polarity   : 1;    /* 0=active high, 1=active low */
        uint64_t remoteirr  : 1;
        uint64_t trigger    : 1;    /* 0=edge, 1=level */
        uint64_t mask       : 1;    /* 0=enabled, 1=masked */
        uint64_t reserved   : 39;
        uint64_t dest       : 8;
    };
    uint64_t raw;
} ioapic_redir_t;

/* I/O APIC structure */
typedef struct ioapic {
    uint32_t id;
    uint32_t version;
    uint32_t max_redir;         /* Max redirection entries - 1 */
    uint32_t gsi_base;          /* Global System Interrupt base */
    volatile uint32_t *base;    /* Memory mapped base address */
} ioapic_t;

/* Maximum I/O APICs */
#define MAX_IOAPICS     8

/* Global I/O APIC array */
extern ioapic_t ioapics[MAX_IOAPICS];
extern uint32_t num_ioapics;

/* Interrupt vectors */
#define VECTOR_SPURIOUS     0xFF
#define VECTOR_ERROR        0xFE
#define VECTOR_TIMER        0xFD
#define VECTOR_THERMAL      0xFC
#define VECTOR_PERF         0xFB
#define VECTOR_IPI_RESCHED  0xFA
#define VECTOR_IPI_CALL     0xF9
#define VECTOR_IPI_TLB      0xF8
#define VECTOR_IPI_STOP     0xF7

/* External IRQ base */
#define VECTOR_IRQ_BASE     0x20

/* Local APIC access */
extern volatile uint32_t *lapic_base;
extern int x2apic_enabled;

/* Read/write Local APIC register */
static inline uint32_t lapic_read(uint32_t reg) {
    if (x2apic_enabled) {
        uint32_t lo, hi;
        __asm__ volatile("rdmsr" : "=a"(lo), "=d"(hi) 
                         : "c"(MSR_X2APIC_BASE + (reg >> 4)));
        return lo;
    }
    return lapic_base[reg >> 2];
}

static inline void lapic_write(uint32_t reg, uint32_t val) {
    if (x2apic_enabled) {
        __asm__ volatile("wrmsr" : : "a"(val), "d"(0), 
                         "c"(MSR_X2APIC_BASE + (reg >> 4)));
    } else {
        lapic_base[reg >> 2] = val;
    }
}

/* Read Local APIC ID */
static inline uint32_t apic_read_id(void) {
    uint32_t id = lapic_read(LAPIC_ID);
    if (!x2apic_enabled) {
        id >>= 24;  /* ID is in bits 24-31 for xAPIC */
    }
    return id;
}

/* Send End of Interrupt */
static inline void lapic_eoi(void) {
    lapic_write(LAPIC_EOI, 0);
}

/* I/O APIC access */
static inline uint32_t ioapic_read(ioapic_t *io, uint32_t reg) {
    io->base[0] = reg;
    return io->base[4];
}

static inline void ioapic_write(ioapic_t *io, uint32_t reg, uint32_t val) {
    io->base[0] = reg;
    io->base[4] = val;
}

/* Read/write I/O APIC redirection entry */
static inline uint64_t ioapic_read_redir(ioapic_t *io, uint32_t irq) {
    uint64_t lo = ioapic_read(io, IOAPIC_REDTBL_BASE + irq * 2);
    uint64_t hi = ioapic_read(io, IOAPIC_REDTBL_BASE + irq * 2 + 1);
    return lo | (hi << 32);
}

static inline void ioapic_write_redir(ioapic_t *io, uint32_t irq, uint64_t val) {
    ioapic_write(io, IOAPIC_REDTBL_BASE + irq * 2, (uint32_t)val);
    ioapic_write(io, IOAPIC_REDTBL_BASE + irq * 2 + 1, (uint32_t)(val >> 32));
}

/* Initialization functions */
int lapic_init(void);
int lapic_init_ap(void);          /* For Application Processors */
int ioapic_init(void);
int x2apic_init(void);

/* APIC timer */
void lapic_timer_init(uint32_t hz);
void lapic_timer_oneshot(uint64_t ns);
void lapic_timer_stop(void);
uint64_t lapic_timer_calibrate(void);

/* IPI sending */
void lapic_send_ipi(uint32_t dest_apic_id, uint32_t vector);
void lapic_send_ipi_self(uint32_t vector);
void lapic_send_ipi_all(uint32_t vector);
void lapic_send_ipi_all_excluding_self(uint32_t vector);
void lapic_send_init(uint32_t dest_apic_id);
void lapic_send_startup(uint32_t dest_apic_id, uint32_t vector);

/* Wait for IPI delivery */
void lapic_wait_ipi(void);

/* IRQ routing */
int ioapic_route_irq(uint32_t irq, uint32_t vector, uint32_t dest_cpu, 
                     int level_triggered, int active_low);
int ioapic_mask_irq(uint32_t irq);
int ioapic_unmask_irq(uint32_t irq);

/* Find I/O APIC for given GSI */
ioapic_t *ioapic_for_gsi(uint32_t gsi);

/* MSI (Message Signaled Interrupts) */
typedef struct msi_msg {
    uint32_t addr_lo;
    uint32_t addr_hi;
    uint32_t data;
} msi_msg_t;

void apic_compose_msi(msi_msg_t *msg, uint32_t dest_cpu, uint32_t vector);

#endif /* _OS3_APIC_H_ */
