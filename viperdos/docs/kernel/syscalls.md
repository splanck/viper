# Syscalls: ABI and Dispatch

Syscalls are the boundary where user space asks the kernel to do something. In ViperDOS, syscalls arrive via the AArch64
`svc #0` instruction and are dispatched from the EL1 synchronous exception handler.

This page describes the syscall ABI, how dispatch works, and how the syscall layer ties together subsystems like
scheduling, IPC, VFS, networking, and assigns.

## ABI: how arguments and results are passed

The syscall ABI is documented directly in `kernel/syscall/dispatch.cpp`:

**Inputs**

- `x8`: syscall number (`SYS_*`)
- `x0`–`x5`: up to 6 arguments

**Outputs**

- `x0`: `VError` (0 = success, negative = error)
- `x1`–`x3`: result values (when a syscall returns data)

This convention standardizes error checking (`if (x0 != 0)`) and gives syscalls a consistent way to return multiple
values without pointer-heavy user buffers.

Key files:

- `kernel/include/syscall_nums.hpp`
- `kernel/include/syscall.hpp`
- `kernel/syscall/dispatch.hpp`
- `kernel/syscall/dispatch.cpp`
- `kernel/include/error.hpp`

## Dispatch path: from exception vectors to `syscall::dispatch`

### 1) Exception entry

The AArch64 exception vector table (`kernel/arch/aarch64/exceptions.S`) saves register state into an
`exceptions::ExceptionFrame` and calls C++ handlers in `kernel/arch/aarch64/exceptions.cpp`.

### 2) Detecting syscalls

In `handle_sync_exception(...)` (EL1) and `handle_el0_sync(...)` (EL0), the kernel checks `ESR_EL1.EC` for `SVC_A64`. If
it’s an SVC, it routes to `syscall::dispatch(frame)`.

### 3) Running the syscall

`syscall::dispatch` reads `x8` to pick a syscall handler and writes results back into `frame->x[0..3]`.

This “frame-in, frame-out” model is convenient: the exception return (`eret`) naturally restores registers with the
syscall results, without special case code.

Key files:

- `kernel/arch/aarch64/exceptions.S`
- `kernel/arch/aarch64/exceptions.cpp`
- `kernel/syscall/dispatch.cpp`

## What syscalls do today

The dispatcher wires together many subsystems. Some key categories:

### Tasking and time

- yield / exit / current task id
- sleep and time-now (via `poll` + timer ticks)

Backed by:

- `kernel/sched/*`
- `kernel/ipc/poll.*`
- `kernel/arch/aarch64/timer.*`

### IPC

- channel create/send/recv/close
- poll wait (the primary blocking syscall in the current model)

Backed by:

- `kernel/ipc/channel.*`
- `kernel/ipc/poll.*`
- `kernel/ipc/pollset.*`

### Filesystem and assigns

- VFS operations (open/read/write/seek/stat, directory ops)
- assign set/unset/resolve (v0.2.0)

Backed by:

- `kernel/fs/vfs/*`
- `kernel/fs/viperfs/*`
- `kernel/assign/*`
- `kernel/kobj/*` (file/dir objects used by assigns)

### Networking

Networking is implemented in the kernel with `VIPER_KERNEL_ENABLE_NET=1`:

- TCP/UDP sockets via kernel syscalls (SYS_SOCKET_*, SYS_TLS_*)
- DNS resolution in kernel
- TLS 1.3 in kernel (VIPER_KERNEL_ENABLE_TLS=1)

Backed by:

- `kernel/net/*` (TCP/IP stack)
- `kernel/net/tls/*` (TLS 1.3)

### Input and console interaction

Input and graphics console syscalls exist primarily for bring-up and UX integration.

Backed by:

- `user/servers/consoled/*` (GUI terminal emulator)
- `user/servers/displayd/*` (window manager)
- `kernel/console/gcon.*` (kernel graphics console)
- `kernel/drivers/virtio/input.*` (keyboard/mouse drivers)

### Device Access

Syscalls for user-space display servers to access hardware (used by consoled, displayd):

| Syscall          | Number | Description                     |
|------------------|--------|---------------------------------|
| `map_device`     | 0x100  | Map device MMIO into user space |
| `irq_register`   | 0x101  | Register for IRQ delivery       |
| `irq_wait`       | 0x102  | Wait for IRQ                    |
| `irq_ack`        | 0x103  | Acknowledge IRQ                 |
| `dma_alloc`      | 0x104  | Allocate DMA buffer             |
| `dma_free`       | 0x105  | Free DMA buffer                 |
| `virt_to_phys`   | 0x106  | Translate VA to PA              |
| `device_enum`    | 0x107  | Enumerate devices               |
| `irq_unregister` | 0x108  | Unregister IRQ                  |

These syscalls require `CAP_DEVICE_ACCESS` capability.

Backed by:

- `kernel/syscall/device.cpp`

### Shared Memory (IPC)

Syscalls for shared memory between processes (used by display servers for framebuffers):

| Syscall      | Number | Description                  |
|--------------|--------|------------------------------|
| `shm_create` | 0x109  | Create shared memory region  |
| `shm_map`    | 0x10A  | Map SHM into address space   |
| `shm_unmap`  | 0x10B  | Unmap SHM from address space |
| `shm_close`  | 0x10C  | Close SHM handle             |

Backed by:

- `kernel/syscall/shm.cpp`
- `kernel/mm/shm.*`

### Process Management

Process lifecycle and session management:

| Syscall   | Number | Description            |
|-----------|--------|------------------------|
| `fork`    | 0x0B   | Fork process with COW  |
| `getpid`  | 0xA0   | Get process ID         |
| `getppid` | 0xA1   | Get parent process ID  |
| `getpgid` | 0xA2   | Get process group ID   |
| `setpgid` | 0xA3   | Set process group ID   |
| `getsid`  | 0xA4   | Get session ID         |
| `setsid`  | 0xA5   | Create new session     |
| `waitpid` | 0xA6   | Wait for child process |

Backed by:

- `kernel/viper/viper.*`
- `kernel/syscall/process.cpp`

### Capability Management

| Syscall      | Number | Description                       |
|--------------|--------|-----------------------------------|
| `cap_derive` | 0x70   | Derive with reduced rights        |
| `cap_revoke` | 0x71   | Revoke capability and derivatives |
| `cap_query`  | 0x72   | Query capability info             |
| `cap_list`   | 0x73   | List all capabilities             |

Backed by:

- `kernel/cap/*`
- `kernel/syscall/cap.cpp`

## Error model: `VError`

Most syscalls return a negative error code from `kernel/include/error.hpp` on failure.

Some older subsystems (notably parts of VFS) still return `-1` internally and rely on the syscall layer to translate
into richer errors later. This is expected to tighten up over time.

## Current limitations and next steps

- User pointers are not universally validated yet (some operations assume trusted callers during bring-up).
- Some EL0 exception paths are still “panic-y” (fatal faults may halt rather than cleanly terminating a process/task).
- As capabilities become more central, expect syscalls to shift toward “operate on handles” rather than “operate on
  global IDs / strings”.

