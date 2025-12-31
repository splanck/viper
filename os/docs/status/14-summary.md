# ViperOS Summary and Roadmap

**Version:** 0.2.7 (December 2024)
**Target:** AArch64 (Cortex-A72) on QEMU virt machine
**Total Lines of Code:** ~100,000

## Executive Summary

ViperOS is a capability-based microkernel operating system for AArch64 that combines modern kernel design with an Amiga-inspired user experience. The system is fully functional for QEMU bring-up with:

- Complete memory management (demand paging, COW, buddy/slab allocators)
- Preemptive multitasking with 4-core SMP infrastructure
- Full TCP/IP networking with TLS 1.3 and SSH-2 support
- Journaling filesystem with block/inode caching
- Capability-based security model
- Comprehensive user-space libc and C++ support
- User-space microkernel servers (partial)

---

## Implementation Completeness

### Kernel Core (95% Complete)

| Component | Status | Notes |
|-----------|--------|-------|
| Boot/Init | 100% | QEMU virt, PSCI multicore |
| Memory Management | 100% | PMM, VMM, COW, buddy, slab |
| Process Model (Viper) | 95% | Fork, wait, exit, VMA |
| Scheduler | 90% | FIFO, priorities, wait queues |
| IPC Channels | 95% | Send, recv, handles |
| Capability System | 85% | Tables, rights, derivation |
| Syscall Interface | 100% | 80+ syscalls |
| Exception Handling | 100% | Faults, IRQ, signals |

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

### Filesystem (90% Complete)

| Component | Status | Notes |
|-----------|--------|-------|
| VFS | 85% | Path resolution, FD table |
| ViperFS | 90% | Journal, caching |
| Block Cache | 100% | LRU, pinning |
| Inode Cache | 100% | Refcounting |
| Assigns | 100% | Logical volumes |

### Networking (95% Complete)

| Protocol | Status | Notes |
|----------|--------|-------|
| Ethernet | 100% | Frame handling |
| ARP | 100% | Cache, resolution |
| IPv4 | 100% | Fragmentation, reassembly |
| IPv6 | 60% | Basic support |
| ICMP/ICMPv6 | 100% | Ping, errors |
| UDP | 100% | DNS queries |
| TCP | 90% | Congestion control, SACK |
| DNS | 90% | A/AAAA records |
| TLS 1.3 | 85% | Client, certs |
| HTTP | 80% | GET requests |

### Cryptography (90% Complete)

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

### User Space (85% Complete)

| Component | Status | Notes |
|-----------|--------|-------|
| libc | 85% | POSIX subset |
| C++ headers | 80% | STL subset |
| vinit shell | 90% | Commands, history |
| libssh | 80% | SSH-2, SFTP |
| ssh/sftp clients | 80% | Interactive |
| Utilities | 90% | ping, edit, etc. |

### Microkernel Servers (40% Complete)

| Server | Status | Notes |
|--------|--------|-------|
| blkd | 40% | Basic ops |
| fsd | 50% | File ops |
| netd | 30% | Placeholder |

---

## What Works Today

1. **Boot to Shell**: Kernel boots, loads vinit, presents interactive shell
2. **File Operations**: Create, read, write, delete files and directories
3. **Networking**: TCP connections, DNS resolution, HTTPS fetches
4. **TLS**: Secure connections with certificate verification
5. **SSH**: Connect to remote hosts via SSH-2 protocol
6. **SFTP**: Transfer files via SFTP
7. **Process Management**: Fork, exec (limited), wait, exit
8. **Memory**: Dynamic allocation, demand paging, COW
9. **Multicore**: 4 CPUs boot (limited parallel work)

---

## What's Missing

### High Priority (Blocking Real Use)

1. **exec() Family**
   - Currently only `task_spawn` exists
   - Need full exec() for shell command execution
   - Blocks: Shell pipelines, script execution

2. **pipe() Syscall**
   - No inter-process pipes
   - Blocks: Shell pipelines (`ls | grep foo`)

3. **PTY Subsystem**
   - No kernel pseudo-terminal support
   - Needed for: Terminal emulation, SSH server

4. **Complete Microkernel Migration**
   - Drivers still in kernel
   - Need: IRQ-driven user-space drivers

5. **SMP Scheduling**
   - CPUs boot but limited work distribution
   - Need: Per-CPU run queues, load balancing

### Medium Priority (Feature Gaps)

6. **IPv6 Improvements**
   - Basic support exists
   - Need: Full NDP, SLAAC, DHCPv6

7. **Dynamic Linking**
   - No shared library support
   - Reduces: Memory usage, update flexibility

8. **Real Signals**
   - Signal infrastructure exists
   - Need: User handler invocation trampoline

9. **File Locking**
   - No flock() or fcntl() locking
   - Needed for: Multi-process file access

10. **User/Group Permissions**
    - Files have no ownership
    - Need: UID/GID, permission checks

### Low Priority (Nice to Have)

11. **Sound System**
    - No audio support
    - Would need: VirtIO-sound or USB audio

12. **GUI Toolkit**
    - VirtIO-GPU exists
    - Need: Windowing, widgets

13. **USB Support**
    - No USB stack
    - Would need: VirtIO-USB or XHCI

14. **Hardware Targets**
    - Only QEMU virt supported
    - Would need: Real hardware drivers

---

## Development Roadmap

### Phase 1: Core Completion (1-2 months)

**Goal:** Complete essential missing syscalls

| Task | Priority | Effort |
|------|----------|--------|
| Implement exec() family | High | Large |
| Implement pipe() | High | Medium |
| Implement PTY subsystem | High | Large |
| Complete signal handlers | Medium | Medium |
| Add file locking | Medium | Small |

### Phase 2: Microkernel Migration (2-3 months)

**Goal:** Move drivers to user-space

| Task | Priority | Effort |
|------|----------|--------|
| Complete blkd IRQ handling | High | Medium |
| Complete fsd operations | High | Medium |
| VirtIO-net in netd | Medium | Large |
| Shared memory IPC | Medium | Medium |
| Request batching | Low | Small |

### Phase 3: SMP & Performance (2-3 months)

**Goal:** Efficient multicore operation

| Task | Priority | Effort |
|------|----------|--------|
| Per-CPU run queues | High | Large |
| Load balancing | Medium | Medium |
| Spinlock optimization | Medium | Small |
| Per-CPU caches | Low | Medium |
| Lockless algorithms | Low | Large |

### Phase 4: Networking & Security (1-2 months)

**Goal:** Production networking

| Task | Priority | Effort |
|------|----------|--------|
| Complete IPv6 | Medium | Large |
| TLS session resumption | Medium | Small |
| ECDSA certificates | Low | Medium |
| TLS server mode | Low | Large |
| SSH server | Low | Large |

### Phase 5: User Experience (2-3 months)

**Goal:** Usable system

| Task | Priority | Effort |
|------|----------|--------|
| Shell scripting | High | Large |
| Dynamic linking | Medium | Large |
| User/group permissions | Medium | Medium |
| Package manager | Low | Large |
| GUI toolkit | Low | Very Large |

---

## Architecture Decisions

### Confirmed Directions

1. **Capability-Based Security**
   - All resources accessed via handles
   - Rights can only be reduced, not expanded
   - Enables fine-grained access control

2. **Microkernel Design**
   - Minimal kernel (scheduling, memory, IPC)
   - Drivers in user-space (in progress)
   - Better fault isolation

3. **Amiga-Inspired UX**
   - Logical device assigns (SYS:, C:, etc.)
   - Return codes (OK, WARN, ERROR, FAIL)
   - Interactive shell commands

4. **POSIX-ish libc**
   - Familiar API for applications
   - Not strict POSIX compliance
   - Freestanding implementation

### Open Questions

1. **Networking Location**
   - Currently in kernel for performance
   - Could move to user-space with shared memory
   - Trade-off: IPC overhead vs. isolation

2. **Filesystem Architecture**
   - VFS + single FS type (ViperFS)
   - Could support multiple FS types
   - Need: Mount points, FS plugins

3. **GUI Direction**
   - Wayland-like compositor?
   - Custom toolkit?
   - No clear decision yet

---

## Key Metrics

### Code Size
- Kernel: ~25,000 lines
- Networking: ~16,500 lines
- Crypto: ~6,000 lines
- Filesystem: ~6,400 lines
- User-space: ~35,000 lines
- **Total: ~100,000 lines**

### Binary Sizes (Approximate)
- kernel8.elf: ~800KB
- vinit.elf: ~120KB
- ssh.elf: ~170KB
- sftp.elf: ~190KB
- libviperssh.a: ~250KB
- libviperlibc.a: ~100KB

### Performance (QEMU, Single Core)
- Boot to shell: ~500ms
- HTTPS fetch: ~1-2s (TLS handshake)
- File read (4KB): ~50μs
- Context switch: ~10μs

---

## Contributing

### Quick Start
```bash
cd os
./build_viper.sh           # Build and run (graphics)
./build_viper.sh --serial  # Build and run (serial only)
./build_viper.sh --debug   # Build with GDB debugging
./build_viper.sh --test    # Run test suite
```

### Code Style
- C++17 for kernel
- C11 for libc and user programs
- 4-space indentation
- Clang-format with project settings

### Documentation
- Keep status docs updated when adding features
- Add doxygen comments to headers
- Update CHANGELOG for releases

---

## Conclusion

ViperOS is a functional proof-of-concept operating system demonstrating capability-based security, microkernel architecture, and modern kernel design on AArch64. While not production-ready, it provides a solid foundation for further development toward a usable system.

The most impactful next steps are:
1. Complete exec/pipe/PTY for shell functionality
2. Finish microkernel driver migration
3. Improve SMP scheduling
4. Add shell scripting support

With these additions, ViperOS could become a viable platform for embedded systems, educational use, or specialized applications.
