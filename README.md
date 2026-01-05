# Viper

**Viper** is a monorepo containing two related projects exploring language implementation and operating system design.

---

## Projects

### [compiler/](compiler/) — Viper Compiler Toolchain

An IL-first compiler toolchain and virtual machine for exploring intermediate language design, multi-frontend architectures, and interpreter implementation.

| Component | Description |
|-----------|-------------|
| **Frontends** | BASIC, Pascal, and ViperLang compilers |
| **IL** | Strongly typed, SSA-inspired intermediate language |
| **VM** | Bytecode interpreter with pluggable dispatch strategies |
| **Backends** | Native code generators (AArch64, x86-64) |
| **Runtime** | Portable C libraries for I/O, graphics, networking, threads |

**Quickstart:**

```bash
cd compiler
cmake -S . -B build && cmake --build build -j
./build/src/tools/vbasic/vbasic examples/basic/ex1_hello_cond.bas
```

See [compiler/README.md](compiler/README.md) for full documentation.

---

### [os/](os/) — ViperOS

A capability-based microkernel operating system for AArch64 (ARM64), featuring modern OS concepts and retro-style design.

| Component | Description |
|-----------|-------------|
| **Architecture** | AArch64 microkernel with 4-level MMU, GICv2/v3, ARM timer |
| **Memory** | Demand paging, copy-on-write, buddy allocator, slab allocator |
| **Filesystem** | ViperFS with block/inode caches (user-space fsd server) |
| **Networking** | TCP/IP stack with TLS 1.3, DNS, HTTP, SSH (user-space netd server) |
| **Shell** | Interactive shell with line editing |
| **Security** | Capability-based access control with handle derivation |

**Quickstart:**

```bash
cd os
./scripts/build_viper.sh
```

See [os/README.md](os/README.md) for full documentation.

---

## Repository Structure

```
viper/
├── compiler/              # Viper compiler toolchain
│   ├── src/               # Compiler source code
│   │   ├── il/            # Intermediate language
│   │   ├── vm/            # Virtual machine
│   │   ├── frontends/     # BASIC, Pascal, ViperLang
│   │   ├── codegen/       # Native backends
│   │   └── runtime/       # Runtime libraries
│   ├── docs/              # Compiler documentation
│   ├── examples/          # Example programs
│   └── demos/             # Demo applications
│
├── os/                    # ViperOS operating system
│   ├── kernel/            # Microkernel source code
│   │   ├── arch/          # AArch64 architecture
│   │   ├── mm/            # Memory management
│   │   ├── ipc/           # IPC channels and poll
│   │   ├── sched/         # Scheduler
│   │   └── viper/         # Process model
│   ├── user/              # User space (~60,000 SLOC)
│   │   ├── vinit/         # Shell
│   │   ├── libc/          # C library
│   │   ├── servers/       # Microkernel servers
│   │   │   ├── netd/      # Network server (TCP/IP)
│   │   │   ├── fsd/       # Filesystem server
│   │   │   └── blkd/      # Block device server
│   │   ├── libtls/        # TLS 1.3 library
│   │   ├── libssh/        # SSH-2 library
│   │   └── ...            # Utilities
│   ├── docs/              # OS documentation
│   └── scripts/           # Build scripts
│
└── README.md              # This file
```

---

## Requirements

### Compiler Toolchain

- CMake 3.20+
- C++20 compiler (Clang recommended)

### ViperOS

- CMake 3.20+
- Clang cross-compiler
- AArch64 binutils (`aarch64-elf-binutils`)
- QEMU with AArch64 support

**macOS:**

```bash
brew install llvm qemu aarch64-elf-binutils cmake
```

---

## Project Status

| Project | Status |
|---------|--------|
| **Compiler** | Early development. Core language features work. APIs unstable. |
| **ViperOS** | Functional. Kernel complete, user space expanding. |

Both projects are suitable for experimentation and learning but are not production-ready.

---

## License

Both projects are licensed under the **GNU General Public License v3.0** (GPL-3.0-only).

See [LICENSE](LICENSE) for the full text.
