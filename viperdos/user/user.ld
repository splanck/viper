/* Userspace linker script for ViperDOS */

ENTRY(_start)

SECTIONS
{
    . = 0x80000000;  /* USER_CODE_BASE */

    .text : {
        *(.text._start)
        *(.text .text.*)
    }

    .rodata : {
        *(.rodata .rodata.*)
    }

    .data : {
        *(.data .data.*)
        *(.tdata .tdata.*)    /* Thread-local initialized data (single-threaded: treat as .data) */
    }

    .bss : {
        __bss_start = .;
        *(.bss .bss.*)
        *(.tbss .tbss.*)      /* Thread-local zero-init data (single-threaded: treat as .bss) */
        *(COMMON)
        __bss_end = .;
    }

    /DISCARD/ : {
        *(.comment)
        *(.note.*)
        *(.eh_frame*)
    }
}
