/*
 * osFree x86_64 Kernel Linker Script
 * Copyright (c) 2024 osFree Project
 *
 * Memory layout for 64-bit kernel with SMP support
 */

OUTPUT_FORMAT("elf64-x86-64")
OUTPUT_ARCH(i386:x86-64)
ENTRY(_start)

/* Kernel virtual base address (higher half) */
KERNEL_VMA = 0xFFFFFFFF80000000;
KERNEL_LMA = 0x100000;  /* 1MB physical */

SECTIONS
{
    /* Start at 1MB physical, mapped to higher half virtual */
    . = KERNEL_VMA + KERNEL_LMA;
    
    _kernel_start = .;
    _kernel_phys_start = . - KERNEL_VMA;
    
    /*
     * Boot section - must be identity mapped initially
     */
    .boot ALIGN(4K) : AT(ADDR(.boot) - KERNEL_VMA)
    {
        _boot_start = .;
        *(.multiboot)
        *(.multiboot2)
        *(.boot)
        *(.boot.*)
        _boot_end = .;
    }
    
    /*
     * Text section - executable code
     */
    .text ALIGN(4K) : AT(ADDR(.text) - KERNEL_VMA)
    {
        _text_start = .;
        *(.text)
        *(.text.*)
        
        /* Interrupt handlers */
        . = ALIGN(16);
        _isr_start = .;
        *(.isr)
        _isr_end = .;
        
        /* Syscall handlers */
        . = ALIGN(16);
        _syscall_start = .;
        *(.syscall)
        _syscall_end = .;
        
        _text_end = .;
    }
    
    /*
     * Read-only data
     */
    .rodata ALIGN(4K) : AT(ADDR(.rodata) - KERNEL_VMA)
    {
        _rodata_start = .;
        *(.rodata)
        *(.rodata.*)
        
        /* Exception tables */
        . = ALIGN(8);
        _extable_start = .;
        *(.extable)
        _extable_end = .;
        
        _rodata_end = .;
    }
    
    /*
     * Initialization data - can be reclaimed after boot
     */
    .init ALIGN(4K) : AT(ADDR(.init) - KERNEL_VMA)
    {
        _init_start = .;
        *(.init)
        *(.init.*)
        
        /* Init call tables */
        . = ALIGN(8);
        _initcall_start = .;
        KEEP(*(.initcall0))
        KEEP(*(.initcall1))
        KEEP(*(.initcall2))
        KEEP(*(.initcall3))
        KEEP(*(.initcall4))
        KEEP(*(.initcall5))
        KEEP(*(.initcall6))
        KEEP(*(.initcall7))
        _initcall_end = .;
        
        _init_end = .;
    }
    
    /*
     * Data section - initialized data
     */
    .data ALIGN(4K) : AT(ADDR(.data) - KERNEL_VMA)
    {
        _data_start = .;
        *(.data)
        *(.data.*)
        
        /* Global Descriptor Table */
        . = ALIGN(16);
        _gdt_start = .;
        *(.gdt)
        _gdt_end = .;
        
        /* Interrupt Descriptor Table */
        . = ALIGN(16);
        _idt_start = .;
        *(.idt)
        _idt_end = .;
        
        _data_end = .;
    }
    
    /*
     * Per-CPU data section
     * Each CPU gets its own copy of this section
     */
    .percpu ALIGN(4K) : AT(ADDR(.percpu) - KERNEL_VMA)
    {
        _percpu_start = .;
        *(.percpu)
        *(.percpu.*)
        . = ALIGN(64);  /* Cache line alignment */
        _percpu_end = .;
    }
    
    /* Size of per-CPU section (for replication) */
    _percpu_size = _percpu_end - _percpu_start;
    
    /*
     * BSS section - uninitialized data
     */
    .bss ALIGN(4K) : AT(ADDR(.bss) - KERNEL_VMA)
    {
        _bss_start = .;
        *(.bss)
        *(.bss.*)
        *(COMMON)
        
        /* Initial kernel stack (64KB) */
        . = ALIGN(4K);
        _stack_bottom = .;
        . += 64K;
        _stack_top = .;
        
        /* Per-CPU stacks placeholder
         * Actual stacks allocated dynamically during SMP init
         */
        . = ALIGN(4K);
        _percpu_stacks = .;
        
        _bss_end = .;
    }
    
    /*
     * Page tables section
     */
    .pgtables ALIGN(4K) : AT(ADDR(.pgtables) - KERNEL_VMA)
    {
        _pgtables_start = .;
        
        /* PML4 - Page Map Level 4 (512 entries) */
        . = ALIGN(4K);
        pml4 = .;
        . += 4K;
        
        /* PDPT - Page Directory Pointer Table */
        . = ALIGN(4K);
        pdpt_low = .;   /* For identity mapping */
        . += 4K;
        pdpt_high = .;  /* For higher half */
        . += 4K;
        
        /* PD - Page Directory (for initial mapping) */
        . = ALIGN(4K);
        pd_low = .;
        . += 4K;
        pd_high = .;
        . += 4K;
        
        _pgtables_end = .;
    }
    
    /*
     * AP Trampoline section (copied to low memory)
     */
    .ap_trampoline ALIGN(4K) : AT(ADDR(.ap_trampoline) - KERNEL_VMA)
    {
        _ap_trampoline_start = .;
        *(.text.ap_trampoline)
        _ap_trampoline_end = .;
    }
    
    _ap_trampoline_size = _ap_trampoline_end - _ap_trampoline_start;
    
    /*
     * Debug sections
     */
    .debug_info     0 : { *(.debug_info) }
    .debug_abbrev   0 : { *(.debug_abbrev) }
    .debug_line     0 : { *(.debug_line) }
    .debug_frame    0 : { *(.debug_frame) }
    .debug_str      0 : { *(.debug_str) }
    .debug_loc      0 : { *(.debug_loc) }
    .debug_ranges   0 : { *(.debug_ranges) }
    
    /*
     * Symbol table for debugging
     */
    .symtab         0 : { *(.symtab) }
    .strtab         0 : { *(.strtab) }
    
    /*
     * End markers
     */
    . = ALIGN(4K);
    _kernel_end = .;
    _kernel_phys_end = . - KERNEL_VMA;
    _kernel_size = _kernel_end - _kernel_start;
    
    /*
     * Discard unwanted sections
     */
    /DISCARD/ :
    {
        *(.comment)
        *(.note)
        *(.note.*)
        *(.eh_frame)
        *(.eh_frame_hdr)
        *(.gnu.hash)
        *(.gnu.version*)
    }
}

/*
 * Assertions to verify layout
 */
ASSERT(_kernel_phys_start == 0x100000, "Kernel must start at 1MB physical")
ASSERT(_percpu_size < 64K, "Per-CPU section too large")
ASSERT(_ap_trampoline_size < 4K, "AP trampoline too large for single page")
