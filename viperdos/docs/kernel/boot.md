# Boot and Early Initialization

This page walks through the “first seconds” of ViperDOS: how the CPU enters the kernel, how `kernel_main()` brings up the
minimum viable platform, and where each subsystem gets its first foothold.

If you want the single best starting point in code, open `kernel/main.cpp` and read `kernel_main()` top-to-bottom.

## The story from power-on to `kernel_main()`

On AArch64, the kernel’s first instruction is the `_start` symbol in `kernel/arch/aarch64/boot.S`.

That early stub does a small number of things, in a strict order:

1. **Preserve the boot parameter pointer** (`x0`) so we can pass it into C++ later.
2. **Select the EL1 stack pointer** (`SPSel=1`) and point `SP_EL1` at a small static stack (`kernel_stack_top`).
3. **Disable alignment checking** in `SCTLR_EL1` (the codebase relies on some unaligned accesses during bring-up,
   especially around network packet structs).
4. **Zero `.bss`** so global state starts from known values.
5. **Call `kernel_main(void* boot_info_ptr)`**.

At this moment, the kernel is running in EL1, with a basic stack, a clean `.bss`, and no assumptions beyond “we can
execute code and write to MMIO / RAM via the current mapping”.

## Boot information: “what did the boot environment give us?”

The first real kernel subsystem that runs is `boot::init()` from `kernel/boot/bootinfo.cpp`. It interprets the opaque
pointer passed in `x0`:

- **UEFI/VBoot path**: if the pointer looks like a valid `viper::vboot::Info` structure, we treat it as
  “bootloader-provided boot info”, including GOP framebuffer information and a memory map.
- **QEMU direct `-kernel` path**: otherwise, we treat `x0` as a DTB pointer and fall back to conservative hard-coded
  defaults for QEMU `virt` (notably a single 128 MiB RAM window starting at `0x40000000`).

The result of this parsing is a single `boot::Info` snapshot that the rest of the kernel can query.

Key files:

- `kernel/arch/aarch64/boot.S`
- `kernel/boot/bootinfo.hpp`
- `kernel/boot/bootinfo.cpp`
- `kernel/include/vboot.hpp`

## Bringing up output: “make debugging possible”

The kernel immediately initializes the serial console (`serial::init()` from `kernel/console/serial.cpp`) and prints an
early banner. Serial is the “always works” output path during bring-up.

After that, the kernel attempts to create a framebuffer-backed graphics console:

- If boot info says we have a **UEFI GOP framebuffer**, it uses that via `ramfb::init_external(...)`.
- Otherwise, it uses QEMU’s `fw_cfg` + `ramfb::init(width,height)` to create a RAM framebuffer.

Once a framebuffer exists, the kernel initializes the text renderer in `gcon::init()` and starts echoing boot progress
there as well.

Key files:

- `kernel/console/serial.*`
- `kernel/drivers/fwcfg.*`
- `kernel/drivers/ramfb.*`
- `kernel/console/gcon.*`
- `kernel/console/font.*`

## Memory comes next: PMM → (VMM scaffolding) → kernel heap

With output working, `kernel_main()` initializes memory management in three layers:

1. **PMM**: `pmm::init(ram_start, ram_size, kernel_end)` in `kernel/mm/pmm.cpp` builds a bitmap allocator over the RAM
   window.
2. **VMM (scaffolding)**: `vmm::init()` in `kernel/mm/vmm.cpp` allocates a root page table and provides mapping helpers.
   During bring-up, the kernel often still relies on identity mapping provided by firmware/QEMU, so VMM is mostly
   infrastructure until the MMU is explicitly configured.
3. **Kernel heap**: `kheap::init()` in `kernel/mm/kheap.cpp` sets up a bump allocator on top of PMM pages; `new`/
   `delete` route into this allocator.

This ordering matters: later subsystems (cap tables, ViperFS inodes, loader buffers) allocate from `kheap`, which in
turn needs `pmm`.

Key files:

- `kernel/mm/pmm.*`
- `kernel/mm/vmm.*`
- `kernel/mm/kheap.*`

## Exceptions and interrupts: install vectors, start the timer

Once there is memory for kernel data structures, the kernel sets up the “reactive” side of the system:

- **Exception vectors**: `exceptions::init()` installs the vector base and wires the assembly stubs to C++ handlers (
  `kernel/arch/aarch64/exceptions.S` + `kernel/arch/aarch64/exceptions.cpp`).
- **GIC**: `gic::init()` sets up interrupt delivery for the QEMU `virt` platform.
- **Timer**: `timer::init()` starts the architected timer at 1000 Hz (1 ms tick).

After this, `exceptions::enable_interrupts()` unmasks IRQs.

One important bring-up detail: the timer interrupt handler does more than “tick”. It performs periodic polling work (
input and networking), checks sleep timers, and asks the scheduler to account time slices and preempt.

Key files:

- `kernel/arch/aarch64/exceptions.*`
- `kernel/arch/aarch64/gic.*`
- `kernel/arch/aarch64/timer.*`

## Core services: tasks, scheduler, IPC, polling

From here, the kernel brings up the minimum set of “OS services” needed to run code concurrently:

- `task::init()` creates the idle task and initializes the global task table.
- `scheduler::init()` prepares the ready queue.
- `channel::init()` initializes the IPC channel table.
- `poll::init()` and `pollset::init()` initialize timers and readiness polling structures.

Key files:

- `kernel/sched/task.*`
- `kernel/sched/scheduler.*`
- `kernel/ipc/channel.*`
- `kernel/ipc/poll.*`
- `kernel/ipc/pollset.*`

## Device discovery and storage/network bring-up (virtio)

ViperDOS targets QEMU’s `virt` machine and uses virtio-mmio for devices. `kernel_main()` runs:

- `virtio::init()` to scan MMIO space for virtio devices.
- Device drivers: `virtio::rng::init()`, `virtio::blk_init()`, `virtio::input_init()`, `virtio::net_init()`.
- Stack initializers: `input::init()` and `net::network_init()`.

The current networking model is polling-based; the timer interrupt calls `net::network_poll()` to drain received frames.

Key files:

- `kernel/drivers/virtio/*`
- `kernel/input/*`
- `kernel/net/*`

## Filesystem bring-up: cache → ViperFS → VFS → Assigns

When a virtio block device is present, `kernel_main()` mounts the on-disk filesystem stack:

1. `fs::cache_init()` initializes a fixed-size LRU block cache.
2. `fs::viperfs::viperfs_init()` mounts ViperFS by reading and validating the superblock at block 0.
3. `fs::vfs::init()` initializes the VFS layer that provides path traversal and file-descriptor semantics.
4. `viper::assign::init()` initializes the Assign table and installs system assigns like `SYS:` and `D0:`.

This is a good example of ViperDOS’ bring-up philosophy: simple global tables first, then per-process/per-task
refactoring later.

Key files:

- `kernel/fs/cache.*`
- `kernel/fs/viperfs/*`
- `kernel/fs/vfs/*`
- `kernel/assign/*`

## Transition toward “user space”: MMU, Viper processes, ELF loader

After the kernel has storage and a VFS, it can load a user program:

- `mmu::init()` builds kernel page tables and enables the MMU (still largely identity mapped for the kernel region).
- `viper::init()` initializes the process (“Viper”) subsystem, including ASID allocation and per-Viper capability
  tables.
- `loader::load_elf_from_disk(viper, "/vinit.sys")` maps and copies PT_LOAD segments into the Viper's address space.
- A user stack is mapped, and then either:
    - the kernel enters user mode directly (debug option), or
    - a user task is created and scheduled to enter user mode when first dispatched.

Key files:

- `kernel/arch/aarch64/mmu.*`
- `kernel/viper/*`
- `kernel/loader/*`
- `kernel/sched/task.cpp` (`create_user_task`, `enter_user_mode`)

## Where to go next

- For the mechanics of output and debugging: [Console, Graphics Console, and Logging](console.md)
- For memory: [Memory Management](memory.md)
- For processes and address spaces: [Viper Processes and Address Spaces](viper_processes.md)
- For the “kernel API surface” to user space: [Syscalls](syscalls.md)

