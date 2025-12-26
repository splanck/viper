# VBoot - UEFI Bootloader CMakeLists.txt

# UEFI bootloader is C only (no C++ runtime in UEFI environment)
set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Source files
set(VBOOT_SOURCES
        crt0.S
        main.c
)

# Create ELF executable (will be converted to PE)
add_executable(vboot.elf ${VBOOT_SOURCES})

# UEFI-specific compiler flags
target_compile_options(vboot.elf PRIVATE
        -ffreestanding
        -fno-stack-protector
        -fno-stack-check
        -fshort-wchar          # UEFI uses 16-bit wchar
        -fPIC                   # Position independent code
        -Wall
        -Wextra
)

# UEFI-specific linker flags
target_link_options(vboot.elf PRIVATE
        -nostdlib
        -Wl,--no-dynamic-linker
        -Wl,-pie
        -Wl,-znocombreloc
        -T ${CMAKE_CURRENT_SOURCE_DIR}/vboot.ld
)

# Convert ELF to UEFI PE32+ executable
add_custom_command(TARGET vboot.elf POST_BUILD
        COMMAND ${CMAKE_OBJCOPY}
        -j .text
        -j .rodata
        -j .data
        -j .bss
        -j .rela.dyn
        -O pei-aarch64-little
        --subsystem=10
        $<TARGET_FILE:vboot.elf>
        ${CMAKE_BINARY_DIR}/BOOTAA64.EFI
        COMMENT "Converting vboot.elf to BOOTAA64.EFI"
)

# Add custom target for the EFI file
add_custom_target(bootloader ALL DEPENDS vboot.elf)
