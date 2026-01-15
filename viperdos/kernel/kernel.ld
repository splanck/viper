/* ViperDOS Kernel Linker Script - AArch64 */

OUTPUT_FORMAT(elf64-littleaarch64)
OUTPUT_ARCH(aarch64)
ENTRY(_start)

/* QEMU virt machine loads kernel at 0x40000000 */
KERNEL_PHYS_BASE = 0x40000000;

/* Future: kernel virtual address when using MMU */
/* KERNEL_VIRT_BASE = 0xFFFFFFFF00000000; */

SECTIONS {
    . = KERNEL_PHYS_BASE;
    __kernel_start = .;

    /* Text section - executable code */
    .text : {
        __text_start = .;
        *(.text.boot)       /* Entry point first */
        *(.text .text.*)
        __text_end = .;
    }

    . = ALIGN(4096);

    /* Read-only data */
    .rodata : {
        __rodata_start = .;
        *(.rodata .rodata.*)
        __rodata_end = .;
    }

    . = ALIGN(4096);

    /* Writable initialized data */
    .data : {
        __data_start = .;
        *(.data .data.*)
        __data_end = .;
    }

    . = ALIGN(4096);

    /* Uninitialized data (zeroed by startup code) */
    .bss : {
        __bss_start = .;
        *(.bss .bss.*)
        *(COMMON)
        __bss_end = .;
    }

    . = ALIGN(4096);

    __kernel_end = .;

    /DISCARD/ : {
        *(.comment)
        *(.note.*)
        *(.eh_frame*)
    }
}
