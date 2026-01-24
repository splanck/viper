# ViperDOS User-Space Bug Report

**Date:** December 2025
**Version:** v0.2.3
**Test Programs:** fsinfo, netstat, sysinfo

---

## Summary

Three user-space test programs were created to validate the libc runtime and
syscall interfaces:

- **fsinfo.elf** - Filesystem information utility
- **netstat.elf** - Network statistics utility
- **sysinfo.elf** - System information and runtime tests

All programs compile successfully and are included in the disk image.

---

## Bugs and Issues Found

### BUG-001: Header Conflict Between libc and syscall.hpp

**Severity:** Medium
**Status:** Fixed
**Component:** user/libc/include/unistd.h, user/syscall.hpp

**Description:**
When a program includes both libc headers (e.g., `<unistd.h>`) and `syscall.hpp`,
there are macro/constexpr conflicts for `SEEK_SET`, `SEEK_CUR`, and `SEEK_END`.

The libc `unistd.h` defines these as macros:

```c
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2
```

While `syscall.hpp` tries to define them as constexpr in the `sys` namespace:

```cpp
constexpr i32 SEEK_SET = viper::seek_whence::SET;
```

The preprocessor replaces `SEEK_SET` with `0`, resulting in invalid code.

**Fix Applied:**
Added `#undef` directives in `syscall.hpp` before the constexpr declarations:

```cpp
#ifdef SEEK_SET
#undef SEEK_SET
#endif
constexpr i32 SEEK_SET = viper::seek_whence::SET;
```

**Recommendation:**
Consider a more comprehensive solution:

- Use different names in the sys namespace (e.g., `sys::SEEK_SET_`)
- Or move to a unified header approach

---

### BUG-002: libc puts() vs vinit puts() Conflict

**Severity:** High
**Status:** Fixed
**Component:** user/vinit/vinit.cpp, user/libc/src/stdio.c

**Description:**
The libc `puts()` function adds a newline after the string (per POSIX), but
vinit.cpp had its own local `puts()` that did NOT add a newline. When vinit
was linked against libc, the libc version was used, causing:

1. Cursor escape sequences (`\033[D`, `\033[C`) to be followed by newlines
2. This broke the line editor's cursor positioning for history navigation
3. Arrow-up to access command history would cause display corruption

**Symptoms:**

- Pressing arrow-up would display garbage or misaligned text
- Cursor position got out of sync with the displayed line

**Fix Applied:**
Renamed vinit's local functions to avoid conflicts:

- `puts()` → `print_str()`
- `putchar()` → `print_char()`

---

### BUG-003: Kernel kfree() Warning on ELF Loader

**Severity:** Low
**Status:** Fixed
**Component:** kernel/mm/kheap.cpp, kernel/loader/loader.cpp

**Description:**
When loading user programs, the kernel prints:

```
[kheap] ERROR: kfree() on invalid pointer 0x4022c010 (outside heap range...)
```

This appears to be a double-free or incorrect pointer being passed to kfree()
during ELF loading cleanup.

**Impact:**
The warning is printed but execution continues successfully.

**Root Cause Analysis:**
The issue is in `kernel/mm/kheap.cpp:expand_heap()` (lines 173-181). When the
kernel heap expands non-contiguously (into a new memory region), `heap_end` is
only updated for contiguous expansions:

```cpp
if (new_pages == heap_end)
{
    heap_end += expansion_size;  // Only updated here!
    // ...
}
else
{
    // Non-contiguous case - heap_end NOT updated!
    serial::puts("[kheap] WARNING: Non-contiguous heap expansion...");
}
```

When `kfree()` is called on a pointer allocated in the non-contiguous region,
the bounds check `ptr >= heap_start && ptr < heap_end` fails because `heap_end`
was never updated to include the new region.

**Related:** This is the same root cause as BUG-007.

**Fix Applied:**
Updated `kheap.cpp` to track multiple heap regions:

1. Added `HeapRegion` struct and `heap_regions[]` array to track all allocated regions
2. Added `is_in_heap()` function to check if an address is in any valid region
3. Modified `expand_heap()` to register new regions via `add_heap_region()`
4. Updated `kfree()` to use `is_in_heap()` instead of simple start/end check
5. Updated `dump()` to show all heap regions for debugging

---

### BUG-004: Orphaned Inodes After Storage Tests

**Severity:** Low
**Status:** Fixed
**Component:** kernel/fs/viperfs/viperfs.cpp, kernel/tests/storage_tests.cpp

**Description:**
After running the storage subsystem tests, fsck.ziafs reports:

```
WARNING: Orphaned inode 13 (not reachable from root)
WARNING: Superblock free_blocks=2011 but counted 1893 free
```

The storage tests create and delete files, and it appears some cleanup
is not being performed correctly, leaving orphaned inodes and incorrect
free block counts.

**Impact:**
Minor - the filesystem is still usable, but consistency is degraded.

**Root Cause Analysis:**
The issue is a **missing `sync()` call** after storage tests complete. The
`free_inode()` function at `viperfs.cpp:551-567` correctly sets `inode->mode = 0`
and marks the cache block as dirty:

```cpp
void ViperFS::free_inode(u64 ino)
{
    // ...
    Inode *inode = reinterpret_cast<Inode *>(block->data + offset);
    inode->mode = 0; // Mark as free
    block->dirty = true;
    cache().release(block);
}
```

However, the storage tests at `kernel/main.cpp:758` run without a subsequent
`sync()` call. The test sequence is:

1. `test_vfs_file_create_write_read()` creates `/testfile.txt` (inode 13)
2. File is unlinked - `free_inode()` sets `mode=0` in cache (block marked dirty)
3. Tests complete, but dirty blocks may not be flushed to disk
4. QEMU shuts down before `cache().sync()` is called
5. fsck sees inode 13 still marked as allocated (mode != 0) but with no
   directory entry pointing to it

The `unlink_file()` function correctly calls `free_inode()`, but the change
only exists in the cache. The `sync()` at lines 627 and 692 are only called
in specific command handlers, not after the test suite.

**Fix Applied:**
Added `fs::viperfs::viperfs().sync()` after `tests::run_storage_tests()` in
`kernel/main.cpp:773-776` to ensure all filesystem changes are persisted:

```cpp
if (fs::viperfs::viperfs().is_mounted())
{
    fs::viperfs::viperfs().sync();
    serial::puts("[kernel] Filesystem synced after storage tests\n");
}
```

Also fixed `tools/mkfs.ziafs.cpp` to write correct `free_blocks` count in
the superblock after adding files (was writing initial count before files).

---

### BUG-005: Assign Path Resolution Fails in Kernel

**Severity:** Low (Test Environment Issue)
**Status:** Fixed
**Component:** kernel/assign/assign.cpp, kernel/main.cpp

**Description:**
During kernel initialization, assign path resolution tests fail:

```
SYS: -> inode 4294967295 FAIL
SYS:vinit.elf -> inode 4294967295 FAIL
D0:\vinit.elf -> inode 4294967295 FAIL
```

The return value `4294967295` is `-1` as unsigned, indicating the resolution
is returning an error.

**Impact:**
The assign system tests still pass, suggesting this might be a test harness
issue rather than a functional bug.

**Root Cause Analysis:**
This is a **test environment issue**, not a bug in the assign system. The
`resolve_path()` function at `assign.cpp:448-491` requires a current Viper
context with a capability table:

```cpp
Handle resolve_path(const char *path, u32 flags)
{
    // ...
    cap::Table *ct = viper::current_cap_table();
    if (!ct)
    {
        return cap::HANDLE_INVALID;  // Returns -1 (4294967295 unsigned)
    }
    // ...
}
```

The `current_cap_table()` at `viper.cpp:398-402` returns the capability table
from `viper::current()`, which depends on having a current task with an
associated Viper process:

```cpp
cap::Table *current_cap_table()
{
    Viper *v = current();
    return v ? v->cap_table : nullptr;
}
```

During kernel initialization when these tests run (at `main.cpp:700-718`),
there is no current Viper process context established yet. The tests are
running in kernel mode before any user-space process is loaded.

**Why User-Space Tests Pass:**
The user-space assign tests pass because they execute from within a Viper
context (vinit.elf), where `viper::current()` returns a valid Viper with
an associated capability table.

**Fix Applied:**
Replaced the capability-based `resolve_path()` tests with lower-level
`get_inode()` tests that don't require a Viper context. The tests at
`kernel/main.cpp:699-735` now use:

- `viper::assign::get_inode("SYS")` - tests assign lookup
- `viper::assign::get_inode("D0")` - tests second assign
- `fs::vfs::open("/vinit.elf", ...)` - tests file access via VFS
- `viper::assign::get_inode("NONEXISTENT")` - tests negative case

All tests now pass during kernel initialization.

---

### BUG-006: Type Command Missing Trailing Newline

**Severity:** Low
**Status:** Fixed
**Component:** user/vinit/vinit.cpp

**Description:**
The `type` command (for displaying file contents) did not add a newline
after the file content, causing the shell prompt to appear on the same
line as the last line of the file.

**Fix Applied:**
Added `print_str("\n")` after the file content read loop.

---

### BUG-007: Non-contiguous Heap Warning

**Severity:** Low
**Status:** Fixed
**Component:** kernel/mm/kheap.cpp

**Description:**
The kernel prints:

```
[kheap] WARNING: Non-contiguous heap expansion at 0x4022c000
```

This suggests the kernel heap allocator is expanding into non-contiguous
memory regions, which may indicate fragmentation issues.

**Impact:**
Currently just a warning; functionality is not affected.

**Root Cause Analysis:**
This is the **same underlying issue as BUG-003**. The `expand_heap()` function
at `kheap.cpp:163-181` allocates new pages when the heap needs to grow:

```cpp
u8 *new_pages = static_cast<u8 *>(mm::page::alloc(n_pages, PageFlags::KERNEL));
if (new_pages == heap_end)
{
    heap_end += expansion_size;  // Contiguous: update heap_end
}
else
{
    // Non-contiguous: warning printed, but heap_end NOT updated
    serial::puts("[kheap] WARNING: Non-contiguous heap expansion...");
}
```

The physical page allocator doesn't guarantee contiguous allocations. When
the kernel has been running and allocating/freeing pages, fragmentation
occurs and new allocations may return pages at non-adjacent addresses.

The warning itself is informational, but the failure to update `heap_end`
for non-contiguous expansions causes BUG-003 (kfree bounds check failures).

**Fix Applied:**
Same fix as BUG-003 - the heap allocator now tracks multiple regions and
performs bounds checking against all valid regions. The warning message was
updated to be informational rather than a warning since non-contiguous
expansion is now handled correctly.

---

## Test Results

### Programs Built Successfully

- [x] fsinfo.elf (87,048 bytes)
- [x] netstat.elf (87,016 bytes)
- [x] sysinfo.elf (93,760 bytes)

### Programs Included in Disk Image

- [x] /fsinfo.elf (inode 7)
- [x] /netstat.elf (inode 8)
- [x] /sysinfo.elf (inode 9)

### Kernel-Level Tests

- [x] Storage Tests: ALL PASSED (16/16)
- [x] Viper Tests: ALL PASSED (8/8)
- [x] Poll/Timer Tests: PASSED
- [x] Capability Tests: PASSED
- [x] sbrk/heap Tests: PASSED

---

## Recommendations for Future Work

1. **Unified Header Strategy**: Create a single approach for syscall wrappers
   that doesn't conflict with libc headers.

2. **Memory Debugging**: Add more robust memory debugging to catch the kfree()
   and heap fragmentation issues.

3. **Filesystem Consistency**: Implement fsck-like checks that run automatically
   on mount to detect and correct inconsistencies.

4. **Interactive Testing**: Improve the QEMU test harness to support interactive
   program testing via scripted input.

5. **Runtime Test Suite**: Expand sysinfo.elf to include more comprehensive
   runtime validation tests.
