# ViperOS Implementation Report

**Date:** December 25, 2025
**Version:** v0.1.0
**Architecture:** AArch64 exclusively
**Platform:** QEMU virt machine

---

## Executive Summary

ViperOS is a capability-based microkernel operating system designed to natively host the Viper Platform. This report
documents the current implementation state, covering six development phases from graphics boot through networking.

**Current Status:** Phase 6 (Networking) substantially complete. The system boots to a functional environment with task
scheduling, IPC, filesystem, input handling, and a working TCP/IP network stack capable of HTTP communication.

---

## Table of Contents

1. [Implementation Status Overview](#1-implementation-status-overview)
2. [Detailed Component Analysis](#2-detailed-component-analysis)
3. [Current Limitations](#3-current-limitations)
4. [Recommended Next Steps](#4-recommended-next-steps)
5. [Architecture Summary](#5-architecture-summary)

---

## 1. Implementation Status Overview

### Phase Completion Summary

| Phase   | Description        | Status      | Completeness |
|---------|--------------------|-------------|--------------|
| Phase 1 | Graphics Boot      | Complete    | 95%          |
| Phase 2 | Multitasking       | Complete    | 90%          |
| Phase 3 | User Space         | Partial     | 60%          |
| Phase 4 | Filesystem & Shell | Partial     | 70%          |
| Phase 5 | Input & Polish     | Partial     | 50%          |
| Phase 6 | Networking         | Substantial | 85%          |

### Component Status

| Component                | Status   | Notes                                   |
|--------------------------|----------|-----------------------------------------|
| Build Infrastructure     | Complete | CMake-based, cross-compilation working  |
| UEFI Bootloader (vboot)  | Deferred | Using QEMU `-kernel` instead            |
| Kernel Entry             | Complete | AArch64 assembly boot, stack setup      |
| Serial Console           | Complete | PL011 UART driver                       |
| Graphics Console         | Complete | 8x16 bitmap font, ViperOS colors        |
| Physical Memory Manager  | Complete | Bitmap-based allocator                  |
| Virtual Memory Manager   | Complete | 4-level page tables                     |
| Kernel Heap              | Complete | kmalloc/kfree with slab hints           |
| Exception Handling       | Complete | Full vector table, panic display        |
| GIC Interrupt Controller | Complete | GICv2 support                           |
| Timer                    | Complete | ARM architected timer, 1000Hz           |
| Task Scheduler           | Complete | Round-robin, preemptive                 |
| IPC Channels             | Complete | Non-blocking message passing            |
| Poll System              | Complete | PollWait with timeouts                  |
| Capability Tables        | Partial  | Basic structure, limited enforcement    |
| Address Spaces           | Partial  | Per-process TTBR0 switching             |
| virtio-blk Driver        | Complete | Block device I/O                        |
| ViperFS                  | Complete | On-disk filesystem                      |
| VFS Layer                | Complete | File syscalls (open, read, write, etc.) |
| virtio-input Driver      | Complete | Keyboard and mouse support              |
| Input Subsystem          | Complete | Keycode translation, modifiers          |
| virtio-net Driver        | Complete | Ethernet frame I/O                      |
| Ethernet Layer           | Complete | Frame parsing and construction          |
| ARP                      | Complete | Address resolution                      |
| IPv4                     | Complete | Packet routing                          |
| ICMP                     | Complete | Ping support                            |
| UDP                      | Complete | Datagram sockets                        |
| TCP                      | Complete | Connection-oriented sockets             |
| DNS Resolver             | Complete | Domain name resolution                  |
| HTTP Client              | Complete | Plain HTTP only                         |
| ELF Loader               | Complete | Loads user binaries                     |
| Syscall Dispatch         | Complete | 25+ syscalls implemented                |

---

## 2. Detailed Component Analysis

### 2.1 Kernel Core (45 C++ source files)

**Location:** `kernel/`

The kernel is implemented in C++ with assembly for low-level operations. Key subsystems:

#### Architecture Support (`kernel/arch/aarch64/`)

- `boot.S` - Entry point, stack setup, BSS clearing
- `exceptions.S` / `exceptions.cpp` - Exception vectors, syndrome parsing
- `gic.cpp` - GICv2 interrupt controller driver
- `timer.cpp` - ARM architected timer (1000Hz ticks)
- `mmu.cpp` - Page table management

#### Console (`kernel/console/`)

- `serial.cpp` - PL011 UART driver
- `gcon.cpp` - Graphics console with framebuffer text rendering
- `font.cpp` - Baked-in 8x16 bitmap font

#### Memory Management (`kernel/mm/`)

- `pmm.cpp` - Physical memory manager (bitmap allocator)
- `vmm.cpp` - Virtual memory manager (4-level page tables)
- `kheap.cpp` - Kernel heap allocator

#### Scheduler (`kernel/sched/`)

- `task.cpp` - Task creation, context switching
- `scheduler.cpp` - Round-robin scheduling with preemption

#### IPC (`kernel/ipc/`)

- `channel.cpp` - Non-blocking message channels
- `poll.cpp` - Event polling with timeouts

#### Filesystem (`kernel/fs/`)

- `vfs/vfs.cpp` - Virtual filesystem layer
- `viperfs/viperfs.cpp` - ViperFS on-disk filesystem
- `cache.cpp` - Block cache

#### Networking (`kernel/net/`)

- `network.cpp` - Network stack coordination
- `netif.cpp` - Network interface abstraction
- `eth/ethernet.cpp` - Ethernet frame handling
- `eth/arp.cpp` - ARP implementation
- `ip/ipv4.cpp` - IPv4 packet handling
- `ip/icmp.cpp` - ICMP (ping) support
- `ip/udp.cpp` - UDP sockets
- `ip/tcp.cpp` - TCP sockets with state machine
- `dns/dns.cpp` - DNS resolver
- `http/http.cpp` - HTTP/1.1 client

#### Drivers (`kernel/drivers/`)

- `virtio/virtio.cpp` - Base virtio-mmio support
- `virtio/virtqueue.cpp` - Virtqueue management
- `virtio/blk.cpp` - Block device driver
- `virtio/net.cpp` - Network device driver
- `virtio/input.cpp` - Input device driver
- `ramfb.cpp` - QEMU ramfb framebuffer
- `fwcfg.cpp` - QEMU fw_cfg interface

#### Input (`kernel/input/`)

- `input.cpp` - Input event processing
- `keycodes.hpp` - Linux evdev keycode definitions

#### Syscalls (`kernel/syscall/`)

- `dispatch.cpp` - Syscall dispatch handler

### 2.2 Implemented Syscalls

The syscall dispatcher (`kernel/syscall/dispatch.cpp:129`) implements:

**Task Management:**

- `SYS_TASK_YIELD` - Yield CPU to other tasks
- `SYS_TASK_EXIT` - Terminate current task
- `SYS_TASK_CURRENT` - Get current task ID

**IPC:**

- `SYS_CHANNEL_CREATE` - Create IPC channel
- `SYS_CHANNEL_SEND` - Send message
- `SYS_CHANNEL_RECV` - Receive message
- `SYS_CHANNEL_CLOSE` - Close channel

**Time:**

- `SYS_TIME_NOW` - Get current time (ms)
- `SYS_SLEEP` - Sleep for milliseconds

**Filesystem:**

- `SYS_OPEN` - Open file
- `SYS_CLOSE` - Close file
- `SYS_READ` - Read from file
- `SYS_WRITE` - Write to file
- `SYS_LSEEK` - Seek in file
- `SYS_STAT` - Get file info by path
- `SYS_FSTAT` - Get file info by fd

**Networking:**

- `SYS_SOCKET_CREATE` - Create TCP socket
- `SYS_SOCKET_CONNECT` - Connect to remote host
- `SYS_SOCKET_SEND` - Send data
- `SYS_SOCKET_RECV` - Receive data
- `SYS_SOCKET_CLOSE` - Close socket
- `SYS_DNS_RESOLVE` - Resolve hostname to IP

**Console:**

- `SYS_DEBUG_PRINT` - Print debug string
- `SYS_GETCHAR` - Get character from input
- `SYS_PUTCHAR` - Output character
- `SYS_UPTIME` - Get system uptime

### 2.3 User Space

**Location:** `user/vinit/`

Currently only `vinit.cpp` exists - the init process that runs after kernel initialization. The full shell (`vsh`) and
utilities are not yet implemented as separate programs.

### 2.4 Build System

**Files:**

- `CMakeLists.txt` - Top-level CMake configuration
- `cmake/` - Toolchain files
- `build_viper.sh` - Build and run script
- `scripts/` - Helper scripts

The build uses CMake with the `aarch64-linux-gnu-g++` cross-compiler, outputting to `build/`.

---

## 3. Current Limitations

### 3.1 Critical Limitations

| Limitation              | Impact                                               | Workaround                                |
|-------------------------|------------------------------------------------------|-------------------------------------------|
| **No TLS/HTTPS**        | Cannot access HTTPS-only websites (google.com, etc.) | Use HTTP-only sites or implement TLS      |
| **No vboot bootloader** | Cannot boot via UEFI, requires QEMU `-kernel`        | Continue using QEMU direct kernel loading |
| **Single-core only**    | Limited parallelism                                  | Designed for SMP, implementation deferred |

### 3.2 Functional Limitations

**Networking:**

- HTTP only, no HTTPS/TLS support
- Websites that redirect HTTP to HTTPS will fail
- No persistent connections
- Basic TCP - no advanced options

**Filesystem:**

- ViperFS working but no journaling (crash recovery limited)
- No symbolic links
- No file permissions enforcement
- RAM-based testing, persistence depends on disk.img

**User Space:**

- Only vinit runs, no interactive shell binary
- No loadable `.vpr` executables yet
- No Viper BASIC scripting

**Input:**

- US keyboard layout only
- No keyboard repeat rate configuration
- Mouse events consumed but not processed

**Graphics:**

- Text console only, no GUI
- Single fixed font
- No framebuffer double-buffering

### 3.3 Missing Features (Per Specification)

| Feature                | Status      | Notes                                |
|------------------------|-------------|--------------------------------------|
| vsh (shell)            | Not Started | Interactive shell with assigns       |
| Core utilities         | Not Started | dir, list, copy, delete, etc.        |
| Viper BASIC            | Not Started | Scripting language                   |
| Loadable fonts         | Not Started | .vfont support                       |
| Graphics surfaces      | Not Started | User-space graphics API              |
| Capability enforcement | Partial     | Structure exists, not fully enforced |
| Process spawning       | Partial     | ViperSpawn not fully implemented     |

---

## 4. Recommended Next Steps

### 4.1 Immediate Priorities

#### Priority 1: Interactive Shell (vsh)

The most impactful next step is implementing `vsh` to provide an interactive environment:

1. Create `user/vsh/` directory
2. Implement line editing with cursor movement
3. Add command parsing and execution
4. Implement built-in commands (cd, set, alias)
5. Add command history (up/down arrows work - escape sequences already generated)

**Why:** Users cannot interact with the system beyond vinit's current behavior.

#### Priority 2: Core Utilities

Implement essential shell commands:

1. `dir` / `list` - Directory listing
2. `type` - Display file contents
3. `copy` / `delete` - File manipulation
4. `makedir` - Create directories
5. `echo` - Print text

**Why:** Filesystem exists but no tools to use it interactively.

#### Priority 3: TLS/HTTPS Support

To access modern websites:

1. Implement TLS 1.2/1.3 handshake
2. Add cipher suites (AES-GCM, ChaCha20-Poly1305)
3. Certificate validation (or skip for development)
4. Integrate with TCP sockets

**Why:** Most websites redirect to HTTPS; google.com and others are inaccessible.

### 4.2 Medium-Term Goals

#### Capability System Enforcement

- Enforce CAP_READ/CAP_WRITE on file operations
- Implement capability derivation and revocation
- Add CAP_TRANSFER for IPC

#### Process Spawning

- Complete ViperSpawn syscall
- Load and execute `.vpr` binaries
- Set up child process capabilities

#### Assigns System

- Implement logical device names (SYS:, HOME:, C:)
- Path resolution with assigns
- Persistent assign configuration

### 4.3 Long-Term Vision

Per the specification, the ultimate goals are:

1. **Self-hosting:** ViperIDE and compiler running on ViperOS
2. **Viper Computer:** ViperOS as default OS on ARM hardware
3. **Complete Runtime:** All Viper.* libraries functional

---

## 5. Architecture Summary

### 5.1 Memory Layout

```
User Space (TTBR0_EL1):
0x0000_0000_0000_0000    Null guard (unmapped)
0x0000_0000_0000_1000    User space start
0x0000_0010_0000_0000    Code region
0x0000_0050_0000_0000    LHeap (user-managed)
0x0000_0070_0000_0000    KHeap (kernel-managed)
0x0000_007F_FFFF_F000    Stack top

Kernel Space (TTBR1_EL1):
0xFFFF_0000_0000_0000    HHDM (Higher-Half Direct Map)
0xFFFF_8000_0000_0000    Kernel heap
0xFFFF_FFFF_0000_0000    Kernel image
```

### 5.2 Syscall ABI

```
Invocation:
  X8  = syscall number
  X0-X5 = arguments
  SVC #0

Return:
  X0  = result (error code or value)
```

### 5.3 Key Constants

| Constant        | Value       | Purpose                   |
|-----------------|-------------|---------------------------|
| Timer frequency | 1000 Hz     | Scheduler tick rate       |
| Time slice      | 10 ms       | Task preemption interval  |
| Channel buffer  | 4096 bytes  | IPC message buffer        |
| Max channels    | 256         | Per-system limit          |
| TCP buffer      | 16384 bytes | Per-socket receive buffer |

### 5.4 Color Palette

| Name        | Hex     | Usage          |
|-------------|---------|----------------|
| Viper Green | #00AA44 | Primary text   |
| Dark Brown  | #1A1208 | Background     |
| Yellow      | #FFDD00 | Warnings/panic |
| White       | #EEEEEE | Bright text    |
| Red         | #CC3333 | Errors         |

---

## Appendix A: Source File Inventory

### Kernel Source Files (45 total)

```
kernel/
├── arch/aarch64/
│   ├── boot.S
│   ├── exceptions.S
│   ├── exceptions.cpp
│   ├── gic.cpp
│   ├── mmu.cpp
│   └── timer.cpp
├── cap/
│   └── table.cpp
├── console/
│   ├── font.cpp
│   ├── gcon.cpp
│   └── serial.cpp
├── drivers/
│   ├── fwcfg.cpp
│   ├── ramfb.cpp
│   └── virtio/
│       ├── blk.cpp
│       ├── input.cpp
│       ├── net.cpp
│       ├── virtio.cpp
│       └── virtqueue.cpp
├── fs/
│   ├── cache.cpp
│   ├── vfs/vfs.cpp
│   └── viperfs/viperfs.cpp
├── input/
│   └── input.cpp
├── ipc/
│   ├── channel.cpp
│   └── poll.cpp
├── kobj/
│   ├── blob.cpp
│   └── channel.cpp
├── loader/
│   ├── elf.cpp
│   └── loader.cpp
├── mm/
│   ├── kheap.cpp
│   ├── pmm.cpp
│   └── vmm.cpp
├── net/
│   ├── dns/dns.cpp
│   ├── eth/arp.cpp
│   ├── eth/ethernet.cpp
│   ├── http/http.cpp
│   ├── ip/icmp.cpp
│   ├── ip/ipv4.cpp
│   ├── ip/tcp.cpp
│   ├── ip/udp.cpp
│   ├── netif.cpp
│   └── network.cpp
├── sched/
│   ├── scheduler.cpp
│   └── task.cpp
├── syscall/
│   └── dispatch.cpp
├── viper/
│   ├── address_space.cpp
│   └── viper.cpp
├── crt.cpp
└── main.cpp
```

### User Space (1 file)

```
user/vinit/vinit.cpp
```

---

## Appendix B: Test Commands

### Build and Run

```bash
./build_viper.sh
```

### Run QEMU with Graphics

```bash
qemu-system-aarch64 \
    -machine virt \
    -cpu cortex-a72 \
    -m 512M \
    -kernel build/kernel/kernel.elf \
    -drive file=disk.img,format=raw,if=none,id=hd0 \
    -device virtio-blk-device,drive=hd0 \
    -netdev user,id=net0 \
    -device virtio-net-device,netdev=net0 \
    -device virtio-keyboard-device \
    -device virtio-mouse-device \
    -device ramfb \
    -serial stdio
```

### Debug with GDB

```bash
# Terminal 1
qemu-system-aarch64 ... -s -S

# Terminal 2
aarch64-linux-gnu-gdb build/kernel/kernel.elf
(gdb) target remote :1234
(gdb) continue
```

---

*Report generated by Claude Code*
*ViperOS - "Software should be art."*
