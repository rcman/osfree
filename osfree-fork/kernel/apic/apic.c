/*
 * osFree Local APIC and I/O APIC Implementation
 * Copyright (c) 2024 osFree Project
 * 
 * Interrupt controller support for SMP systems
 */

#include <os3/apic.h>
#include <os3/acpi.h>
#include <os3/smp.h>
#include <os3/memory.h>
#include <os3/io.h>
#include <os3/debug.h>

/* Global Local APIC base address */
volatile uint32_t *lapic_base;
int x2apic_enabled = 0;

/* I/O APIC array */
ioapic_t ioapics[MAX_IOAPICS];
uint32_t num_ioapics = 0;

/* APIC timer calibration value */
static uint32_t lapic_ticks_per_ms;

/*
 * Initialize Local APIC on BSP
 */
int lapic_init(void) {
    uint64_t apic_msr;
    uint32_t version;
    
    kprintf("APIC: Initializing Local APIC\n");
    
    /* Read APIC base MSR */
    __asm__ volatile("rdmsr" : "=A"(apic_msr) : "c"(MSR_APIC_BASE));
    
    /* Check if this is BSP */
    if (!(apic_msr & APIC_BASE_BSP)) {
        kprintf("APIC: Warning - not running on BSP?\n");
    }
    
    /* Try to enable x2APIC if supported */
    if (smp_info.cpus[0]->features & CPU_FEATURE_X2APIC) {
        apic_msr |= APIC_BASE_X2APIC | APIC_BASE_ENABLE;
        __asm__ volatile("wrmsr" : : "A"(apic_msr), "c"(MSR_APIC_BASE));
        x2apic_enabled = 1;
        kprintf("APIC: x2APIC mode enabled\n");
    } else {
        /* Map xAPIC registers */
        uint64_t lapic_phys = acpi_info.lapic_addr;
        if (!lapic_phys) {
            lapic_phys = apic_msr & APIC_BASE_ADDR_MASK;
        }
        
        lapic_base = (volatile uint32_t*)vmalloc_map_io(lapic_phys, 4096);
        if (!lapic_base) {
            kprintf("APIC: Failed to map Local APIC\n");
            return -1;
        }
        
        /* Enable APIC */
        apic_msr |= APIC_BASE_ENABLE;
        __asm__ volatile("wrmsr" : : "A"(apic_msr), "c"(MSR_APIC_BASE));
        
        kprintf("APIC: xAPIC mode, base at 0x%lx\n", lapic_phys);
    }
    
    /* Read version */
    version = lapic_read(LAPIC_VERSION);
    kprintf("APIC: Version 0x%x, Max LVT %d\n", 
            version & 0xFF, ((version >> 16) & 0xFF) + 1);
    
    /* Set spurious interrupt vector and enable APIC */
    lapic_write(LAPIC_SVR, APIC_SVR_ENABLE | VECTOR_SPURIOUS);
    
    /* Setup LVT entries */
    lapic_write(LAPIC_LINT0_LVT, APIC_LVT_MASKED);
    lapic_write(LAPIC_LINT1_LVT, APIC_LVT_MASKED);
    lapic_write(LAPIC_ERROR_LVT, VECTOR_ERROR);
    
    /* Clear error status */
    lapic_write(LAPIC_ESR, 0);
    lapic_write(LAPIC_ESR, 0);
    
    /* Set task priority to accept all interrupts */
    lapic_write(LAPIC_TPR, 0);
    
    /* Calibrate and setup timer */
    lapic_timer_calibrate();
    
    /* Send EOI to clear any pending interrupts */
    lapic_eoi();
    
    kprintf("APIC: Local APIC initialized, ID=%d\n", apic_read_id());
    
    return 0;
}

/*
 * Initialize Local APIC on AP
 */
int lapic_init_ap(void) {
    uint64_t apic_msr;
    
    /* Read and enable APIC */
    __asm__ volatile("rdmsr" : "=A"(apic_msr) : "c"(MSR_APIC_BASE));
    
    if (x2apic_enabled) {
        apic_msr |= APIC_BASE_X2APIC | APIC_BASE_ENABLE;
    } else {
        apic_msr |= APIC_BASE_ENABLE;
    }
    
    __asm__ volatile("wrmsr" : : "A"(apic_msr), "c"(MSR_APIC_BASE));
    
    /* Enable APIC via SVR */
    lapic_write(LAPIC_SVR, APIC_SVR_ENABLE | VECTOR_SPURIOUS);
    
    /* Setup LVT */
    lapic_write(LAPIC_LINT0_LVT, APIC_LVT_MASKED);
    lapic_write(LAPIC_LINT1_LVT, APIC_LVT_MASKED);
    lapic_write(LAPIC_ERROR_LVT, VECTOR_ERROR);
    
    /* Clear errors */
    lapic_write(LAPIC_ESR, 0);
    lapic_write(LAPIC_ESR, 0);
    
    /* Accept all interrupts */
    lapic_write(LAPIC_TPR, 0);
    
    /* Setup timer with calibrated value */
    lapic_write(LAPIC_TIMER_DCR, APIC_TIMER_DIV_16);
    lapic_write(LAPIC_TIMER_LVT, APIC_TIMER_PERIODIC | VECTOR_TIMER);
    lapic_write(LAPIC_TIMER_ICR, lapic_ticks_per_ms * 10);  /* 10ms tick */
    
    lapic_eoi();
    
    return 0;
}

/*
 * Calibrate APIC timer against PIT or HPET
 */
uint64_t lapic_timer_calibrate(void) {
    uint32_t start, end;
    
    kprintf("APIC: Calibrating timer...\n");
    
    /* Setup timer in one-shot mode with max divider */
    lapic_write(LAPIC_TIMER_DCR, APIC_TIMER_DIV_16);
    lapic_write(LAPIC_TIMER_LVT, APIC_LVT_MASKED | APIC_TIMER_ONESHOT);
    
    /* Set max initial count */
    lapic_write(LAPIC_TIMER_ICR, 0xFFFFFFFF);
    
    /* Wait 10ms using PIT */
    pit_wait_ms(10);
    
    /* Read current count */
    end = lapic_read(LAPIC_TIMER_CCR);
    
    /* Calculate ticks per ms */
    lapic_ticks_per_ms = (0xFFFFFFFF - end) / 10;
    
    kprintf("APIC: Timer calibrated: %d ticks/ms\n", lapic_ticks_per_ms);
    
    /* Now setup periodic timer for scheduler (10ms = 100Hz) */
    lapic_write(LAPIC_TIMER_LVT, APIC_TIMER_PERIODIC | VECTOR_TIMER);
    lapic_write(LAPIC_TIMER_ICR, lapic_ticks_per_ms * 10);
    
    return lapic_ticks_per_ms * 1000;  /* Return frequency */
}

/*
 * Setup one-shot timer
 */
void lapic_timer_oneshot(uint64_t ns) {
    uint32_t ticks = (ns * lapic_ticks_per_ms) / 1000000;
    
    lapic_write(LAPIC_TIMER_LVT, APIC_TIMER_ONESHOT | VECTOR_TIMER);
    lapic_write(LAPIC_TIMER_ICR, ticks);
}

/*
 * Stop APIC timer
 */
void lapic_timer_stop(void) {
    lapic_write(LAPIC_TIMER_LVT, APIC_LVT_MASKED);
    lapic_write(LAPIC_TIMER_ICR, 0);
}

/*
 * Send IPI to specific APIC ID
 */
void lapic_send_ipi(uint32_t dest_apic_id, uint32_t vector) {
    if (x2apic_enabled) {
        /* x2APIC uses MSR with 32-bit dest in upper 32 bits */
        uint64_t icr = ((uint64_t)dest_apic_id << 32) | 
                       APIC_DM_FIXED | APIC_DEST_PHYSICAL | 
                       APIC_LEVEL_ASSERT | vector;
        __asm__ volatile("wrmsr" : : "A"(icr), "c"(MSR_X2APIC_ICR));
    } else {
        /* xAPIC uses two 32-bit registers */
        lapic_write(LAPIC_ICR_HI, dest_apic_id << 24);
        lapic_write(LAPIC_ICR_LO, APIC_DM_FIXED | APIC_DEST_PHYSICAL | 
                    APIC_LEVEL_ASSERT | vector);
        lapic_wait_ipi();
    }
}

/*
 * Send INIT IPI (for AP startup)
 */
void lapic_send_init(uint32_t dest_apic_id) {
    if (x2apic_enabled) {
        uint64_t icr = ((uint64_t)dest_apic_id << 32) |
                       APIC_DM_INIT | APIC_DEST_PHYSICAL |
                       APIC_LEVEL_ASSERT | APIC_TM_LEVEL;
        __asm__ volatile("wrmsr" : : "A"(icr), "c"(MSR_X2APIC_ICR));
    } else {
        lapic_write(LAPIC_ICR_HI, dest_apic_id << 24);
        lapic_write(LAPIC_ICR_LO, APIC_DM_INIT | APIC_DEST_PHYSICAL |
                    APIC_LEVEL_ASSERT | APIC_TM_LEVEL);
        lapic_wait_ipi();
        
        /* Deassert INIT */
        lapic_write(LAPIC_ICR_LO, APIC_DM_INIT | APIC_DEST_PHYSICAL |
                    APIC_LEVEL_DEASSERT | APIC_TM_LEVEL);
        lapic_wait_ipi();
    }
}

/*
 * Send STARTUP IPI (SIPI)
 */
void lapic_send_startup(uint32_t dest_apic_id, uint32_t vector) {
    if (x2apic_enabled) {
        uint64_t icr = ((uint64_t)dest_apic_id << 32) |
                       APIC_DM_STARTUP | APIC_DEST_PHYSICAL |
                       APIC_LEVEL_ASSERT | (vector & 0xFF);
        __asm__ volatile("wrmsr" : : "A"(icr), "c"(MSR_X2APIC_ICR));
    } else {
        lapic_write(LAPIC_ICR_HI, dest_apic_id << 24);
        lapic_write(LAPIC_ICR_LO, APIC_DM_STARTUP | APIC_DEST_PHYSICAL |
                    APIC_LEVEL_ASSERT | (vector & 0xFF));
        lapic_wait_ipi();
    }
}

/*
 * Wait for IPI delivery (xAPIC only)
 */
void lapic_wait_ipi(void) {
    if (x2apic_enabled) return;
    
    /* Poll delivery status bit */
    while (lapic_read(LAPIC_ICR_LO) & APIC_DS_PENDING) {
        cpu_relax();
    }
}

/*
 * Initialize I/O APIC(s)
 */
int ioapic_init(void) {
    uint32_t i, j;
    uint32_t ver, max_redir;
    
    kprintf("IOAPIC: Initializing I/O APIC(s)\n");
    
    num_ioapics = acpi_info.num_ioapics;
    
    for (i = 0; i < num_ioapics; i++) {
        ioapic_t *io = &ioapics[i];
        
        io->id = acpi_info.ioapics[i].id;
        io->gsi_base = acpi_info.ioapics[i].gsi_base;
        
        /* Map I/O APIC registers */
        io->base = (volatile uint32_t*)vmalloc_map_io(
            acpi_info.ioapics[i].address, 4096);
        
        if (!io->base) {
            kprintf("IOAPIC: Failed to map I/O APIC %d\n", i);
            return -1;
        }
        
        /* Read version and max redirections */
        ver = ioapic_read(io, IOAPIC_VERSION);
        max_redir = (ver >> 16) & 0xFF;
        io->max_redir = max_redir;
        io->version = ver & 0xFF;
        
        kprintf("IOAPIC: ID=%d, Version=0x%x, GSI base=%d, "
                "Max redirections=%d\n",
                io->id, io->version, io->gsi_base, max_redir + 1);
        
        /* Mask all interrupts initially */
        for (j = 0; j <= max_redir; j++) {
            ioapic_redir_t redir = { .raw = 0 };
            redir.mask = 1;
            redir.vector = VECTOR_IRQ_BASE + io->gsi_base + j;
            ioapic_write_redir(io, j, redir.raw);
        }
    }
    
    /* Apply interrupt source overrides from ACPI */
    for (i = 0; i < acpi_info.num_overrides; i++) {
        acpi_int_override_t *ovr = &acpi_info.overrides[i];
        
        kprintf("IOAPIC: IRQ%d -> GSI%d (flags=0x%x)\n",
                ovr->source_irq, ovr->gsi, ovr->flags);
    }
    
    return 0;
}

/*
 * Route an IRQ through I/O APIC
 */
int ioapic_route_irq(uint32_t irq, uint32_t vector, uint32_t dest_cpu,
                     int level_triggered, int active_low) {
    ioapic_t *io;
    ioapic_redir_t redir;
    uint32_t gsi;
    
    /* Convert IRQ to GSI using overrides */
    gsi = acpi_irq_to_gsi(irq);
    
    /* Find correct I/O APIC */
    io = ioapic_for_gsi(gsi);
    if (!io) return -1;
    
    /* Setup redirection entry */
    redir.raw = 0;
    redir.vector = vector;
    redir.delvmode = 0;  /* Fixed */
    redir.destmode = 0;  /* Physical */
    redir.polarity = active_low ? 1 : 0;
    redir.trigger = level_triggered ? 1 : 0;
    redir.mask = 0;      /* Enabled */
    redir.dest = smp_info.cpus[dest_cpu]->apic_id;
    
    ioapic_write_redir(io, gsi - io->gsi_base, redir.raw);
    
    return 0;
}

/*
 * Mask an IRQ
 */
int ioapic_mask_irq(uint32_t irq) {
    ioapic_t *io;
    uint32_t gsi = acpi_irq_to_gsi(irq);
    uint64_t redir;
    
    io = ioapic_for_gsi(gsi);
    if (!io) return -1;
    
    redir = ioapic_read_redir(io, gsi - io->gsi_base);
    redir |= (1ULL << 16);  /* Set mask bit */
    ioapic_write_redir(io, gsi - io->gsi_base, redir);
    
    return 0;
}

/*
 * Unmask an IRQ
 */
int ioapic_unmask_irq(uint32_t irq) {
    ioapic_t *io;
    uint32_t gsi = acpi_irq_to_gsi(irq);
    uint64_t redir;
    
    io = ioapic_for_gsi(gsi);
    if (!io) return -1;
    
    redir = ioapic_read_redir(io, gsi - io->gsi_base);
    redir &= ~(1ULL << 16);  /* Clear mask bit */
    ioapic_write_redir(io, gsi - io->gsi_base, redir);
    
    return 0;
}

/*
 * Find I/O APIC responsible for a GSI
 */
ioapic_t *ioapic_for_gsi(uint32_t gsi) {
    uint32_t i;
    
    for (i = 0; i < num_ioapics; i++) {
        ioapic_t *io = &ioapics[i];
        if (gsi >= io->gsi_base && 
            gsi <= io->gsi_base + io->max_redir) {
            return io;
        }
    }
    
    return NULL;
}

/*
 * Timer interrupt handler
 */
void apic_timer_handler(void) {
    /* Call scheduler tick */
    sched_tick();
    
    /* Send EOI */
    lapic_eoi();
    
    /* Check if we need to reschedule */
    if (need_resched()) {
        schedule();
    }
}
