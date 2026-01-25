# ViperDOS Summary and Roadmap

**Version:** 0.3.2 (January 2026)
**Target:** AArch64 (Cortex-A57) on QEMU virt machine
**Total Lines of Code:** ~143,000

## Executive Summary

ViperDOS is a **capability-based hybrid kernel** operating system for AArch64. The kernel provides comprehensive
services (scheduling, memory, IPC, filesystem, networking, TLS), while display services run in user-space servers
(consoled, displayd) communicating via message-passing channels.

The system is fully functional for QEMU bring-up with:

- **UEFI Boot**: Custom VBoot bootloader with GOP framebuffer support
- **Hybrid kernel**: Priority-based SMP scheduler, capability tables, IPC channels
- **Complete memory management**: Demand paging, COW, buddy/slab allocators
- **Kernel services**: TCP/IP stack, TLS 1.3, VFS, ViperFS with journaling
- **Display servers**: consoled (GUI terminal), displayd (window manager)
- **Full networking**: TCP/IP, TLS 1.3, HTTP, SSH-2/SFTP (kernel-based)
- **Journaling filesystem**: ViperFS with block/inode caching (kernel-based)
- **Capability-based security**: Handle-based access with rights derivation
- **Comprehensive libc**: POSIX-compatible C library with C++ support
- **GUI subsystem**: User-space display server with windowing

---

## Implementation Completeness

### Kernel Core (98% Complete)

| Component          | Status | Notes                                                  |
|--------------------|--------|--------------------------------------------------------|
| VBoot Bootloader   | 100%   | UEFI, GOP, ELF loading                                 |
| Boot/Init          | 100%   | QEMU virt, PSCI multicore                              |
| Memory Management  | 100%   | PMM, VMM, COW, buddy, slab                             |
| Priority Scheduler | 100%   | 8 priority queues, SMP, CFS, EDF, priority inheritance |
| IPC Channels       | 100%   | Send, recv, handle transfer                            |
| Capability System  | 95%    | Tables, rights, derivation                             |
| Device Primitives  | 100%   | MAP_DEVICE, IRQ, DMA, framebuffer                      |
| Syscall Interface  | 100%   | 90+ syscalls                                           |
| Exception Handling | 100%   | Faults, IRQ, signals                                   |

### User-Space Display Servers (95% Complete)

| Server   | Status | Notes                                            |
|----------|--------|--------------------------------------------------|
| consoled | 95%    | GUI terminal emulator, ANSI, keyboard forwarding |
| displayd | 90%    | Window compositing, event delivery               |

### Drivers (100% Complete for QEMU)

| Driver           | Status | Notes                |
|------------------|--------|----------------------|
| Serial (PL011)   | 100%   | Console I/O          |
| Graphics (ramfb) | 100%   | Framebuffer, fonts   |
| VirtIO-blk       | 100%   | Block device         |
| VirtIO-net       | 100%   | Ethernet, IRQ        |
| VirtIO-gpu       | 95%    | 2D operations        |
| VirtIO-rng       | 100%   | Random numbers       |
| VirtIO-input     | 100%   | Keyboard, mouse      |
| GIC              | 100%   | v2 and v3 support    |
| Timer            | 100%   | Nanosecond precision |

### Graphics Console (100% Complete)

| Feature           | Status | Notes                    |
|-------------------|--------|--------------------------|
| Text Rendering    | 100%   | Scaled 10x20 font        |
| ANSI Escape Codes | 100%   | Cursor, colors, clearing |
| Blinking Cursor   | 100%   | 500ms interval           |
| Scrollback Buffer | 100%   | 1000 lines               |
| Green Border      | 100%   | 4px + 4px padding        |

### Filesystem (95% Complete)

| Component   | Status | Notes                     |
|-------------|--------|---------------------------|
| VFS         | 95%    | Path resolution, FD table |
| ViperFS     | 95%    | Journal, caching          |
| Block Cache | 100%   | LRU, pinning              |
| Inode Cache | 100%   | Refcounting               |
| Assigns     | 100%   | Logical volumes           |

### Networking (95% Complete)

| Protocol | Status | Notes                          |
|----------|--------|--------------------------------|
| Ethernet | 100%   | Frame handling                 |
| ARP      | 100%   | Cache, resolution              |
| IPv4     | 100%   | Fragmentation, reassembly      |
| ICMP     | 100%   | Ping, errors                   |
| UDP      | 100%   | DNS queries                    |
| TCP      | 95%    | Congestion control, retransmit |
| DNS      | 95%    | A records                      |
| TLS 1.3  | 90%    | Client, certs                  |
| HTTP     | 90%    | GET/POST, chunked              |
| SSH-2    | 85%    | Client, SFTP                   |

### Cryptography (95% Complete)

| Algorithm         | Status | Notes        |
|-------------------|--------|--------------|
| SHA-256/384/512   | 100%   | TLS, SSH     |
| SHA-1             | 100%   | SSH legacy   |
| AES-GCM           | 100%   | TLS          |
| AES-CTR           | 100%   | SSH          |
| ChaCha20-Poly1305 | 100%   | TLS          |
| X25519            | 100%   | Key exchange |
| Ed25519           | 100%   | Signatures   |
| RSA               | 90%    | Sign/verify  |
| X.509             | 90%    | Cert parsing |

### User Space (92% Complete)

| Component    | Status | Notes                         |
|--------------|--------|-------------------------------|
| libc         | 92%    | 56 source files, POSIX subset |
| C++ headers  | 85%    | 66 header files               |
| libvirtio    | 95%    | VirtIO driver helpers         |
| libwidget    | 90%    | Widget toolkit for GUI        |
| libgui       | 90%    | displayd client, font scaling |
| libtls       | 90%    | TLS 1.3                       |
| libhttp      | 90%    | HTTP client                   |
| libssh       | 85%    | SSH-2, SFTP                   |
| vinit shell  | 95%    | 40+ commands                  |
| Applications | 95%    | edit, ssh, sftp, ping, etc.   |

---

## What Works Today

1. **UEFI Boot**: VBoot loads kernel via UEFI on AArch64
2. **Two-Disk Architecture**: Separate system and user disks
3. **Hybrid Kernel Boot**: Kernel starts with all services, display servers initialize
4. **SMP Scheduling**: 4 CPUs with work stealing and load balancing
5. **IPC Communication**: Display servers communicate via channels
6. **File Operations**: Create, read, write, delete via kernel VFS
7. **Networking**: Kernel TCP/IP stack, TLS, HTTP, SSH
8. **Block I/O**: Kernel filesystem with VirtIO-blk driver
9. **GUI Terminal**: consoled runs as a window with ANSI colors and keyboard forwarding
10. **GUI Windows**: Display server with window compositing, mouse/keyboard events
11. **Process Management**: Fork, wait, exit with capability tables
12. **Memory**: Dynamic allocation, demand paging, COW
13. **Text Editor**: Full-screen nano-like editor (edit)
14. **Graphics Console**: ANSI escapes, scrollback, cursor blinking

---

## Hybrid Kernel Architecture

### Kernel Services (EL1)

- Priority-based scheduler (8 queues, SMP, CFS, EDF, priority inheritance)
- Physical/virtual memory management (demand paging, COW)
- IPC channels with handle transfer
- Capability tables (per-process)
- Device drivers (VirtIO-blk, VirtIO-net, ramfb, PL011)
- VFS and ViperFS filesystem with journaling
- TCP/IP networking stack
- TLS 1.3 encryption
- Exception/interrupt handling

### User-Space Display Services (EL0)

| Service  | Assign   | Purpose                    |
|----------|----------|----------------------------|
| consoled | CONSOLED | GUI terminal emulator      |
| displayd | DISPLAY  | Window management, GUI     |

### Build Configuration

```cpp
#define VIPER_KERNEL_ENABLE_FS 1    // Kernel filesystem (enabled)
#define VIPER_KERNEL_ENABLE_NET 1   // Kernel networking (enabled)
#define VIPER_KERNEL_ENABLE_TLS 1   // Kernel TLS 1.3 (enabled)
```

---

## What's Missing

### High Priority

1. **exec() Family**
    - Currently only `task_spawn` exists
    - Need: Full exec() for shell command execution

2. **pipe() Syscall**
    - No inter-process pipes
    - Blocks: Shell pipelines (`ls | grep foo`)

3. **PTY Subsystem**
    - No kernel pseudo-terminal support
    - Needed for: SSH server

4. **Signal Handlers**
    - Signal infrastructure exists
    - Need: User handler invocation trampoline

5. **True Window Resize**
    - Window resize currently visual-only
    - Need: Pixel buffer reallocation on resize

### Medium Priority

6. **IPv6**
    - Not yet implemented
    - Need: Full NDP, SLAAC

7. **Dynamic Linking**
    - No shared library support
    - Reduces: Memory usage, update flexibility

8. **File Locking**
    - No flock() or fcntl() locking
    - Needed for: Multi-process file access

9. **TLS Improvements**
    - Session resumption
    - ECDSA certificates
    - Server mode

### Low Priority

10. **Sound System**
11. **Alt+Tab Window Switching**
12. **USB Support**
13. **Real Hardware Targets**

---

## Key Metrics

### Code Size

- Kernel: ~60,000 lines
- Bootloader: ~1,700 lines
- User-space: ~81,000 lines
- **Total: ~143,000 lines**

### Component Breakdown

| Component           | SLOC    |
|---------------------|---------|
| VBoot Bootloader    | ~1,700  |
| Architecture        | ~3,600  |
| Memory Management   | ~5,550  |
| Scheduler           | ~4,500  |
| IPC                 | ~2,500  |
| Kernel Filesystem   | ~6,600  |
| Kernel Networking   | ~5,400  |
| Drivers             | ~6,000  |
| Console             | ~3,500  |
| Capabilities        | ~2,900  |
| Syscalls            | ~4,000  |
| Display Servers     | ~4,200  |
| libc                | ~28,000 |
| Libraries           | ~32,000 |
| GUI Applications    | ~8,000  |
| Console Applications| ~5,000  |

### Binary Sizes (Approximate)

- kernel.sys: ~1.2MB
- BOOTAA64.EFI: ~15KB
- vinit.sys: ~130KB
- consoled.sys: ~100KB
- displayd.sys: ~100KB
- ssh.prg: ~175KB
- sftp.prg: ~195KB
- edit.prg: ~60KB

### Performance (QEMU, 4 Cores)

- Boot to shell: ~400ms
- IPC round-trip: ~10-15μs
- Context switch: ~1-2μs
- File read (4KB): ~150μs
- Socket send (small): ~100μs
- Work stealing latency: ~50μs

---

## Architecture Decisions

### Confirmed Directions

1. **Capability-Based Security**
    - All resources accessed via handles
    - Rights can only be reduced, not expanded
    - Handle transfer via IPC for delegation

2. **Hybrid Kernel Design**
    - Comprehensive kernel (scheduling, memory, IPC, filesystem, networking, TLS)
    - Display services in user-space (consoled, displayd)
    - Efficient performance with kernel-based services

3. **UEFI Boot**
    - Custom VBoot bootloader
    - Standard UEFI interfaces (GOP, memory map)
    - Two-disk architecture (system + user)

4. **SMP with Work Stealing**
    - Per-CPU run queues
    - Automatic load balancing
    - CPU affinity support

5. **Message-Passing IPC**
    - Bidirectional channels (256 bytes/msg)
    - Up to 4 handles per message
    - Blocking and non-blocking operations

6. **Amiga-Inspired UX**
    - Logical device assigns (SYS:, C:, etc.)
    - Return codes (OK, WARN, ERROR, FAIL)
    - Interactive shell commands

7. **POSIX-ish libc**
    - Familiar API for applications
    - Direct kernel syscalls for all operations
    - Freestanding implementation

---

## Building and Running

### Quick Start

```bash
cd os
./scripts/build_viperdos.sh            # Build and run (UEFI graphics)
./scripts/build_viperdos.sh --direct   # Direct kernel boot
./scripts/build_viperdos.sh --serial   # Serial only mode
./scripts/build_viperdos.sh --debug    # Build with GDB debugging
./scripts/build_viperdos.sh --test     # Run test suite
```

### Requirements

- Clang with AArch64 support (LLVM clang for UEFI)
- AArch64 GNU binutils
- QEMU with aarch64 support
- CMake 3.16+
- UEFI tools: sgdisk, mtools (for UEFI mode)

---

## What's New in v0.3.2

### GUI Terminal Emulator (consoled)

- **GUI Window**: consoled now runs as a window via libgui/displayd
- **ANSI Escape Sequences**: Full CSI support (colors, cursor movement, erase)
- **Bidirectional IPC**: Keyboard events forwarded from displayd to connected clients
- **1.5x Font Scaling**: 12x12 pixel cells (half-unit scaling system)
- **Per-Cell Attributes**: Bold, dim, italic, underline, blink, reverse, hidden, strikethrough

### libgui Enhancements

- **gui_draw_char()**: Character drawing with foreground and background colors
- **gui_draw_char_scaled()**: Fractional font scaling (scale=3 for 1.5x, etc.)
- **Built-in 8x8 Font**: Complete ASCII character set

### Infrastructure Improvements

- **Blocking IPC Send**: Console write retries until message delivered
- **Message Draining**: consoled processes all pending messages before rendering
- **Service Timing**: Fixed wait_for_service() to use actual sleep intervals

---

## What's New in v0.3.1

### Boot Infrastructure

- **VBoot UEFI bootloader**: Custom UEFI bootloader with GOP support
- **Two-disk architecture**: Separate system and user disks
- **ESP creation**: Automated EFI System Partition generation

### SMP and Scheduler Improvements

- **Per-CPU run queues**: Private priority queues per CPU
- **Work stealing**: Automatic task stealing when queue empty
- **Load balancing**: Periodic task migration (100ms intervals)
- **CPU affinity**: Explicit task-to-CPU binding (bitmask per task)
- **CFS Fair Scheduling**: vruntime tracking with nice values (-20 to +19)
- **SCHED_DEADLINE**: EDF ordering with bandwidth reservation (95% max)
- **Priority Inheritance**: PI mutexes prevent priority inversion
- **Idle State Tracking**: WFI enter/exit statistics per CPU

### Graphics Console

- **ANSI escape codes**: Cursor positioning, colors, clearing
- **Scrollback buffer**: 1000 lines of history
- **Blinking cursor**: 500ms XOR-based cursor
- **Dynamic sizing**: Console adapts to framebuffer resolution

### GUI Subsystem

- **Display server (displayd)**: Window compositing
- **libgui library**: Client API for GUI applications
- **Window decorations**: Title bar, border, close button
- **Software cursor**: 16x16 arrow with background save

### New Applications

- **edit**: Nano-like text editor with file save/load
- **hello_gui**: GUI demo with window creation
- **devices**: Hardware device listing
- **fsinfo**: Filesystem information display

---

## Priority Recommendations: Next 5 Steps

### 1. exec() and pipe() Implementation

**Impact:** Full Unix shell functionality

- Process image replacement (exec family)
- Inter-process pipes for command chaining
- Shell pipeline support (`cmd1 | cmd2 | cmd3`)
- Standard Unix development workflow

### 2. True Window Resize

**Impact:** User-adjustable window sizes

- DISP_RESIZE_SURFACE message type
- Reallocate shared memory for new size
- GUI_EVENT_RESIZE to client
- Client remaps buffer and redraws

### 3. PTY Subsystem

**Impact:** Remote access (SSH server)

- Pseudo-terminal master/slave pairs
- Required for SSH server implementation
- Job control with Ctrl+C, Ctrl+Z

### 4. IPv6 Support

**Impact:** Modern network compatibility

- Dual-stack networking (IPv4 + IPv6)
- NDP for neighbor discovery
- SLAAC for address autoconfiguration
- Required for IPv6-only networks

### 5. Signal Handler Trampoline

**Impact:** POSIX-compatible signal handling

- User-space signal handler invocation
- Proper context save/restore
- Signal masking during handler
- Graceful cleanup on SIGTERM/SIGINT

---

## Conclusion

ViperDOS v0.3.2 represents a complete hybrid kernel architecture with UEFI boot, SMP scheduling, and kernel-based
filesystem, networking, and TLS services. User-space display servers (consoled, displayd) provide the GUI layer.
The system demonstrates capability-based security, message-passing IPC, and modern boot infrastructure.

The addition of the GUI terminal emulator (consoled with ANSI support and keyboard forwarding) enables the shell to run
in a graphical window that coexists with other GUI applications, marking a significant step toward a full desktop
environment.

With these additions, ViperDOS would be suitable for embedded systems, educational use, or specialized applications
requiring efficient performance with a graphical interface.
