# ViperDOS Kernel CMakeLists.txt

# Kernel is C++ (freestanding)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# -----------------------------------------------------------------------------
# Build-time feature toggles
# -----------------------------------------------------------------------------

option(VIPER_KERNEL_ENABLE_FS "Enable in-kernel filesystem (VFS + ViperFS)" ON)
option(VIPER_KERNEL_ENABLE_NET "Enable in-kernel TCP/IP stack + socket/DNS syscalls" ON)
option(VIPER_KERNEL_ENABLE_TLS "Enable kernel-managed TLS sessions (SYS_TLS_*)" OFF)
option(VIPER_KERNEL_DEBUG_NET_SYSCALL "Enable verbose socket/DNS syscall logging" OFF)
option(VIPER_KERNEL_DEBUG_TCP "Enable verbose TCP stack logging" OFF)
option(VIPER_KERNEL_DEBUG_VIRTIO_NET_IRQ "Enable verbose virtio-net IRQ logging" OFF)

# Source files
set(KERNEL_SOURCES
        crt.cpp
        arch/aarch64/boot.S
        arch/aarch64/exceptions.S
        arch/aarch64/exceptions.cpp
        arch/aarch64/gic.cpp
        arch/aarch64/timer.cpp
        arch/aarch64/mmu.cpp
        arch/aarch64/cpu.cpp
        boot/bootinfo.cpp
        dtb/fdt.cpp
        main.cpp
        console/serial.cpp
        console/console.cpp
        console/font.cpp
        console/gcon.cpp
        drivers/fwcfg.cpp
        drivers/ramfb.cpp
        mm/pmm.cpp
        mm/buddy.cpp
        mm/vmm.cpp
        mm/kheap.cpp
        mm/slab.cpp
        mm/fault.cpp
        mm/vma.cpp
        mm/cow.cpp
        mm/pressure.cpp
        lib/timerwheel.cpp
        sched/task.cpp
        sched/context.S
        sched/scheduler.cpp
        sched/wait.cpp
        sched/signal.cpp
        sched/idle.cpp
        sched/pi.cpp
        sched/deadline.cpp
        syscall/dispatch.cpp
        syscall/table.cpp
        syscall/handlers/task.cpp
        syscall/handlers/channel.cpp
        syscall/handlers/poll.cpp
        syscall/handlers/time.cpp
        syscall/handlers/file.cpp
        syscall/handlers/dir.cpp
        syscall/handlers/net.cpp
        syscall/handlers/cap.cpp
        syscall/handlers/handle_fs.cpp
        syscall/handlers/signal.cpp
        syscall/handlers/procgroup.cpp
        syscall/handlers/assign.cpp
        syscall/handlers/tls.cpp
        syscall/handlers/sysinfo.cpp
        syscall/handlers/debug.cpp
        syscall/handlers/device.cpp
        syscall/handlers/gui.cpp
        syscall/handlers/tty.cpp
        ipc/channel.cpp
        ipc/poll.cpp
        ipc/pollset.cpp
        viper/viper.cpp
        viper/address_space.cpp
        cap/table.cpp
        kobj/blob.cpp
        kobj/channel.cpp
        kobj/file.cpp
        kobj/dir.cpp
        kobj/shm.cpp
        loader/elf.cpp
        loader/loader.cpp
        drivers/virtio/virtio.cpp
        drivers/virtio/virtqueue.cpp
        drivers/virtio/blk.cpp
        drivers/virtio/gpu.cpp
        drivers/virtio/input.cpp
        drivers/virtio/rng.cpp
        drivers/virtio/net.cpp
        net/netstack.cpp
        input/input.cpp
        tty/tty.cpp
        fs/cache.cpp
        fs/viperfs/viperfs.cpp
        fs/viperfs/journal.cpp
        fs/vfs/vfs.cpp
        # Assign system (v0.2.0)
        assign/assign.cpp
        # Tests
        tests/ipc_tests.cpp
        tests/storage_tests.cpp
        tests/viper_tests.cpp
        tests/syscall_tests.cpp
        tests/userfault_tests.cpp
)

# Create kernel executable
add_executable(kernel.sys ${KERNEL_SOURCES})

# Compile-time config macros (see kernel/include/config.hpp)
target_compile_definitions(kernel.sys PRIVATE
        VIPER_KERNEL_ENABLE_FS=$<BOOL:${VIPER_KERNEL_ENABLE_FS}>
        VIPER_KERNEL_ENABLE_NET=$<BOOL:${VIPER_KERNEL_ENABLE_NET}>
        VIPER_KERNEL_ENABLE_TLS=$<BOOL:${VIPER_KERNEL_ENABLE_TLS}>
        VIPER_KERNEL_DEBUG_NET_SYSCALL=$<BOOL:${VIPER_KERNEL_DEBUG_NET_SYSCALL}>
        VIPER_KERNEL_DEBUG_TCP=$<BOOL:${VIPER_KERNEL_DEBUG_TCP}>
        VIPER_KERNEL_DEBUG_VIRTIO_NET_IRQ=$<BOOL:${VIPER_KERNEL_DEBUG_VIRTIO_NET_IRQ}>
)

# Include directories
target_include_directories(kernel.sys PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}
)

# Kernel-specific compiler flags
target_compile_options(kernel.sys PRIVATE
        -ffreestanding
        -fno-stack-protector
        -fno-exceptions
        -fno-rtti
        -fno-threadsafe-statics
        -fno-use-cxa-atexit
        -mcpu=cortex-a72
        -mstrict-align
        -mgeneral-regs-only   # Kernel must not use FPU (no saving FP state on context switch)
        -Wall
        -Wextra
)

# Kernel-specific linker flags
target_link_options(kernel.sys PRIVATE
        -nostdlib
        -static
        -T ${CMAKE_CURRENT_SOURCE_DIR}/kernel.ld
)
