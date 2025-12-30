# ViperOS Documentation Index

Welcome to the ViperOS documentation.

---

## Getting Started

| Document | Description |
|----------|-------------|
| [Main README](../README.md) | Build instructions and quickstart |
| [Shell Commands](shell-commands.md) | Complete command reference |
| [Syscall Reference](syscalls.md) | System call API documentation |

---

## Implementation Status

Detailed documentation of the current implementation:

| Document | Description |
|----------|-------------|
| [Overview](status/00-overview.md) | Architecture diagram and statistics |
| [Architecture](status/01-architecture.md) | AArch64 boot, MMU, GIC, timer |
| [Memory Management](status/02-memory-management.md) | PMM, VMM, slab, buddy, COW |
| [Console](status/03-console.md) | Serial and graphics console |
| [Drivers](status/04-drivers.md) | VirtIO and device drivers |
| [Filesystem](status/05-filesystem.md) | VFS and ViperFS |
| [IPC](status/06-ipc.md) | Channels and poll sets |
| [Networking](status/07-networking.md) | TCP/IP, TLS, DNS, HTTP |
| [Scheduler](status/08-scheduler.md) | Tasks and scheduling |
| [Process Model](status/09-viper-process.md) | Processes and capabilities |
| [User Space](status/10-userspace.md) | libc and applications |
| [Tools](status/11-tools.md) | Build tools |

---

## Specifications

| Document | Description |
|----------|-------------|
| [ARM64 Specification](spec/ViperOS_ARM64_Spec.md) | Technical specification |
| [v0.2.0 Specification](spec/ViperOS_v0.2.0_Specification.md) | v0.2.0 implementation plan |
