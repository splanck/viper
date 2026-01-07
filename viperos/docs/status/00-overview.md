# ViperOS Implementation Status

**Version:** January 2026 (v0.3.2)
**Target:** AArch64 (ARM64) on QEMU virt machine
**Total SLOC:** ~115,000

## Executive Summary

ViperOS is a **capability-based microkernel** operating system targeting AArch64. In microkernel mode, the kernel provides only essential services: task scheduling, memory management, IPC channels with capability transfer, and device access primitives. Higher-level services run as user-space servers communicating via message-passing IPC.

The current implementation provides:
- **Microkernel core**: Priority-based preemptive scheduler with SMP, capability tables, bidirectional IPC channels
- **UEFI boot**: Custom VBoot bootloader supporting UEFI boot on AArch64
- **Two-disk architecture**: Separate system disk (servers) and user disk (programs)
- **Memory management**: Demand paging, VMA tracking, copy-on-write, buddy allocator, slab allocator
- **User-space servers**: netd (TCP/IP stack), fsd (filesystem), blkd (block devices), consoled (console), inputd (keyboard/mouse), displayd (GUI)
- **Complete libc**: POSIX-compatible C library with 56 source files
- **Networking**: Full TCP/IP stack with TLS 1.3, HTTP client, SSH/SFTP clients
- **Filesystem**: Crash-consistent journaling filesystem (ViperFS) with inode and block caching
- **GUI subsystem**: User-space display server with windowing, libgui API, demo applications

The system is designed for QEMU's `virt` machine but is structured for future hardware portability.

---

## Project Statistics

| Component | SLOC | Status |
|-----------|------|--------|
| Architecture (AArch64) | ~3,600 | Complete for QEMU (GICv2/v3, PSCI, hi-res timer) |
| VBoot (UEFI bootloader) | ~1,700 | Complete (UEFI boot, GOP framebuffer) |
| Memory Management | ~5,550 | Complete (PMM, VMM, slab, buddy, COW, VMA) |
| Console (Serial/Graphics) | ~3,500 | Complete (ANSI escape codes, scrollback, cursor) |
| Drivers (VirtIO/fw_cfg) | ~6,000 | Complete for QEMU (blk, net, gpu, rng, input) |
| Filesystem (VFS/ViperFS) | ~9,600 | Complete (journal, inode cache, block cache) |
| IPC (Channels/Poll) | ~2,500 | Complete |
| Scheduler/Tasks | ~4,500 | Complete (8-level priority, SMP, CFS, EDF, priority inheritance) |
| Viper/Capabilities | ~2,900 | Complete (handle-based access, rights derivation) |
| User-Space Servers | ~10,500 | Complete (netd, fsd, blkd, consoled, inputd, displayd) |
| User Space (libc/C++/libs) | ~55,000 | Complete (libc, libhttp, libtls, libssh, libgui) |
| User Applications | ~5,000 | Complete (20+ programs) |
| Tools | ~2,200 | Complete |

---

## Subsystem Documentation

| Document | Description |
|----------|-------------|
| [01-architecture.md](01-architecture.md) | AArch64 boot, MMU, GIC, timer, exceptions, syscalls |
| [02-memory-management.md](02-memory-management.md) | PMM, VMM, slab, buddy, COW, VMA, kernel heap |
| [03-console.md](03-console.md) | Serial UART, graphics console, ANSI escapes, fonts |
| [04-drivers.md](04-drivers.md) | VirtIO (blk, net, gpu, rng, input), fw_cfg, ramfb |
| [05-filesystem.md](05-filesystem.md) | VFS, ViperFS, block cache, inode cache, journal |
| [06-ipc.md](06-ipc.md) | Channels, poll, poll sets, capability transfer |
| [07-networking.md](07-networking.md) | User-space TCP/IP stack via netd server |
| [08-scheduler.md](08-scheduler.md) | Priority-based scheduler, SMP, CFS, EDF, priority inheritance |
| [09-viper-process.md](09-viper-process.md) | Viper processes, address spaces, VMA, capabilities |
| [10-userspace.md](10-userspace.md) | vinit, libc, C++ runtime, applications, GUI |
| [11-tools.md](11-tools.md) | mkfs.viperfs, fsck.viperfs, gen_roots_der |
| [12-crypto.md](12-crypto.md) | TLS 1.3, SSH crypto, hash functions, encryption |
| [13-servers.md](13-servers.md) | Microkernel servers (netd, fsd, blkd, consoled, inputd, displayd) |
| [14-summary.md](14-summary.md) | Implementation summary and development roadmap |
| [15-boot.md](15-boot.md) | VBoot UEFI bootloader, two-disk architecture |
| [16-gui.md](16-gui.md) | GUI subsystem (displayd, libgui, taskbar) |

---

## Architecture Overview

```
┌─────────────────────────────────────────────────────────────────┐
│                        User Space (EL0)                          │
│  ┌───────────────────────────────────────────────────────────┐  │
│  │  vinit - Init Process & Interactive Shell                 │  │
│  │  • AmigaDOS-style commands (dir, type, copy, etc.)        │  │
│  │  • Networking demos (fetch, ssh, sftp, ping)              │  │
│  │  • Server lifecycle management                             │  │
│  └───────────────────────────────────────────────────────────┘  │
│  ┌───────────────────────────────────────────────────────────┐  │
│  │  User Applications                                         │  │
│  │  edit (text editor), ssh, sftp, ping, netstat, sysinfo    │  │
│  │  devices, fsinfo, mathtest, hello, hello_gui              │  │
│  └───────────────────────────────────────────────────────────┘  │
│  ┌───────────────┐ ┌───────────────┐ ┌───────────────┐         │
│  │     netd      │ │     fsd       │ │     blkd      │         │
│  │  TCP/IP stack │ │  Filesystem   │ │  Block device │         │
│  │  via NETD:    │ │   via FSD:    │ │  via BLKD:    │         │
│  └───────────────┘ └───────────────┘ └───────────────┘         │
│  ┌───────────────┐ ┌───────────────┐ ┌───────────────┐         │
│  │   consoled    │ │    inputd     │ │   displayd    │         │
│  │ GUI Terminal  │ │  Keyboard/    │ │ Display/GUI   │         │
│  │ via CONSOLED: │ │  Mouse input  │ │ via DISPLAY:  │         │
│  └───────────────┘ └───────────────┘ └───────────────┘         │
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
│  │  • SMP ready  │ │    transfer   │ │  • COW/VMA    │         │
│  │  • Work steal │ │  • Poll sets  │ │  • Demand pg  │         │
│  └───────────────┘ └───────────────┘ └───────────────┘         │
│  ┌─────────────────────────────────────────────────────────────┐│
│  │ Device Primitives (for user-space drivers)                  ││
│  │ MAP_DEVICE │ IRQ_REGISTER │ IRQ_WAIT │ DMA_ALLOC            ││
│  │ MAP_FRAMEBUFFER │ SHM_CREATE │ SHM_MAP                      ││
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

## Boot Architecture

### VBoot UEFI Bootloader

ViperOS uses a custom UEFI bootloader (VBoot) for UEFI systems:

```
UEFI Firmware → VBoot (BOOTAA64.EFI) → Kernel (kernel.sys)
```

VBoot features:
- Pure UEFI implementation (no external dependencies)
- ELF64 kernel loading
- GOP framebuffer configuration
- Memory map collection and conversion
- AArch64 cache coherency handling

### Two-Disk Architecture

ViperOS uses separate disks for system and user content:

| Disk | Image | Size | Contents |
|------|-------|------|----------|
| ESP | esp.img | 40MB | VBoot bootloader + kernel (UEFI mode only) |
| System | sys.img | 2MB | Core servers (vinit, netd, fsd, blkd, etc.) |
| User | user.img | 8MB | User programs, certificates, data |

This separation enables:
- Clean kernel/userspace separation
- Boot without user disk (system-only mode)
- Independent rebuild of user programs
- Different security contexts

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

- **Task scheduler**: 8-level priority queues with preemption and SMP
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
| consoled | CONSOLED | GUI terminal emulator |
| inputd | INPUTD | Keyboard/mouse input |
| displayd | DISPLAY | Window management, GUI compositing |

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

### SMP Support
- Multi-core scheduling with per-CPU run queues
- Work stealing for load balancing
- CPU affinity support (bitmask per task)
- IPI-based reschedule notifications
- Per-CPU statistics tracking

### Advanced Scheduling Features
- **CFS (Completely Fair Scheduler)**: vruntime tracking with nice values (-20 to +19)
- **SCHED_DEADLINE**: Earliest Deadline First (EDF) with bandwidth reservation
- **SCHED_FIFO/RR**: Real-time scheduling policies
- **Priority Inheritance**: PI mutexes prevent priority inversion
- **Idle State Tracking**: WFI enter/exit statistics per CPU

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

### GUI Subsystem
- User-space display server (displayd)
- Window compositing with decorations and z-ordering
- Minimize/maximize/close button handling
- libgui client API with drawing primitives (including scaled fonts)
- Shared memory pixel buffers (zero-copy)
- Per-surface event queues
- Desktop taskbar with window list
- Mouse cursor rendering
- **GUI terminal emulator (consoled)**: ANSI escape sequences, 1.5x font scaling, bidirectional IPC for keyboard forwarding

See [16-gui.md](16-gui.md) for complete GUI documentation.

---

## User Applications

| Application | Purpose |
|-------------|---------|
| vinit | Init process and interactive shell (40+ commands) |
| edit | Nano-like text editor with file save/load |
| ssh | SSH-2 client with Ed25519/RSA authentication |
| sftp | Interactive SFTP file transfer client |
| ping | ICMP ping utility with RTT statistics |
| netstat | Network statistics display |
| devices | Hardware device listing |
| sysinfo | System information and runtime tests |
| fsinfo | Filesystem information display |
| mathtest | Math library validation |
| hello | Malloc/heap test program |
| hello_gui | GUI demo with window creation |
| taskbar | Desktop shell taskbar |

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
- `C:` - User disk root (`/c`)
- `SYS:` - System disk root (`/`)
- `S:` - System directory
- `L:` - Library directory
- `T:` - Temporary directory
- `CERTS:` - Certificate directory

---

## Building and Running

### Prerequisites
- Clang with AArch64 support (LLVM clang, not Apple clang for UEFI)
- AArch64 GNU binutils (aarch64-elf-ld, aarch64-elf-ar, aarch64-elf-objcopy)
- QEMU with aarch64 support
- CMake 3.16+
- C++17 compiler for host tools
- UEFI tools: sgdisk, mtools (for UEFI mode)

### Quick Start
```bash
cd os
./scripts/build_viperos.sh           # UEFI mode (default)
./scripts/build_viperos.sh --direct  # Direct kernel boot
./scripts/build_viperos.sh --serial  # Serial-only mode
./scripts/build_viperos.sh --debug   # GDB debugging
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
├── vboot/               # UEFI bootloader
│   ├── main.c           # Boot logic (load ELF, setup GOP, exit boot services)
│   ├── efi.h            # Minimal UEFI types/protocols
│   ├── crt0.S           # Entry stub
│   └── vboot.ld         # Linker script
├── kernel/
│   ├── arch/aarch64/    # Boot, MMU, GIC, timer, exceptions, SMP
│   ├── boot/            # Boot info parsing (VBoot and DTB)
│   ├── mm/              # PMM, VMM, heap, slab, buddy, COW, VMA
│   ├── console/         # Serial, graphics console, font
│   ├── drivers/         # VirtIO, fw_cfg, ramfb
│   ├── fs/              # VFS, ViperFS, cache, journal
│   ├── ipc/             # Channels, poll, pollset
│   ├── sched/           # Tasks, scheduler, signals, context switch
│   ├── viper/           # Process model, address spaces
│   ├── cap/             # Capability tables, rights, handles
│   ├── assign/          # Logical device assigns
│   ├── syscall/         # Syscall dispatch table
│   └── kobj/            # Kernel objects (file, dir, shm, channel)
├── user/
│   ├── servers/         # User-space servers
│   │   ├── netd/        # Network server (TCP/IP stack)
│   │   ├── fsd/         # Filesystem server
│   │   ├── blkd/        # Block device server
│   │   ├── consoled/    # Console server
│   │   ├── inputd/      # Input server
│   │   └── displayd/    # Display/GUI server
│   ├── vinit/           # Init process + shell
│   ├── edit/            # Text editor
│   ├── ssh/             # SSH client
│   ├── sftp/            # SFTP client
│   ├── ping/            # Ping utility
│   ├── hello_gui/       # GUI demo
│   ├── libc/            # Freestanding C library
│   │   ├── include/     # C headers (stdio.h, string.h, etc.)
│   │   │   └── c++/     # C++ headers
│   │   └── src/         # Implementation files (56 sources)
│   ├── libnetclient/    # Client library for netd
│   ├── libfsclient/     # Client library for fsd
│   ├── libgui/          # GUI client library
│   ├── libtls/          # TLS 1.3 library
│   ├── libhttp/         # HTTP client library
│   ├── libssh/          # SSH-2/SFTP library
│   ├── libvirtio/       # User-space VirtIO library
│   └── syscall.hpp      # Low-level syscall wrappers
├── include/viperos/     # Shared kernel/user ABI headers
├── tools/               # Host-side build tools
├── scripts/             # Build scripts
├── docs/status/         # This documentation
└── CMakeLists.txt       # Main build configuration
```

---

## What's Missing (Not Yet Implemented)

### Kernel
- User-space signal handlers (sigaction) - infrastructure ready
- Power management
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

### GUI
- Window resize via mouse drag (move is implemented)
- True window resize (reallocating pixel buffer)
- Alt+Tab window switching
- Desktop background image
- Application launcher menu

---

## Priority Recommendations: Next 5 Steps

### 1. GUI Window Drag and Resize
**Impact:** Enables desktop-like window management
- Track mouse down on title bar for window move
- Detect mouse near window edges for resize
- Update window position/size on mouse drag
- Required for proper desktop interaction

### 2. exec() Family Implementation
**Impact:** Enables proper shell command execution
- Replace current process image with new program (no new PID)
- Required for shell `!` commands and proper process replacement
- Implement execve(), execl(), execvp() variants
- More Unix-like process model than current task_spawn

### 3. pipe() Syscall and Shell Pipelines
**Impact:** Enables command chaining (`ls | grep | sort`)
- Kernel pipe object with read/write endpoints
- FD-based pipe access for standard read()/write()
- Shell integration for `|` operator
- Enables powerful command composition

### 4. PTY Subsystem (Pseudo-Terminals)
**Impact:** Enables terminal emulation and SSH server
- Kernel-side PTY master/slave pair
- Required for GUI terminal emulator
- Required for SSH server implementation
- Enables job control (Ctrl+C, Ctrl+Z)

### 5. Signal Handler User Trampoline
**Impact:** POSIX-compatible signal handling
- Complete sigaction() implementation
- Save user context, invoke handler, restore context
- Signal masking during handler execution
- Enables graceful cleanup on SIGTERM/SIGINT

---

## Version History

- **January 2026 (v0.3.2)**: GUI terminal emulator
  - **consoled**: Now runs as a GUI window via libgui/displayd
  - **ANSI escape sequences**: Full CSI support (colors, cursor, erase)
  - **Bidirectional IPC**: Keyboard forwarding from consoled to shell
  - **Font scaling**: Half-unit scaling system (1.5x = 12x12 pixel cells)
  - **libgui**: Added gui_draw_char() and gui_draw_char_scaled()

- **January 2026 (v0.3.1)**: UEFI boot and GUI expansion
  - **VBoot bootloader**: Complete UEFI bootloader with GOP support
  - **Two-disk architecture**: Separate system and user disks
  - **Display server (displayd)**: Window compositing and GUI
  - **libgui**: GUI client library with drawing primitives
  - **SMP improvements**: Work stealing, CPU affinity, load balancing
  - **New applications**: edit, hello_gui, devices, fsinfo
  - **Graphics console**: ANSI escape codes, scrollback buffer, cursor blinking

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
