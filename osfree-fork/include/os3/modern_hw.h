/*
 * osFree Modern Hardware Support
 * Copyright (c) 2024 osFree Project
 * 
 * Support for modern hardware features:
 * - PCIe and MSI/MSI-X
 * - UEFI
 * - NVMe
 * - USB 3.x/xHCI
 * - Modern GPU interfaces
 */

#ifndef _OS3_MODERN_HW_H_
#define _OS3_MODERN_HW_H_

#include <os3/types.h>
#include <os3/pci.h>

/*============================================================================
 * PCIe and MSI/MSI-X Support
 *============================================================================*/

/* MSI Capability structure offset */
#define PCI_CAP_MSI         0x05
#define PCI_CAP_MSIX        0x11
#define PCI_CAP_PCIE        0x10

/* MSI Message Control */
#define MSI_CTRL_ENABLE     (1 << 0)
#define MSI_CTRL_64BIT      (1 << 7)
#define MSI_CTRL_PERVEC     (1 << 8)

/* MSI-X Message Control */
#define MSIX_CTRL_ENABLE    (1 << 15)
#define MSIX_CTRL_FUNC_MASK (1 << 14)

/* MSI-X Table Entry */
typedef struct msix_entry {
    uint32_t msg_addr_lo;
    uint32_t msg_addr_hi;
    uint32_t msg_data;
    uint32_t vector_ctrl;
} msix_entry_t;

#define MSIX_ENTRY_MASKED   (1 << 0)

/* MSI/MSI-X functions */
int pci_enable_msi(pci_device_t *dev, uint32_t *vector);
int pci_enable_msix(pci_device_t *dev, msix_entry_t *entries, int count);
void pci_disable_msi(pci_device_t *dev);
void pci_disable_msix(pci_device_t *dev);
int pci_msi_supported(pci_device_t *dev);
int pci_msix_supported(pci_device_t *dev);
int pci_msix_table_size(pci_device_t *dev);

/*============================================================================
 * UEFI Support
 *============================================================================*/

/* EFI Memory Types */
#define EFI_RESERVED                    0
#define EFI_LOADER_CODE                 1
#define EFI_LOADER_DATA                 2
#define EFI_BOOT_SERVICES_CODE          3
#define EFI_BOOT_SERVICES_DATA          4
#define EFI_RUNTIME_SERVICES_CODE       5
#define EFI_RUNTIME_SERVICES_DATA       6
#define EFI_CONVENTIONAL_MEMORY         7
#define EFI_UNUSABLE_MEMORY             8
#define EFI_ACPI_RECLAIM_MEMORY         9
#define EFI_ACPI_MEMORY_NVS             10
#define EFI_MEMORY_MAPPED_IO            11
#define EFI_MEMORY_MAPPED_IO_PORT_SPACE 12
#define EFI_PAL_CODE                    13
#define EFI_PERSISTENT_MEMORY           14

/* EFI Memory Descriptor */
typedef struct efi_memory_desc {
    uint32_t type;
    uint32_t pad;
    uint64_t phys_start;
    uint64_t virt_start;
    uint64_t num_pages;
    uint64_t attribute;
} efi_memory_desc_t;

/* EFI System Table (simplified) */
typedef struct efi_system_table {
    uint64_t signature;
    uint32_t revision;
    uint32_t header_size;
    uint32_t crc32;
    uint32_t reserved;
    void *firmware_vendor;
    uint32_t firmware_revision;
    void *console_in_handle;
    void *con_in;
    void *console_out_handle;
    void *con_out;
    void *standard_error_handle;
    void *std_err;
    void *runtime_services;
    void *boot_services;
    uint64_t num_table_entries;
    void *configuration_table;
} efi_system_table_t;

/* EFI Runtime Services */
typedef struct efi_runtime_services {
    uint64_t signature;
    uint32_t revision;
    uint32_t header_size;
    uint32_t crc32;
    uint32_t reserved;
    void *get_time;
    void *set_time;
    void *get_wakeup_time;
    void *set_wakeup_time;
    void *set_virtual_address_map;
    void *convert_pointer;
    void *get_variable;
    void *get_next_variable_name;
    void *set_variable;
    void *get_next_high_monotonic_count;
    void *reset_system;
    void *update_capsule;
    void *query_capsule_capabilities;
    void *query_variable_info;
} efi_runtime_services_t;

/* UEFI boot info passed from bootloader */
typedef struct uefi_boot_info {
    efi_system_table_t *system_table;
    efi_memory_desc_t *memory_map;
    uint64_t memory_map_size;
    uint64_t descriptor_size;
    uint32_t descriptor_version;
    uint64_t framebuffer_base;
    uint32_t framebuffer_width;
    uint32_t framebuffer_height;
    uint32_t framebuffer_pitch;
    uint32_t framebuffer_bpp;
} uefi_boot_info_t;

extern uefi_boot_info_t *uefi_boot_info;

int uefi_init(uefi_boot_info_t *info);
int uefi_runtime_available(void);
void uefi_reset_system(int reset_type);

/*============================================================================
 * NVMe Support
 *============================================================================*/

/* NVMe Admin Commands */
#define NVME_ADMIN_DELETE_SQ    0x00
#define NVME_ADMIN_CREATE_SQ    0x01
#define NVME_ADMIN_GET_LOG      0x02
#define NVME_ADMIN_DELETE_CQ    0x04
#define NVME_ADMIN_CREATE_CQ    0x05
#define NVME_ADMIN_IDENTIFY     0x06
#define NVME_ADMIN_ABORT        0x08
#define NVME_ADMIN_SET_FEATURES 0x09
#define NVME_ADMIN_GET_FEATURES 0x0A

/* NVMe I/O Commands */
#define NVME_IO_FLUSH           0x00
#define NVME_IO_WRITE           0x01
#define NVME_IO_READ            0x02

/* NVMe Submission Queue Entry */
typedef struct nvme_sqe {
    uint32_t cdw0;          /* Command Dword 0 */
    uint32_t nsid;          /* Namespace ID */
    uint64_t reserved;
    uint64_t mptr;          /* Metadata Pointer */
    uint64_t prp1;          /* PRP Entry 1 */
    uint64_t prp2;          /* PRP Entry 2 */
    uint32_t cdw10;
    uint32_t cdw11;
    uint32_t cdw12;
    uint32_t cdw13;
    uint32_t cdw14;
    uint32_t cdw15;
} __attribute__((packed)) nvme_sqe_t;

/* NVMe Completion Queue Entry */
typedef struct nvme_cqe {
    uint32_t result;        /* Command specific result */
    uint32_t reserved;
    uint16_t sq_head;       /* SQ Head Pointer */
    uint16_t sq_id;         /* SQ Identifier */
    uint16_t cid;           /* Command Identifier */
    uint16_t status;        /* Status Field */
} __attribute__((packed)) nvme_cqe_t;

/* NVMe Controller structure */
typedef struct nvme_controller {
    pci_device_t *pci_dev;
    volatile uint32_t *regs;    /* Memory-mapped registers */
    
    /* Queue info */
    uint32_t queue_depth;
    uint32_t num_queues;
    
    /* Admin queue */
    nvme_sqe_t *admin_sq;
    nvme_cqe_t *admin_cq;
    uint32_t admin_sq_tail;
    uint32_t admin_cq_head;
    
    /* I/O queues (per CPU) */
    nvme_sqe_t **io_sq;
    nvme_cqe_t **io_cq;
    uint32_t *io_sq_tail;
    uint32_t *io_cq_head;
    
    /* Doorbell stride */
    uint32_t db_stride;
    
    /* Controller capabilities */
    uint64_t cap;
    uint32_t max_transfer;
    uint32_t num_namespaces;
    
} nvme_controller_t;

int nvme_init(void);
int nvme_probe(pci_device_t *dev);
int nvme_read(nvme_controller_t *ctrl, uint32_t nsid, 
              uint64_t lba, uint32_t count, void *buffer);
int nvme_write(nvme_controller_t *ctrl, uint32_t nsid,
               uint64_t lba, uint32_t count, const void *buffer);

/*============================================================================
 * xHCI (USB 3.x) Support
 *============================================================================*/

/* xHCI Capability Registers */
typedef struct xhci_cap_regs {
    uint8_t caplength;
    uint8_t reserved;
    uint16_t hciversion;
    uint32_t hcsparams1;
    uint32_t hcsparams2;
    uint32_t hcsparams3;
    uint32_t hccparams1;
    uint32_t dboff;
    uint32_t rtsoff;
    uint32_t hccparams2;
} __attribute__((packed)) xhci_cap_regs_t;

/* xHCI Operational Registers */
typedef struct xhci_op_regs {
    uint32_t usbcmd;
    uint32_t usbsts;
    uint32_t pagesize;
    uint32_t reserved1[2];
    uint32_t dnctrl;
    uint64_t crcr;
    uint32_t reserved2[4];
    uint64_t dcbaap;
    uint32_t config;
} __attribute__((packed)) xhci_op_regs_t;

/* xHCI TRB (Transfer Request Block) */
typedef struct xhci_trb {
    uint64_t param;
    uint32_t status;
    uint32_t control;
} __attribute__((packed)) xhci_trb_t;

/* TRB Types */
#define TRB_TYPE_NORMAL         1
#define TRB_TYPE_SETUP          2
#define TRB_TYPE_DATA           3
#define TRB_TYPE_STATUS         4
#define TRB_TYPE_LINK           6
#define TRB_TYPE_EVENT_DATA     7
#define TRB_TYPE_ENABLE_SLOT    9
#define TRB_TYPE_DISABLE_SLOT   10
#define TRB_TYPE_ADDRESS_DEV    11
#define TRB_TYPE_CONFIG_EP      12
#define TRB_TYPE_TRANSFER       32
#define TRB_TYPE_CMD_COMPLETE   33
#define TRB_TYPE_PORT_CHANGE    34

/* xHCI Controller structure */
typedef struct xhci_controller {
    pci_device_t *pci_dev;
    xhci_cap_regs_t *cap;
    xhci_op_regs_t *op;
    volatile uint32_t *doorbell;
    volatile uint32_t *runtime;
    
    /* Device context base array */
    uint64_t *dcbaa;
    
    /* Command ring */
    xhci_trb_t *cmd_ring;
    uint32_t cmd_ring_enq;
    uint32_t cmd_ring_cycle;
    
    /* Event ring */
    xhci_trb_t *event_ring;
    uint32_t event_ring_deq;
    uint32_t event_ring_cycle;
    
    /* Scratchpad */
    void **scratchpad;
    
    /* Port info */
    uint32_t num_ports;
    uint32_t num_slots;
    
} xhci_controller_t;

int xhci_init(void);
int xhci_probe(pci_device_t *dev);
int xhci_reset(xhci_controller_t *ctrl);
int xhci_start(xhci_controller_t *ctrl);

/*============================================================================
 * AHCI (SATA) Support
 *============================================================================*/

/* AHCI HBA Memory Registers */
typedef struct ahci_hba {
    uint32_t cap;           /* Host Capabilities */
    uint32_t ghc;           /* Global Host Control */
    uint32_t is;            /* Interrupt Status */
    uint32_t pi;            /* Ports Implemented */
    uint32_t vs;            /* Version */
    uint32_t ccc_ctl;       /* Command Completion Coalescing Control */
    uint32_t ccc_ports;
    uint32_t em_loc;        /* Enclosure Management Location */
    uint32_t em_ctl;        /* Enclosure Management Control */
    uint32_t cap2;          /* Extended Capabilities */
    uint32_t bohc;          /* BIOS/OS Handoff Control */
    uint8_t reserved[116];
    uint8_t vendor[96];
    /* Port registers at offset 0x100 */
} __attribute__((packed)) ahci_hba_t;

/* AHCI Port Registers */
typedef struct ahci_port {
    uint64_t clb;           /* Command List Base Address */
    uint64_t fb;            /* FIS Base Address */
    uint32_t is;            /* Interrupt Status */
    uint32_t ie;            /* Interrupt Enable */
    uint32_t cmd;           /* Command and Status */
    uint32_t reserved;
    uint32_t tfd;           /* Task File Data */
    uint32_t sig;           /* Signature */
    uint32_t ssts;          /* SATA Status */
    uint32_t sctl;          /* SATA Control */
    uint32_t serr;          /* SATA Error */
    uint32_t sact;          /* SATA Active */
    uint32_t ci;            /* Command Issue */
    uint32_t sntf;          /* SATA Notification */
    uint32_t fbs;           /* FIS-based Switching */
    uint32_t devslp;        /* Device Sleep */
    uint8_t reserved2[40];
    uint8_t vendor[16];
} __attribute__((packed)) ahci_port_t;

int ahci_init(void);
int ahci_probe(pci_device_t *dev);

/*============================================================================
 * GOP (Graphics Output Protocol) Framebuffer
 *============================================================================*/

typedef struct gop_framebuffer {
    uint64_t base;
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
    uint32_t bpp;
    uint32_t red_mask;
    uint32_t green_mask;
    uint32_t blue_mask;
    uint32_t reserved_mask;
} gop_framebuffer_t;

extern gop_framebuffer_t gop_fb;

int gop_init(uefi_boot_info_t *info);
void gop_put_pixel(uint32_t x, uint32_t y, uint32_t color);
void gop_fill_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color);
void gop_scroll(uint32_t lines);

#endif /* _OS3_MODERN_HW_H_ */
