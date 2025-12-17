/*
 * osFree AP (Application Processor) Trampoline Code
 * Copyright (c) 2024 osFree Project
 * 
 * This code is copied to low memory (below 1MB) and executed
 * by Application Processors when they receive a STARTUP IPI.
 * APs start in 16-bit real mode and transition to protected/long mode.
 */

.code16
.section .text.ap_trampoline, "ax"

.globl ap_trampoline_start
.globl ap_trampoline_end

ap_trampoline_start:
    cli
    cld
    
    /* Setup segments */
    xorw    %ax, %ax
    movw    %ax, %ds
    movw    %ax, %es
    movw    %ax, %ss
    
    /* Load the GDT pointer (stored at known offset) */
    lgdtl   (ap_gdt_ptr - ap_trampoline_start + AP_TRAMPOLINE_ADDR)
    
    /* Enable protected mode */
    movl    %cr0, %eax
    orl     $0x1, %eax
    movl    %eax, %cr0
    
    /* Far jump to flush prefetch queue and enter 32-bit code */
    ljmpl   $0x08, $(ap_protected_entry - ap_trampoline_start + AP_TRAMPOLINE_ADDR)

.code32
ap_protected_entry:
    /* Setup 32-bit segments */
    movw    $0x10, %ax
    movw    %ax, %ds
    movw    %ax, %es
    movw    %ax, %ss
    movw    %ax, %fs
    movw    %ax, %gs
    
    /* Enable PAE (Physical Address Extension) */
    movl    %cr4, %eax
    orl     $0x20, %eax         /* CR4.PAE */
    movl    %eax, %cr4
    
    /* Load the PML4 (Page Map Level 4) address */
    movl    (ap_pml4_addr - ap_trampoline_start + AP_TRAMPOLINE_ADDR), %eax
    movl    %eax, %cr3
    
    /* Enable long mode via EFER MSR */
    movl    $0xC0000080, %ecx   /* EFER MSR */
    rdmsr
    orl     $0x100, %eax        /* EFER.LME (Long Mode Enable) */
    wrmsr
    
    /* Enable paging */
    movl    %cr0, %eax
    orl     $0x80000000, %eax   /* CR0.PG */
    movl    %eax, %cr0
    
    /* Load 64-bit GDT */
    lgdt    (ap_gdt64_ptr - ap_trampoline_start + AP_TRAMPOLINE_ADDR)
    
    /* Jump to 64-bit code */
    ljmpl   $0x08, $(ap_long_mode_entry - ap_trampoline_start + AP_TRAMPOLINE_ADDR)

.code64
ap_long_mode_entry:
    /* Setup 64-bit segments */
    movw    $0x10, %ax
    movw    %ax, %ds
    movw    %ax, %es
    movw    %ax, %ss
    movw    %ax, %fs
    movw    %ax, %gs
    
    /* Clear upper 32 bits of segment registers */
    xorl    %eax, %eax
    
    /* Get our APIC ID to determine which CPU we are */
    movl    $1, %eax
    cpuid
    shrl    $24, %ebx           /* APIC ID in EBX[31:24] */
    
    /* Look up our CPU ID and stack from the boot info table */
    movq    (ap_boot_info - ap_trampoline_start + AP_TRAMPOLINE_ADDR), %rdi
    
    /* Each entry is 16 bytes: [apic_id(4), cpu_id(4), stack_ptr(8)] */
    xorq    %rcx, %rcx
.find_cpu_loop:
    movl    (%rdi, %rcx, 1), %eax
    cmpl    %eax, %ebx
    je      .found_cpu
    addq    $16, %rcx
    cmpq    $4096, %rcx         /* Max entries */
    jl      .find_cpu_loop
    
    /* Not found - halt */
    cli
    hlt
    jmp     . - 2

.found_cpu:
    /* Load our stack pointer */
    movq    8(%rdi, %rcx, 1), %rsp
    addq    $KERNEL_STACK_SIZE, %rsp    /* Point to top of stack */
    
    /* Save CPU ID for C code */
    movl    4(%rdi, %rcx, 1), %edi
    
    /* Enable SSE */
    movq    %cr0, %rax
    andw    $0xFFFB, %ax        /* Clear CR0.EM */
    orw     $0x2, %ax           /* Set CR0.MP */
    movq    %rax, %cr0
    
    movq    %cr4, %rax
    orw     $0x600, %ax         /* Set CR4.OSFXSR and CR4.OSXMMEXCPT */
    movq    %rax, %cr4
    
    /* Jump to C entry point */
    movabsq $ap_entry, %rax
    call    *%rax
    
    /* Should never return */
    cli
    hlt
    jmp     . - 2

/* Align data structures */
.align 16

/* 32-bit GDT for initial protected mode */
ap_gdt:
    .quad   0x0000000000000000  /* Null descriptor */
    .quad   0x00CF9A000000FFFF  /* 32-bit code segment */
    .quad   0x00CF92000000FFFF  /* 32-bit data segment */
ap_gdt_end:

ap_gdt_ptr:
    .word   ap_gdt_end - ap_gdt - 1
    .long   ap_gdt - ap_trampoline_start + AP_TRAMPOLINE_ADDR

/* 64-bit GDT */
ap_gdt64:
    .quad   0x0000000000000000  /* Null descriptor */
    .quad   0x00209A0000000000  /* 64-bit code segment */
    .quad   0x0000920000000000  /* 64-bit data segment */
ap_gdt64_end:

ap_gdt64_ptr:
    .word   ap_gdt64_end - ap_gdt64 - 1
    .quad   ap_gdt64 - ap_trampoline_start + AP_TRAMPOLINE_ADDR

/* Page table address (filled in by BSP) */
.align 8
ap_pml4_addr:
    .long   0

/* Boot info table address (filled in by BSP) */
.align 8
ap_boot_info:
    .quad   0

/* Constants */
.set AP_TRAMPOLINE_ADDR, 0x8000
.set KERNEL_STACK_SIZE, 16384

ap_trampoline_end:

/*
 * Symbols that BSP code uses to fill in data
 */
.section .data
.globl ap_trampoline_pml4
.globl ap_trampoline_boot_info

ap_trampoline_pml4:
    .quad   ap_pml4_addr - ap_trampoline_start

ap_trampoline_boot_info:
    .quad   ap_boot_info - ap_trampoline_start
