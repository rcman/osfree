/*
 * osFree ACPI (Advanced Configuration and Power Interface) Support
 * Copyright (c) 2024 osFree Project
 * 
 * ACPI table parsing for hardware discovery and configuration
 */

#ifndef _OS3_ACPI_H_
#define _OS3_ACPI_H_

#include <os3/types.h>

/* ACPI Table Signatures */
#define ACPI_SIG_RSDP   "RSD PTR "
#define ACPI_SIG_RSDT   "RSDT"
#define ACPI_SIG_XSDT   "XSDT"
#define ACPI_SIG_MADT   "APIC"
#define ACPI_SIG_FADT   "FACP"
#define ACPI_SIG_HPET   "HPET"
#define ACPI_SIG_MCFG   "MCFG"
#define ACPI_SIG_SRAT   "SRAT"
#define ACPI_SIG_SLIT   "SLIT"
#define ACPI_SIG_DSDT   "DSDT"
#define ACPI_SIG_SSDT   "SSDT"
#define ACPI_SIG_DMAR   "DMAR"

#pragma pack(push, 1)

/*
 * RSDP - Root System Description Pointer
 */
typedef struct acpi_rsdp {
    char signature[8];          /* "RSD PTR " */
    uint8_t checksum;
    char oem_id[6];
    uint8_t revision;           /* 0=ACPI 1.0, 2=ACPI 2.0+ */
    uint32_t rsdt_addr;         /* Physical address of RSDT */
    /* ACPI 2.0+ fields */
    uint32_t length;
    uint64_t xsdt_addr;         /* Physical address of XSDT */
    uint8_t ext_checksum;
    uint8_t reserved[3];
} acpi_rsdp_t;

/*
 * Generic ACPI Table Header
 */
typedef struct acpi_header {
    char signature[4];
    uint32_t length;
    uint8_t revision;
    uint8_t checksum;
    char oem_id[6];
    char oem_table_id[8];
    uint32_t oem_revision;
    uint32_t creator_id;
    uint32_t creator_revision;
} acpi_header_t;

/*
 * RSDT - Root System Description Table
 */
typedef struct acpi_rsdt {
    acpi_header_t header;
    uint32_t tables[];          /* Array of 32-bit table addresses */
} acpi_rsdt_t;

/*
 * XSDT - Extended System Description Table (ACPI 2.0+)
 */
typedef struct acpi_xsdt {
    acpi_header_t header;
    uint64_t tables[];          /* Array of 64-bit table addresses */
} acpi_xsdt_t;

/*
 * MADT - Multiple APIC Description Table
 */
typedef struct acpi_madt {
    acpi_header_t header;
    uint32_t lapic_addr;        /* Local APIC address */
    uint32_t flags;             /* Bit 0: PCAT_COMPAT */
    /* Followed by variable-length entries */
} acpi_madt_t;

/* MADT Entry Types */
#define MADT_TYPE_LAPIC         0
#define MADT_TYPE_IOAPIC        1
#define MADT_TYPE_INT_OVERRIDE  2
#define MADT_TYPE_NMI_SOURCE    3
#define MADT_TYPE_LAPIC_NMI     4
#define MADT_TYPE_LAPIC_ADDR    5
#define MADT_TYPE_IOSAPIC       6
#define MADT_TYPE_LSAPIC        7
#define MADT_TYPE_PLATFORM_INT  8
#define MADT_TYPE_X2APIC        9
#define MADT_TYPE_X2APIC_NMI    10
#define MADT_TYPE_GICC          11
#define MADT_TYPE_GICD          12

/* Generic MADT entry header */
typedef struct madt_entry_header {
    uint8_t type;
    uint8_t length;
} madt_entry_header_t;

/* Local APIC entry */
typedef struct madt_lapic {
    madt_entry_header_t header;
    uint8_t acpi_id;            /* ACPI Processor ID */
    uint8_t apic_id;            /* Local APIC ID */
    uint32_t flags;             /* Bit 0: Processor Enabled */
} madt_lapic_t;

#define MADT_LAPIC_ENABLED      (1 << 0)
#define MADT_LAPIC_ONLINE_CAP   (1 << 1)

/* x2APIC entry (for systems with >255 CPUs) */
typedef struct madt_x2apic {
    madt_entry_header_t header;
    uint16_t reserved;
    uint32_t x2apic_id;
    uint32_t flags;
    uint32_t acpi_uid;
} madt_x2apic_t;

/* I/O APIC entry */
typedef struct madt_ioapic {
    madt_entry_header_t header;
    uint8_t ioapic_id;
    uint8_t reserved;
    uint32_t address;
    uint32_t gsi_base;          /* Global System Interrupt base */
} madt_ioapic_t;

/* Interrupt Source Override */
typedef struct madt_int_override {
    madt_entry_header_t header;
    uint8_t bus;                /* Always 0 (ISA) */
    uint8_t source;             /* IRQ */
    uint32_t gsi;               /* Global System Interrupt */
    uint16_t flags;             /* MPS INTI flags */
} madt_int_override_t;

/* Local APIC NMI */
typedef struct madt_lapic_nmi {
    madt_entry_header_t header;
    uint8_t acpi_id;            /* 0xFF = all processors */
    uint16_t flags;
    uint8_t lint;               /* 0 or 1 */
} madt_lapic_nmi_t;

/*
 * FADT - Fixed ACPI Description Table
 */
typedef struct acpi_fadt {
    acpi_header_t header;
    uint32_t firmware_ctrl;
    uint32_t dsdt;
    uint8_t reserved1;
    uint8_t preferred_pm_profile;
    uint16_t sci_int;
    uint32_t smi_cmd;
    uint8_t acpi_enable;
    uint8_t acpi_disable;
    uint8_t s4bios_req;
    uint8_t pstate_cnt;
    uint32_t pm1a_evt_blk;
    uint32_t pm1b_evt_blk;
    uint32_t pm1a_cnt_blk;
    uint32_t pm1b_cnt_blk;
    uint32_t pm2_cnt_blk;
    uint32_t pm_tmr_blk;
    uint32_t gpe0_blk;
    uint32_t gpe1_blk;
    uint8_t pm1_evt_len;
    uint8_t pm1_cnt_len;
    uint8_t pm2_cnt_len;
    uint8_t pm_tmr_len;
    uint8_t gpe0_blk_len;
    uint8_t gpe1_blk_len;
    uint8_t gpe1_base;
    uint8_t cst_cnt;
    uint16_t p_lvl2_lat;
    uint16_t p_lvl3_lat;
    uint16_t flush_size;
    uint16_t flush_stride;
    uint8_t duty_offset;
    uint8_t duty_width;
    uint8_t day_alarm;
    uint8_t mon_alarm;
    uint8_t century;
    uint16_t boot_flags;
    uint8_t reserved2;
    uint32_t flags;
    /* Extended fields for ACPI 2.0+ continue... */
} acpi_fadt_t;

/*
 * SRAT - System Resource Affinity Table (NUMA)
 */
typedef struct acpi_srat {
    acpi_header_t header;
    uint32_t reserved1;
    uint64_t reserved2;
    /* Followed by affinity entries */
} acpi_srat_t;

/* SRAT Entry Types */
#define SRAT_TYPE_LAPIC_AFFINITY    0
#define SRAT_TYPE_MEMORY_AFFINITY   1
#define SRAT_TYPE_X2APIC_AFFINITY   2

/* Processor Local APIC Affinity */
typedef struct srat_lapic_affinity {
    madt_entry_header_t header;
    uint8_t proximity_lo;
    uint8_t apic_id;
    uint32_t flags;
    uint8_t sapic_eid;
    uint8_t proximity_hi[3];
    uint32_t clock_domain;
} srat_lapic_affinity_t;

/* Memory Affinity */
typedef struct srat_memory_affinity {
    madt_entry_header_t header;
    uint32_t proximity;
    uint16_t reserved1;
    uint64_t base_addr;
    uint64_t length;
    uint32_t reserved2;
    uint32_t flags;
    uint64_t reserved3;
} srat_memory_affinity_t;

#define SRAT_MEM_ENABLED    (1 << 0)
#define SRAT_MEM_HOTPLUG    (1 << 1)
#define SRAT_MEM_NONVOL     (1 << 2)

/*
 * SLIT - System Locality Information Table (NUMA distances)
 */
typedef struct acpi_slit {
    acpi_header_t header;
    uint64_t num_localities;
    uint8_t distances[];        /* num_localities^2 entries */
} acpi_slit_t;

/*
 * HPET - High Precision Event Timer
 */
typedef struct acpi_hpet {
    acpi_header_t header;
    uint32_t event_timer_block_id;
    uint8_t address_space_id;
    uint8_t register_bit_width;
    uint8_t register_bit_offset;
    uint8_t reserved;
    uint64_t base_address;
    uint8_t hpet_number;
    uint16_t min_tick;
    uint8_t page_protection;
} acpi_hpet_t;

/*
 * MCFG - PCI Express Memory Mapped Configuration
 */
typedef struct acpi_mcfg {
    acpi_header_t header;
    uint64_t reserved;
    /* Followed by configuration entries */
} acpi_mcfg_t;

typedef struct mcfg_entry {
    uint64_t base_addr;
    uint16_t segment;
    uint8_t start_bus;
    uint8_t end_bus;
    uint32_t reserved;
} mcfg_entry_t;

#pragma pack(pop)

/* ACPI parsed data structures */
typedef struct acpi_cpu_info {
    uint32_t apic_id;
    uint32_t acpi_id;
    uint32_t flags;
    uint8_t numa_node;
} acpi_cpu_info_t;

typedef struct acpi_ioapic_info {
    uint32_t id;
    uint32_t address;
    uint32_t gsi_base;
} acpi_ioapic_info_t;

typedef struct acpi_int_override {
    uint8_t bus;
    uint8_t source_irq;
    uint32_t gsi;
    uint16_t flags;
} acpi_int_override_t;

/* Global ACPI data */
typedef struct acpi_info {
    /* Table pointers */
    acpi_rsdp_t *rsdp;
    acpi_xsdt_t *xsdt;
    acpi_rsdt_t *rsdt;
    acpi_madt_t *madt;
    acpi_fadt_t *fadt;
    acpi_srat_t *srat;
    acpi_slit_t *slit;
    acpi_hpet_t *hpet;
    acpi_mcfg_t *mcfg;
    
    /* Parsed CPU info */
    acpi_cpu_info_t cpus[256];
    uint32_t num_cpus;
    
    /* Parsed I/O APIC info */
    acpi_ioapic_info_t ioapics[8];
    uint32_t num_ioapics;
    
    /* Interrupt overrides */
    acpi_int_override_t overrides[32];
    uint32_t num_overrides;
    
    /* Local APIC address */
    uint64_t lapic_addr;
    
    /* NUMA info */
    uint32_t numa_nodes;
    
    /* ACPI version */
    uint8_t revision;
    
} acpi_info_t;

extern acpi_info_t acpi_info;

/* Initialization */
int acpi_init(void);
int acpi_early_init(void);      /* Before memory manager */

/* Table lookup */
void *acpi_find_table(const char *signature);

/* MADT parsing */
int acpi_parse_madt(void);

/* SRAT/SLIT parsing (NUMA) */
int acpi_parse_numa(void);
uint8_t acpi_get_numa_node(uint32_t apic_id);
uint8_t acpi_get_numa_distance(uint8_t node1, uint8_t node2);

/* IRQ routing */
uint32_t acpi_irq_to_gsi(uint8_t irq);
uint16_t acpi_get_irq_flags(uint8_t irq);

/* Power management */
int acpi_enable(void);
int acpi_disable(void);
void acpi_poweroff(void);
void acpi_reboot(void);

/* Table checksum validation */
int acpi_validate_checksum(void *table, uint32_t length);

#endif /* _OS3_ACPI_H_ */
