# ViperOS Implementation Status

**Version:** December 2025
**Target:** AArch64 (ARM64) on QEMU virt machine
**Total SLOC:** ~29,300

## Executive Summary

ViperOS is a capability-based microkernel operating system targeting AArch64. The current implementation provides a functional kernel with user-space process support, a complete TCP/IP stack with TLS 1.3, and an interactive shell. The system is designed for QEMU's `virt` machine but is structured for future hardware portability.

---

## Project Statistics

| Component | SLOC | Status |
|-----------|------|--------|
| Architecture (AArch64) | ~1,700 | Complete for QEMU |
| Memory Management | ~900 | Functional |
| Console (Serial/Graphics) | ~1,900 | Complete |
| Drivers (VirtIO/fw_cfg) | ~4,100 | Complete for QEMU |
| Filesystem (VFS/ViperFS) | ~3,400 | Functional |
| IPC (Channels/Poll) | ~2,100 | Complete |
| Networking (TCP/IP/TLS) | ~14,700 | Complete |
| Scheduler/Tasks | ~1,400 | Functional |
| Viper/Capabilities | ~2,100 | Functional |
| User Space | ~4,500 | Functional |
| Tools | ~1,300 | Complete |

---

## Subsystem Documentation

| Document | Description |
|----------|-------------|
| [01-architecture.md](01-architecture.md) | AArch64 boot, MMU, GIC, timer, exceptions |
| [02-memory-management.md](02-memory-management.md) | PMM, VMM, kernel heap |
| [03-console.md](03-console.md) | Serial UART, graphics console, fonts |
| [04-drivers.md](04-drivers.md) | VirtIO (blk, net, rng, input), fw_cfg, ramfb |
| [05-filesystem.md](05-filesystem.md) | VFS, ViperFS, block cache |
| [06-ipc.md](06-ipc.md) | Channels, poll, poll sets |
| [07-networking.md](07-networking.md) | Ethernet, ARP, IPv4, TCP, UDP, DNS, TLS, HTTP |
| [08-scheduler.md](08-scheduler.md) | Tasks, scheduler, context switch |
| [09-viper-process.md](09-viper-process.md) | Viper processes, address spaces, capabilities |
| [10-userspace.md](10-userspace.md) | vinit, syscall wrappers, shell |
| [11-tools.md](11-tools.md) | mkfs.viperfs, gen_roots_der |

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

### Complete Network Stack
- Ethernet/ARP/IPv4/ICMP
- TCP with connection state machine
- UDP for DNS queries
- TLS 1.3 with certificate verification
- HTTP/1.0 client

### Amiga-Inspired Design
- Logical device assigns (SYS:, etc.)
- Return codes: OK/WARN/ERROR/FAIL
- Interactive shell with completion

---

## Priority Development Roadmap

### High Priority
1. Page fault handling for demand paging
2. Priority-based scheduler
3. Per-process file descriptor tables
4. Interrupt-driven I/O

### Medium Priority
1. ELF loader for spawning programs
2. Proper TCP retransmission/congestion control
3. Journaling for filesystem crash consistency
4. Multicore (SMP) support

### Low Priority
1. IPv6 support
2. VirtIO-GPU for hardware acceleration
3. TLS session resumption
4. Shared libraries

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
- AArch64 cross-compiler (aarch64-elf-gcc)
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
│   ├── mm/               # PMM, VMM, heap
│   ├── console/          # Serial, graphics console, font
│   ├── drivers/          # VirtIO, fw_cfg, ramfb
│   ├── fs/               # VFS, ViperFS, cache
│   ├── ipc/              # Channels, poll, pollset
│   ├── net/              # TCP/IP, TLS, DNS, HTTP
│   ├── sched/            # Tasks, scheduler
│   ├── viper/            # Process model, address spaces
│   └── cap/              # Capability tables, rights
├── user/
│   ├── vinit/            # Init process
│   └── syscall.hpp       # Syscall wrappers
├── include/viperos/      # Shared kernel/user ABI headers
├── tools/                # Host-side build tools
├── docs/status/          # This documentation
└── build_viper.sh        # Build and run script
```

---

## What's Missing (Not Yet Implemented)

### Kernel
- Page fault handling / demand paging
- Signal delivery
- Process groups / sessions
- SMP / multicore
- Power management
- Real-time scheduling
- Kernel modules

### Networking
- IPv6
- TCP window scaling
- Out-of-order reassembly
- Congestion control
- TLS server mode / ECDSA

### Filesystem
- Journaling
- Hard links
- File locking

### User Space
- Dynamic linking
- Multiple programs
- Shell scripting
- Pipes

---

## Version History

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
- **December 2025 (v0.2.0)**: Initial documentation of complete implementation status
- System is functional for QEMU bring-up and networking demos
- All core subsystems implemented for single-core QEMU virt machine
