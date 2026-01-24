# VBoot - UEFI Bootloader CMakeLists.txt
#
# Builds a proper PE32+ executable for UEFI using LLVM toolchain.
# Requires: Homebrew llvm and lld packages

# Find LLVM tools (prefer Homebrew LLVM over Apple Clang for PE support)
find_program(LLVM_CLANG clang PATHS /opt/homebrew/opt/llvm/bin NO_DEFAULT_PATH)
find_program(LLD_LINK lld-link PATHS /opt/homebrew/bin NO_DEFAULT_PATH)

if (NOT LLVM_CLANG)
    # Fallback to system clang
    find_program(LLVM_CLANG clang)
endif ()

if (NOT LLD_LINK)
    find_program(LLD_LINK lld-link)
endif ()

if (NOT LLVM_CLANG OR NOT LLD_LINK)
    message(WARNING "LLVM clang or lld-link not found - bootloader will not be built")
    message(WARNING "Install with: brew install llvm lld")
    return()
endif ()

message(STATUS "VBoot using clang: ${LLVM_CLANG}")
message(STATUS "VBoot using lld-link: ${LLD_LINK}")

# Output directory
set(VBOOT_OUTPUT_DIR ${CMAKE_BINARY_DIR})

# Source files
set(VBOOT_C_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/main.c)
set(VBOOT_ASM_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/crt0.S)

# Compile flags for UEFI AArch64
set(VBOOT_CFLAGS
        -target aarch64-unknown-windows
        -ffreestanding
        -fno-stack-protector
        -fno-stack-check
        -fshort-wchar
        -mno-red-zone
        -Wall
        -Wextra
        -O2
)

# Object files
set(VBOOT_OBJS
        ${CMAKE_CURRENT_BINARY_DIR}/main.obj
        ${CMAKE_CURRENT_BINARY_DIR}/crt0.obj
)

# Compile C source
add_custom_command(
        OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/main.obj
        COMMAND ${LLVM_CLANG} ${VBOOT_CFLAGS} -c ${VBOOT_C_SOURCES} -o ${CMAKE_CURRENT_BINARY_DIR}/main.obj
        DEPENDS ${VBOOT_C_SOURCES}
        COMMENT "Compiling main.c for UEFI"
)

# Compile assembly (need different target for asm)
add_custom_command(
        OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/crt0.obj
        COMMAND ${LLVM_CLANG} -target aarch64-unknown-windows -c ${VBOOT_ASM_SOURCES} -o ${CMAKE_CURRENT_BINARY_DIR}/crt0.obj
        DEPENDS ${VBOOT_ASM_SOURCES}
        COMMENT "Compiling crt0.S for UEFI"
)

# Link to PE executable
# lld-link options:
#   /subsystem:efi_application - UEFI application
#   /entry:_start - entry point
#   /nodefaultlib - no default libraries
add_custom_command(
        OUTPUT ${VBOOT_OUTPUT_DIR}/BOOTAA64.EFI
        COMMAND ${LLD_LINK}
        /subsystem:efi_application
        /entry:_start
        /nodefaultlib
        /out:${VBOOT_OUTPUT_DIR}/BOOTAA64.EFI
        ${VBOOT_OBJS}
        DEPENDS ${VBOOT_OBJS}
        COMMENT "Linking BOOTAA64.EFI"
)

# Also create ELF version for debugging (using the cross-linker)
if (AARCH64_ELF_LD)
    add_executable(vboot.elf
            crt0.S
            main.c
    )

    target_compile_options(vboot.elf PRIVATE
            -ffreestanding
            -fno-stack-protector
            -fno-stack-check
            -fshort-wchar
            -fPIC
            -Wall
            -Wextra
    )

    target_link_options(vboot.elf PRIVATE
            -nostdlib
            -Wl,--no-dynamic-linker
            -Wl,-pie
            -Wl,-znocombreloc
            -T ${CMAKE_CURRENT_SOURCE_DIR}/vboot.ld
    )
endif ()

# Main bootloader target
add_custom_target(bootloader ALL
        DEPENDS ${VBOOT_OUTPUT_DIR}/BOOTAA64.EFI
)
