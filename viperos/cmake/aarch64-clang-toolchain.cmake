# AArch64 Cross-Compilation Toolchain for ViperOS (Clang)
# Targets: vboot (C), kernel (C++)

set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

# Clang with AArch64 bare-metal target
set(CMAKE_C_COMPILER clang)
set(CMAKE_CXX_COMPILER clang++)
set(CMAKE_ASM_COMPILER clang)

# Target triple for bare-metal AArch64
set(CMAKE_C_COMPILER_TARGET aarch64-none-elf)
set(CMAKE_CXX_COMPILER_TARGET aarch64-none-elf)
set(CMAKE_ASM_COMPILER_TARGET aarch64-none-elf)

# Find GNU binutils for linking and object manipulation
# (LLD and llvm-ar may not be available on all systems)
find_program(AARCH64_ELF_LD aarch64-elf-ld REQUIRED)
find_program(AARCH64_ELF_OBJCOPY aarch64-elf-objcopy REQUIRED)
find_program(AARCH64_ELF_AR aarch64-elf-ar REQUIRED)
find_program(AARCH64_ELF_RANLIB aarch64-elf-ranlib REQUIRED)

set(CMAKE_LINKER ${AARCH64_ELF_LD})
set(CMAKE_OBJCOPY ${AARCH64_ELF_OBJCOPY})
set(CMAKE_AR ${AARCH64_ELF_AR})
set(CMAKE_RANLIB ${AARCH64_ELF_RANLIB})

# Common flags for freestanding environment
# Note: -mgeneral-regs-only is NOT set here - only kernel should use it
# Userspace needs FPU access for math library
set(COMMON_FLAGS "-ffreestanding -nostdlib -mcpu=cortex-a72")
set(COMMON_FLAGS "${COMMON_FLAGS} -Wall -Wextra -Werror")
set(COMMON_FLAGS "${COMMON_FLAGS} -fno-stack-protector")
set(COMMON_FLAGS "${COMMON_FLAGS} -mstrict-align")
set(COMMON_FLAGS "${COMMON_FLAGS} -fno-pie")

# Clang-specific: suppress some warnings that are common in kernel code
set(COMMON_FLAGS "${COMMON_FLAGS} -Wno-unused-command-line-argument")

# C flags
set(CMAKE_C_FLAGS_INIT "${COMMON_FLAGS}")

# C++ flags (additional restrictions for freestanding C++)
set(CMAKE_CXX_FLAGS_INIT "${COMMON_FLAGS}")
set(CMAKE_CXX_FLAGS_INIT "${CMAKE_CXX_FLAGS_INIT} -fno-exceptions")
set(CMAKE_CXX_FLAGS_INIT "${CMAKE_CXX_FLAGS_INIT} -fno-rtti")
set(CMAKE_CXX_FLAGS_INIT "${CMAKE_CXX_FLAGS_INIT} -fno-threadsafe-statics")
set(CMAKE_CXX_FLAGS_INIT "${CMAKE_CXX_FLAGS_INIT} -fno-use-cxa-atexit")

# Linker flags - explicitly use GNU ld via --ld-path (Clang defaults to LLD which may not be installed)
set(CMAKE_EXE_LINKER_FLAGS_INIT "-nostdlib -static --ld-path=${AARCH64_ELF_LD}")

# Don't try to compile test programs (we're freestanding)
set(CMAKE_C_COMPILER_WORKS 1)
set(CMAKE_CXX_COMPILER_WORKS 1)

# Search paths
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
