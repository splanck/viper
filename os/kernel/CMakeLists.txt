# ViperOS Kernel CMakeLists.txt

# Kernel is C++ (freestanding)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

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
        lib/timerwheel.cpp
        sched/task.cpp
        sched/context.S
        sched/scheduler.cpp
        sched/wait.cpp
        sched/signal.cpp
        syscall/dispatch.cpp
        syscall/table.cpp
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
        drivers/virtio/net.cpp
        drivers/virtio/rng.cpp
        input/input.cpp
        net/network.cpp
        net/netif.cpp
        net/eth/ethernet.cpp
        net/eth/arp.cpp
        net/ip/ipv4.cpp
        net/ip/ipv6.cpp
        net/ip/icmp.cpp
        net/ip/icmpv6.cpp
        net/ip/udp.cpp
        net/ip/tcp.cpp
        net/dns/dns.cpp
        net/http/http.cpp
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
        # TLS/Crypto (v0.2.0)
        net/tls/tls.cpp
        net/tls/record.cpp
        net/tls/asn1/asn1.cpp
        net/tls/cert/ca_store.cpp
        net/tls/cert/verify.cpp
        net/tls/cert/x509.cpp
        net/tls/crypto/aes_gcm.cpp
        net/tls/crypto/chacha20.cpp
        net/tls/crypto/hkdf.cpp
        net/tls/crypto/random.cpp
        net/tls/crypto/sha256.cpp
        net/tls/crypto/sha384.cpp
        net/tls/crypto/x25519.cpp
        # SSH Crypto additions
        net/tls/crypto/sha1.cpp
        net/tls/crypto/aes_ctr.cpp
        net/tls/crypto/ed25519.cpp
        net/tls/crypto/rsa.cpp
)

# Create kernel executable
add_executable(kernel.elf ${KERNEL_SOURCES})

# Include directories
target_include_directories(kernel.elf PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}
)

# Kernel-specific compiler flags
target_compile_options(kernel.elf PRIVATE
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
target_link_options(kernel.elf PRIVATE
        -nostdlib
        -static
        -T ${CMAKE_CURRENT_SOURCE_DIR}/kernel.ld
)
