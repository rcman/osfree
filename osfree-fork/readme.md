# osFree Multi-Core CPU & Modern Hardware Update

This document describes the comprehensive updates to add multi-core CPU (SMP) support and modern hardware compatibility to the osFree project.

## Overview of Changes

### New Features

1. **Symmetric Multi-Processing (SMP) Support**
   - Per-CPU run queues with O(1) scheduling
   - Inter-Processor Interrupts (IPI)
   - CPU hotplug support
   - Load balancing across CPUs
   - CPU affinity for threads

2. **Modern Hardware Support**
   - Local APIC and I/O APIC (including x2APIC)
   - ACPI table parsing (MADT, SRAT, SLIT, MCFG)
   - PCIe with MSI/MSI-X interrupts
   - NVMe storage
   - xHCI (USB 3.x)
   - AHCI (SATA)
   - UEFI boot support
   - GOP framebuffer

3. **NUMA Support**
   - NUMA topology discovery via ACPI
   - NUMA-aware memory allocation
   - Node distance matrix

4. **New Build System**
   - CMake-based build
   - Cross-compilation support
   - QEMU testing targets

## File Structure

```
osfree/
├── include/
│   └── os3/
│       ├── smp.h           # SMP data structures and API
│       ├── spinlock.h      # SMP-safe locking primitives
│       ├── atomic.h        # Atomic operations
│       ├── scheduler.h     # SMP-aware scheduler
│       ├── apic.h          # Local/IO APIC support
│       ├── acpi.h          # ACPI table parsing
│       └── modern_hw.h     # Modern hardware (NVMe, xHCI, etc.)
├── kernel/
│   ├── smp/
│   │   ├── smp.c           # SMP initialization and IPI
│   │   └── ap_trampoline.S # AP bootstrap code
│   ├── sched/
│   │   └── scheduler.c     # Per-CPU run queues
│   ├── apic/
│   │   └── apic.c          # APIC implementation
│   ├── mm/
│   │   └── numa.c          # NUMA-aware allocator
│   └── arch/x86_64/
│       └── kernel.ld       # 64-bit linker script
├── os3/
│   └── api/
│       └── doscalls_thread.c # OS/2 thread API (SMP-aware)
├── CMakeLists.txt          # Modern build system
└── README-SMP-UPDATE.md    # This file
```

## How to Apply These Changes

### Step 1: Clone and Prepare Repository

```bash
# Clone your fork
git clone https://github.com/rcman/osfree.git
cd osfree

# Create a new branch for SMP updates
git checkout -b feature/smp-multicore-support
```

### Step 2: Create Directory Structure

```bash
mkdir -p include/os3
mkdir -p kernel/smp
mkdir -p kernel/sched
mkdir -p kernel/apic
mkdir -p kernel/mm
mkdir -p kernel/arch/x86_64
mkdir -p os3/api
```

### Step 3: Copy the New Files

Copy each artifact file to its appropriate location:

```bash
# Headers
cp smp.h include/os3/
cp spinlock.h include/os3/
cp atomic.h include/os3/
cp scheduler.h include/os3/
cp apic.h include/os3/
cp acpi.h include/os3/
cp modern_hw.h include/os3/

# Kernel sources
cp smp.c kernel/smp/
cp ap_trampoline.S kernel/smp/
cp scheduler.c kernel/sched/
cp apic.c kernel/apic/
cp numa.c kernel/mm/
cp kernel.ld kernel/arch/x86_64/

# OS/2 API
cp doscalls_thread.c os3/api/

# Build system
cp CMakeLists.txt ./
```

### Step 4: Build the Project

```bash
# Create build directory
mkdir build && cd build

# Configure with CMake
cmake .. -DOSFREE_SMP=ON -DOSFREE_64BIT=ON -DOSFREE_DEBUG=ON

# Build
make -j$(nproc)

# Create bootable ISO
make iso
```

### Step 5: Test with QEMU

```bash
# Run with 4 CPUs
make qemu

# Or manually:
qemu-system-x86_64 \
    -cdrom osfree.iso \
    -m 512M \
    -smp 4 \
    -serial stdio \
    -enable-kvm
```

### Step 6: Commit and Push

```bash
git add .
git commit -m "Add SMP multi-core support and modern hardware compatibility"
git push origin feature/smp-multicore-support
```

## Key Implementation Details

### SMP Boot Sequence

1. **BSP (Bootstrap Processor) Initialization**
   - Parse ACPI MADT to discover CPUs
   - Initialize Local APIC
   - Initialize I/O APICs
   - Setup per-CPU scheduler run queue

2. **AP (Application Processor) Startup**
   - Copy trampoline code to low memory (0x8000)
   - Send INIT IPI to wake AP
   - Send STARTUP IPI with trampoline address
   - AP transitions: Real Mode → Protected Mode → Long Mode
   - AP initializes its Local APIC and joins scheduler

### Scheduler Design

- **Per-CPU Run Queues**: Each CPU has its own run queue with independent locking
- **O(1) Scheduling**: Bitmap-based priority lookup
- **Priority Classes**: OS/2 compatible (Idle, Regular, Time-Critical, Server)
- **Load Balancing**: Periodic migration of threads between CPUs
- **CPU Affinity**: Threads can be bound to specific CPUs

### Locking Strategy

- **Ticket Spinlocks**: Fair FIFO ordering, prevents starvation
- **Read-Write Locks**: Multiple readers OR single writer
- **Sequence Locks**: For read-mostly data (readers don't block)
- **IRQ-Safe Variants**: For code that may run in interrupt context

### New OS/2 API Extensions

```c
// Set CPU affinity for a thread
APIRET DosSetThreadAffinity(TID tid, ULONG64 affinity_mask);

// Get CPU affinity
APIRET DosGetThreadAffinity(TID tid, PULONG64 paffinity_mask);

// Query number of processors
DosQuerySysInfo(QSV_NUMPROCESSORS, ...);

// Query current CPU ID
DosQuerySysInfo(QSV_PROCESSOR_ID, ...);
```

## Configuration Options

| CMake Option | Default | Description |
|--------------|---------|-------------|
| `OSFREE_SMP` | ON | Enable multi-core support |
| `OSFREE_64BIT` | ON | Build 64-bit kernel |
| `OSFREE_UEFI` | ON | UEFI boot support |
| `OSFREE_NVME` | ON | NVMe storage driver |
| `OSFREE_XHCI` | ON | USB 3.x support |
| `OSFREE_AHCI` | ON | SATA support |
| `OSFREE_DEBUG` | ON | Debug symbols and logging |

## Testing Checklist

- [ ] Single CPU boot works
- [ ] Multi-CPU boot (4+ CPUs) works
- [ ] Scheduler distributes load across CPUs
- [ ] Thread affinity works correctly
- [ ] IPIs delivered correctly
- [ ] No deadlocks under load
- [ ] OS/2 threading APIs work
- [ ] ACPI tables parsed correctly
- [ ] APIC timer interrupts on all CPUs

## Known Limitations

1. **CPU Hotplug**: Online only at boot (no runtime add/remove)
2. **Power Management**: Basic only (no C-states optimization)
3. **Real-Time**: Soft real-time only
4. **Max CPUs**: 256 (can be increased)

## Future Improvements

- [ ] Runtime CPU hotplug
- [ ] Advanced power management (P-states, C-states)
- [ ] NUMA-aware scheduler
- [ ] Hardware performance counters
- [ ] RCU (Read-Copy-Update) synchronization

## References

- Intel Software Developer Manuals (Vol 3A: System Programming)
- AMD64 Architecture Programmer's Manual
- ACPI Specification 6.4
- OS/2 Warp Control Program Reference

## License

This code is part of the osFree project and is licensed under the same terms as the main project.
