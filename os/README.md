# ViperOS

**ViperOS** is a capability-based operating system for AArch64 (ARM64), designed to explore microkernel architecture, capability-based security, and modern OS concepts. It runs on QEMU's `virt` machine and features a complete TCP/IP stack with TLS 1.3, a crash-consistent journaling filesystem, and a retro-style interactive shell.

> **Status:** Functional and actively developed. Suitable for experimentation and learning.
>
> **Microkernel status:** User-space servers operational (`netd`, `fsd`, `blkd`, `consoled`, `inputd`). Kernel retains fallback implementations for boot and development. Total: ~109,000 SLOC (kernel ~49K, user ~60K).

---

## Download

Clone the repository:

```bash
git clone https://github.com/splanck/viper.git
cd viper/os
```

---

## Quickstart

Build and run ViperOS:

```bash
# Build and launch with graphics
./scripts/build_viper.sh

# Or run in serial-only mode (no graphics window)
./scripts/build_viper.sh --serial

# Build + run tests, but don't launch QEMU
./scripts/build_viper.sh --test --no-run
```

The build script automatically:
- Configures CMake with the Clang cross-compiler
- Builds the kernel and all user programs
- Creates a ViperFS disk image
- Launches QEMU

If the microkernel server ELFs are present, the script also provisions dedicated devices (a separate `microkernel.img` and a second virtio-net device) so the user-space servers can claim them without fighting the kernel drivers during bring-up.

**Requirements:**
- CMake 3.20+
- Clang (Apple Clang on macOS, clang++ on Linux)
- AArch64 cross-linker (`brew install aarch64-elf-binutils` on macOS)
- QEMU with AArch64 support (`brew install qemu` on macOS)

---

## What is ViperOS?

ViperOS is a research operating system exploring:

| Concept | Implementation |
|---------|----------------|
| **Capability-Based Security** | Handle-based access control with rights derivation and revocation |
| **Microkernel Design** | Minimal kernel with user-space servers (netd, fsd, blkd, consoled, inputd) |
| **Modern Memory Management** | Demand paging, copy-on-write, shared memory, buddy allocator |
| **Full Network Stack** | TCP/IP, TLS 1.3, DNS, HTTP, SSH-2/SFTP (user-space) |
| **Crash-Consistent Storage** | Write-ahead journaling filesystem (user-space fsd) |

### Why ViperOS?

- **Educational**: Clear, readable implementation of OS concepts
- **Complete**: Full stack from boot to TLS-secured web requests
- **Retro Design**: Logical assigns, structured return codes, readable commands
- **Modern C++**: Clean C++20 codebase with minimal dependencies

---

## Features

### Kernel (~49,000 SLOC)

| Component | Features |
|-----------|----------|
| **Architecture** | AArch64 boot, 4-level MMU, GICv2/v3, ARM timer, exception handling |
| **Memory** | PMM bitmap, buddy allocator, slab allocator, demand paging, COW, shared memory |
| **Scheduler** | 8 priority queues, 5-20ms time slices, wait queues, signals, SMP support |
| **IPC** | Synchronous channels (64 entries, 256-byte messages), poll sets, capability transfer |
| **Filesystem** | VFS layer, ViperFS with journal, block/inode caches (kernel mode) |
| **Drivers** | VirtIO (block, network, GPU, RNG, input), PL011 UART, ramfb |
| **Console** | Serial UART + graphics console with ANSI escape codes |

### User Space (~60,000 SLOC)

| Component | Features |
|-----------|----------|
| **libc** | POSIX-compatible C library (stdio, string, stdlib, unistd, socket, poll) |
| **Servers** | netd (TCP/IP), fsd (filesystem), blkd (block), consoled, inputd |
| **libtls** | TLS 1.3 client with X.509 certificate verification |
| **libssh** | SSH-2 client with SFTP, Ed25519/RSA authentication |
| **libhttp** | HTTP/1.1 client library |
| **vinit Shell** | Interactive shell with line editing, history, tab completion |
| **User Programs** | hello, fsinfo, sysinfo, netstat, ping, edit, devices, ssh, fetch |

---

## Shell Commands

The ViperOS shell includes these commands:

| Category | Commands |
|----------|----------|
| **Files** | `Dir`, `List`, `Type`, `Copy`, `Delete`, `MakeDir`, `Rename` |
| **Navigation** | `chdir`, `cwd`, `Path`, `Assign` |
| **Programs** | `Run <program>` |
| **System** | `Version`, `Uptime`, `Avail`, `Status`, `Caps`, `Date`, `Time` |
| **Network** | `Fetch <url>` (HTTP/HTTPS), `ssh <user@host>` (SSH-2) |
| **Utility** | `Echo`, `Cls`, `History`, `Why`, `Help` |
| **Session** | `EndShell` |

**Line Editing:**
- Arrow keys for cursor movement and history
- Tab for command completion
- Ctrl+U to clear line, Ctrl+K to kill to end
- Home/End for line navigation

**Return Codes:** OK (0), WARN (5), ERROR (10), FAIL (20)

---

## Architecture

```
┌─────────────────────────────────────────────────────────────────┐
│                        User Space (EL0)                          │
│  ┌───────────────────────────────────────────────────────────┐  │
│  │  vinit - Interactive Shell                                 │  │
│  │  hello, fsinfo, sysinfo, netstat, ping, edit, ...         │  │
│  └───────────────────────────────────────────────────────────┘  │
│  ┌───────────────────────────────────────────────────────────┐  │
│  │  libc + C++ Runtime                                        │  │
│  │  Syscall wrappers, memory allocator, string functions      │  │
│  └───────────────────────────────────────────────────────────┘  │
│  ┌───────────────────────────────────────────────────────────┐  │
│  │  Microkernel Servers (bring-up)                            │  │
│  │  blkd (VirtIO-blk), fsd (ViperFS), netd (VirtIO-net)       │  │
│  └───────────────────────────────────────────────────────────┘  │
└─────────────────────────────┬───────────────────────────────────┘
                              │ SVC #0 (Syscalls)
┌─────────────────────────────┴───────────────────────────────────┐
│                        Kernel (EL1)                              │
│  ┌─────────────────────────────────────────────────────────────┐│
│  │ Viper Process Model                                         ││
│  │ • Per-process address spaces (TTBR0 + ASID)                ││
│  │ • Capability tables with rights enforcement                 ││
│  │ • VMA tracking for demand paging                           ││
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
│  ARM Cortex-A72  |  128MB RAM  |  GICv2  |  VirtIO-MMIO        │
└─────────────────────────────────────────────────────────────────┘
```

---

## Project Structure

```
os/
├── kernel/              # Kernel source code
│   ├── arch/aarch64/    # Architecture-specific (boot, MMU, GIC, exceptions)
│   ├── console/         # Serial and graphics console
│   ├── drivers/         # VirtIO, fw_cfg, ramfb drivers
│   ├── fs/              # VFS and ViperFS implementation
│   ├── ipc/             # Channels and poll sets
│   ├── lib/             # Kernel utilities (spinlock, etc.)
│   ├── mm/              # Memory management (PMM, VMM, heap, slab)
│   ├── net/             # TCP/IP stack, TLS, DNS
│   ├── sched/           # Scheduler, tasks, wait queues
│   ├── syscall/         # System call handlers
│   └── viper/           # Process model, capabilities
├── user/                # User space (~60,000 SLOC)
│   ├── vinit/           # Shell and commands
│   ├── libc/            # C library implementation
│   ├── servers/         # Microkernel servers
│   │   ├── netd/        # Network server (TCP/IP stack)
│   │   ├── fsd/         # Filesystem server (ViperFS)
│   │   ├── blkd/        # Block device server
│   │   ├── consoled/    # Console output server
│   │   └── inputd/      # Keyboard/mouse input server
│   ├── libtls/          # TLS 1.3 client library
│   ├── libssh/          # SSH-2 + SFTP client library
│   ├── libhttp/         # HTTP/1.1 client library
│   ├── libnetclient/    # IPC client for netd
│   ├── libvirtio/       # User-space VirtIO driver library
│   ├── edit/            # Text editor
│   ├── hello/           # Hello world program
│   └── ...
├── vboot/               # Bootloader
├── tools/               # Build tools (mkfs.viperfs, etc.)
├── scripts/             # Build and run scripts
├── cmake/               # CMake toolchain files
└── docs/                # Documentation
```

---

## Building

### Requirements

| Tool | Version | Notes |
|------|---------|-------|
| CMake | 3.20+ | Build system |
| Clang | 15+ | Cross-compiler (or Apple Clang) |
| aarch64-elf-binutils | Any | Cross-linker and tools |
| QEMU | 7.0+ | With `qemu-system-aarch64` |

### macOS Setup

```bash
brew install llvm qemu aarch64-elf-binutils cmake
```

### Linux Setup

```bash
# Ubuntu/Debian
sudo apt install clang lld qemu-system-arm cmake
# Cross-linker may need to be built from source or use gcc-aarch64-linux-gnu
```

### Build Commands

```bash
# Quick build and run
./scripts/build_viper.sh

# Build only (no QEMU)
cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE=cmake/aarch64-clang-toolchain.cmake
cmake --build build -j

# Run with options
./scripts/build_viper.sh --serial      # No graphics window
./scripts/build_viper.sh --debug       # Wait for GDB on port 1234
./scripts/build_viper.sh --no-net      # Disable networking
./scripts/build_viper.sh --memory 256M # Set memory size
```

---

## Running

### Graphics Mode (Default)

A QEMU window opens with the ViperOS graphical console. The terminal shows serial output.

### Serial Mode

```bash
./scripts/build_viper.sh --serial
```

All output goes to the terminal. Exit with Ctrl+A, then X.

### Debugging

```bash
./scripts/build_viper.sh --debug
```

QEMU waits for GDB connection:

```bash
aarch64-elf-gdb build/kernel.sys -ex 'target remote :1234'
```

---

## Demo: HTTPS Fetch

ViperOS includes a complete TLS 1.3 implementation. From the shell:

```
SYS:> Fetch https://example.com
Resolving example.com...
Connecting to 93.184.215.14:443 (HTTPS)...
Connected! Starting TLS handshake...
TLS handshake complete. Sending request...
Request sent, receiving response...

HTTP/1.0 200 OK
Content-Type: text/html; charset=UTF-8
...

[Received 1256 bytes, encrypted]
```

---

## Documentation

| Document | Description |
|----------|-------------|
| [docs/status/00-overview.md](docs/status/00-overview.md) | Implementation status and statistics |
| [docs/status/01-architecture.md](docs/status/01-architecture.md) | AArch64 boot, MMU, GIC, timer |
| [docs/status/02-memory-management.md](docs/status/02-memory-management.md) | PMM, VMM, slab, buddy, COW |
| [docs/status/03-console.md](docs/status/03-console.md) | Serial and graphics console |
| [docs/status/04-drivers.md](docs/status/04-drivers.md) | VirtIO and other drivers |
| [docs/status/05-filesystem.md](docs/status/05-filesystem.md) | VFS and ViperFS |
| [docs/status/06-ipc.md](docs/status/06-ipc.md) | Channels and poll sets |
| [docs/status/07-networking.md](docs/status/07-networking.md) | TCP/IP, TLS, DNS, HTTP |
| [docs/status/08-scheduler.md](docs/status/08-scheduler.md) | Tasks and scheduling |
| [docs/status/09-viper-process.md](docs/status/09-viper-process.md) | Process model and capabilities |
| [docs/status/10-userspace.md](docs/status/10-userspace.md) | User space and libc |
| [docs/status/11-tools.md](docs/status/11-tools.md) | Build tools |
| [docs/shell-commands.md](docs/shell-commands.md) | Shell command reference |
| [docs/syscalls.md](docs/syscalls.md) | System call reference |

---

## Key Differences from Unix

ViperOS uses a different design philosophy than Unix:

| Concept | Unix | ViperOS |
|---------|------|---------|
| **Device Names** | `/dev/sda` | `SYS:`, `C:` (logical assigns) |
| **Path Separator** | `/` | `/` (with optional assign prefixes) |
| **Return Codes** | 0 = success | 0=OK, 5=WARN, 10=ERROR, 20=FAIL |
| **Commands** | `ls`, `cat`, `rm` | `Dir`, `Type`, `Delete` |
| **Process Model** | Fork/exec | Spawn with capabilities |

---

## Syscalls

ViperOS provides ~90 system calls organized by category:

| Category | Syscalls |
|----------|----------|
| **Task** | yield, exit, sleep, spawn, wait, join, list, priority |
| **Process** | fork, getpid, getppid, getpgid, setpgid, getsid, setsid, waitpid |
| **Memory** | alloc, retain, release, get_len, set_len, brk |
| **IPC** | channel_create, send, recv, close, poll |
| **Shared Memory** | shm_create, shm_map, shm_unmap, shm_close |
| **Timer** | create, set, cancel, close |
| **Filesystem** | open, create, read, write, seek, close, stat, readdir, rename, delete, sync |
| **Network** | socket, bind, connect, listen, accept, send, recv, close |
| **Device** | map_device, irq_register, irq_wait, irq_ack, dma_alloc, virt_to_phys |
| **Capability** | derive, revoke, query, list |
| **Assign** | set, remove, get, list, resolve |
| **Debug** | print, putchar, getchar, uptime |

See [docs/syscalls.md](docs/syscalls.md) for detailed documentation.

---

## Contributing

ViperOS is in active development. We welcome:

- Bug reports and issues
- Documentation improvements
- Small fixes and enhancements
- Feedback and suggestions

---

## License

ViperOS is licensed under the **GNU General Public License v3.0** (GPL-3.0-only).

See [LICENSE](../LICENSE) for the full text.
