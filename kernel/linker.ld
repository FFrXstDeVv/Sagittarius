ENTRY(_start)

SECTIONS {
    . = 0x100000;

    .multiboot ALIGN(8) : {
        *(.multiboot)
    }

    .text ALIGN(4) : {
        *(.text*)
    }

    .rodata ALIGN(4) : {
        *(.rodata*)
    }

    .data ALIGN(4) : {
        *(.data*)
    }

    .bss ALIGN(16) : {
        *(COMMON)
        *(.bss*)
    }

    /DISCARD/ : {
        *(.comment)
        *(.eh_frame)
        *(.note*)
    }
}