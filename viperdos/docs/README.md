# ViperDOS Documentation

This directory contains the complete documentation for ViperDOS, a capability-based operating system for AArch64.

---

## Quick Links

| Document                                       | Description                         |
|------------------------------------------------|-------------------------------------|
| [Shell Commands](shell-commands.md)            | Complete shell command reference    |
| [Syscall Reference](syscalls.md)               | System call API documentation       |
| [Implementation Status](status/00-overview.md) | Current implementation status       |

---

## User Guides

### Getting Started

See the main [README](../README.md) for build instructions and quickstart.

### Shell Usage

The ViperDOS shell provides a command-line interface:

- [Shell Commands](shell-commands.md) - All commands with examples
- Line editing with arrow keys, history, and tab completion
- Structured return codes (OK/WARN/ERROR/FAIL)

### Writing Programs

User programs are written in C or C++ and link against the ViperDOS libc:

- [Syscall Reference](syscalls.md) - Complete syscall documentation
- See `user/hello/` for a minimal console example
- See `user/hello_gui/` for a minimal GUI example
- See `user/vedit/` for a full GUI text editor
- See `user/calc/` for a GUI calculator application

---

## Implementation Status

Detailed documentation of the ViperDOS implementation:

| Document                                                  | Description                                         |
|-----------------------------------------------------------|-----------------------------------------------------|
| [00-overview.md](status/00-overview.md)                   | Executive summary, architecture diagram, statistics |
| [01-architecture.md](status/01-architecture.md)           | AArch64 boot, MMU, GIC, timer, exceptions           |
| [02-memory-management.md](status/02-memory-management.md) | PMM, VMM, slab, buddy, COW, VMA                     |
| [03-console.md](status/03-console.md)                     | Serial UART, graphics console, ANSI escapes, fonts  |
| [04-drivers.md](status/04-drivers.md)                     | VirtIO (blk, net, gpu, rng, input), fw_cfg, ramfb   |
| [05-filesystem.md](status/05-filesystem.md)               | VFS, ViperFS, block cache, inode cache, journal     |
| [06-ipc.md](status/06-ipc.md)                             | Channels, poll sets                                 |
| [07-networking.md](status/07-networking.md)               | Ethernet, ARP, IPv4, TCP, UDP, DNS, TLS, HTTP       |
| [08-scheduler.md](status/08-scheduler.md)                 | SMP scheduler, work stealing, CPU affinity          |
| [09-viper-process.md](status/09-viper-process.md)         | Process model, address spaces, capabilities         |
| [10-userspace.md](status/10-userspace.md)                 | vinit shell, syscall wrappers, libc, C++ runtime    |
| [11-tools.md](status/11-tools.md)                         | mkfs.ziafs, fsck.ziafs, gen_roots_der               |
| [12-crypto.md](status/12-crypto.md)                       | SHA, AES, ChaCha20, X25519, Ed25519, RSA            |
| [13-servers.md](status/13-servers.md)                     | User-space display servers (consoled, displayd)     |
| [14-summary.md](status/14-summary.md)                     | Summary and roadmap                                 |
| [15-boot.md](status/15-boot.md)                           | VBoot UEFI bootloader, two-disk architecture        |

---

## Specifications

| Document                                                      | Description                |
|---------------------------------------------------------------|----------------------------|
| [ViperDOS ARM64 Spec](spec/ViperDOS_ARM64_Spec.md)            | Technical specification    |
| [ViperDOS v0.2.0 Spec](spec/ViperDOS_v0.2.0_Specification.md) | v0.2.0 implementation plan |

---

## Architecture Overview

```
┌─────────────────────────────────────────────────────────────────┐
│                        User Space (EL0)                          │
│  ┌───────────────────────────────────────────────────────────┐  │
│  │  GUI Applications                                          │  │
│  │  workbench, calc, clock, vedit, viewer, prefs, taskman    │  │
│  └───────────────────────────────────────────────────────────┘  │
│  ┌───────────────────────────────────────────────────────────┐  │
│  │  Shell & Utilities                                         │  │
│  │  vinit, hello, sysinfo, netstat, ping, edit, fsinfo       │  │
│  └───────────────────────────────────────────────────────────┘  │
│  ┌───────────────────────────────────────────────────────────┐  │
│  │  Libraries: libc, libgui, libwidget, libtls, libhttp      │  │
│  └───────────────────────────────────────────────────────────┘  │
│  ┌───────────────────────────────────────────────────────────┐  │
│  │  Display Servers: displayd, consoled                       │  │
│  └───────────────────────────────────────────────────────────┘  │
└─────────────────────────────┬───────────────────────────────────┘
                              │ SVC #0 (Syscalls)
┌─────────────────────────────┴───────────────────────────────────┐
│                        Kernel (EL1)                              │
│  ┌─────────────────────────────────────────────────────────────┐│
│  │ Viper Process Model                                         ││
│  │ • Per-process address spaces (TTBR0 + ASID)                ││
│  │ • Capability tables with rights enforcement                 ││
│  └─────────────────────────────────────────────────────────────┘│
│  ┌───────────────┐ ┌───────────────┐ ┌───────────────┐         │
│  │  Scheduler    │ │     IPC       │ │   Networking  │         │
│  │  8 priority   │ │  Channels     │ │  TCP/IP/TLS   │         │
│  │  queues       │ │  Poll sets    │ │  DNS/HTTP     │         │
│  └───────────────┘ └───────────────┘ └───────────────┘         │
│  ┌───────────────┐ ┌───────────────┐ ┌───────────────┐         │
│  │  Filesystem   │ │   Console     │ │    Memory     │         │
│  │  VFS/ViperFS  │ │  Serial/Gfx   │ │  PMM/VMM/COW  │         │
│  │  Journal      │ │  ANSI codes   │ │  Buddy/Slab   │         │
│  └───────────────┘ └───────────────┘ └───────────────┘         │
│  ┌─────────────────────────────────────────────────────────────┐│
│  │                        Drivers                               ││
│  │  VirtIO: blk, net, gpu, rng, input  |  PL011  |  ramfb     ││
│  └─────────────────────────────────────────────────────────────┘│
│  ┌─────────────────────────────────────────────────────────────┐│
│  │                     Architecture                             ││
│  │  Boot  |  MMU (4-level)  |  GIC  |  Timer  |  Exceptions    ││
│  └─────────────────────────────────────────────────────────────┘│
└─────────────────────────────────────────────────────────────────┘
                              │
┌─────────────────────────────┴───────────────────────────────────┐
│                     QEMU virt Machine                            │
│  ARM Cortex-A57  |  128MB RAM  |  GICv2  |  VirtIO-MMIO        │
└─────────────────────────────────────────────────────────────────┘
```

---

## Key Features

| Feature                         | Description                                         |
|---------------------------------|-----------------------------------------------------|
| **Capability-Based Security**   | Handle-based access control with rights derivation  |
| **Advanced Memory Management**  | Demand paging, COW, buddy allocator, slab allocator |
| **Complete Network Stack**      | TCP/IP with congestion control, TLS 1.3, DNS, HTTP  |
| **Crash-Consistent Filesystem** | Write-ahead journaling, inode/block caches          |
| **Retro-Style Design**          | Logical assigns, return codes, interactive shell    |

---

## Project Statistics

| Component              | SLOC         | Status   |
|------------------------|--------------|----------|
| VBoot Bootloader       | ~1,700       | Complete |
| Architecture (AArch64) | ~3,450       | Complete |
| Memory Management      | ~5,200       | Complete |
| Console                | ~3,950       | Complete |
| Drivers                | ~5,450       | Complete |
| Filesystem             | ~6,600       | Complete |
| IPC                    | ~2,700       | Complete |
| Scheduler              | ~4,450       | Complete |
| Viper/Capabilities     | ~2,300       | Complete |
| Display Servers        | ~4,200       | Complete |
| libc                   | ~30,200      | Complete |
| Libraries (GUI, etc.)  | ~17,100      | Complete |
| GUI Applications       | ~17,700      | Complete |
| Tools                  | ~2,000       | Complete |
| **Total**              | **~143,000** |          |

---

## Color Palette

| Name        | Hex       | Usage        |
|-------------|-----------|--------------|
| Viper Green | `#00AA44` | Primary text |
| Dark Brown  | `#1A1208` | Background   |
| Yellow      | `#FFDD00` | Warnings     |
| White       | `#EEEEEE` | Bright text  |
| Red         | `#CC3333` | Errors       |

---

## Source Code

| Directory     | Description         |
|---------------|---------------------|
| `../kernel/`  | Kernel source code  |
| `../user/`    | User space programs |
| `../vboot/`   | Bootloader          |
| `../tools/`   | Build tools         |
| `../scripts/` | Build scripts       |
