# Syscalls: ABI and Dispatch

Syscalls are the boundary where user space asks the kernel to do something. In ViperOS, syscalls arrive via the AArch64
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

- TCP sockets
- DNS resolution
- TLS support (for HTTPS)

Backed by:

- `kernel/net/*`
- `kernel/drivers/virtio/net.*`

### Input and console interaction

Input and graphics console syscalls exist primarily for bring-up and UX integration; many paths also still use direct
kernel calls.

Backed by:

- `kernel/input/*`
- `kernel/console/gcon.*`
- `kernel/drivers/virtio/input.*`

## Error model: `VError`

Most syscalls return a negative error code from `kernel/include/error.hpp` on failure.

Some older subsystems (notably parts of VFS) still return `-1` internally and rely on the syscall layer to translate
into richer errors later. This is expected to tighten up over time.

## Current limitations and next steps

- User pointers are not universally validated yet (some operations assume trusted callers during bring-up).
- Some EL0 exception paths are still “panic-y” (fatal faults may halt rather than cleanly terminating a process/task).
- As capabilities become more central, expect syscalls to shift toward “operate on handles” rather than “operate on
  global IDs / strings”.

