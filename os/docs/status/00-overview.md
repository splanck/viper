# ViperOS Implementation Status

**Version:** December 2025 (v0.2.5)
**Target:** AArch64 (ARM64) on QEMU virt machine
**Total SLOC:** ~66,000

## Executive Summary

ViperOS is a capability-based microkernel operating system targeting AArch64. The current implementation provides a functional kernel with full virtual memory support (demand paging, VMA tracking, copy-on-write), advanced memory allocators (buddy allocator, slab allocator), a complete TCP/IP stack with TLS 1.3 and congestion control, a crash-consistent journaling filesystem with inode caching and block pinning, and an interactive shell with user-space heap support. A complete minimal libc and C++ standard library support enables portable user-space application development. The system is designed for QEMU's `virt` machine but is structured for future hardware portability.

---

## Project Statistics

| Component | SLOC | Status |
|-----------|------|--------|
| Architecture (AArch64) | ~3,000 | Complete for QEMU (GICv2/v3, PSCI, hi-res timer) |
| Memory Management | ~3,200 | Complete (PMM, VMM, slab, buddy, COW, VMA) |
| Console (Serial/Graphics) | ~2,000 | Complete |
| Drivers (VirtIO/fw_cfg) | ~4,100 | Complete for QEMU |
| Filesystem (VFS/ViperFS) | ~5,500 | Complete (journal, inode cache, block cache) |
| IPC (Channels/Poll) | ~2,100 | Complete |
| Networking (TCP/IP/TLS) | ~15,500 | Complete |
| Scheduler/Tasks | ~2,500 | Complete (wait queues, priorities) |
| Viper/Capabilities | ~3,500 | Complete (VMA, address spaces) |
| User Space (libc/C++) | ~9,000 | Complete |
| Tools | ~1,800 | Complete |

---

## Subsystem Documentation

| Document | Description |
|----------|-------------|
| [01-architecture.md](01-architecture.md) | AArch64 boot, MMU, GIC, timer, exceptions, syscalls |
| [02-memory-management.md](02-memory-management.md) | PMM, VMM, slab, buddy, COW, VMA, kernel heap |
| [03-console.md](03-console.md) | Serial UART, graphics console, fonts |
| [04-drivers.md](04-drivers.md) | VirtIO (blk, net, rng, input), fw_cfg, ramfb |
| [05-filesystem.md](05-filesystem.md) | VFS, ViperFS, block cache, inode cache, journal |
| [06-ipc.md](06-ipc.md) | Channels, poll, poll sets |
| [07-networking.md](07-networking.md) | Ethernet, ARP, IPv4, TCP, UDP, DNS, TLS, HTTP |
| [08-scheduler.md](08-scheduler.md) | Tasks, scheduler, context switch, wait queues |
| [09-viper-process.md](09-viper-process.md) | Viper processes, address spaces, VMA, capabilities |
| [10-userspace.md](10-userspace.md) | vinit, syscall wrappers, libc, C++ runtime |
| [11-tools.md](11-tools.md) | mkfs.viperfs, fsck.viperfs, gen_roots_der |

---

## Architecture Overview

```
┌─────────────────────────────────────────────────────────────────┐
│                        User Space (EL0)                          │
│  ┌───────────────────────────────────────────────────────────┐  │
│  │  vinit - Amiga-inspired shell with networking demos       │  │
│  │  • Line editing, history, tab completion                  │  │
│  │  • File/directory commands, HTTPS fetch                   │  │
│  └───────────────────────────────────────────────────────────┘  │
└─────────────────────────────┬───────────────────────────────────┘
                              │ Syscalls (SVC)
                              ▼
┌─────────────────────────────────────────────────────────────────┐
│                        Kernel (EL1)                              │
│  ┌─────────────────────────────────────────────────────────────┐│
│  │ Viper Process Model                                         ││
│  │ • Address spaces (TTBR0 + ASID)                             ││
│  │ • Capability tables (handle-based access control)          ││
│  │ • Per-process resource limits                               ││
│  └─────────────────────────────────────────────────────────────┘│
│  ┌───────────────┐ ┌───────────────┐ ┌───────────────┐         │
│  │  Scheduler    │ │     IPC       │ │   Networking  │         │
│  │  • FIFO queue │ │  • Channels   │ │  • TCP/IP     │         │
│  │  • 10ms slice │ │  • Poll sets  │ │  • TLS 1.3    │         │
│  └───────────────┘ └───────────────┘ └───────────────┘         │
│  ┌───────────────┐ ┌───────────────┐ ┌───────────────┐         │
│  │  Filesystem   │ │   Console     │ │    Memory     │         │
│  │  • VFS/ViperFS│ │  • Serial     │ │  • PMM/VMM    │         │
│  │  • Block cache│ │  • Graphics   │ │  • Heap       │         │
│  └───────────────┘ └───────────────┘ └───────────────┘         │
│  ┌─────────────────────────────────────────────────────────────┐│
│  │                        Drivers                               ││
│  │  VirtIO-blk │ VirtIO-net │ VirtIO-rng │ VirtIO-input        ││
│  │  fw_cfg     │ ramfb                                         ││
│  └─────────────────────────────────────────────────────────────┘│
│  ┌─────────────────────────────────────────────────────────────┐│
│  │                     Architecture                             ││
│  │  Boot │ MMU │ GIC (interrupts) │ Timer │ Exceptions         ││
│  └─────────────────────────────────────────────────────────────┘│
└─────────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────────┐
│                     QEMU virt Machine                            │
│  • ARM Cortex-A72 CPU                                            │
│  • 128MB RAM (configurable)                                      │
│  • GICv2 interrupt controller                                    │
│  • PL011 UART                                                    │
│  • VirtIO-MMIO devices                                           │
│  • ramfb (graphics output)                                       │
└─────────────────────────────────────────────────────────────────┘
```

---

## Key Features

### Capability-Based Security
- Handle-based access to kernel objects
- Rights derivation (least privilege)
- Per-process capability tables
- IPC handle transfer

### Advanced Memory Management
- Demand paging with VMA tracking
- Copy-on-write (COW) page sharing
- Buddy allocator for O(log n) page allocation
- Slab allocator for fixed-size kernel objects
- User fault recovery with graceful task termination

### Complete Network Stack
- Ethernet/ARP/IPv4/ICMP
- TCP with connection state machine and congestion control
- UDP for DNS queries
- TLS 1.3 with certificate verification
- HTTP/1.0 client
- Interrupt-driven packet reception

### Crash-Consistent Filesystem
- Write-ahead journaling for metadata
- Inode cache with LRU eviction
- Block cache with pinning and read-ahead
- File truncation and fsync support
- Timestamp tracking (atime, mtime, ctime)

### Amiga-Inspired Design
- Logical device assigns (SYS:, etc.)
- Return codes: OK/WARN/ERROR/FAIL
- Interactive shell with completion

---

## Recent Implementations (v0.2.5)

### Completed in This Release
1. **Clang Toolchain Support** - Alternative to GCC
   - New `aarch64-clang-toolchain.cmake` for cross-compilation
   - Uses `aarch64-none-elf` target triple
   - Compatible with GNU binutils (ld, ar, objcopy)
   - build_viper.sh defaults to Clang

2. **FPU/SIMD Enablement** - Required for Clang
   - CPACR_EL1.FPEN enabled for EL1 and EL0 in boot.S
   - Allows SIMD instructions Clang generates for memory operations
   - Enabled for both primary and secondary CPUs

3. **Multicore Infrastructure (PSCI)** - Ready for SMP
   - Per-CPU data structures and stacks (4 CPUs, 16KB each)
   - PSCI CPU_ON implementation for secondary boot
   - Secondary CPU entry point in boot.S
   - IPI support via GIC SGIs

4. **GICv3 Support** - Dual GIC version support
   - Automatic version detection via GICD_PIDR2
   - GICv3 redistributor initialization
   - System register interface (ICC_*)
   - Affinity-based interrupt routing

5. **High-Resolution Timers** - Nanosecond precision
   - `now()` - Raw counter timestamp (16ns resolution)
   - `delay_ns()` / `delay_us()` - Precise short delays
   - `schedule_oneshot()` - Deadline-based callbacks
   - 128-bit arithmetic for overflow-free conversions

6. **Signal Delivery Infrastructure**
   - POSIX signal numbers (SIGHUP through SIGSYS)
   - Hardware fault signals (SIGSEGV, SIGBUS, SIGILL, SIGFPE)
   - Fault info with address, PC, ESR, and kind
   - Currently terminates on fatal signals

### Previous Release (v0.2.4)
- **ViperFS Thread Safety**: Block cache, journal, and inode cache spinlock protection
- **Inode Cache**: 32-entry LRU cache with hash lookup, refcounting, dirty tracking
- **Block Cache Pinning**: pin/unpin for critical metadata, dump_stats()
- **File Operations**: truncate(), fsync(), atime/mtime timestamp updates
- **Journal Improvements**: Checksum verification, commit record validation

### Previous Release (v0.2.3)
- Complete Freestanding libc (stdio, string, stdlib, ctype, time, unistd, errno)
- C++ Standard Library Support (type_traits, utility, new, initializer_list)
- Per-Process Current Working Directory
- Graphics Console Green Border

### Priority Development Roadmap

### High Priority
1. Priority-based scheduler (currently FIFO)
2. Multicore (SMP) support
3. Shell scripting support

### Medium Priority
1. IPv6 support
2. VirtIO-GPU for hardware acceleration
3. TLS session resumption
4. Pipes between processes

### Low Priority
1. Shared libraries / dynamic linking
2. Real-time scheduling class
3. ECDSA certificate verification

---

## Testing

The kernel includes QEMU-based tests:

| Test | Description |
|------|-------------|
| `qemu_kernel_boot` | Verify kernel boots and prints banner |
| `qemu_scheduler_start` | Verify scheduler starts correctly |
| `qemu_storage_tests` | File I/O operations |
| `qemu_toolchain_test` | User-space toolchain validation |

Run tests:
```bash
cd os && ./build_viper.sh --test
```

---

## Building and Running

### Prerequisites
- Clang with AArch64 support (default) or aarch64-elf-gcc
- AArch64 GNU binutils (aarch64-elf-ld, aarch64-elf-ar, aarch64-elf-objcopy)
- QEMU with aarch64 support
- CMake 3.16+
- C++17 compiler for host tools

### Quick Start
```bash
cd os
./build_viper.sh           # Graphics mode
./build_viper.sh --serial  # Serial-only mode
./build_viper.sh --debug   # GDB debugging
```

### QEMU Configuration
- Machine: `virt`
- CPU: `cortex-a72`
- RAM: 128MB (default)
- Devices: virtio-blk, virtio-net, virtio-rng, virtio-keyboard/mouse, ramfb

---

## Directory Structure

```
os/
├── kernel/
│   ├── arch/aarch64/     # Boot, MMU, GIC, timer, exceptions
│   ├── mm/               # PMM, VMM, heap, slab allocator
│   ├── console/          # Serial, graphics console, font
│   ├── drivers/          # VirtIO, fw_cfg, ramfb
│   ├── fs/               # VFS, ViperFS, cache, journal
│   ├── ipc/              # Channels, poll, pollset
│   ├── net/              # TCP/IP, TLS, DNS, HTTP
│   ├── sched/            # Tasks, scheduler
│   ├── viper/            # Process model, address spaces
│   └── cap/              # Capability tables, rights
├── user/
│   ├── vinit/            # Init process + shell
│   ├── hello/            # Test program
│   ├── libc/             # Freestanding C library
│   │   ├── include/      # C headers (stdio.h, string.h, etc.)
│   │   │   └── c++/      # C++ headers (type_traits, utility, etc.)
│   │   └── src/          # Implementation files
│   └── syscall.hpp       # Low-level syscall wrappers
├── include/viperos/      # Shared kernel/user ABI headers
├── tools/                # Host-side build tools
├── docs/status/          # This documentation
└── build_viper.sh        # Build and run script
```

---

## What's Missing (Not Yet Implemented)

### Kernel
- User-space signal handlers (sigaction) - infrastructure ready
- Process groups / sessions
- SMP scheduler integration - infrastructure ready, CPUs idle
- Power management
- Real-time scheduling class
- Kernel modules
- Priority-based scheduling (currently FIFO)

### Networking
- IPv6
- TCP window scaling
- TLS server mode / ECDSA
- TLS session resumption

### Filesystem
- Hard links
- File locking
- Extended attributes
- Mount points (single FS only)

### User Space
- Dynamic linking / shared libraries
- Shell scripting
- Pipes between commands
- Environment variables
- Signal handling

---

## Version History

- **December 2025 (v0.2.5)**: Clang toolchain and architecture improvements
  - **Clang Toolchain**: New aarch64-clang-toolchain.cmake, build_viper.sh defaults to Clang
  - **FPU/SIMD**: CPACR_EL1.FPEN enabled in boot.S for Clang-generated SIMD code
  - **Multicore Infrastructure**: PSCI CPU_ON, per-CPU stacks, IPI via SGI
  - **GICv3 Support**: Dual GICv2/v3 with auto-detection
  - **High-Resolution Timers**: Nanosecond precision, one-shot callbacks
  - **Signal Delivery**: POSIX signals with fault info (handlers not yet supported)

- **December 2025 (v0.2.4)**: Filesystem enhancements and thread safety
  - **ViperFS Thread Safety**: Block cache, journal, and inode cache spinlock protection
  - **Inode Cache**: 32-entry LRU cache with hash lookup, refcounting, dirty tracking
  - **Block Cache Pinning**: pin/unpin for critical metadata, dump_stats()
  - **File Operations**: truncate(), fsync(), atime/mtime timestamp updates
  - **Journal Improvements**: Checksum verification, commit record validation
  - **Documentation**: Comprehensive status document updates and accuracy fixes

- **December 2025 (v0.2.3)**: Complete libc and C++ support
  - **Complete Freestanding libc**: stdio, string, stdlib, ctype, time, unistd, errno, limits, stddef, stdbool, assert
  - **C++ Standard Library**: type_traits, utility, new, initializer_list, cstddef, cstdint
  - **Per-Process CWD**: getcwd/chdir syscalls, path normalization, shell chdir/cwd commands
  - **Graphics Console Border**: 20px green decorative border with 8px padding
  - **Build System**: add_user_program() helper, automatic libc linking
  - **New libc functions**: sscanf, fputs, fputc, fgets, fgetc, strstr, strpbrk, strdup, strtok_r, qsort, bsearch, rand/srand, getpid, usleep, nanosleep, clock

- **December 2025 (v0.2.2)**: Production-readiness features
  - **Demand Paging**: VMA list per process, page fault handling for heap/stack
  - **Copy-on-Write**: COW page sharing with reference counting
  - **User-Space Heap**: sbrk syscall, working malloc/free in userspace
  - **Interrupt-Driven Network**: VirtIO-net IRQ handling, RX wait queues
  - **Filesystem Journaling**: Write-ahead log, transaction commit/replay for crash recovery
  - **TCP Congestion Control**: RFC 5681 slow start, congestion avoidance, fast retransmit
  - **TCP Out-of-Order Reassembly**: 8-segment OOO queue with delivery on sequence match
  - **Slab Allocator**: Kernel memory allocator for fixed-size objects
  - **Buddy Allocator**: O(log n) physical page allocator

- **December 2025 (v0.2.1)**: Major feature additions
  - Process lifecycle: wait/exit/zombie handling
  - File descriptors: dup/dup2 support
  - TCP MSS option negotiation and retransmission
  - Block cache read-ahead
  - Symbolic link support (create/read)
  - Event notification for poll sets
  - Basic libc implementation (stdio, string, stdlib)
  - Enhanced heap allocator with double-free detection
  - Task list with CPU statistics
  - Network statistics syscall
  - Improved kernel panic output with stack traces
  - Console input buffer with line editing
  - Filesystem check tool (fsck.viperfs)

- **December 2025 (v0.2.0)**: Initial documentation
  - System functional for QEMU bring-up and networking demos
  - All core subsystems implemented for single-core QEMU virt machine
