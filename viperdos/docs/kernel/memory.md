# Memory Management (PMM, VMM, Kernel Heap)

ViperDOS’ memory stack is currently designed for transparency during bring-up: simple data structures, obvious
invariants, and lots of serial diagnostics.

There are three main layers:

1. **PMM**: physical page allocation (`kernel/mm/pmm.*`)
2. **VMM**: page table construction/mapping helpers (`kernel/mm/vmm.*`) and low-level MMU bring-up (
   `kernel/arch/aarch64/mmu.*`)
3. **Kernel heap**: dynamic allocation for kernel objects (`kernel/mm/kheap.*`)

## 1) PMM: bitmap-backed physical pages

The physical memory manager in `kernel/mm/pmm.cpp` manages a single contiguous RAM window and tracks page allocation
with a bitmap:

- Each bit represents one 4 KiB page (`pmm::PAGE_SIZE`).
- `0` means free; `1` means used/reserved.

### Initialization narrative

At boot, `pmm::init(ram_start, ram_size, kernel_end)`:

1. Computes total page count from the RAM window.
2. Places the bitmap **immediately after the kernel image**, rounded up to a page boundary.
3. Marks every page “used” by default, then does a second pass to mark usable pages “free”.
4. Reserves special regions (for example, the framebuffer address range) by leaving them marked used.

This “start used, then free known-good pages” approach makes it harder to accidentally allocate memory that overlaps the
kernel, the bitmap itself, or MMIO.

### Allocation model

Allocation uses first-fit scanning over bitmap words and bits:

- `pmm::alloc_page()` finds the first free bit and sets it.
- `pmm::alloc_pages(n)` searches for a contiguous run of `n` free pages.

Freeing clears the bit and increments the free count, with basic checks for invalid frees and double frees.

Key files:

- `kernel/mm/pmm.hpp`
- `kernel/mm/pmm.cpp`

## 2) VMM and MMU: page tables and translation

There are two complementary pieces in the tree:

- `kernel/mm/vmm.*`: a “general” page-table walker that can map/unmap and translate addresses.
- `kernel/arch/aarch64/mmu.*`: the bring-up path that builds initial kernel tables and flips the MMU on.

### `mmu::init()`: enabling the MMU

`mmu::init()` is the point where the kernel explicitly programs AArch64 translation registers:

- builds an identity-mapped kernel table root (TTBR0) that covers:
    - low region as device memory (MMIO)
    - RAM region as normal cacheable memory
- sets MAIR/TCR/TTBR0
- invalidates TLBs
- sets `SCTLR_EL1.M` (enable MMU) plus caches

This is a “get the CPU into a stable state” step that makes user address spaces feasible.

Key files:

- `kernel/arch/aarch64/mmu.hpp`
- `kernel/arch/aarch64/mmu.cpp`

### `vmm`: mapping helpers (bring-up scaffolding)

`vmm` (`kernel/mm/vmm.cpp`) provides routines like:

- `vmm::map_page(virt, phys, flags)`
- `vmm::unmap_page(virt)`
- `vmm::virt_to_phys(virt)`

It allocates page-table pages from PMM and performs TLB invalidation after mapping updates.

One important bring-up detail: `vmm::virt_to_phys()` falls back to an “identity mapping” behavior if `vmm::init()`
hasn’t run yet. That makes it usable even very early, but it also means you need to be clear about which translation
regime you’re in when debugging.

Key files:

- `kernel/mm/vmm.hpp`
- `kernel/mm/vmm.cpp`

## 3) Kernel heap: bump allocation on top of PMM

The kernel heap in `kernel/mm/kheap.cpp` is a bump allocator:

- `kmalloc(n)` returns the current heap cursor and moves it forward.
- allocations are aligned to 16 bytes
- the heap grows by requesting more pages from PMM when needed
- `kfree()` is currently a no-op (no reclamation)

This is explicitly a bring-up allocator: it’s easy to reason about and unlikely to fail in surprising ways, but it will
leak memory over time.

The global C++ `new`/`delete` operators are defined here and route into `kmalloc`/`kfree`, which makes it natural for
kernel subsystems to allocate small helper objects (cap tables, inodes, loader buffers, etc.) without reinventing
allocation every time.

Key files:

- `kernel/mm/kheap.hpp`
- `kernel/mm/kheap.cpp`

## How user memory differs: address spaces vs kernel heap

User memory is not allocated from `kheap`. Instead, user processes (Vipers) have an `AddressSpace` that allocates PMM
pages and maps them into a process-visible virtual range.

That code lives under `kernel/viper/address_space.*` and is covered
in [Viper Processes and Address Spaces](viper_processes.md).

## Shared Memory for IPC

In microkernel mode, user-space servers communicate via shared memory regions. The kernel provides syscalls for
creating and mapping shared memory:

| Syscall      | Number | Description                            |
|--------------|--------|----------------------------------------|
| `shm_create` | 0x109  | Create a shared memory region          |
| `shm_map`    | 0x10A  | Map shared memory into calling process |
| `shm_unmap`  | 0x10B  | Unmap shared memory from address space |
| `shm_close`  | 0x10C  | Close shared memory handle             |

Shared memory is used for:

- **Large data transfers**: Bulk file/network data between servers and applications
- **Zero-copy I/O**: DMA buffers shared between drivers and applications
- **Server communication**: High-bandwidth IPC between user-space servers

Key files:

- `kernel/syscall/shm.cpp`: Shared memory syscall implementations
- `kernel/mm/shm.hpp`: Shared memory region management

## Current limitations and gotchas

- PMM assumes a single contiguous RAM window (bring-up for QEMU `virt`); it does not yet fully consume UEFI memory maps
  or DTB-derived RAM layouts.
- `kheap` cannot free memory yet, which can hide leaks during long runs.

