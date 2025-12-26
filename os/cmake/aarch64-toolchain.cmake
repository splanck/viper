# AArch64 Cross-Compilation Toolchain for ViperOS
# Targets: vboot (C), kernel (C++)

set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

# Cross-compilers for macOS (Homebrew aarch64-elf-*)
set(CMAKE_C_COMPILER aarch64-elf-gcc)
set(CMAKE_CXX_COMPILER aarch64-elf-g++)
set(CMAKE_ASM_COMPILER aarch64-elf-gcc)
set(CMAKE_OBJCOPY aarch64-elf-objcopy)
set(CMAKE_AR aarch64-elf-ar)
set(CMAKE_RANLIB aarch64-elf-ranlib)

# Common flags for freestanding environment
set(COMMON_FLAGS "-ffreestanding -nostdlib -mcpu=cortex-a72")
set(COMMON_FLAGS "${COMMON_FLAGS} -Wall -Wextra -Werror")
set(COMMON_FLAGS "${COMMON_FLAGS} -fno-stack-protector")
set(COMMON_FLAGS "${COMMON_FLAGS} -mgeneral-regs-only")
set(COMMON_FLAGS "${COMMON_FLAGS} -mstrict-align")
set(COMMON_FLAGS "${COMMON_FLAGS} -fno-pie -no-pie")

# C flags
set(CMAKE_C_FLAGS_INIT "${COMMON_FLAGS}")

# C++ flags (additional restrictions for freestanding C++)
set(CMAKE_CXX_FLAGS_INIT "${COMMON_FLAGS}")
set(CMAKE_CXX_FLAGS_INIT "${CMAKE_CXX_FLAGS_INIT} -fno-exceptions")
set(CMAKE_CXX_FLAGS_INIT "${CMAKE_CXX_FLAGS_INIT} -fno-rtti")
set(CMAKE_CXX_FLAGS_INIT "${CMAKE_CXX_FLAGS_INIT} -fno-threadsafe-statics")
set(CMAKE_CXX_FLAGS_INIT "${CMAKE_CXX_FLAGS_INIT} -fno-use-cxa-atexit")

# Linker flags
set(CMAKE_EXE_LINKER_FLAGS_INIT "-nostdlib -static")

# Don't try to compile test programs (we're freestanding)
set(CMAKE_C_COMPILER_WORKS 1)
set(CMAKE_CXX_COMPILER_WORKS 1)

# Search paths
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
