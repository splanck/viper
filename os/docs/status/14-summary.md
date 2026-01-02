# ViperOS Summary and Roadmap

**Version:** 0.3.0 (January 2026)
**Target:** AArch64 (Cortex-A72) on QEMU virt machine
**Total Lines of Code:** ~109,000

## Executive Summary

ViperOS is a **capability-based microkernel** operating system for AArch64. The kernel provides only essential services (scheduling, memory, IPC, device primitives), while higher-level functionality runs in user-space servers communicating via message-passing channels.

The system is fully functional for QEMU bring-up with:

- **Microkernel core**: Priority-based scheduler, capability tables, IPC channels
- **Complete memory management**: Demand paging, COW, buddy/slab allocators
- **User-space servers**: netd (TCP/IP), fsd (filesystem), blkd (block), consoled, inputd
- **Full networking**: TCP/IP, TLS 1.3, HTTP, SSH-2/SFTP
- **Journaling filesystem**: ViperFS with block/inode caching
- **Capability-based security**: Handle-based access with rights derivation
- **Comprehensive libc**: POSIX-compatible C library with C++ support

---

## Implementation Completeness

### Microkernel Core (95% Complete)

| Component | Status | Notes |
|-----------|--------|-------|
| Boot/Init | 100% | QEMU virt, PSCI multicore |
| Memory Management | 100% | PMM, VMM, COW, buddy, slab |
| Priority Scheduler | 100% | 8 priority queues, preemption |
| IPC Channels | 100% | Send, recv, handle transfer |
| Capability System | 95% | Tables, rights, derivation |
| Device Primitives | 100% | MAP_DEVICE, IRQ, DMA |
| Syscall Interface | 100% | 90+ syscalls |
| Exception Handling | 100% | Faults, IRQ, signals |

### User-Space Servers (95% Complete)

| Server | Status | Notes |
|--------|--------|-------|
| netd | 95% | Complete TCP/IP stack |
| fsd | 95% | Full filesystem operations |
| blkd | 95% | VirtIO-blk with IRQ |
| consoled | 90% | Console output |
| inputd | 90% | Keyboard/mouse input |

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
| SHA-256/384 | 100% | TLS |
| SHA-1 | 100% | SSH legacy |
| AES-GCM | 100% | TLS |
| AES-CTR | 100% | SSH |
| ChaCha20-Poly1305 | 100% | TLS |
| X25519 | 100% | Key exchange |
| Ed25519 | 100% | Signatures |
| RSA | 90% | Sign/verify |
| X.509 | 90% | Cert parsing |

### User Space (90% Complete)

| Component | Status | Notes |
|-----------|--------|-------|
| libc | 90% | 56 source files, POSIX subset |
| C++ headers | 85% | 66 header files |
| libnetclient | 95% | netd client |
| libfsclient | 95% | fsd client |
| libtls | 90% | TLS 1.3 |
| libhttp | 90% | HTTP client |
| libssh | 85% | SSH-2, SFTP |
| vinit shell | 90% | Commands, history |
| Utilities | 90% | ping, edit, etc. |

---

## What Works Today

1. **Microkernel Boot**: Kernel starts, servers initialize, shell runs
2. **IPC Communication**: All user-space servers communicate via channels
3. **File Operations**: Create, read, write, delete via fsd
4. **Networking**: TCP/IP via netd, TLS, HTTP, SSH
5. **Block I/O**: Filesystem backed by blkd with ViperFS
6. **Console/Input**: Output via consoled, input via inputd
7. **Process Management**: Fork, wait, exit with capability tables
8. **Memory**: Dynamic allocation, demand paging, COW
9. **Multicore**: 4 CPUs boot (scheduler is single-threaded)

---

## Microkernel Architecture

### Kernel Services (EL1)

- Priority-based scheduler (8 queues)
- Physical/virtual memory management
- IPC channels with handle transfer
- Capability tables (per-process)
- Device access primitives (MMIO, IRQ, DMA)
- Exception/interrupt handling

### User-Space Services (EL0)

| Service | Assign | Purpose |
|---------|--------|---------|
| netd | NETD: | TCP/IP stack, sockets, DNS |
| fsd | FSD: | Filesystem operations |
| blkd | BLKD: | Block device I/O |
| consoled | CONSOLED: | Console output |
| inputd | INPUTD: | Keyboard/mouse |

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

1. **SMP Scheduling**
   - CPUs boot but scheduler is single-threaded
   - Need: Per-CPU run queues, load balancing

2. **exec() Family**
   - Currently only `task_spawn` exists
   - Need: Full exec() for shell command execution

3. **pipe() Syscall**
   - No inter-process pipes
   - Blocks: Shell pipelines (`ls | grep foo`)

4. **PTY Subsystem**
   - No kernel pseudo-terminal support
   - Needed for: Terminal emulation, SSH server

5. **Signal Handlers**
   - Signal infrastructure exists
   - Need: User handler invocation trampoline

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
11. **GUI Toolkit**
12. **USB Support**
13. **Real Hardware Targets**

---

## Key Metrics

### Code Size

- Kernel: ~49,000 lines
- User-space: ~60,000 lines
- **Total: ~109,000 lines**

### Component Breakdown

| Component | SLOC |
|-----------|------|
| Architecture | ~3,600 |
| Memory Management | ~5,400 |
| Scheduler | ~3,600 |
| IPC | ~2,500 |
| Filesystem | ~6,500 |
| Drivers | ~5,000 |
| Console | ~3,500 |
| Capabilities | ~700 |
| Syscalls | ~2,000 |
| User Servers | ~8,900 |
| libc | ~28,000 |
| Libraries | ~20,000 |

### Binary Sizes (Approximate)

- kernel.sys: ~800KB
- vinit.sys: ~120KB
- netd.sys: ~150KB
- fsd.sys: ~120KB
- blkd.sys: ~80KB
- ssh.prg: ~170KB
- sftp.prg: ~190KB

### Performance (QEMU, Single Core)

- Boot to shell: ~500ms
- IPC round-trip: ~10-15μs
- Context switch: ~1-2μs
- File read (4KB via fsd): ~150μs
- Socket send (small): ~100μs

---

## Architecture Decisions

### Confirmed Directions

1. **Capability-Based Security**
   - All resources accessed via handles
   - Rights can only be reduced, not expanded
   - Handle transfer via IPC for delegation

2. **Microkernel Design**
   - Minimal kernel (scheduling, memory, IPC)
   - Drivers in user-space (netd, fsd, blkd)
   - Better fault isolation and security

3. **Message-Passing IPC**
   - Bidirectional channels (256 bytes/msg)
   - Up to 4 handles per message
   - Blocking and non-blocking operations

4. **Amiga-Inspired UX**
   - Logical device assigns (SYS:, C:, etc.)
   - Return codes (OK, WARN, ERROR, FAIL)
   - Interactive shell commands

5. **POSIX-ish libc**
   - Familiar API for applications
   - Routes to appropriate server (netd/fsd)
   - Freestanding implementation

---

## Building and Running

### Quick Start

```bash
cd os
./build_viper.sh           # Build and run (graphics)
./build_viper.sh --serial  # Build and run (serial only)
./build_viper.sh --debug   # Build with GDB debugging
./build_viper.sh --test    # Run test suite
```

### Requirements

- Clang with AArch64 support (or aarch64-elf-gcc)
- AArch64 GNU binutils
- QEMU with aarch64 support
- CMake 3.16+

---

## Conclusion

ViperOS v0.3.0 represents a complete microkernel architecture with user-space servers handling networking, filesystem, block I/O, console, and input. The system demonstrates capability-based security, message-passing IPC, and priority-based scheduling.

The most impactful next steps are:
1. **SMP scheduling** for multicore utilization
2. **exec/pipe/PTY** for full shell functionality
3. **IPv6** for modern networking
4. **Signal handlers** for POSIX compatibility

With these additions, ViperOS would be suitable for embedded systems, educational use, or specialized applications requiring strong isolation guarantees.
