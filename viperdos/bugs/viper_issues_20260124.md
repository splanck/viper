# Viperdos Codebase Analysis Report

**Date:** 2026-01-24
**Status:** Phase 5 Complete - Documentation Pass Done

---

## Executive Summary

| Metric                                | Value                                                             |
|---------------------------------------|-------------------------------------------------------------------|
| Total C++ source files analyzed       | 294+ files                                                        |
| Total lines of code                   | ~57,834 lines                                                     |
| Major subsystems                      | Kernel (15 subsystems), User-space (9+ applications, 5 libraries) |
| High-impact refactoring opportunities | 6                                                                 |
| Documentation gaps identified         | 8 major files                                                     |
| Potential code reduction              | 1,700-2,400 lines through refactoring                             |

---

## 1. Codebase Structure Overview

**Root:** `/Users/stephen/git/viper/viperdos/`

### Main Directories

| Directory           | Purpose                               |
|---------------------|---------------------------------------|
| `kernel/`           | Kernel subsystems (15 directories)    |
| `user/`             | User-space applications and libraries |
| `include/viperdos/` | Public API headers                    |
| `tools/`            | Filesystem and build tools            |
| `tests/`            | Test frameworks                       |
| `docs/`             | Documentation                         |
| `vboot/`            | Bootloader                            |

### Kernel Subsystems (`kernel/`)

| Subsystem      | Location               | Purpose                            | Files |
|----------------|------------------------|------------------------------------|-------|
| Architecture   | `kernel/arch/aarch64/` | AArch64: CPU, exceptions, GIC, MMU | 8     |
| Memory Mgmt    | `kernel/mm/`           | Buddy/slab allocators, VMM, COW    | 11    |
| Scheduling     | `kernel/sched/`        | Task management, CFS, priority     | 10    |
| IPC            | `kernel/ipc/`          | Channels, pollsets, messages       | 6     |
| Filesystem     | `kernel/fs/`           | VFS, ViperFS, caching              | 10    |
| Drivers        | `kernel/drivers/`      | Virtio (blk/net/gpu/input/rng)     | 12    |
| Console        | `kernel/console/`      | Serial, graphics, fonts            | 5     |
| Kernel Objects | `kernel/kobj/`         | Ref-counted objects                | 7     |
| Capability Sys | `kernel/cap/`          | Handle table, rights               | 3     |
| Syscall        | `kernel/syscall/`      | Dispatcher + 18 handler files      | 20    |

**Largest kernel files:**

- `viperfs.cpp` (2,144 lines)
- `netstack.cpp` (1,419 lines)
- `gcon.cpp` (1,405 lines)
- `vfs.cpp` (1,330 lines)
- `scheduler.cpp` (1,234 lines)

### User Space Components (`user/`)

| Component | Purpose                   | Notable Files                |
|-----------|---------------------------|------------------------------|
| libc      | Standard C library        | 80+ headers, 2 src files     |
| libgui    | GUI client library        | gui.h, gui.cpp (1,274 lines) |
| libhttp   | HTTP/HTTPS client         | http.hpp                     |
| libssh    | SSH/SFTP client           | ssh.hpp, sftp.hpp            |
| libtls    | TLS/SSL implementation    | tls.hpp                      |
| libvirtio | User-space virtio drivers | 5 headers, 4 src files       |
| displayd  | Display server            | main.cpp (2,109 lines)       |
| consoled  | Console server            | main.cpp (1,746 lines)       |
| workbench | Desktop environment       | 6 source files               |
| edit      | Text editor               | edit.cpp (924 lines)         |

---

## 2. Critical Duplication Issues

### Issue #1: User Pointer Validation Duplication [HIGH PRIORITY]

**Files affected:** All 18 syscall handler files
**Lines duplicated:** ~200+ occurrences

**Current pattern (repeated 50+ times):**

```cpp
if (!validate_user_read(data, size)) return SyscallResult::err(error::VERR_INVALID_ARG);
if (!validate_user_write(buf, count)) return SyscallResult::err(error::VERR_INVALID_ARG);
if (validate_user_string(path, MAX_PATH) < 0) return SyscallResult::err(...);
```

**Proposed solution:** Add validation macros to `kernel/syscall/table.hpp`:

```cpp
#define VALIDATE_USER_READ(ptr, size) \
    do { if (!validate_user_read(ptr, size)) \
         return SyscallResult::err(error::VERR_INVALID_ARG); } while(0)

#define VALIDATE_USER_WRITE(ptr, size) \
    do { if (!validate_user_write(ptr, size)) \
         return SyscallResult::err(error::VERR_INVALID_ARG); } while(0)

#define VALIDATE_USER_STRING(ptr, max_len) \
    do { if (validate_user_string(ptr, max_len) < 0) \
         return SyscallResult::err(error::VERR_INVALID_ARG); } while(0)
```

**Estimated savings:** 300-400 lines
**Effort:** 2-4 hours

**Files to modify:**

- `kernel/syscall/table.hpp` (add macros)
- `kernel/syscall/handlers/channel.cpp`
- `kernel/syscall/handlers/file.cpp`
- `kernel/syscall/handlers/dir.cpp`
- `kernel/syscall/handlers/handle_fs.cpp`
- `kernel/syscall/handlers/device.cpp`
- `kernel/syscall/handlers/net.cpp`
- `kernel/syscall/handlers/tls.cpp`
- `kernel/syscall/handlers/task.cpp`
- (and 10 more handler files)

---

### Issue #2: SyscallResult Construction Boilerplate [MEDIUM PRIORITY]

**Files affected:** All 18 handler files
**Lines duplicated:** ~100+ occurrences

**Current pattern:**

```cpp
return SyscallResult::ok(static_cast<u64>(value));
return SyscallResult::err(error::VERR_INVALID_ARG);
return SyscallResult::ok(); // for void returns
```

**Proposed solution:** Add inline helpers to `kernel/syscall/table.hpp`:

```cpp
inline SyscallResult ok_u64(u64 val) { return SyscallResult::ok(val); }
inline SyscallResult ok_u32(u32 val) { return SyscallResult::ok(static_cast<u64>(val)); }
inline SyscallResult ok_i64(i64 val) { return SyscallResult::ok(static_cast<u64>(val)); }
inline SyscallResult err_arg() { return SyscallResult::err(error::VERR_INVALID_ARG); }
inline SyscallResult err_handle() { return SyscallResult::err(error::VERR_INVALID_HANDLE); }
inline SyscallResult err_code(error::Code c) { return SyscallResult::err(c); }
```

**Estimated savings:** 150-200 lines
**Effort:** 1-2 hours

---

### Issue #3: Handle Table Lookup Boilerplate [MEDIUM PRIORITY]

**Files affected:** channel.cpp, device.cpp, tls.cpp, handle_fs.cpp, cap.cpp
**Lines duplicated:** ~10+ occurrences per file

**Current pattern:**

```cpp
cap::Table *table = get_current_cap_table();
if (!table) return SyscallResult::err(error::VERR_NOT_FOUND);
cap::Entry *entry = table->get_with_rights(handle, cap::Kind::Channel, rights);
if (!entry) return SyscallResult::err(error::VERR_INVALID_HANDLE);
kobj::Channel *ch = static_cast<kobj::Channel *>(entry->object);
```

**Proposed solution:** Add template helper to `kernel/syscall/table.hpp`:

```cpp
template<typename T>
T* get_object(cap::Handle h, cap::Kind expected_kind, u32 required_rights) {
    cap::Table *table = get_current_cap_table();
    if (!table) return nullptr;
    cap::Entry *entry = table->get_with_rights(h, expected_kind, required_rights);
    if (!entry) return nullptr;
    return static_cast<T*>(entry->object);
}

// Usage:
auto* ch = get_object<kobj::Channel>(handle, cap::Kind::Channel, cap::Rights::WRITE);
if (!ch) return SyscallResult::err(error::VERR_INVALID_HANDLE);
```

**Estimated savings:** 100-150 lines
**Effort:** 2-3 hours

---

### Issue #4: File Descriptor Edge Cases [MEDIUM PRIORITY]

**Files affected:** file.cpp, tty.cpp
**Lines duplicated:** Pattern repeats ~6 times

**Current pattern:**

```cpp
if (fd == 0) { /* stdin */ }
else if (fd == 1 || fd == 2) { /* stdout/stderr */ }
else { /* regular file */ }
```

**Proposed solution:** Add inline helpers:

```cpp
inline bool is_stdin(i32 fd) { return fd == 0; }
inline bool is_stdout(i32 fd) { return fd == 1; }
inline bool is_stderr(i32 fd) { return fd == 2; }
inline bool is_pseudo_fd(i32 fd) { return fd >= 0 && fd <= 2; }
inline bool is_output_fd(i32 fd) { return fd == 1 || fd == 2; }
```

**Estimated savings:** 30-50 lines, improved readability
**Effort:** 30 minutes

---

### Issue #5: Virtio Driver Boilerplate [ALREADY ADDRESSED]

**Status:** ✅ Already well-designed

Upon code review, the virtio driver infrastructure is already properly factored:

- `virtio::Device` base class (`virtio.hpp`) provides:
    - `basic_init()` - common initialization sequence
    - `read32()`, `write32()` - MMIO register access
    - `read_config8/16/32/64()` - config space access
    - `negotiate_features()` - feature negotiation
    - `set_status()`, `add_status()`, `get_status()` - status management
    - `read_isr()`, `ack_interrupt()` - interrupt handling

- `virtio::Virtqueue` class (`virtqueue.hpp`) provides:
    - `init()`, `destroy()` - queue setup/teardown
    - `alloc_desc()`, `free_desc()`, `free_chain()` - descriptor management
    - `set_desc()`, `chain_desc()` - descriptor configuration
    - `submit()`, `kick()` - request submission
    - `poll_used()`, `get_used_len()` - completion polling

- All device drivers (`BlkDevice`, `NetDevice`, `InputDevice`) inherit from `Device`
- RNG uses `Device` directly as a composition

**Original estimate was incorrect.** No refactoring needed.

---

### Issue #6: IPC Server Framework [LOW-MEDIUM PRIORITY]

**Files affected:** `displayd/main.cpp` (2,109 lines), `consoled/main.cpp` (1,746 lines)
**Lines duplicated:** ~400+ lines of common patterns

**Common pattern:**

- Message receive loops
- Request/reply handling
- Error checking
- Client management

**Proposed solution:** Create IPC server base class with message dispatch table:

```cpp
class IpcServer {
protected:
    cap::Handle listen_handle_;

    virtual void on_message(u32 type, void* data, size_t len) = 0;
    virtual void on_client_disconnect(u32 client_id) = 0;

public:
    void run_loop();
    void send_reply(u32 client_id, u32 type, void* data, size_t len);
};
```

**Estimated savings:** 400-500 lines
**Effort:** 3-4 hours

---

## 3. Missing Documentation

### High Priority (Core System Logic)

#### `kernel/fs/viperfs/viperfs.cpp` (2,144 lines)

- Block allocation algorithm not explained
- Directory structure format undocumented
- Inode structure lacks field descriptions
- Error recovery logic not commented
- Disk layout not diagrammed

#### `kernel/net/netstack.cpp` (1,419 lines)

- Packet flow not documented
- Buffer management strategy not explained
- TCP state machine transitions not commented
- Socket lifecycle not described

#### `kernel/console/gcon.cpp` (1,405 lines)

- Framebuffer access patterns not explained
- Dirty rectangle optimization not documented
- Font rendering pipeline not described
- Double buffering strategy not commented

### Medium Priority (Complex Logic)

#### `kernel/drivers/virtio/*.cpp` (5 files, ~50 undocumented functions each)

- MMIO register layouts need comments
- Queue management operations need explanation
- Interrupt handling flow not documented

#### `user/servers/displayd/main.cpp` (2,109 lines)

- Window Z-order management not explained
- Event routing logic not documented
- Client connection lifecycle not described
- Drawing protocol not fully commented

#### `user/servers/consoled/main.cpp` (1,746 lines)

- Terminal emulation state machine not documented
- Input handling not explained
- Escape sequence parsing not commented

#### `user/workbench/src/filebrowser.cpp` (1,452 lines)

- UI event handling not documented
- View state management not explained
- Icon rendering pipeline not described

#### `kernel/sched/scheduler.cpp` (1,234 lines)

- Priority queue implementation not explained
- Task selection algorithm not documented
- Preemption logic not commented

---

## 4. Antipatterns and Code Smells

### Antipattern #1: Global State Without Initialization Guards

**Files:** Multiple (net, fs, sched)

**Example:**

```cpp
extern "C" kobj::Channel *ch_;  // Global channel pointer
extern Spinlock state_lock;     // Global lock
```

**Issue:** No pattern for ensuring initialization order.
**Recommendation:** Use init-on-first-use pattern or explicit init functions.

---

### Antipattern #2: Magic Numbers in Protocol Headers

**File:** `user/servers/displayd/display_protocol.hpp` (317 lines)

**Examples:**

- Message types use hex constants (0x81, 0x82, 0x83)
- Window max = 16 (not explained)
- Payload size = 512 bytes (not explained)

**Recommendation:** Add explanatory comments for all limits and magic values.

---

### Antipattern #3: Incomplete Error Context

**Files:** Multiple syscall handlers

**Example (channel.cpp):**

```cpp
i64 result = channel::try_send(...);
if (result < 0) return SyscallResult::err(result);
```

**Issue:** No logging of WHY the operation failed.
**Recommendation:** Add debug logging before returning errors.

---

### Antipattern #4: Inconsistent Feature Toggles

**File:** `kernel/syscall/table.cpp`

**Example:**

```cpp
#ifdef CONFIG_SYSCALL_TRACE
static bool g_tracing_enabled = false;
#endif
```

**Issue:** Some features use compile-time toggles, others use runtime flags.
**Recommendation:** Standardize feature toggle pattern across codebase.

---

### Antipattern #5: Oversized Files

Files exceeding recommended size limits:

- `viperfs.cpp` (2,144 lines) - Consider splitting into `viperfs_inode.cpp`, `viperfs_dir.cpp`
- `displayd/main.cpp` (2,109 lines) - Split into `display_window.cpp`, `display_draw.cpp`
- `consoled/main.cpp` (1,746 lines) - Split into `console_input.cpp`, `console_render.cpp`
- `font.cpp` (1,671 lines) - Move pixel data to separate file

---

## 5. Existing Well-Designed Patterns

**Positive patterns to preserve:**

### 1. Error Handling

- Consistent `error::Code` enum across all subsystems
- `Result<T>` template for error propagation
- `TRY()` and `TRY_VOID()` macros for clean error handling

### 2. Reference Counting

- `kobj::Object` base class with intrusive `ref_count_`
- `kobj::Ref<T>` smart pointer template
- Type-safe downcast via `as<T>()` using `cap::Kind` tags

### 3. Header Organization

- Clear separation of public (`include/viperdos/`) and internal headers
- Consistent per-subsystem namespacing

### 4. IPC Protocols

- Well-defined binary protocols in `*_protocol.hpp` files
- Clear message types (requests/replies/events)
- Fixed struct sizes for binary safety

### 5. Utility Libraries

- `kernel/lib/` contains well-documented header-only utilities
- `log.hpp`, `str.hpp`, `mem.hpp`, `spinlock.hpp`

---

## 6. TODO/FIXME Markers in Code

Files with incomplete work (10 files identified):

| File                                 | Status                       |
|--------------------------------------|------------------------------|
| `user/taskman/main.cpp`              | Task manager UI incomplete   |
| `user/edit/edit.cpp`                 | Text editor features pending |
| `user/workbench/src/filebrowser.cpp` | File browser enhancements    |
| `user/servers/displayd/main.cpp`     | Display server optimizations |
| `kernel/net/netstack.cpp`            | Network stack features       |
| `kernel/viper/viper.cpp`             | VM core enhancements         |
| `kernel/mm/vma.cpp`                  | Virtual memory area handling |
| `kernel/mm/fault.hpp`                | Fault handler improvements   |
| `kernel/ipc/channel.cpp`             | IPC channel features         |
| `kernel/fs/viperfs/journal.cpp`      | Journal implementation       |

---

## 7. Refactoring Priority Matrix

| Priority | Target                 | Impact         | Effort | Lines Saved | Status         |
|----------|------------------------|----------------|--------|-------------|----------------|
| 1        | User validation macros | HIGH           | 2-4h   | 300-400     | ✅ Complete     |
| 2        | SyscallResult helpers  | MEDIUM-HIGH    | 1-2h   | 150-200     | ✅ Complete     |
| 3        | Handle lookup template | MEDIUM         | 2-3h   | 100-150     | ✅ Complete     |
| 4        | Virtio base class      | N/A            | 0h     | 0           | ✅ Already done |
| 5        | IPC server framework   | MEDIUM         | 3-4h   | 400-500     | Pending        |
| 6        | Documentation pass     | HIGH (quality) | 8-12h  | N/A         | ✅ Complete     |

**Total estimated code reduction:** 950-1,250 lines (revised after Phase 3 review)
**Total estimated effort:** 11-16 hours remaining

---

## 8. Recommended Implementation Order

### Phase 1: Quick Wins (4-6 hours) ✅ COMPLETED

- [x] Add user validation macros to table.hpp
    - Added VALIDATE_USER_READ, VALIDATE_USER_WRITE, VALIDATE_USER_STRING macros
- [x] Add SyscallResult helper functions
    - Added ok_u64(), ok_u32(), ok_i64() for success returns
    - Added err_invalid_arg(), err_invalid_handle(), err_not_found(), err_out_of_memory(), err_permission(), err_code()
      for error returns
    - Added err_io(), err_not_supported(), err_would_block() for additional error types
- [x] Update all 18 handler files to use new helpers
    - channel.cpp, file.cpp, dir.cpp, handle_fs.cpp, net.cpp, device.cpp, task.cpp
    - tls.cpp, cap.cpp, debug.cpp, gui.cpp, tty.cpp, procgroup.cpp
    - signal.cpp, assign.cpp, poll.cpp, sysinfo.cpp
    - (time.cpp was already minimal, no changes needed)
- [x] Verify builds and tests pass
    - Build: SUCCESS (100% targets built)
    - Tests: 3/3 host tests passed

### Phase 2: Core Refactoring (6-9 hours) ✅ COMPLETED

- [x] Create handle lookup macros in handlers_internal.hpp
    - Added GET_CAP_TABLE_OR_RETURN() macro
    - Added GET_OBJECT_WITH_RIGHTS(var, T, table, handle, kind, rights) macro
    - Added GET_OBJECT_CHECKED(var, T, table, handle, kind) macro
- [x] Add file descriptor helper functions to table.hpp
    - Added is_stdin(), is_stdout(), is_stderr(), is_console_fd(), is_output_fd()
- [x] Refactor affected handler files
    - channel.cpp - updated to use handle lookup macros
    - handle_fs.cpp - updated to use handle lookup macros
    - file.cpp - updated to use file descriptor helpers
    - cap.cpp - updated to use GET_CAP_TABLE_OR_RETURN()
- [x] Verify builds and tests pass
    - Build: SUCCESS (100% targets built)
    - Tests: 3/3 host tests passed

### Phase 3: Driver Consolidation (4-6 hours) ✅ ALREADY IMPLEMENTED

Upon review, the virtio driver infrastructure is already well-designed:

- [x] `virtio::Device` base class exists in `virtio.hpp`
    - Provides `basic_init()`, register access, config space reads
    - Handles feature negotiation, status management, ISR handling
- [x] `virtio::Virtqueue` class exists in `virtqueue.hpp`
    - Provides descriptor allocation, chain management, submission, polling
- [x] All drivers (BlkDevice, NetDevice, InputDevice) inherit from Device
- [x] Common patterns already factored out

**Note:** The original estimate of 800-1000 lines savings was based on incomplete
analysis. The existing architecture already implements the proposed refactoring.

### Phase 4: Server Framework (3-4 hours)

- [ ] Create IpcServer base class
- [ ] Refactor displayd to use framework
- [ ] Refactor consoled to use framework
- [ ] Verify builds and tests pass

### Phase 5: Documentation (8-12 hours) ✅ COMPLETED

- [x] Document viperfs.cpp core algorithms
    - Added disk layout diagram to format.hpp
    - Added inode block pointer layout diagram
    - Added block allocation algorithm documentation with complexity analysis
- [x] Document netstack.cpp packet flow
    - Added packet flow diagrams (receive and transmit paths)
    - Added TCP state machine diagram with all state transitions
    - Added buffer management documentation
- [x] Document gcon.cpp rendering pipeline
    - Added rendering architecture overview
    - Documented design choices (no double buffering, no dirty rectangles)
    - Explained font scaling and scrollback buffer implementation
- [x] Document scheduler.cpp task selection
    - Added task selection algorithm documentation (EDF, RT, CFS)
    - Added preemption logic documentation
    - Explained scheduling policy differences (FIFO, RR, OTHER)

---

## 9. Architectural Notes

**Verified:**

- No cross-layer dependency violations found
- No circular dependencies detected
- User-space code properly isolated from kernel internals
- Driver code properly contained in `kernel/drivers/`

**Potential concern:** Some handler files are large and could be split:

- `device.cpp` (928 lines)
- `task.cpp` (1,012 lines)

These aren't violations but could improve maintainability.

---

*End of Report*
