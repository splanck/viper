# ViperDOS Implementation Status

**Version:** January 2026 (v0.3.2)
**Target:** AArch64 (ARM64) on QEMU virt machine
**Total SLOC:** ~143,000

## Executive Summary

ViperDOS is a **capability-based hybrid kernel** operating system targeting AArch64. The kernel provides filesystem,
networking, and device access directly, while display servers run in user space for GUI functionality.

The current implementation provides:

- **Kernel core**: Priority-based preemptive scheduler with SMP, capability tables, bidirectional IPC channels
- **UEFI boot**: Custom VBoot bootloader supporting UEFI boot on AArch64
- **Memory management**: Demand paging, VMA tracking, copy-on-write, buddy allocator, slab allocator
- **Kernel filesystem**: VFS + ViperFS with journaling, inode/block caching
- **Kernel networking**: Full TCP/IP stack with TLS 1.3, HTTP client, SSH/SFTP clients
- **User-space display servers**: consoled (GUI terminal), displayd (window manager)
- **Complete libc**: POSIX-compatible C library with ~56 source files
- **GUI subsystem**: User-space display server with windowing, libgui/libwidget APIs, Workbench desktop

The system is designed for QEMU's `virt` machine but is structured for future hardware portability.

---

## Project Statistics

| Component                 | SLOC         | Status                                                           |
|---------------------------|--------------|------------------------------------------------------------------|
| VBoot (UEFI bootloader)   | ~1,700       | Complete (UEFI boot, GOP framebuffer)                            |
| Architecture (AArch64)    | ~3,450       | Complete for QEMU (GICv2/v3, PSCI, hi-res timer)                 |
| Memory Management         | ~5,200       | Complete (PMM, VMM, slab, buddy, COW, VMA)                       |
| Console (Serial/Graphics) | ~3,950       | Complete (ANSI escape codes, scrollback, cursor)                 |
| Drivers (VirtIO/fw_cfg)   | ~5,450       | Complete for QEMU (blk, net, gpu, rng, input)                    |
| Filesystem (VFS/ViperFS)  | ~6,600       | Complete (journal, inode cache, block cache)                     |
| IPC (Channels/Poll)       | ~2,700       | Complete                                                         |
| Scheduler/Tasks           | ~4,450       | Complete (8-level priority, SMP, CFS, EDF, priority inheritance) |
| Viper/Capabilities        | ~2,300       | Complete (handle-based access, rights derivation)                |
| Display Servers           | ~4,200       | Complete (consoled, displayd)                                    |
| libc                      | ~30,200      | Complete (POSIX C library)                                       |
| Libraries (GUI, etc.)     | ~17,100      | Complete (libgui, libwidget, libtls, libhttp, libssh)            |
| GUI Applications          | ~17,700      | Complete (workbench, calc, clock, vedit, etc.)                   |
| Tools                     | ~2,000       | Complete                                                         |
| **Total**                 | **~143,000** |                                                                  |

---

## Subsystem Documentation

| Document                                           | Description                                                   |
|----------------------------------------------------|---------------------------------------------------------------|
| [01-architecture.md](01-architecture.md)           | AArch64 boot, MMU, GIC, timer, exceptions, syscalls           |
| [02-memory-management.md](02-memory-management.md) | PMM, VMM, slab, buddy, COW, VMA, kernel heap                  |
| [03-console.md](03-console.md)                     | Serial UART, graphics console, ANSI escapes, fonts            |
| [04-drivers.md](04-drivers.md)                     | VirtIO (blk, net, gpu, rng, input), fw_cfg, ramfb             |
| [05-filesystem.md](05-filesystem.md)               | VFS, ViperFS, block cache, inode cache, journal               |
| [06-ipc.md](06-ipc.md)                             | Channels, poll, poll sets, capability transfer                |
| [07-networking.md](07-networking.md)               | Kernel TCP/IP stack, TLS, DNS, HTTP                           |
| [08-scheduler.md](08-scheduler.md)                 | Priority-based scheduler, SMP, CFS, EDF, priority inheritance |
| [09-viper-process.md](09-viper-process.md)         | Viper processes, address spaces, VMA, capabilities            |
| [10-userspace.md](10-userspace.md)                 | vinit, libc, C++ runtime, applications, GUI                   |
| [11-tools.md](11-tools.md)                         | mkfs.ziafs, fsck.ziafs, gen_roots_der                         |
| [12-crypto.md](12-crypto.md)                       | TLS 1.3, SSH crypto, hash functions, encryption               |
| [13-servers.md](13-servers.md)                     | Display servers (consoled, displayd)                          |
| [14-summary.md](14-summary.md)                     | Implementation summary and development roadmap                |
| [15-boot.md](15-boot.md)                           | VBoot UEFI bootloader, two-disk architecture                  |
| [16-gui.md](16-gui.md)                             | GUI subsystem (displayd, libgui, taskbar)                     |

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
└─────────────────────────┬───────────────────────────────────────┘
                          │ SVC #0 (Syscalls)
┌─────────────────────────┴───────────────────────────────────────┐
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

## Boot Architecture

### VBoot UEFI Bootloader

ViperDOS uses a custom UEFI bootloader (VBoot) for UEFI systems:

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

ViperDOS uses separate disks for system and user content:

| Disk   | Image    | Size | Contents                                   |
|--------|----------|------|--------------------------------------------|
| ESP    | esp.img  | 40MB | VBoot bootloader + kernel (UEFI mode only) |
| System | sys.img  | 2MB  | vinit, consoled, displayd                  |
| User   | user.img | 8MB  | User programs, certificates, data          |

This separation enables:

- Clean kernel/userspace separation
- Boot without user disk (system-only mode)
- Independent rebuild of user programs
- Different security contexts

---

## Kernel Design

### Build Configuration

```cpp
// kernel/include/config.hpp
#define VIPER_KERNEL_ENABLE_FS 1      // Kernel provides filesystem
#define VIPER_KERNEL_ENABLE_NET 1     // Kernel provides TCP/IP stack
#define VIPER_KERNEL_ENABLE_TLS 1     // Kernel provides TLS 1.3
```

### What Runs in Kernel Space

- **Task scheduler**: 8-level priority queues with preemption and SMP
- **Memory management**: PMM, VMM, demand paging, COW
- **IPC channels**: Bidirectional message passing with capability transfer
- **Capability tables**: Handle-based access control
- **Filesystem**: VFS + ViperFS with journaling
- **Networking**: Full TCP/IP stack with TLS 1.3
- **Device drivers**: VirtIO (blk, net, gpu, rng, input)
- **Interrupt/exception handling**: GICv2, timer, syscall dispatch

### What Runs in User Space

| Server   | Assign   | Purpose                            |
|----------|----------|------------------------------------|
| consoled | CONSOLED | GUI terminal emulator              |
| displayd | DISPLAY  | Window management, GUI compositing |

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

### Kernel Network Stack

- Ethernet/ARP/IPv4/ICMP in kernel
- TCP with congestion control and retransmission
- UDP for DNS queries
- TLS 1.3 in kernel
- HTTP/1.1 client with chunked encoding
- SSH-2/SFTP client via libssh (user-space)

### Crash-Consistent Filesystem

- Write-ahead journaling for metadata
- Inode cache with LRU eviction
- Block cache with pinning and read-ahead
- File truncation and fsync support
- Directly accessible via kernel syscalls

### GUI Subsystem

- User-space display server (displayd)
- Window compositing with decorations and z-ordering
- Minimize/maximize/close button handling
- libgui client API with drawing primitives (including scaled fonts)
- Shared memory pixel buffers (zero-copy)
- Per-surface event queues
- Desktop taskbar with window list
- Mouse cursor rendering
- **GUI terminal emulator (consoled)**: ANSI escape sequences, 1.5x font scaling, bidirectional IPC for keyboard
  forwarding

See [16-gui.md](16-gui.md) for complete GUI documentation.

---

## User Applications

### GUI Applications

| Application | Purpose                                     |
|-------------|---------------------------------------------|
| workbench   | Desktop environment with icon grid          |
| calc        | Calculator with digit buttons and operators |
| clock       | Analog clock display                        |
| vedit       | GUI text editor with file save/load         |
| viewer      | Image and text file viewer                  |
| prefs       | System preferences panel                    |
| taskman     | Task manager with process list              |
| guisysinfo  | GUI system information display              |
| taskbar     | Desktop shell taskbar                       |
| hello_gui   | GUI demo with window creation               |

### Console Applications

| Application | Purpose                                           |
|-------------|---------------------------------------------------|
| vinit       | Init process and interactive shell (40+ commands) |
| edit        | Nano-like text editor with file save/load         |
| ssh         | SSH-2 client with Ed25519/RSA authentication      |
| sftp        | Interactive SFTP file transfer client             |
| ping        | ICMP ping utility with RTT statistics             |
| netstat     | Network statistics display                        |
| devices     | Hardware device listing                           |
| sysinfo     | System information and runtime tests              |
| fsinfo      | Filesystem information display                    |
| mathtest    | Math library validation                           |
| hello       | Malloc/heap test program                          |

---

## Service Discovery via Assigns

User-space servers register themselves using the assign system:

```cpp
// Server registration
sys::assign_set("DISPLAY", service_channel);

// Client discovery
u32 display_handle;
sys::assign_get("DISPLAY", &display_handle);
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
./scripts/build_viperdos.sh           # UEFI mode (default)
./scripts/build_viperdos.sh --direct  # Direct kernel boot
./scripts/build_viperdos.sh --serial  # Serial-only mode
./scripts/build_viperdos.sh --debug   # GDB debugging
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
│   ├── net/             # TCP/IP stack, TLS, HTTP, DNS
│   ├── ipc/             # Channels, poll, pollset
│   ├── sched/           # Tasks, scheduler, signals, context switch
│   ├── viper/           # Process model, address spaces
│   ├── cap/             # Capability tables, rights, handles
│   ├── assign/          # Logical device assigns
│   ├── syscall/         # Syscall dispatch table
│   └── kobj/            # Kernel objects (file, dir, shm, channel)
├── user/
│   ├── servers/         # User-space display servers
│   │   ├── consoled/    # GUI terminal emulator
│   │   └── displayd/    # Window manager/compositor
│   ├── vinit/           # Init process + shell
│   ├── workbench/       # Desktop environment
│   ├── calc/            # Calculator
│   ├── clock/           # Analog clock
│   ├── vedit/           # GUI text editor
│   ├── viewer/          # Image/text viewer
│   ├── prefs/           # System preferences
│   ├── taskman/         # Task manager
│   ├── edit/            # Console text editor
│   ├── ssh/             # SSH client
│   ├── sftp/            # SFTP client
│   ├── ping/            # Ping utility
│   ├── hello_gui/       # GUI demo
│   ├── libc/            # Freestanding C library
│   │   ├── include/     # C headers (stdio.h, string.h, etc.)
│   │   │   └── c++/     # C++ headers
│   │   └── src/         # Implementation files (~56 sources)
│   ├── libgui/          # GUI client library
│   ├── libwidget/       # Widget toolkit library
│   ├── libtls/          # TLS library (user-space API)
│   ├── libhttp/         # HTTP client library
│   ├── libssh/          # SSH-2/SFTP library
│   └── libvirtio/       # User-space VirtIO library
├── include/viperdos/     # Shared kernel/user ABI headers
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

- **January 2026 (v0.3.0)**: Hybrid kernel architecture
    - **Kernel services**: Filesystem, networking, TLS 1.3 implemented in kernel
    - **User-space display servers**: consoled (GUI terminal), displayd (window manager)
    - **Device syscalls**: MAP_DEVICE, IRQ_REGISTER, DMA_ALLOC for display servers
    - **Shared memory IPC**: SHM_CREATE, SHM_MAP for framebuffer sharing

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
