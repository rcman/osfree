# Integrating Intel Framebuffer Driver into osFree

This guide explains how to add Intel GPU framebuffer support to your osFree fork using the L4 microkernel architecture.

## Understanding osFree's Architecture

osFree is built on top of L4 microkernel with these key characteristics:

1. **L4 Microkernel Base**: Runs in privileged mode, handles IPC, memory management, and scheduling
2. **Userspace Drivers**: All device drivers run as userspace servers/tasks
3. **IPC Communication**: Hardware interrupts delivered via IPC, device access through message passing
4. **Hardware Mapping**: Physical memory (MMIO) and I/O ports mapped to userspace via L4 mechanisms
5. **DDE (Device Driver Environment)**: Framework for porting Linux drivers to L4 userspace

## Three Approaches to Add Intel Framebuffer Support

### Approach 1: Native L4 Driver (Recommended for Learning)

Write a native framebuffer driver as an L4 userspace server.

**Advantages:**
- Full control over implementation
- Smaller code footprint
- Better understanding of L4 primitives
- Easier to debug initially

**Architecture:**
```
┌─────────────────────────────────────┐
│   OS/2 Applications                 │
└──────────────┬──────────────────────┘
               │ IPC
┌──────────────▼──────────────────────┐
│   Graphics Server (OS/2 API)        │
└──────────────┬──────────────────────┘
               │ IPC
┌──────────────▼──────────────────────┐
│   Intel Framebuffer Server          │ ← Userspace
│   (L4 Task)                         │
└──────────────┬──────────────────────┘
               │ L4 Mappings
┌──────────────▼──────────────────────┐
│   L4 Microkernel                    │ ← Kernel mode
│   - Memory mapping                  │
│   - Interrupt delivery              │
└──────────────┬──────────────────────┘
               │
┌──────────────▼──────────────────────┐
│   Intel GPU Hardware                │
└─────────────────────────────────────┘
```

### Approach 2: DDE/Linux Port (Best Long-term)

Port the Linux i915 driver using L4Re's DDE framework.

**Advantages:**
- Reuse mature, tested code
- Automatic updates as Linux i915 evolves
- Full hardware support (3D acceleration, power management)
- Less custom code to maintain

**Architecture:**
```
┌─────────────────────────────────────┐
│   OS/2 Applications                 │
└──────────────┬──────────────────────┘
               │ IPC
┌──────────────▼──────────────────────┐
│   Graphics Server (OS/2 API)        │
└──────────────┬──────────────────────┘
               │ IPC
┌──────────────▼──────────────────────┐
│   DDE/Linux Environment             │ ← Userspace
│   ┌─────────────────────────────┐   │
│   │ Linux i915 driver           │   │
│   └──────────┬──────────────────┘   │
│   ┌──────────▼──────────────────┐   │
│   │ DDE Linux compatibility     │   │
│   │ layer (emulates Linux APIs) │   │
│   └──────────┬──────────────────┘   │
└──────────────┬──────────────────────┘
               │ L4 IPC
┌──────────────▼──────────────────────┐
│   L4 Microkernel                    │
└─────────────────────────────────────┘
```

### Approach 3: Hybrid (Practical Balance)

Start with basic framebuffer, migrate to DDE later.

## Detailed Implementation: Native L4 Driver

### Step 1: Project Structure

Create the driver in your osFree fork:

```
osfree/
├── l4/
│   └── pkg/
│       └── intel_fb/          ← New package
│           ├── server/        ← Main driver code
│           │   ├── src/
│           │   │   ├── main.cc
│           │   │   ├── pci.cc
│           │   │   ├── modesetting.cc
│           │   │   ├── gmbus.cc
│           │   │   ├── gtt.cc
│           │   │   └── framebuffer.cc
│           │   └── Makefile
│           ├── include/
│           │   └── intel_fb/
│           │       ├── device.h
│           │       ├── registers.h
│           │       └── protocol.h
│           └── Control          ← Build control file
```

### Step 2: L4 Capabilities and IPC Interface

Define the IPC protocol for your framebuffer server:

```cpp
// include/intel_fb/protocol.h
#pragma once

#include <l4/sys/types.h>

namespace Intel_fb {

// IPC opcodes
enum Opcodes {
  OP_GET_MODE_INFO = 0,
  OP_SET_MODE      = 1,
  OP_GET_FB_MEMORY = 2,
  OP_UPDATE_REGION = 3,
};

struct Mode_info {
  l4_uint32_t width;
  l4_uint32_t height;
  l4_uint32_t bpp;
  l4_uint32_t stride;
  l4_addr_t   fb_addr;
  l4_size_t   fb_size;
};

} // namespace Intel_fb
```

### Step 3: Main Server Implementation

```cpp
// server/src/main.cc
#include <l4/re/env>
#include <l4/re/util/cap_alloc>
#include <l4/re/util/object_registry>
#include <l4/sys/factory>
#include <l4/sys/irq>
#include <l4/re/error_helper>
#include <l4/re/dataspace>
#include <l4/re/rm>
#include <l4/io/io>

#include "intel_fb/device.h"
#include "intel_fb/protocol.h"

class Framebuffer_server : public L4::Epiface_t<Framebuffer_server, L4::Factory>
{
private:
  Intel_gpu_device _gpu;
  Framebuffer _fb;
  L4::Cap<L4Re::Dataspace> _fb_ds;
  
public:
  long op_create(L4::Factory::Rights, L4::Ipc::Cap<void> &res,
                 l4_umword_t, L4::Ipc::Varg_list<> const &args)
  {
    // Client registration logic
    return L4_EOK;
  }
  
  // Handle mode setting requests
  long handle_set_mode(unsigned width, unsigned height)
  {
    Display_mode mode = {
      .hdisplay = width,
      .vdisplay = height,
      // ... fill other timing parameters
    };
    
    if (!configure_pipe(&_gpu, 0, &mode))
      return -L4_EIO;
      
    if (!configure_plane(&_gpu, 0, &mode, &_fb))
      return -L4_EIO;
      
    return L4_EOK;
  }
  
  // Handle framebuffer memory mapping
  long handle_get_fb_ds(L4::Ipc::Cap<L4Re::Dataspace> &ds)
  {
    ds = _fb_ds;
    return L4_EOK;
  }
};

// Hardware resource access
class Hardware_access
{
public:
  static void *map_device_memory(l4_addr_t phys_addr, l4_size_t size)
  {
    // Request IO port access from 'io' server
    auto io = L4Re::Env::env()->get_cap<L4vbus::Vbus>("vbus");
    if (!io.is_valid())
      L4Re::chksys(-L4_ENOENT, "Getting vbus capability");
    
    // Create dataspace for device memory
    auto ds = L4Re::Util::cap_alloc.alloc<L4Re::Dataspace>();
    L4Re::chksys(L4Re::Env::env()->mem_alloc()->alloc(size, ds, 
                 L4Re::Mem_alloc::Continuous | L4Re::Mem_alloc::Pinned));
    
    // Map into our address space
    l4_addr_t virt_addr;
    L4Re::chksys(L4Re::Env::env()->rm()->attach(&virt_addr, size,
                 L4Re::Rm::F::Search_addr | L4Re::Rm::F::RW | 
                 L4Re::Rm::F::Cache_uncached, ds));
    
    return reinterpret_cast<void*>(virt_addr);
  }
  
  static L4::Cap<L4::Irq> request_irq(unsigned irq_num)
  {
    auto irq_cap = L4Re::Util::cap_alloc.alloc<L4::Irq>();
    L4Re::chksys(L4Re::Env::env()->factory()->create(irq_cap));
    
    // Bind IRQ through IO server
    auto io = L4Re::Env::env()->get_cap<L4vbus::Vbus>("vbus");
    L4Re::chksys(io->request_ioirq(irq_num, irq_cap));
    
    return irq_cap;
  }
};

int main()
{
  printf("Intel framebuffer driver starting...\n");
  
  // Set up IPC server
  static L4Re::Util::Registry_server<> server;
  
  Framebuffer_server fb_srv;
  
  // Initialize hardware
  if (!detect_intel_gpu(&fb_srv._gpu)) {
    printf("No Intel GPU found\n");
    return 1;
  }
  
  // Map GPU registers using L4 mechanisms
  fb_srv._gpu.mmio_base = Hardware_access::map_device_memory(
    fb_srv._gpu.bar0_addr, 16 * 1024 * 1024);
  
  fb_srv._gpu.aperture_base = Hardware_access::map_device_memory(
    fb_srv._gpu.bar2_addr, 256 * 1024 * 1024);
  
  // Initialize display
  if (!init_intel_framebuffer(&fb_srv._gpu, &fb_srv._fb)) {
    printf("Failed to initialize framebuffer\n");
    return 1;
  }
  
  // Create dataspace for framebuffer to share with clients
  fb_srv._fb_ds = L4Re::Util::cap_alloc.alloc<L4Re::Dataspace>();
  L4Re::chksys(L4Re::Env::env()->mem_alloc()->alloc(
    fb_srv._fb.size, fb_srv._fb_ds));
  
  // Register with server loop
  server.registry()->register_obj(&fb_srv, "intel_fb");
  
  printf("Intel framebuffer driver ready\n");
  
  // Enter IPC dispatch loop
  server.loop();
  
  return 0;
}
```

### Step 4: PCI Device Discovery via L4vbus

L4Re uses the 'io' server for device management:

```cpp
// server/src/pci.cc
#include <l4/vbus/vbus>
#include <l4/vbus/vbus_pci>

bool detect_intel_gpu(Intel_gpu_device* gpu)
{
  auto vbus = L4Re::Env::env()->get_cap<L4vbus::Vbus>("vbus");
  if (!vbus.is_valid()) {
    printf("Cannot get vbus capability\n");
    return false;
  }
  
  // Enumerate PCI devices
  L4vbus::Pci_dev pci_dev;
  L4vbus::Device root = vbus->root();
  
  for (auto dev = root.first_child(); dev.valid(); dev = dev.next_sibling()) {
    l4vbus_device_t dev_info;
    if (dev.device_info(&dev_info) < 0)
      continue;
      
    // Check if it's a PCI device
    if (!(dev_info.type & L4VBUS_DEVICE_TYPE_PCI))
      continue;
    
    pci_dev = L4vbus::Pci_dev(dev);
    
    l4_uint32_t vendor_device;
    pci_dev.cfg_read(0, &vendor_device, 32);
    
    l4_uint16_t vendor = vendor_device & 0xFFFF;
    l4_uint16_t device = (vendor_device >> 16) & 0xFFFF;
    
    // Check for Intel VGA device
    l4_uint32_t class_rev;
    pci_dev.cfg_read(0x08, &class_rev, 32);
    l4_uint8_t class_code = (class_rev >> 24) & 0xFF;
    
    if (vendor == 0x8086 && class_code == 0x03) {
      printf("Found Intel GPU: %04x:%04x\n", vendor, device);
      
      gpu->vendor_id = vendor;
      gpu->device_id = device;
      gpu->pci_dev = dev;
      
      // Read BARs
      l4_uint32_t bar0, bar2;
      pci_dev.cfg_read(0x10, &bar0, 32);
      pci_dev.cfg_read(0x18, &bar2, 32);
      
      gpu->bar0_addr = bar0 & ~0xF;
      gpu->bar2_addr = bar2 & ~0xF;
      
      // Enable bus mastering
      l4_uint16_t cmd;
      pci_dev.cfg_read(0x04, &cmd, 16);
      cmd |= 0x06;
      pci_dev.cfg_write(0x04, cmd, 16);
      
      return true;
    }
  }
  
  return false;
}
```

### Step 5: Build Configuration

```makefile
# server/Makefile
PKGDIR  ?= ../..
L4DIR   ?= $(PKGDIR)/../../..

TARGET          = intel_fb_server
SRC_CC          = main.cc pci.cc modesetting.cc gmbus.cc gtt.cc framebuffer.cc
REQUIRES_LIBS   = libstdc++ l4re_c l4re_c-util libio-vbus

include $(L4DIR)/mk/prog.mk
```

```
# Control file
provides: intel_fb
requires: libstdc++ l4re vbus
maintainer: your-name
```

### Step 6: Integration with osFree Graphics Stack

Create a bridge between OS/2 GRADD API and your L4 framebuffer server:

```cpp
// In osFree's graphics layer
class OS2_GRADD_Bridge
{
private:
  L4::Cap<Intel_fb::Server> _fb_server;
  
public:
  bool init() {
    // Get capability to framebuffer server
    _fb_server = L4Re::Env::env()->get_cap<Intel_fb::Server>("intel_fb");
    return _fb_server.is_valid();
  }
  
  // GRADD API implementation
  int SetMode(int width, int height, int bpp) {
    // Call framebuffer server via IPC
    Intel_fb::Mode_info mode;
    
    L4::Ipc::Iostream s(l4_utcb());
    s << Intel_fb::OP_SET_MODE << width << height << bpp;
    
    auto err = l4_ipc_call(_fb_server.cap(), s.tag(), L4_IPC_NEVER);
    if (err < 0)
      return err;
      
    s >> mode;
    // Update GRADD structures
    return 0;
  }
  
  void* MapFramebuffer() {
    // Get framebuffer dataspace from server
    L4::Cap<L4Re::Dataspace> fb_ds;
    
    L4::Ipc::Iostream s(l4_utcb());
    s << Intel_fb::OP_GET_FB_MEMORY;
    
    l4_ipc_call(_fb_server.cap(), s.tag(), L4_IPC_NEVER);
    s >> L4::Ipc::Rcv_fpage::mem(0) >> fb_ds;
    
    // Map into our address space
    void* fb_addr;
    L4Re::Env::env()->rm()->attach(&fb_addr, fb_ds->size(),
      L4Re::Rm::F::Search_addr | L4Re::Rm::F::RW, fb_ds);
      
    return fb_addr;
  }
};
```

## Building and Testing

### Build Steps

1. **Add to osFree build system:**
```bash
cd osfree/l4/pkg
mkdir intel_fb
# Copy files from above
```

2. **Configure build:**
```bash
cd osfree
make O=build-x86_64
cd build-x86_64
make config
# Enable intel_fb package
```

3. **Build:**
```bash
make
```

4. **Create boot configuration:**
```lua
-- In your l4re boot script
local fb = L4.default_loader:new_channel();
L4.default_loader:start(
  {
    caps = {
      vbus = vbus_cap,
      fb = fb:svr(),
    },
  },
  "rom/intel_fb_server"
);

-- Start OS/2 personality with framebuffer access
L4.default_loader:start(
  {
    caps = {
      intel_fb = fb,
    },
  },
  "rom/os2srv"
);
```

### Testing

1. **Basic functionality test:**
```cpp
// Test client
int main() {
  auto fb = L4Re::Env::env()->get_cap<Intel_fb::Server>("intel_fb");
  
  // Set 1920x1080 mode
  fb->set_mode(1920, 1080);
  
  // Get framebuffer
  void* fb_mem = fb->map_framebuffer();
  
  // Draw test pattern
  uint32_t* pixels = (uint32_t*)fb_mem;
  for (int i = 0; i < 1920 * 1080; i++) {
    pixels[i] = 0xFF0000FF; // Blue screen
  }
  
  return 0;
}
```

## Approach 2: DDE/Linux Integration

For the DDE approach, you'd leverage L4Linux:

### Structure
```
osfree/
├── l4/
│   └── pkg/
│       └── dde/
│           └── linux/
│               └── drm/      ← Port i915 here
│                   ├── src/
│                   │   └── i915/ ← Linux i915 driver
│                   └── dde_glue/ ← DDE compatibility layer
```

### Key Components

1. **Linux Driver Sources**: Copy from Linux kernel `drivers/gpu/drm/i915/`
2. **DDE Glue Layer**: Implement Linux APIs using L4Re primitives
3. **Build Integration**: Adapt Linux Kbuild to L4Re makefiles

The DDE layer emulates:
- `kmalloc`/`kfree` → L4Re memory allocators
- `ioremap` → L4Re dataspace mapping
- `request_irq` → L4 IRQ capabilities
- `pci_*` functions → L4vbus calls
- DMA operations → L4Re DMA spaces

## Recommendations

**For immediate functionality:**
- Start with **Approach 1** (Native L4 driver)
- Use the framebuffer pseudocode I provided earlier
- Adapt memory management and IPC as shown above
- This gets you a working display quickly

**For long-term maintainability:**
- Migrate to **Approach 2** (DDE/Linux)
- Full i915 driver with 3D acceleration
- Automatic updates from Linux mainline
- Better hardware support

**Best practice:**
- Implement Approach 1 first (2-4 weeks)
- Use it to validate your L4 integration
- Then port to DDE/Linux (2-3 months)
- Keep native driver as fallback

## Resources

- L4Re documentation: https://l4re.org
- L4Linux source: Check TU Dresden repositories
- Intel GPU documentation: https://01.org/linuxgraphics/documentation
- osFree wiki: http://www.osfree.org

## Next Steps

1. Clone osFree and build it
2. Create the intel_fb package structure
3. Implement basic PCI detection
4. Add MMIO mapping via L4vbus
5. Port modesetting code with L4 adaptations
6. Test with simple framebuffer operations
7. Integrate with OS/2 graphics layer

Would you like me to elaborate on any specific part of this integration?
