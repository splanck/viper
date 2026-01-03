# ViperOS Summary and Roadmap

**Version:** 0.3.1 (January 2026)
**Target:** AArch64 (Cortex-A72) on QEMU virt machine
**Total Lines of Code:** ~115,000

## Executive Summary

ViperOS is a **capability-based microkernel** operating system for AArch64. The kernel provides only essential services (scheduling, memory, IPC, device primitives), while higher-level functionality runs in user-space servers communicating via message-passing channels.

The system is fully functional for QEMU bring-up with:

- **UEFI Boot**: Custom VBoot bootloader with GOP framebuffer support
- **Microkernel core**: Priority-based SMP scheduler, capability tables, IPC channels
- **Complete memory management**: Demand paging, COW, buddy/slab allocators
- **User-space servers**: netd (TCP/IP), fsd (filesystem), blkd (block), consoled, inputd, displayd
- **Full networking**: TCP/IP, TLS 1.3, HTTP, SSH-2/SFTP
- **Journaling filesystem**: ViperFS with block/inode caching
- **Capability-based security**: Handle-based access with rights derivation
- **Comprehensive libc**: POSIX-compatible C library with C++ support
- **GUI subsystem**: User-space display server with windowing

---

## Implementation Completeness

### Microkernel Core (98% Complete)

| Component | Status | Notes |
|-----------|--------|-------|
| VBoot Bootloader | 100% | UEFI, GOP, ELF loading |
| Boot/Init | 100% | QEMU virt, PSCI multicore |
| Memory Management | 100% | PMM, VMM, COW, buddy, slab |
| Priority Scheduler | 100% | 8 priority queues, SMP, work stealing |
| IPC Channels | 100% | Send, recv, handle transfer |
| Capability System | 95% | Tables, rights, derivation |
| Device Primitives | 100% | MAP_DEVICE, IRQ, DMA, framebuffer |
| Syscall Interface | 100% | 90+ syscalls |
| Exception Handling | 100% | Faults, IRQ, signals |

### User-Space Servers (95% Complete)

| Server | Status | Notes |
|--------|--------|-------|
| netd | 95% | Complete TCP/IP stack |
| fsd | 95% | Full filesystem operations |
| blkd | 95% | VirtIO-blk with IRQ |
| consoled | 95% | Console output |
| inputd | 95% | Keyboard/mouse input |
| displayd | 90% | Desktop shell with taskbar, window management |

### Drivers (100% Complete for QEMU)

| Driver | Status | Notes |
|--------|--------|-------|
| Serial (PL011) | 100% | Console I/O |
| Graphics (ramfb) | 100% | Framebuffer, fonts |
| VirtIO-blk | 100% | Block device |
| VirtIO-net | 100% | Ethernet, IRQ |
| VirtIO-gpu | 95% | 2D operations |
| VirtIO-rng | 100% | Random numbers |
| VirtIO-input | 100% | Keyboard, mouse |
| GIC | 100% | v2 and v3 support |
| Timer | 100% | Nanosecond precision |

### Graphics Console (100% Complete)

| Feature | Status | Notes |
|---------|--------|-------|
| Text Rendering | 100% | Scaled 10x20 font |
| ANSI Escape Codes | 100% | Cursor, colors, clearing |
| Blinking Cursor | 100% | 500ms interval |
| Scrollback Buffer | 100% | 1000 lines, Shift+Up/Down navigation |
| Green Border | 100% | 4px + 4px padding |

### Filesystem (95% Complete)

| Component | Status | Notes |
|-----------|--------|-------|
| VFS | 95% | Path resolution, FD table |
| ViperFS | 95% | Journal, caching |
| Block Cache | 100% | LRU, pinning |
| Inode Cache | 100% | Refcounting |
| Assigns | 100% | Logical volumes |

### Networking (95% Complete)

| Protocol | Status | Notes |
|----------|--------|-------|
| Ethernet | 100% | Frame handling |
| ARP | 100% | Cache, resolution |
| IPv4 | 100% | Fragmentation, reassembly |
| ICMP | 100% | Ping, errors |
| UDP | 100% | DNS queries |
| TCP | 95% | Congestion control, retransmit |
| DNS | 95% | A records |
| TLS 1.3 | 90% | Client, certs |
| HTTP | 90% | GET/POST, chunked |
| SSH-2 | 85% | Client, SFTP |

### Cryptography (95% Complete)

| Algorithm | Status | Notes |
|-----------|--------|-------|
| SHA-256/384/512 | 100% | TLS, SSH |
| SHA-1 | 100% | SSH legacy |
| AES-GCM | 100% | TLS |
| AES-CTR | 100% | SSH |
| ChaCha20-Poly1305 | 100% | TLS |
| X25519 | 100% | Key exchange |
| Ed25519 | 100% | Signatures |
| RSA | 90% | Sign/verify |
| X.509 | 90% | Cert parsing |

### User Space (92% Complete)

| Component | Status | Notes |
|-----------|--------|-------|
| libc | 92% | 56 source files, POSIX subset |
| C++ headers | 85% | 66 header files |
| libnetclient | 95% | netd client |
| libfsclient | 95% | fsd client |
| libgui | 90% | displayd client, window list, drawing helpers |
| libtls | 90% | TLS 1.3 |
| libhttp | 90% | HTTP client |
| libssh | 85% | SSH-2, SFTP |
| vinit shell | 95% | 40+ commands |
| Applications | 95% | edit, ssh, sftp, ping, etc. |

---

## What Works Today

1. **UEFI Boot**: VBoot loads kernel via UEFI on AArch64
2. **Two-Disk Architecture**: Separate system (servers) and user (programs) disks
3. **Microkernel Boot**: Kernel starts, servers initialize, shell runs
4. **SMP Scheduling**: 4 CPUs with work stealing and load balancing
5. **IPC Communication**: All user-space servers communicate via channels
6. **File Operations**: Create, read, write, delete via fsd
7. **Networking**: TCP/IP via netd, TLS, HTTP, SSH
8. **Block I/O**: Filesystem backed by blkd with ViperFS
9. **Console/Input**: Output via consoled, input via inputd
10. **GUI Windows**: Display server with window compositing
11. **Process Management**: Fork, wait, exit with capability tables
12. **Memory**: Dynamic allocation, demand paging, COW
13. **Text Editor**: Full-screen nano-like editor (edit)
14. **Graphics Console**: ANSI escapes, scrollback, cursor blinking

---

## Microkernel Architecture

### Kernel Services (EL1)

- Priority-based scheduler (8 queues, SMP with work stealing)
- Physical/virtual memory management (demand paging, COW)
- IPC channels with handle transfer
- Capability tables (per-process)
- Device access primitives (MMIO, IRQ, DMA, framebuffer)
- Exception/interrupt handling

### User-Space Services (EL0)

| Service | Assign | Purpose |
|---------|--------|---------|
| netd | NETD: | TCP/IP stack, sockets, DNS |
| fsd | FSD: | Filesystem operations |
| blkd | BLKD: | Block device I/O |
| consoled | CONSOLED: | Console output |
| inputd | INPUTD: | Keyboard/mouse |
| displayd | DISPLAY: | Window management, GUI |

### Build Configuration

```cpp
#define VIPER_MICROKERNEL_MODE 1    // Microkernel mode
#define VIPER_KERNEL_ENABLE_FS 1    // Kernel FS for boot
#define VIPER_KERNEL_ENABLE_NET 0   // Use netd instead
#define VIPER_KERNEL_ENABLE_TLS 0   // Use libtls instead
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
   - Needed for: Terminal emulation, SSH server

4. **Signal Handlers**
   - Signal infrastructure exists
   - Need: User handler invocation trampoline

5. **GUI Window Drag/Resize**
   - Mouse events delivered but window drag not implemented
   - Blocks: Desktop-like window management

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

- Kernel: ~50,000 lines
- Bootloader: ~1,700 lines
- User-space: ~63,000 lines
- **Total: ~115,000 lines**

### Component Breakdown

| Component | SLOC |
|-----------|------|
| VBoot Bootloader | ~1,700 |
| Architecture | ~3,600 |
| Memory Management | ~5,550 |
| Scheduler | ~3,600 |
| IPC | ~2,500 |
| Filesystem | ~9,600 |
| Drivers | ~6,000 |
| Console | ~3,500 |
| Capabilities | ~2,900 |
| Syscalls | ~2,000 |
| User Servers | ~10,500 |
| libc | ~28,000 |
| Libraries | ~23,000 |
| Applications | ~5,000 |

### Binary Sizes (Approximate)

- kernel.sys: ~850KB
- BOOTAA64.EFI: ~15KB
- vinit.sys: ~130KB
- netd.sys: ~160KB
- fsd.sys: ~130KB
- blkd.sys: ~85KB
- displayd.sys: ~100KB
- ssh.prg: ~175KB
- sftp.prg: ~195KB
- edit.prg: ~60KB

### Performance (QEMU, 4 Cores)

- Boot to shell: ~400ms
- IPC round-trip: ~10-15μs
- Context switch: ~1-2μs
- File read (4KB via fsd): ~150μs
- Socket send (small): ~100μs
- Work stealing latency: ~50μs

---

## Architecture Decisions

### Confirmed Directions

1. **Capability-Based Security**
   - All resources accessed via handles
   - Rights can only be reduced, not expanded
   - Handle transfer via IPC for delegation

2. **Microkernel Design**
   - Minimal kernel (scheduling, memory, IPC)
   - Drivers in user-space (netd, fsd, blkd, displayd)
   - Better fault isolation and security

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
   - Routes to appropriate server (netd/fsd)
   - Freestanding implementation

---

## Building and Running

### Quick Start

```bash
cd os
./scripts/build_viperos.sh            # Build and run (UEFI graphics)
./scripts/build_viperos.sh --direct   # Direct kernel boot
./scripts/build_viperos.sh --serial   # Serial only mode
./scripts/build_viperos.sh --debug    # Build with GDB debugging
./scripts/build_viperos.sh --test     # Run test suite
```

### Requirements

- Clang with AArch64 support (LLVM clang for UEFI)
- AArch64 GNU binutils
- QEMU with aarch64 support
- CMake 3.16+
- UEFI tools: sgdisk, mtools (for UEFI mode)

---

## What's New in v0.3.1

### Boot Infrastructure
- **VBoot UEFI bootloader**: Custom UEFI bootloader with GOP support
- **Two-disk architecture**: Separate system and user disks
- **ESP creation**: Automated EFI System Partition generation

### SMP Improvements
- **Per-CPU run queues**: Private priority queues per CPU
- **Work stealing**: Automatic task stealing when queue empty
- **Load balancing**: Periodic task migration (100ms intervals)
- **CPU affinity**: Explicit task-to-CPU binding

### Graphics Console
- **ANSI escape codes**: Cursor positioning, colors, clearing
- **Scrollback buffer**: 1000 lines of history
- **Blinking cursor**: 500ms XOR-based cursor
- **Dynamic sizing**: Console adapts to framebuffer resolution

### GUI Subsystem
- **Display server (displayd)**: Window compositing with z-ordering
- **libgui library**: Client API for GUI applications
- **Window decorations**: Title bar, border, minimize/maximize/close buttons
- **Desktop taskbar**: Window list, click to restore/focus
- **Per-surface event queues**: Keyboard, mouse, focus, close events
- **Software cursor**: 16x16 arrow with background save

### New Applications
- **edit**: Nano-like text editor with file save/load
- **hello_gui**: GUI demo with window creation
- **taskbar**: Desktop shell taskbar
- **devices**: Hardware device listing
- **fsinfo**: Filesystem information display

---

## Conclusion

ViperOS v0.3.1 represents a complete microkernel architecture with UEFI boot, SMP scheduling, and user-space servers handling networking, filesystem, block I/O, console, input, and display. The system demonstrates capability-based security, message-passing IPC, and modern boot infrastructure.

---

## Priority Recommendations: Next 5 Steps

### 1. GUI Window Drag and Resize
**Impact:** Desktop-like window management
- Title bar drag for window movement
- Edge/corner drag for resizing
- Minimum window size constraints
- Foundation for usable desktop environment

### 2. exec() and pipe() Implementation
**Impact:** Full Unix shell functionality
- Process image replacement (exec family)
- Inter-process pipes for command chaining
- Shell pipeline support (`cmd1 | cmd2 | cmd3`)
- Standard Unix development workflow

### 3. PTY Subsystem
**Impact:** Terminal emulation and remote access
- Pseudo-terminal master/slave pairs
- Required for SSH server implementation
- GUI terminal emulator support
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

ViperOS v0.3.1 represents a complete microkernel architecture with UEFI boot, SMP scheduling, and user-space servers handling networking, filesystem, block I/O, console, input, and display. The system demonstrates capability-based security, message-passing IPC, and modern boot infrastructure.

With these additions, ViperOS would be suitable for embedded systems, educational use, or specialized applications requiring strong isolation guarantees.
