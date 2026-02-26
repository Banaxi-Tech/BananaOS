MBALIGN  equ  1 << 0            ; align loaded modules on page boundaries
MEMINFO  equ  1 << 1            ; provide memory map
VIDMODE  equ  1 << 2            ; provide video mode table
FLAGS    equ  MBALIGN | MEMINFO | VIDMODE
MAGIC    equ  0x1BADB002        ; 'magic number' lets bootloader find the header
CHECKSUM equ -(MAGIC + FLAGS)   ; checksum required

section .multiboot
align 4
    dd MAGIC
    dd FLAGS
    dd CHECKSUM
    ; These next 5 fields determine the desired video mode
    dd 0    ; header_addr
    dd 0    ; load_addr
    dd 0    ; load_end_addr
    dd 0    ; bss_end_addr
    dd 0    ; entry_addr
    ; Graphical Requirements
    dd 0    ; mode_type (0 for linear graphics)
    dd 1920 ; width
    dd 1080 ; height
    dd 32   ; color depth

section .bss
align 16
stack_bottom:
resb 16384 ; 16 KiB
stack_top:

section .text
global _start
extern kernel_main

_start:
    mov esp, stack_top
    
    ; Pass Multiboot Magic Number and Pointer to Kernel
    push ebx ; MBOOT_PTR
    push eax ; MAGIC
    
    call kernel_main
    
    cli
.hang:  hlt
    jmp .hang

extern isr6_handler
global as_isr6
as_isr6:
    pushad
    push ds
    push es
    push fs
    push gs
    
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    
    push esp ; Pass stack pointer (struct registers*)
    call isr6_handler
    add esp, 4
    
    pop gs
    pop fs
    pop es
    pop ds
    popad
    iret
