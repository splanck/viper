# Viper Processes and Address Spaces

In ViperDOS terminology, a “Viper” is a process-like container:

- owns an address space (user mappings + kernel mappings for exception handling)
- owns a capability table (the process’ authority)
- owns one or more tasks/threads (still evolving)

This page explains the relationship between tasks, Vipers, and address spaces, and how user-mode execution is entered.

## The big picture

Think of the layers as:

- **Task** (`kernel/sched/task.*`): “something the scheduler can run”
- **Viper** (`kernel/viper/viper.*`): “a process container with identity/ownership”
- **AddressSpace** (`kernel/viper/address_space.*`): “the page tables and ASID that define what virtual addresses mean
  for this Viper”
- **Capabilities** (`kernel/cap/*`): “the handles and rights that define what this Viper can do”

## Viper subsystem: a fixed-size process table (for now)

The Viper subsystem in `kernel/viper/viper.cpp` uses a fixed-size table of `Viper` structs plus parallel arrays:

- `vipers[MAX_VIPERS]`
- `address_spaces[MAX_VIPERS]`
- `cap_tables[MAX_VIPERS]`

This is a common bring-up pattern: it avoids dynamic allocation complexity and makes process identity stable.

### Creating a Viper

`viper::create(parent, name)`:

1. Allocates a free `Viper` slot.
2. Initializes a fresh `AddressSpace` (`AddressSpace::init()`).
3. Initializes a fresh capability table (`cap::Table::init()`).
4. Links parent/child relationships (tree).
5. Marks the Viper running and inserts it into a global list for iteration/debugging.

The intent is that later: tasks become “threads inside a Viper”, and Viper teardown reaps those tasks.

Key files:

- `kernel/viper/viper.hpp`
- `kernel/viper/viper.cpp`

## Address spaces: ASIDs + translation tables

`kernel/viper/address_space.cpp` implements a minimal AArch64 user address space:

- allocates an **ASID** from a small bitmap (8-bit ASIDs, 0 reserved for kernel)
- allocates a root L0 table from PMM
- installs an L1 table that preserves kernel mappings for exception entry

### Why copy kernel mappings?

When user space triggers an exception (syscall, page fault, IRQ), the CPU must execute kernel code reliably.

The bring-up approach here is:

- the kernel builds a kernel mapping (via `mmu::init()` and TTBR0)
- each user address space includes those kernel mappings in its own tables so exceptions can run without switching
  translation regimes first

This makes “first user mode” easier to get right, at the cost of more page table complexity later.

### Mapping user memory

The `AddressSpace` API provides helpers such as:

- `alloc_map(virt, size, prot)` to allocate physical pages and map them into user VA space
- `translate(virt)` to translate user VA to physical (useful for copying during ELF load)
- `map(virt, phys, size, prot)` for explicit mappings
- `unmap(virt, size)` to remove mappings

These are the primitives the ELF loader uses.

Key files:

- `kernel/viper/address_space.hpp`
- `kernel/viper/address_space.cpp`
- `kernel/arch/aarch64/mmu.*` (kernel mapping base)

## Entering user mode: from kernel task to EL0

ViperDOS reaches user mode through a controlled transition:

1. The kernel loads an ELF into a Viper’s address space (see `kernel/loader/loader.cpp`).
2. It maps a user stack near `viper::layout::USER_STACK_TOP`.
3. It creates a user task (`task::create_user_task(...)`) that remembers:
    - which Viper to run under
    - entry point
    - user stack pointer
4. When that task runs for the first time, it switches to the Viper’s address space and uses
   `enter_user_mode(entry, sp, arg)` to perform the `eret` transition to EL0.

During bring-up there is also an optional “direct user mode” path (bypassing the scheduler), useful for debugging early
user-mode transitions.

Key files:

- `kernel/sched/task.cpp` (`create_user_task`, trampoline)
- `kernel/arch/aarch64/exceptions.S` (mode switch helpers)
- `kernel/arch/aarch64/exceptions.cpp` (EL0 exception handlers)

## How process state is found

Many subsystems need "current process" state, like "which capability table should this syscall use?".

The pattern uses per-CPU tracking for SMP support:

- Each CPU has a `CpuData` structure containing `current_viper`
- `viper::current()` returns the current Viper for the executing CPU
- `viper::set_current(v)` updates the per-CPU current Viper during context switches
- For kernel tasks without an associated Viper, syscalls use explicit capability tables

Key files:

- `kernel/arch/aarch64/cpu.hpp`: `CpuData` with `current_viper` field
- `kernel/viper/viper.cpp`: `current()` and `set_current()` implementations

## Copy-on-Write (COW) fork

ViperDOS supports efficient process forking with copy-on-write semantics:

### Fork implementation

When `fork()` is called:

1. A new Viper is created with a cloned address space
2. Page table entries are copied, but physical pages are shared
3. Both parent and child mappings are marked read-only
4. When either process writes to a shared page, a page fault triggers COW

### COW page fault handling

The page fault handler in `kernel/mm/fault.cpp`:

1. Detects write faults to COW pages (read-only but writable in VMA)
2. Allocates a new physical page
3. Copies the original page content
4. Updates the faulting process's page table with the new writable page
5. If reference count drops to 1, the other process can also make its page writable

Key files:

- `kernel/viper/address_space.cpp`: `clone_cow()` for fork
- `kernel/mm/fault.cpp`: `handle_cow_fault()` implementation

## Process lifecycle: wait/exit/zombie

ViperDOS implements full process lifecycle management:

### Exit

When a process calls `exit(code)`:

1. The process state changes to `Exiting`
2. All tasks in the process are terminated
3. Resources (file handles, channels) are cleaned up
4. The process becomes a `Zombie` until reaped

### Wait

Parent processes can wait for children via `waitpid()`:

- Blocks until a child exits (or returns immediately with WNOHANG)
- Returns the child's exit code
- Reaps the zombie, freeing the process slot

### Orphan handling

When a parent exits before its children:

- Children are re-parented to the init process (PID 1)
- Init is responsible for reaping orphaned zombies

Key files:

- `kernel/viper/viper.cpp`: `exit()`, wait queue management
- `kernel/syscall/process.cpp`: `sys_waitpid()` implementation

## Process groups and sessions

ViperDOS supports POSIX-style process groups and sessions:

| Field               | Description                              |
|---------------------|------------------------------------------|
| `pgid`              | Process group ID                         |
| `sid`               | Session ID                               |
| `is_session_leader` | True if this process created the session |

Syscalls:

| Syscall   | Number | Description           |
|-----------|--------|-----------------------|
| `getpid`  | 0xA0   | Get process ID        |
| `getppid` | 0xA1   | Get parent process ID |
| `getpgid` | 0xA2   | Get process group ID  |
| `setpgid` | 0xA3   | Set process group ID  |
| `getsid`  | 0xA4   | Get session ID        |
| `setsid`  | 0xA5   | Create new session    |

## Address space destruction

Address space cleanup is complete with recursive page table freeing:

- `AddressSpace::destroy()` walks all page table levels
- Physical pages are freed back to PMM
- Page table pages themselves are freed
- ASID is released for reuse

## Current limitations

- ASID allocation uses a simple bitmap (may need generation counters for wrap-around)
- No swap or memory overcommit
- Page table sharing between processes not yet optimized

