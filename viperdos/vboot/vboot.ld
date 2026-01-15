/* VBoot UEFI Linker Script */
/* Position-independent executable for conversion to PE */

OUTPUT_FORMAT(elf64-littleaarch64)
OUTPUT_ARCH(aarch64)
ENTRY(_start)

SECTIONS {
    . = 0;

    /* Text section - executable code */
    .text : {
        *(.text.entry)   /* Entry point first */
        *(.text .text.*)
    }

    . = ALIGN(4096);

    /* Read-only data */
    .rodata : {
        *(.rodata .rodata.*)
    }

    . = ALIGN(4096);

    /* Writable data */
    .data : {
        *(.sdata .sdata.*)
        *(.data .data.*)
        *(.got .got.*)
        *(.got.plt)
    }

    . = ALIGN(4096);

    /* Uninitialized data */
    .bss : {
        *(.sbss .sbss.*)
        *(.bss .bss.*)
        *(COMMON)
    }

    . = ALIGN(4096);

    /* Relocations - needed for PIE */
    .rela.dyn : {
        *(.rela .rela.* .rela.dyn)
    }

    /DISCARD/ : {
        *(.comment)
        *(.note.*)
        *(.eh_frame*)
        *(.interp)
        *(.gnu.hash)
        *(.dynsym)
        *(.dynstr)
        *(.dynamic)
    }
}
