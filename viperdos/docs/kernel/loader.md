# Program Loading: ELF Loader

ViperDOS loads user programs from disk as ELF64 images and maps them into a Viper’s address space. This page explains the
end-to-end “load an ELF and run it” story.

## The main entrypoints

The loader API lives in `kernel/loader/loader.*`:

- `loader::load_elf(viper, elf_data, elf_size)`
- `loader::load_elf_from_blob(viper, data, size)`
- `loader::load_elf_from_disk(viper, path)`

The loader is used from `kernel/main.cpp` as part of the "bring up user space" demo path, typically loading
`/vinit.sys`.

Key files:

- `kernel/loader/loader.hpp`
- `kernel/loader/loader.cpp`
- `kernel/loader/elf.hpp`
- `kernel/loader/elf.cpp`

## The narrative: from file to runnable address space

### 1) Read the ELF file

`load_elf_from_disk()` uses the VFS layer:

1. `fs::vfs::open(path, O_RDONLY)`
2. `fs::vfs::fstat(fd, &st)` to find file size
3. allocate a buffer from the kernel heap
4. `fs::vfs::read(fd, buf, file_size)`

Now the loader has the entire ELF image in memory.

### 2) Validate the ELF header

`load_elf()` verifies the ELF header (magic, class, machine, etc.). If it’s invalid, loading fails early with
diagnostics.

### 3) Choose a load base (PIE support)

If the ELF is `ET_DYN` (PIE/shared-object style), the loader chooses a base address (currently
`viper::layout::USER_CODE_BASE`) and adds it to segment virtual addresses.

If the ELF is a fixed-position executable, it uses the segment’s `p_vaddr` directly.

### 4) For each PT_LOAD segment: map pages and copy bytes

For each loadable segment:

1. compute page-aligned region covering the segment
2. convert segment flags into protection bits (R/W/X)
3. allocate + map physical pages into the Viper’s `AddressSpace` (`as->alloc_map(...)`)
4. translate the mapped virtual address back to physical (`as->translate(...)`)
5. zero-fill the mapped region (for BSS)
6. copy `p_filesz` bytes from the ELF image into the mapped memory
7. if the segment is executable:
    - clean data cache
    - invalidate instruction cache

This is a classic “map, copy, cache-maintain” ELF load routine.

### 5) Return entry point and initial `brk`

The loader returns a `LoadResult`:

- success/failure
- entry point (`e_entry` + base)
- base address (if PIE)
- suggested `brk` (page-aligned end of the highest loaded segment)

User-space memory allocation (`brk` / heap growth) is expected to build on this later.

## The handoff to execution

After loading succeeds, `kernel/main.cpp`:

- maps a user stack near `viper::layout::USER_STACK_TOP`
- creates a user task bound to the Viper and schedules it

The first time that task is dispatched, it enters user mode via `enter_user_mode(entry, sp, arg)`.

See [Viper Processes and Address Spaces](viper_processes.md) for the user-mode transition story.

## Current limitations and next steps

- The loader assumes the kernel can write to user-mapped physical pages via identity mapping during bring-up; future
  versions will likely need a safer “copy into user” abstraction.
- There is no dynamic linker; PIE is supported only in the sense of “load at a base”.
- There is no enforcement of W^X transitions (segments are mapped with their final permissions up front).

