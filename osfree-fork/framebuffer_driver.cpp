/*
 * Intel GPU Basic Framebuffer Driver - Pseudocode
 * Targets Gen 9+ Intel integrated GPUs
 * 
 * This provides the core logic for getting a basic framebuffer working.
 * Adapt register offsets and sequences for your specific GPU generation.
 */

#include <stdint.h>
#include <stdbool.h>

// ============================================================================
// REGISTER DEFINITIONS (Gen 9+ offsets from BAR0)
// ============================================================================

// PCI Configuration
#define PCI_VENDOR_INTEL        0x8086
#define PCI_CLASS_DISPLAY       0x03

// Power Management
#define PWR_WELL_CTL            0x45400
#define PWR_WELL_STATE          0x45404
#define   PWR_WELL_PG2_ENABLE   (1 << 1)
#define   PWR_WELL_DDI_A_E      (1 << 26)

// GMBUS (I2C) Registers
#define GMBUS0                  0xC5100
#define GMBUS1                  0xC5104
#define GMBUS2                  0xC5108
#define GMBUS3                  0xC510C
#define GMBUS4                  0xC5110
#define GMBUS5                  0xC5120

// Display PLL
#define DPLL_CTRL1              0x6C058
#define DPLL_CTRL2              0x6C05C
#define LCPLL1_CTL              0x46010

// Pipe A registers (add 0x1000 for Pipe B, 0x2000 for Pipe C)
#define PIPE_CONF_A             0x70008
#define HTOTAL_A                0x60000
#define HBLANK_A                0x60004
#define HSYNC_A                 0x60008
#define VTOTAL_A                0x6000C
#define VBLANK_A                0x60010
#define VSYNC_A                 0x60014
#define PIPESRC_A               0x6001C
#define PIPE_SCANLINE_A         0x70000

// Primary Plane (Pipe A)
#define PLANE_CTL_A             0x70180
#define PLANE_STRIDE_A          0x70188
#define PLANE_SIZE_A            0x70190
#define PLANE_SURF_A            0x7019C
#define PLANE_OFFSET_A          0x701A4

// Transcoder/DDI
#define TRANS_CONF_A            0x70008
#define TRANS_HTOTAL_A          0x60000
#define TRANS_VTOTAL_A          0x6000C
#define DDI_BUF_CTL_A           0x64000

// GTT (Graphics Translation Table)
#define GTT_BASE                0x800000  // Offset within BAR0

// ============================================================================
// DATA STRUCTURES
// ============================================================================

typedef struct {
    uint32_t vendor_id;
    uint32_t device_id;
    uint8_t bus;
    uint8_t device;
    uint8_t function;
    void* mmio_base;      // BAR0 - register space
    void* aperture_base;  // BAR2 - VRAM/aperture
    uint64_t aperture_size;
    void* gtt_base;       // GTT location in BAR0
    uint32_t gtt_entries;
} intel_gpu_device;

typedef struct {
    uint16_t hdisplay;    // Horizontal active pixels
    uint16_t vdisplay;    // Vertical active lines
    uint16_t htotal;      // Total horizontal pixels (including blanking)
    uint16_t vtotal;      // Total vertical lines
    uint16_t hblank_start;
    uint16_t hblank_end;
    uint16_t hsync_start;
    uint16_t hsync_end;
    uint16_t vblank_start;
    uint16_t vblank_end;
    uint16_t vsync_start;
    uint16_t vsync_end;
    uint32_t pixel_clock; // in kHz
} display_mode;

typedef struct {
    uint8_t manufacturer[4];
    uint16_t product_code;
    char model_name[14];
    uint16_t width_mm;
    uint16_t height_mm;
    display_mode preferred_mode;
    // Simplified - real EDID has much more data
} edid_info;

typedef struct {
    void* virtual_addr;   // CPU-visible address
    uint64_t physical_addr;
    uint32_t gtt_offset;  // GTT entry offset
    uint32_t size;
    uint32_t stride;      // Bytes per row
} framebuffer;

// ============================================================================
// UTILITY FUNCTIONS
// ============================================================================

static inline uint32_t mmio_read32(intel_gpu_device* gpu, uint32_t offset) {
    volatile uint32_t* addr = (uint32_t*)((uint8_t*)gpu->mmio_base + offset);
    return *addr;
}

static inline void mmio_write32(intel_gpu_device* gpu, uint32_t offset, uint32_t value) {
    volatile uint32_t* addr = (uint32_t*)((uint8_t*)gpu->mmio_base + offset);
    *addr = value;
}

static void usleep(uint32_t microseconds) {
    // OS-specific microsecond delay
    // Implement using your kernel's timer facility
}

// ============================================================================
// 1. PCI ENUMERATION AND INITIALIZATION
// ============================================================================

bool detect_intel_gpu(intel_gpu_device* gpu) {
    // Scan PCI configuration space
    for (int bus = 0; bus < 256; bus++) {
        for (int dev = 0; dev < 32; dev++) {
            for (int func = 0; func < 8; func++) {
                uint32_t vendor_device = pci_read_config32(bus, dev, func, 0x00);
                uint16_t vendor = vendor_device & 0xFFFF;
                uint16_t device = (vendor_device >> 16) & 0xFFFF;
                
                if (vendor == 0xFFFF) continue; // No device
                
                uint32_t class_rev = pci_read_config32(bus, dev, func, 0x08);
                uint8_t class_code = (class_rev >> 24) & 0xFF;
                
                // Check if it's an Intel display controller
                if (vendor == PCI_VENDOR_INTEL && class_code == PCI_CLASS_DISPLAY) {
                    gpu->vendor_id = vendor;
                    gpu->device_id = device;
                    gpu->bus = bus;
                    gpu->device = dev;
                    gpu->function = func;
                    
                    printf("Found Intel GPU: %04x:%04x at %02x:%02x.%x\n",
                           vendor, device, bus, dev, func);
                    return true;
                }
            }
        }
    }
    return false;
}

bool map_gpu_resources(intel_gpu_device* gpu) {
    // Read BAR0 (MMIO registers) - typically 16MB
    uint32_t bar0_low = pci_read_config32(gpu->bus, gpu->device, gpu->function, 0x10);
    uint64_t bar0_addr = bar0_low & ~0xF; // Clear lower 4 bits
    
    // Check if 64-bit BAR
    if ((bar0_low & 0x6) == 0x4) {
        uint32_t bar0_high = pci_read_config32(gpu->bus, gpu->device, gpu->function, 0x14);
        bar0_addr |= ((uint64_t)bar0_high << 32);
    }
    
    // Read BAR2 (Aperture/VRAM) - can be 256MB or larger
    uint32_t bar2_low = pci_read_config32(gpu->bus, gpu->device, gpu->function, 0x18);
    uint64_t bar2_addr = bar2_low & ~0xF;
    
    if ((bar2_low & 0x6) == 0x4) {
        uint32_t bar2_high = pci_read_config32(gpu->bus, gpu->device, gpu->function, 0x1C);
        bar2_addr |= ((uint64_t)bar2_high << 32);
    }
    
    // Map BARs into kernel virtual address space
    gpu->mmio_base = map_physical_memory(bar0_addr, 16 * 1024 * 1024); // 16MB
    gpu->aperture_base = map_physical_memory(bar2_addr, 256 * 1024 * 1024); // 256MB
    gpu->aperture_size = 256 * 1024 * 1024;
    
    if (!gpu->mmio_base || !gpu->aperture_base) {
        printf("Failed to map GPU resources\n");
        return false;
    }
    
    // GTT is at offset 0x800000 within BAR0 on most Gen 9+ GPUs
    gpu->gtt_base = (void*)((uint8_t*)gpu->mmio_base + GTT_BASE);
    gpu->gtt_entries = gpu->aperture_size / 4096; // One entry per 4KB page
    
    // Enable bus mastering
    uint16_t command = pci_read_config16(gpu->bus, gpu->device, gpu->function, 0x04);
    command |= 0x06; // Enable memory space and bus master
    pci_write_config16(gpu->bus, gpu->device, gpu->function, 0x04, command);
    
    printf("Mapped MMIO at %p, Aperture at %p\n", gpu->mmio_base, gpu->aperture_base);
    return true;
}

// ============================================================================
// 2. POWER MANAGEMENT
// ============================================================================

bool enable_display_power_wells(intel_gpu_device* gpu) {
    // Read current state
    uint32_t pwr_well = mmio_read32(gpu, PWR_WELL_CTL);
    
    // Enable power well 2 (needed for display)
    pwr_well |= PWR_WELL_PG2_ENABLE;
    pwr_well |= PWR_WELL_DDI_A_E; // Enable DDI ports
    mmio_write32(gpu, PWR_WELL_CTL, pwr_well);
    
    // Wait for power wells to stabilize (up to 1ms)
    for (int i = 0; i < 100; i++) {
        uint32_t state = mmio_read32(gpu, PWR_WELL_STATE);
        if ((state & PWR_WELL_PG2_ENABLE) && (state & PWR_WELL_DDI_A_E)) {
            printf("Display power wells enabled\n");
            return true;
        }
        usleep(10);
    }
    
    printf("Timeout waiting for power wells\n");
    return false;
}

// ============================================================================
// 3. I2C/GMBUS AND EDID READING
// ============================================================================

bool gmbus_wait_idle(intel_gpu_device* gpu) {
    for (int i = 0; i < 100; i++) {
        uint32_t status = mmio_read32(gpu, GMBUS2);
        if ((status & (1 << 11)) == 0) { // HW_RDY bit clear = idle
            return true;
        }
        usleep(10);
    }
    return false;
}

bool gmbus_read_block(intel_gpu_device* gpu, uint8_t port, uint8_t addr, 
                      uint8_t* buffer, uint32_t length) {
    // Wait for bus to be idle
    if (!gmbus_wait_idle(gpu)) {
        printf("GMBUS busy timeout\n");
        return false;
    }
    
    // Select port (DDI port A = 3, B = 5, C = 4, D = 6)
    mmio_write32(gpu, GMBUS0, port);
    
    // Set up read transaction
    uint32_t cmd = (1 << 31) |           // SW_RDY
                   (1 << 27) |           // ENT (enable NACK)
                   ((length & 0x1FF) << 16) | // Byte count
                   (addr << 1) |         // Slave address (shifted)
                   (1 << 0);            // Read operation
    
    mmio_write32(gpu, GMBUS1, cmd);
    
    // Read data in 4-byte chunks
    for (uint32_t i = 0; i < length; i += 4) {
        // Wait for data ready
        bool ready = false;
        for (int retry = 0; retry < 100; retry++) {
            uint32_t status = mmio_read32(gpu, GMBUS2);
            if (status & (1 << 11)) { // HW_RDY
                ready = true;
                break;
            }
            usleep(10);
        }
        
        if (!ready) {
            printf("GMBUS read timeout at byte %d\n", i);
            return false;
        }
        
        // Read 4 bytes
        uint32_t data = mmio_read32(gpu, GMBUS3);
        for (int j = 0; j < 4 && (i + j) < length; j++) {
            buffer[i + j] = (data >> (j * 8)) & 0xFF;
        }
    }
    
    // Wait for completion
    gmbus_wait_idle(gpu);
    
    // Clear status
    mmio_write32(gpu, GMBUS1, 0);
    mmio_write32(gpu, GMBUS0, 0);
    
    return true;
}

bool read_edid(intel_gpu_device* gpu, uint8_t port, edid_info* edid) {
    uint8_t edid_data[128];
    
    // EDID is at I2C address 0x50
    if (!gmbus_read_block(gpu, port, 0x50, edid_data, 128)) {
        printf("Failed to read EDID from port %d\n", port);
        return false;
    }
    
    // Verify EDID header (00 FF FF FF FF FF FF 00)
    if (edid_data[0] != 0x00 || edid_data[1] != 0xFF || edid_data[7] != 0x00) {
        printf("Invalid EDID header\n");
        return false;
    }
    
    // Parse manufacturer ID (bytes 8-9)
    uint16_t mfg = (edid_data[8] << 8) | edid_data[9];
    edid->manufacturer[0] = '@' + ((mfg >> 10) & 0x1F);
    edid->manufacturer[1] = '@' + ((mfg >> 5) & 0x1F);
    edid->manufacturer[2] = '@' + (mfg & 0x1F);
    edid->manufacturer[3] = '\0';
    
    // Parse product code
    edid->product_code = edid_data[10] | (edid_data[11] << 8);
    
    // Find preferred timing (first detailed timing descriptor at offset 54)
    uint8_t* dtd = &edid_data[54];
    
    // Parse pixel clock (in 10kHz units)
    edid->preferred_mode.pixel_clock = (dtd[0] | (dtd[1] << 8)) * 10;
    
    // Parse horizontal timing
    uint16_t h_active = dtd[2] | ((dtd[4] & 0xF0) << 4);
    uint16_t h_blank = dtd[3] | ((dtd[4] & 0x0F) << 8);
    
    edid->preferred_mode.hdisplay = h_active;
    edid->preferred_mode.htotal = h_active + h_blank;
    edid->preferred_mode.hblank_start = h_active;
    edid->preferred_mode.hblank_end = h_active + h_blank;
    
    // Parse vertical timing
    uint16_t v_active = dtd[5] | ((dtd[7] & 0xF0) << 4);
    uint16_t v_blank = dtd[6] | ((dtd[7] & 0x0F) << 8);
    
    edid->preferred_mode.vdisplay = v_active;
    edid->preferred_mode.vtotal = v_active + v_blank;
    edid->preferred_mode.vblank_start = v_active;
    edid->preferred_mode.vblank_end = v_active + v_blank;
    
    // Parse sync offsets (simplified)
    uint16_t h_sync_offset = dtd[8] | ((dtd[11] & 0xC0) << 2);
    uint16_t h_sync_width = dtd[9] | ((dtd[11] & 0x30) << 4);
    
    edid->preferred_mode.hsync_start = h_active + h_sync_offset;
    edid->preferred_mode.hsync_end = h_active + h_sync_offset + h_sync_width;
    
    uint16_t v_sync_offset = ((dtd[10] & 0xF0) >> 4) | ((dtd[11] & 0x0C) << 2);
    uint16_t v_sync_width = (dtd[10] & 0x0F) | ((dtd[11] & 0x03) << 4);
    
    edid->preferred_mode.vsync_start = v_active + v_sync_offset;
    edid->preferred_mode.vsync_end = v_active + v_sync_offset + v_sync_width;
    
    printf("EDID: %s, %dx%d @%d kHz\n", 
           edid->manufacturer,
           edid->preferred_mode.hdisplay,
           edid->preferred_mode.vdisplay,
           edid->preferred_mode.pixel_clock);
    
    return true;
}

// ============================================================================
// 4. GTT (GRAPHICS TRANSLATION TABLE) MANAGEMENT
// ============================================================================

void init_gtt(intel_gpu_device* gpu) {
    // Clear all GTT entries
    volatile uint64_t* gtt = (uint64_t*)gpu->gtt_base;
    
    for (uint32_t i = 0; i < gpu->gtt_entries; i++) {
        gtt[i] = 0; // Invalid entry
    }
    
    printf("Initialized GTT with %d entries\n", gpu->gtt_entries);
}

bool map_framebuffer_to_gtt(intel_gpu_device* gpu, framebuffer* fb) {
    // Allocate physical pages for framebuffer
    uint32_t num_pages = (fb->size + 4095) / 4096;
    
    // Get GTT base
    volatile uint64_t* gtt = (uint64_t*)gpu->gtt_base;
    
    // Find free GTT space (simplified - should use proper allocator)
    uint32_t gtt_start = 0; // Start at beginning for simplicity
    fb->gtt_offset = gtt_start * 4096;
    
    // Map each page into GTT
    uint64_t phys_addr = fb->physical_addr;
    for (uint32_t i = 0; i < num_pages; i++) {
        // GTT entry format (Gen 9+):
        // Bits 0: Valid
        // Bits 1-11: Reserved/caching
        // Bits 12+: Physical page frame number
        uint64_t entry = (phys_addr & ~0xFFF) | 0x1; // Valid bit
        gtt[gtt_start + i] = entry;
        phys_addr += 4096;
    }
    
    printf("Mapped framebuffer to GTT offset 0x%x (%d pages)\n", 
           fb->gtt_offset, num_pages);
    
    return true;
}

// ============================================================================
// 5. DISPLAY MODE SETTING
// ============================================================================

bool configure_pll(intel_gpu_device* gpu, display_mode* mode) {
    // Simplified PLL configuration - real version needs proper calculations
    // This assumes we're using DPLL 0 and a common configuration
    
    // Read current DPLL control
    uint32_t dpll_ctrl1 = mmio_read32(gpu, DPLL_CTRL1);
    
    // Configure DPLL 0 for the target pixel clock
    // This is highly simplified - proper implementation needs to calculate
    // dividers based on reference clock and target frequency
    dpll_ctrl1 &= ~(0x7 << 0); // Clear DPLL 0 link rate
    
    // Set a common configuration (e.g., for ~148.5 MHz pixel clock)
    // Real implementation must calculate proper dividers
    dpll_ctrl1 |= (0x0 << 0); // Example link rate
    
    mmio_write32(gpu, DPLL_CTRL1, dpll_ctrl1);
    
    // Enable DPLL
    uint32_t lcpll = mmio_read32(gpu, LCPLL1_CTL);
    lcpll |= (1 << 31); // PLL enable
    mmio_write32(gpu, LCPLL1_CTL, lcpll);
    
    // Wait for PLL lock (bit 30)
    for (int i = 0; i < 100; i++) {
        lcpll = mmio_read32(gpu, LCPLL1_CTL);
        if (lcpll & (1 << 30)) {
            printf("PLL locked\n");
            return true;
        }
        usleep(10);
    }
    
    printf("PLL lock timeout\n");
    return false;
}

bool configure_pipe(intel_gpu_device* gpu, int pipe, display_mode* mode) {
    uint32_t pipe_offset = pipe * 0x1000; // Pipe B = +0x1000, Pipe C = +0x2000
    
    // Disable pipe first
    uint32_t conf = mmio_read32(gpu, PIPE_CONF_A + pipe_offset);
    conf &= ~(1 << 31); // Disable
    mmio_write32(gpu, PIPE_CONF_A + pipe_offset, conf);
    
    // Wait for pipe to disable
    for (int i = 0; i < 100; i++) {
        conf = mmio_read32(gpu, PIPE_CONF_A + pipe_offset);
        if ((conf & (1 << 30)) == 0) { // State bit cleared
            break;
        }
        usleep(10);
    }
    
    // Configure timing registers
    mmio_write32(gpu, HTOTAL_A + pipe_offset, 
                 (mode->htotal - 1) << 16 | (mode->hdisplay - 1));
    
    mmio_write32(gpu, HBLANK_A + pipe_offset,
                 (mode->hblank_end - 1) << 16 | (mode->hblank_start - 1));
    
    mmio_write32(gpu, HSYNC_A + pipe_offset,
                 (mode->hsync_end - 1) << 16 | (mode->hsync_start - 1));
    
    mmio_write32(gpu, VTOTAL_A + pipe_offset,
                 (mode->vtotal - 1) << 16 | (mode->vdisplay - 1));
    
    mmio_write32(gpu, VBLANK_A + pipe_offset,
                 (mode->vblank_end - 1) << 16 | (mode->vblank_start - 1));
    
    mmio_write32(gpu, VSYNC_A + pipe_offset,
                 (mode->vsync_end - 1) << 16 | (mode->vsync_start - 1));
    
    // Set pipe source size
    mmio_write32(gpu, PIPESRC_A + pipe_offset,
                 (mode->hdisplay - 1) << 16 | (mode->vdisplay - 1));
    
    // Enable pipe
    conf = mmio_read32(gpu, PIPE_CONF_A + pipe_offset);
    conf |= (1 << 31); // Enable
    mmio_write32(gpu, PIPE_CONF_A + pipe_offset, conf);
    
    // Wait for pipe to enable
    for (int i = 0; i < 100; i++) {
        conf = mmio_read32(gpu, PIPE_CONF_A + pipe_offset);
        if (conf & (1 << 30)) { // State bit set
            printf("Pipe %c enabled\n", 'A' + pipe);
            return true;
        }
        usleep(10);
    }
    
    printf("Pipe enable timeout\n");
    return false;
}

bool configure_plane(intel_gpu_device* gpu, int pipe, display_mode* mode, 
                     framebuffer* fb) {
    uint32_t plane_offset = pipe * 0x1000;
    
    // Disable plane first
    mmio_write32(gpu, PLANE_CTL_A + plane_offset, 0);
    
    // Configure plane control
    uint32_t plane_ctl = (1 << 31) |    // Plane enable
                         (0 << 24) |    // Format: 8:8:8:8 XRGB (0x0)
                         (0 << 10);     // No rotation
    
    // Set stride (bytes per row, must be 64-byte aligned)
    mmio_write32(gpu, PLANE_STRIDE_A + plane_offset, fb->stride);
    
    // Set plane size
    mmio_write32(gpu, PLANE_SIZE_A + plane_offset,
                 ((mode->vdisplay - 1) << 16) | (mode->hdisplay - 1));
    
    // Set plane offset (usually 0)
    mmio_write32(gpu, PLANE_OFFSET_A + plane_offset, 0);
    
    // Set surface address (GTT offset)
    // This must be written LAST to trigger the plane update
    mmio_write32(gpu, PLANE_SURF_A + plane_offset, fb->gtt_offset);
    
    // Enable the plane
    mmio_write32(gpu, PLANE_CTL_A + plane_offset, plane_ctl);
    
    printf("Plane %c configured at GTT 0x%x\n", 'A' + pipe, fb->gtt_offset);
    return true;
}

bool enable_ddi_output(intel_gpu_device* gpu, int ddi_port) {
    // Enable DDI buffer (simplified - real version needs proper configuration)
    uint32_t ddi_offset = ddi_port * 0x100; // DDI A = 0, B = 0x100, etc.
    
    uint32_t ddi_buf = mmio_read32(gpu, DDI_BUF_CTL_A + ddi_offset);
    ddi_buf |= (1 << 31);  // DDI buffer enable
    ddi_buf |= (0x1 << 24); // Port width (1 lane for eDP/HDMI, adjust as needed)
    mmio_write32(gpu, DDI_BUF_CTL_A + ddi_offset, ddi_buf);
    
    printf("DDI port %c enabled\n", 'A' + ddi_port);
    return true;
}

// ============================================================================
// 6. FRAMEBUFFER ALLOCATION
// ============================================================================

bool allocate_framebuffer(intel_gpu_device* gpu, display_mode* mode, 
                          framebuffer* fb) {
    // Calculate framebuffer size (32bpp = 4 bytes per pixel)
    uint32_t bytes_per_pixel = 4;
    fb->stride = ALIGN_UP(mode->hdisplay * bytes_per_pixel, 64); // 64-byte alignment
    fb->size = fb->stride * mode->vdisplay;
    
    // Allocate physical memory (contiguous or use GTT to make it appear contiguous)
    fb->physical_addr = allocate_physical_pages(fb->size);
    if (!fb->physical_addr) {
        printf("Failed to allocate framebuffer memory\n");
        return false;
    }
    
    // Map to kernel virtual address space
    fb->virtual_addr = map_physical_memory(fb->physical_addr, fb->size);
    if (!fb->virtual_addr) {
        printf("Failed to map framebuffer\n");
        free_physical_pages(fb->physical_addr, fb->size);
        return false;
    }
    
    // Clear framebuffer (black screen)
    memset(fb->virtual_addr, 0, fb->size);
    
    printf("Allocated framebuffer: %dx%d, stride=%d, size=%d bytes\n",
           mode->hdisplay, mode->vdisplay, fb->stride, fb->size);
    
    return true;
}

// ============================================================================
// 7. MAIN INITIALIZATION SEQUENCE
// ============================================================================

bool init_intel_framebuffer(intel_gpu_device* gpu, framebuffer* fb) {
    // Step 1: Detect and map GPU
    if (!detect_intel_gpu(gpu)) {
        printf("No Intel GPU found\n");
        return false;
    }
    
    if (!map_gpu_resources(gpu)) {
        printf("Failed to map GPU resources\n");
        return false;
    }
    
    // Step 2: Enable power
    if (!enable_display_power_wells(gpu)) {
        printf("Failed to enable display power\n");
        return false;
    }
    
    // Step 3: Initialize GTT
    init_gtt(gpu);
    
    // Step 4: Detect displays and read EDID
    edid_info edid;
    bool display_found = false;
    
    // Try each DDI port (A=3, B=5, C=4, D=6 for GMBUS port numbers)
    uint8_t gmbus_ports[] = {3, 5, 4, 6}; // DDI A, B, C, D
    int active_port = -1;
    
    for (int i = 0; i < 4; i++) {
        if (read_edid(gpu, gmbus_ports[i], &edid)) {
            printf("Found display on DDI port %c\n", 'A' + i);
            display_found = true;
            active_port = i;
            break;
        }
    }
    
    if (!display_found) {
        printf("No display detected on any port\n");
        return false;
    }
    
    // Step 5: Allocate framebuffer
    if (!allocate_framebuffer(gpu, &edid.preferred_mode, fb)) {
        printf("Failed to allocate framebuffer\n");
        return false;
    }
    
    // Step 6: Map framebuffer into GTT
    if (!map_framebuffer_to_gtt(gpu, fb)) {
        printf("Failed to map framebuffer to GTT\n");
        return false;
    }
    
    // Step 7: Configure PLL for pixel clock
    if (!configure_pll(gpu, &edid.preferred_mode)) {
        printf("Failed to configure PLL\n");
        return false;
    }
    
    // Step 8: Configure pipe (using Pipe A)
    int pipe = 0; // Pipe A
    if (!configure_pipe(gpu, pipe, &edid.preferred_mode)) {
        printf("Failed to configure pipe\n");
        return false;
    }
    
    // Step 9: Configure primary plane
    if (!configure_plane(gpu, pipe, &edid.preferred_mode, fb)) {
        printf("Failed to configure plane\n");
        return false;
    }
    
    // Step 10: Enable DDI output
    if (!enable_ddi_output(gpu, active_port)) {
        printf("Failed to enable DDI output\n");
        return false;
    }
    
    printf("Intel framebuffer initialized successfully!\n");
    printf("Framebuffer at %p, size %dx%d\n", 
           fb->virtual_addr, 
           edid.preferred_mode.hdisplay,
           edid.preferred_mode.vdisplay);
    
    return true;
}

// ============================================================================
// 8. TEST FUNCTIONS - DRAWING TO FRAMEBUFFER
// ============================================================================

void draw_test_pattern(framebuffer* fb, display_mode* mode) {
    uint32_t* pixels = (uint32_t*)fb->virtual_addr;
    
    for (int y = 0; y < mode->vdisplay; y++) {
        for (int x = 0; x < mode->hdisplay; x++) {
            uint32_t color;
            
            // Draw color bars
            if (x < mode->hdisplay / 8) {
                color = 0xFFFFFFFF; // White
            } else if (x < mode->hdisplay * 2 / 8) {
                color = 0xFFFFFF00; // Yellow
            } else if (x < mode->hdisplay * 3 / 8) {
                color = 0xFF00FFFF; // Cyan
            } else if (x < mode->hdisplay * 4 / 8) {
                color = 0xFF00FF00; // Green
            } else if (x < mode->hdisplay * 5 / 8) {
                color = 0xFFFF00FF; // Magenta
            } else if (x < mode->hdisplay * 6 / 8) {
                color = 0xFFFF0000; // Red
            } else if (x < mode->hdisplay * 7 / 8) {
                color = 0xFF0000FF; // Blue
            } else {
                color = 0xFF000000; // Black
            }
            
            // Calculate pixel offset
            uint32_t offset = y * (fb->stride / 4) + x;
            pixels[offset] = color;
        }
    }
    
    printf("Drew test pattern\n");
}

void fill_screen(framebuffer* fb, display_mode* mode, uint32_t color) {
    uint32_t* pixels = (uint32_t*)fb->virtual_addr;
    
    for (int y = 0; y < mode->vdisplay; y++) {
        for (int x = 0; x < mode->hdisplay; x++) {
            uint32_t offset = y * (fb->stride / 4) + x;
            pixels[offset] = color;
        }
    }
}

void draw_rectangle(framebuffer* fb, display_mode* mode, 
                    int x, int y, int width, int height, uint32_t color) {
    uint32_t* pixels = (uint32_t*)fb->virtual_addr;
    
    for (int dy = 0; dy < height; dy++) {
        for (int dx = 0; dx < width; dx++) {
            int px = x + dx;
            int py = y + dy;
            
            if (px >= 0 && px < mode->hdisplay && 
                py >= 0 && py < mode->vdisplay) {
                uint32_t offset = py * (fb->stride / 4) + px;
                pixels[offset] = color;
            }
        }
    }
}

// ============================================================================
// 9. EXAMPLE USAGE
// ============================================================================

void example_main() {
    intel_gpu_device gpu = {0};
    framebuffer fb = {0};
    
    // Initialize the framebuffer
    if (!init_intel_framebuffer(&gpu, &fb)) {
        printf("Failed to initialize Intel framebuffer\n");
        return;
    }
    
    // Draw a test pattern
    edid_info edid;
    read_edid(&gpu, 3, &edid); // Re-read for mode info
    draw_test_pattern(&fb, &edid.preferred_mode);
    
    // Wait a bit
    usleep(2000000); // 2 seconds
    
    // Clear to blue
    fill_screen(&fb, &edid.preferred_mode, 0xFF0000FF);
    
    // Draw some rectangles
    draw_rectangle(&fb, &edid.preferred_mode, 100, 100, 200, 150, 0xFFFF0000); // Red
    draw_rectangle(&fb, &edid.preferred_mode, 350, 200, 300, 200, 0xFF00FF00); // Green
    draw_rectangle(&fb, &edid.preferred_mode, 200, 400, 400, 100, 0xFFFFFFFF); // White
    
    printf("Display test complete\n");
}

// ============================================================================
// 10. UTILITY MACROS AND HELPERS
// ============================================================================

#define ALIGN_UP(x, align) (((x) + ((align) - 1)) & ~((align) - 1))

// PCI configuration space access - you'll need to implement these
// based on your OS's PCI subsystem
uint32_t pci_read_config32(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset) {
    // Implementation depends on your OS
    // Typically uses I/O ports 0xCF8 and 0xCFC
    uint32_t address = (1 << 31) | (bus << 16) | (dev << 11) | 
                       (func << 8) | (offset & 0xFC);
    outl(0xCF8, address);
    return inl(0xCFC);
}

uint16_t pci_read_config16(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset) {
    uint32_t val = pci_read_config32(bus, dev, func, offset & 0xFC);
    return (val >> ((offset & 2) * 8)) & 0xFFFF;
}

void pci_write_config32(uint8_t bus, uint8_t dev, uint8_t func, 
                        uint8_t offset, uint32_t value) {
    uint32_t address = (1 << 31) | (bus << 16) | (dev << 11) | 
                       (func << 8) | (offset & 0xFC);
    outl(0xCF8, address);
    outl(0xCFC, value);
}

void pci_write_config16(uint8_t bus, uint8_t dev, uint8_t func, 
                        uint8_t offset, uint16_t value) {
    uint32_t val = pci_read_config32(bus, dev, func, offset & 0xFC);
    int shift = (offset & 2) * 8;
    val = (val & ~(0xFFFF << shift)) | (value << shift);
    pci_write_config32(bus, dev, func, offset & 0xFC, val);
}

// Memory mapping functions - implement based on your OS's memory manager
void* map_physical_memory(uint64_t physical_addr, uint64_t size) {
    // Map physical address into kernel virtual address space
    // This is OS-specific - typically involves:
    // 1. Reserving virtual address space
    // 2. Setting up page tables to map to physical address
    // 3. Setting appropriate memory attributes (uncached for MMIO)
    return NULL; // Replace with actual implementation
}

uint64_t allocate_physical_pages(uint64_t size) {
    // Allocate contiguous physical memory
    // Return physical address of allocated memory
    return 0; // Replace with actual implementation
}

void free_physical_pages(uint64_t physical_addr, uint64_t size) {
    // Free previously allocated physical memory
}

// I/O port access - implement based on your architecture
void outl(uint16_t port, uint32_t value) {
    #ifdef __x86_64__
    __asm__ volatile ("outl %0, %1" : : "a"(value), "Nd"(port));
    #endif
}

uint32_t inl(uint16_t port) {
    #ifdef __x86_64__
    uint32_t value;
    __asm__ volatile ("inl %1, %0" : "=a"(value) : "Nd"(port));
    return value;
    #endif
    return 0;
}

// ============================================================================
// NOTES AND IMPORTANT CONSIDERATIONS
// ============================================================================

/*
 * CRITICAL IMPLEMENTATION NOTES:
 * 
 * 1. GENERATION-SPECIFIC DIFFERENCES:
 *    - Register offsets vary between GPU generations (Gen 9, 11, 12+)
 *    - Always consult Intel's programmer reference manual for your target GPU
 *    - Gen 12+ has significantly different display architecture
 * 
 * 2. PLL CONFIGURATION:
 *    - The PLL configuration here is HIGHLY simplified
 *    - Real implementation needs proper frequency calculations
 *    - Must consider reference clock (24 MHz or 19.2 MHz)
 *    - Different PLL algorithms for different generations
 * 
 * 3. POWER MANAGEMENT:
 *    - Modern Intel GPUs have complex power well dependencies
 *    - Must enable power wells in correct order
 *    - Some operations require specific power domains active
 * 
 * 4. SYNCHRONIZATION:
 *    - Many register writes need VBlank synchronization
 *    - Plane surface updates should occur during VBlank
 *    - Read PIPE_SCANLINE register to detect VBlank periods
 * 
 * 5. ERROR HANDLING:
 *    - Production code needs much more robust error handling
 *    - Check for hardware errors and timeouts
 *    - Implement proper cleanup on failure paths
 * 
 * 6. MULTI-DISPLAY:
 *    - This code focuses on single display
 *    - Multiple displays require configuring additional pipes and transcoders
 *    - Need to handle display port MST (Multi-Stream Transport)
 * 
 * 7. HDCP AND AUDIO:
 *    - HDCP (content protection) requires additional setup
 *    - Audio over HDMI/DP needs separate configuration
 * 
 * 8. PERFORMANCE:
 *    - Consider using write-combining memory for framebuffer
 *    - Implement double/triple buffering for smooth updates
 *    - Use hardware cursor for mouse pointer
 * 
 * 9. TESTING:
 *    - Test on multiple GPU generations
 *    - Verify with different display types (eDP, HDMI, DP)
 *    - Check various resolutions and refresh rates
 * 
 * 10. RESOURCES:
 *     - Intel Open Source Graphics Programmer's Reference Manual
 *     - Linux i915 driver source code (drivers/gpu/drm/i915/)
 *     - Intel XDC presentations and documentation
 */
