# ViperOS Implementation Status

**Version:** January 2026 (v0.3.0)
**Target:** AArch64 (ARM64) on QEMU virt machine
**Total SLOC:** ~109,000

## Executive Summary

ViperOS is a **capability-based microkernel** operating system targeting AArch64. In microkernel mode, the kernel provides only essential services: task scheduling, memory management, IPC channels with capability transfer, and device access primitives. Higher-level services run as user-space servers communicating via message-passing IPC.

The current implementation provides:
- **Microkernel core**: Priority-based preemptive scheduler, capability tables, bidirectional IPC channels
- **Memory management**: Demand paging, VMA tracking, copy-on-write, buddy allocator, slab allocator
- **User-space servers**: netd (TCP/IP stack), fsd (filesystem), blkd (block devices), consoled (console), inputd (keyboard)
- **Complete libc**: POSIX-compatible C library with 56 source files
- **Networking**: Full TCP/IP stack with TLS 1.3, HTTP client, SSH/SFTP client
- **Filesystem**: Crash-consistent journaling filesystem (ViperFS) with inode and block caching

The system is designed for QEMU's `virt` machine but is structured for future hardware portability.

---

## Project Statistics

| Component | SLOC | Status |
|-----------|------|--------|
| Architecture (AArch64) | ~3,600 | Complete for QEMU (GICv2/v3, PSCI, hi-res timer) |
| Memory Management | ~5,400 | Complete (PMM, VMM, slab, buddy, COW, VMA) |
| Console (Serial/Graphics) | ~3,500 | Complete |
| Drivers (VirtIO/fw_cfg) | ~5,000 | Complete for QEMU (blk, net, gpu, rng, input) |
| Filesystem (VFS/ViperFS) | ~6,500 | Complete (journal, inode cache, block cache) |
| IPC (Channels/Poll) | ~2,500 | Complete |
| Scheduler/Tasks | ~3,600 | Complete (8-level priority, wait queues, signals) |
| Viper/Capabilities | ~2,300 | Complete (handle-based access, rights derivation) |
| User-Space Servers | ~8,900 | Complete (netd, fsd, blkd, consoled, inputd) |
| User Space (libc/C++/libs) | ~51,000 | Complete (libc, libhttp, libtls, libssh, libnetclient) |
| Tools | ~2,200 | Complete |

---

## Subsystem Documentation

| Document | Description |
|----------|-------------|
| [01-architecture.md](01-architecture.md) | AArch64 boot, MMU, GIC, timer, exceptions, syscalls |
| [02-memory-management.md](02-memory-management.md) | PMM, VMM, slab, buddy, COW, VMA, kernel heap |
| [03-console.md](03-console.md) | Serial UART, graphics console, fonts |
| [04-drivers.md](04-drivers.md) | VirtIO (blk, net, gpu, rng, input), fw_cfg, ramfb |
| [05-filesystem.md](05-filesystem.md) | VFS, ViperFS, block cache, inode cache, journal |
| [06-ipc.md](06-ipc.md) | Channels, poll, poll sets, capability transfer |
| [07-networking.md](07-networking.md) | User-space TCP/IP stack via netd server |
| [08-scheduler.md](08-scheduler.md) | Priority-based scheduler, tasks, context switch |
| [09-viper-process.md](09-viper-process.md) | Viper processes, address spaces, VMA, capabilities |
| [10-userspace.md](10-userspace.md) | vinit, syscall wrappers, libc, C++ runtime, SSH/SFTP |
| [11-tools.md](11-tools.md) | mkfs.viperfs, fsck.viperfs, gen_roots_der |
| [12-crypto.md](12-crypto.md) | TLS 1.3, SSH crypto, hash functions, encryption |
| [13-servers.md](13-servers.md) | Microkernel servers (netd, fsd, blkd, consoled, inputd) |
| [14-summary.md](14-summary.md) | Implementation summary and development roadmap |

---

## Architecture Overview

```
┌─────────────────────────────────────────────────────────────────┐
│                        User Space (EL0)                          │
│  ┌───────────────────────────────────────────────────────────┐  │
│  │  vinit - Interactive shell with networking demos          │  │
│  │  • Line editing, history, tab completion                  │  │
│  │  • File/directory commands, HTTPS fetch, SSH client       │  │
│  │  • Text editor (edit), system utilities                   │  │
│  └───────────────────────────────────────────────────────────┘  │
│  ┌───────────────┐ ┌───────────────┐ ┌───────────────┐         │
│  │     netd      │ │     fsd       │ │     blkd      │         │
│  │  TCP/IP stack │ │  Filesystem   │ │  Block device │         │
│  │  via NETD:    │ │   via FSD:    │ │  via BLKD:    │         │
│  └───────────────┘ └───────────────┘ └───────────────┘         │
│  ┌───────────────┐ ┌───────────────┐                           │
│  │   consoled    │ │    inputd     │                           │
│  │  Console I/O  │ │  Keyboard/    │                           │
│  │ via CONSOLED: │ │  Mouse input  │                           │
│  └───────────────┘ └───────────────┘                           │
└─────────────────────────┬───────────────────────────────────────┘
                          │ IPC (Channels) + Syscalls (SVC)
                          ▼
┌─────────────────────────────────────────────────────────────────┐
│                   Microkernel (EL1)                              │
│  ┌─────────────────────────────────────────────────────────────┐│
│  │ Capability System                                           ││
│  │ • Handle-based access control (16M handles per process)    ││
│  │ • Rights: READ, WRITE, EXECUTE, DERIVE, TRANSFER, etc.     ││
│  │ • Generation counters prevent use-after-free               ││
│  └─────────────────────────────────────────────────────────────┘│
│  ┌───────────────┐ ┌───────────────┐ ┌───────────────┐         │
│  │  Scheduler    │ │     IPC       │ │    Memory     │         │
│  │  • 8 priority │ │  • Channels   │ │  • PMM/VMM    │         │
│  │    queues     │ │  • Handle     │ │  • Slab/Buddy │         │
│  │  • Preemptive │ │    transfer   │ │  • COW/VMA    │         │
│  └───────────────┘ └───────────────┘ └───────────────┘         │
│  ┌─────────────────────────────────────────────────────────────┐│
│  │ Device Primitives (for user-space drivers)                  ││
│  │ MAP_DEVICE │ IRQ_REGISTER │ IRQ_WAIT │ DMA_ALLOC           ││
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
│  • ARM Cortex-A72 CPU (4 cores supported)                       │
│  • 128MB RAM (configurable)                                      │
│  • GICv2/GICv3 interrupt controller (auto-detected)             │
│  • PL011 UART                                                    │
│  • VirtIO-MMIO devices                                           │
│  • ramfb (graphics output)                                       │
└─────────────────────────────────────────────────────────────────┘
```

---

## Microkernel Design

### Build Configuration

```cpp
// kernel/include/config.hpp
#define VIPER_MICROKERNEL_MODE 1      // Microkernel mode enabled
#define VIPER_KERNEL_ENABLE_FS 1      // Kernel FS enabled for boot
#define VIPER_KERNEL_ENABLE_NET 0     // Use netd server instead
#define VIPER_KERNEL_ENABLE_TLS 0     // Use libtls instead
```

### What Runs in Kernel Space

- **Task scheduler**: 8-level priority queues with preemption
- **Memory management**: PMM, VMM, demand paging, COW
- **IPC channels**: Bidirectional message passing with capability transfer
- **Capability tables**: Handle-based access control
- **Device primitives**: MMIO mapping, IRQ routing, DMA allocation
- **Interrupt/exception handling**: GICv2/v3, timer, syscall dispatch
- **Filesystem** (optional): VFS + ViperFS (enabled for boot)

### What Runs in User Space

| Server | Assign | Purpose |
|--------|--------|---------|
| netd | NETD: | TCP/IP stack, DNS, socket API |
| fsd | FSD: | Filesystem operations via blkd |
| blkd | BLKD: | VirtIO-blk device access |
| consoled | CONSOLED: | Console output |
| inputd | INPUTD: | Keyboard/mouse input |

---

## Key Features

### Capability-Based Security
- Handle-based access to kernel objects
- 24-bit index + 8-bit generation counter prevents use-after-free
- Rights derivation (least privilege via CAP_DERIVE)
- Per-process capability tables (256-1024 handles)
- IPC handle transfer for cross-process object sharing

### Advanced Memory Management
- Demand paging with VMA tracking
- Copy-on-write (COW) page sharing
- Buddy allocator for O(log n) page allocation
- Slab allocator for fixed-size kernel objects
- User fault recovery with graceful task termination

### Message-Passing IPC
- Bidirectional channels (up to 256 bytes/message)
- Up to 4 capability handles per message
- Blocking and non-blocking send/receive
- Poll sets for multiplexing multiple channels
- Shared memory for large data transfers

### User-Space Network Stack
- Ethernet/ARP/IPv4/ICMP in netd server
- TCP with congestion control and retransmission
- UDP for DNS queries
- TLS 1.3 via libtls library
- HTTP/1.1 client with chunked encoding
- SSH-2/SFTP client via libssh

### Crash-Consistent Filesystem
- Write-ahead journaling for metadata
- Inode cache with LRU eviction
- Block cache with pinning and read-ahead
- File truncation and fsync support
- Accessible via fsd server

---

## Service Discovery via Assigns

User-space servers register themselves using the assign system:

```cpp
// Server registration
sys::assign_set("NETD", service_channel);

// Client discovery
u32 netd_handle;
sys::assign_get("NETD", &netd_handle);
```

Standard assigns:
- `C:` - Current directory (default `/`)
- `S:` - System root (`/`)
- `L:` - Library directory (`/lib`)
- `T:` - Temporary directory (`/tmp`)
- `CERTS:` - Certificate directory (`/certs`)

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
- CPU: `cortex-a72` (4 cores)
- RAM: 128MB (default)
- Devices: virtio-blk, virtio-net, virtio-gpu, virtio-rng, virtio-keyboard/mouse, ramfb

---

## Directory Structure

```
os/
├── kernel/
│   ├── arch/aarch64/     # Boot, MMU, GIC, timer, exceptions
│   ├── mm/               # PMM, VMM, heap, slab, buddy allocator
│   ├── console/          # Serial, graphics console, font
│   ├── drivers/          # VirtIO, fw_cfg, ramfb
│   ├── fs/               # VFS, ViperFS, cache, journal
│   ├── ipc/              # Channels, poll, pollset
│   ├── sched/            # Tasks, scheduler, signals, context switch
│   ├── viper/            # Process model, address spaces
│   ├── cap/              # Capability tables, rights, handles
│   ├── assign/           # Logical device assigns
│   ├── syscall/          # Syscall dispatch table
│   └── kobj/             # Kernel objects (file, dir, shm, channel)
├── user/
│   ├── servers/          # User-space servers
│   │   ├── netd/         # Network server (TCP/IP stack)
│   │   ├── fsd/          # Filesystem server
│   │   ├── blkd/         # Block device server
│   │   ├── consoled/     # Console server
│   │   └── inputd/       # Input server
│   ├── vinit/            # Init process + shell
│   ├── libc/             # Freestanding C library
│   │   ├── include/      # C headers (stdio.h, string.h, etc.)
│   │   │   └── c++/      # C++ headers (type_traits, utility, etc.)
│   │   └── src/          # Implementation files (56 sources)
│   ├── libnetclient/     # Client library for netd
│   ├── libfsclient/      # Client library for fsd
│   ├── libtls/           # TLS 1.3 library
│   ├── libhttp/          # HTTP client library
│   ├── libssh/           # SSH-2/SFTP library
│   ├── libvirtio/        # User-space VirtIO library
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
- SMP scheduler integration - CPUs boot but idle
- Power management
- Real-time scheduling class
- Kernel modules

### Networking
- IPv6
- TCP window scaling
- TLS server mode / ECDSA
- TLS session resumption

### Filesystem
- Hard links
- File locking
- Extended attributes
- Multiple mount points

### User Space
- Dynamic linking / shared libraries
- Shell scripting
- Pipes between commands
- Environment variables

---

## Version History

- **January 2026 (v0.3.0)**: Microkernel architecture
  - **Microkernel mode**: VIPER_MICROKERNEL_MODE=1 by default
  - **User-space servers**: netd, fsd, blkd, consoled, inputd complete
  - **libc-to-server routing**: Socket/file calls route to netd/fsd
  - **Device syscalls**: MAP_DEVICE, IRQ_REGISTER, DMA_ALLOC for user drivers
  - **libnetclient**: Client library for netd communication
  - **libfsclient**: Client library for fsd communication
  - **Shared memory IPC**: SHM_CREATE, SHM_MAP for large data transfer

- **December 2025 (v0.2.7)**: SSH/SFTP client implementation
  - Complete SSH-2 protocol (curve25519, Ed25519, aes-ctr, chacha20)
  - SFTP v3 protocol
  - libssh library

- **December 2025 (v0.2.6)**: Comprehensive libc expansion
  - 55+ source files, 66 C++ headers
  - POSIX compliance improvements

- **December 2025 (v0.2.5)**: Architecture improvements
  - Clang toolchain, GICv3, high-resolution timers, PSCI multicore

- **December 2025 (v0.2.4)**: Filesystem enhancements
  - Thread-safe caches, inode cache, block pinning, journal improvements

- **December 2025 (v0.2.3)**: Complete libc and C++ support

- **December 2025 (v0.2.2)**: Production-readiness features
  - Demand paging, COW, interrupt-driven networking, TCP congestion control

- **December 2025 (v0.2.0)**: Initial documentation
