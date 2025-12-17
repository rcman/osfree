# osFree Video Driver Support Options

This document outlines video driver architectures and implementations that can be added to osFree beyond the existing DDE (Device Driver Environment) support.

## Overview

osFree is an open-source OS/2 clone targeting OS/2 Warp 4 compatibility. The OS/2 video subsystem supports multiple driver models, each with different trade-offs between compatibility, performance, and implementation complexity.

## 1. GRADD Architecture

GRADD (Graphics Adapter Device Driver) was originally developed for OS/2 on PowerPC and provides a modular video driver architecture where driver writers use video helper services that shield them from OS semantics.

### Components

| Component | Description |
|-----------|-------------|
| **VMAN** | Video Manager - core coordination layer |
| **SOFTDRAW** | Software rendering fallback |
| **GRE2VMAN** | PM GRE to VMAN translation layer |
| **GENGRADD** | Generic VESA GRADD driver |

### VMAN Helper Services

VMAN exports helper services for GRADDs:

- `VHAllocMem` - Memory allocation
- `VHFreeMem` - Free allocated memory
- `VHLockMem` - Lock memory for ring 0 access
- `VHPhystoVirt` - Physical to virtual address conversion
- `VHMapVRAM` - Map video RAM

### Benefits

A typical GRADD can be written in approximately 2K-5K lines of code since it consists mainly of hardware-dependent code. The helper services handle most OS-level complexity.

## 2. GENGRADD (Generic VESA Driver)

GENGRADD is an unaccelerated SVGA driver that works with any video card supporting VESA BIOS extensions. This provides universal compatibility as a baseline driver.

### Features

- Works with essentially any VESA-compliant video hardware
- No hardware-specific acceleration
- Good fallback option for unsupported hardware
- Suitable for virtual machine environments

## 3. Virtual Machine Display Drivers

### Bochs Graphics Adapter (BGA)

The Bochs emulated graphics hardware is commonly used in QEMU, Bochs, and VirtualBox.

**I/O Ports:**
- `0x01CE` - Index port
- `0x01CF` - Data port

**Capabilities (VBE 0xB0C2+):**
- 15/16/24/32 BPP color modes
- Linear framebuffer support
- Configurable X/Y resolution
- Up to 256 MB video memory (QEMU stdvga)

**Supported Emulators:**
- QEMU (default stdvga device)
- Bochs
- VirtualBox (VBoxVGA mode)

### VirtualBox VBoxVGA

VirtualBox emulates a generic VESA-compatible device built upon the Bochs Graphics Adapter. Uses the same I/O ports as BGA with support up to VBE level 0xB0C4.

### VMware SVGA-II

A paravirtual display device with wider feature support.

**Supported Platforms:**
- VMware Workstation/Player
- VirtualBox (as VMSVGA)
- QEMU (vmware-svga device)

**Features:**
- 2D acceleration
- Cursor hardware support
- Multiple display support (depending on version)

## 4. Panorama-Style VESA Driver

Panorama is a modern video driver approach for OS/2 with the following goals:

- Universal accelerated VESA-compliant video driver
- Accelerated drivers for popular video adapters
- Active maintenance and modern hardware support

This architecture could serve as a reference for implementing modern VESA support in osFree.

## 5. SNAP Graphics Architecture

SNAP is a GRADD-based video driver providing accelerated support for select video chipsets. The source code has been licensed by Arca Noae from SciTech and could serve as reference material.

## Implementation Roadmap

### Phase 1: Core Infrastructure
1. Implement VMAN core services
2. Implement SOFTDRAW software renderer
3. Implement GRE2VMAN translation layer

### Phase 2: Generic Hardware Support
4. Implement GENGRADD for universal VESA compatibility
5. Test on real hardware and common virtual machines

### Phase 3: Virtual Machine Optimization
6. Implement Bochs/BGA driver for QEMU/Bochs/VirtualBox
7. Implement VMware SVGA driver for VMware environments

### Phase 4: Hardware Acceleration
8. Implement hardware-specific GRADDs (Intel, AMD, etc.)
9. Port or reference SNAP/Panorama acceleration code

## Architecture Diagram

```
┌─────────────────────────────────────────────────────┐
│                  PM / GRE Layer                     │
└─────────────────────┬───────────────────────────────┘
                      │
┌─────────────────────▼───────────────────────────────┐
│                   GRE2VMAN                          │
└─────────────────────┬───────────────────────────────┘
                      │
┌─────────────────────▼───────────────────────────────┐
│                     VMAN                            │
│  (Video Manager - coordinates all video access)    │
└───┬─────────────┬─────────────┬─────────────┬──────┘
    │             │             │             │
┌───▼───┐   ┌─────▼─────┐ ┌─────▼─────┐ ┌─────▼─────┐
│SOFTDRAW│   │ GENGRADD  │ │ BGA GRADD │ │ HW GRADD  │
│(SW)    │   │ (VESA)    │ │ (VM)      │ │ (Accel)   │
└───┬───┘   └─────┬─────┘ └─────┬─────┘ └─────┬─────┘
    │             │             │             │
────▼─────────────▼─────────────▼─────────────▼──────
                    Hardware / VM
```

## MVM Integration

The osFree MVM (Multiple Virtual Machine) subsystem uses GRADD for seamless video sharing between OS/2 and DOS/Windows sessions. The VVMI component enables communication between guest video drivers and GRADD's VMAN, allowing multiple graphics engines to share the same display.

## References

- [osFree Project](http://www.osfree.org/)
- [osFree GitHub](https://github.com/osfree-project/osfree)
- [OS/2 Museum](http://www.os2museum.com/)
- [OSDev Wiki - Bochs VBE](https://wiki.osdev.org/Bochs_VBE_Extensions)
- [OSDev Wiki - VMware SVGA-II](https://wiki.osdev.org/VMware_SVGA-II)

## License

This documentation is provided for the osFree project. See the main project repository for licensing information.
