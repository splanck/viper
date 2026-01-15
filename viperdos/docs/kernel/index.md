# Kernel Components (Narrative Index)

These pages are meant to be read like a guided tour of the running system: “what happens”, “who calls whom”, and “what
invariants each subsystem relies on”.

The canonical “timeline” of initialization lives in `kernel/main.cpp` (`kernel_main()`); most pages link back to the
relevant source files so you can follow along in code.

## Boot and platform bring-up

- [Boot and Early Initialization](boot.md)
- [Console, Graphics Console, and Logging](console.md)

## Core kernel services

- [Memory Management (PMM, VMM, Kernel Heap)](memory.md)
- [Tasks and Scheduling](scheduling.md)
- [Viper Processes and Address Spaces](viper_processes.md)
- [Capabilities and Kernel Objects](capabilities.md)
- [IPC: Channels, Poll, and Timers](ipc.md)
- [Syscalls: ABI and Dispatch](syscalls.md)

## I/O stacks

- [Filesystems: Block Cache, ViperFS, and VFS](filesystems.md)
- [Program Loading: ELF Loader](loader.md)
- [Drivers: Virtio-MMIO and Devices](drivers_virtio.md)
- [Networking Stack (Ethernet → TCP, DNS, HTTP, TLS)](networking.md)
- [Assigns (Logical Names Like `SYS:` and `C:`)](assigns.md)

