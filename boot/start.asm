; Sagittarius SGS-1 — Boot Entry v2
bits 32

section .multiboot
align 8
mb_start:
    dd 0xE85250D6
    dd 0
    dd mb_end - mb_start
    dd -(0xE85250D6 + 0 + (mb_end - mb_start))
    dw 0, 0
    dd 8
mb_end:

section .bss
align 16
stack_bottom: resb 32768
stack_top:

section .text
global _start
extern kernel_main

_start:
    cli
    mov esp, stack_top
    and esp, 0xFFFFFFF0    ; выровнять по 16 байт ← добавь эту строку
    push ebx
    push eax
    call kernel_main
.halt:
    cli
    hlt
    jmp .halt