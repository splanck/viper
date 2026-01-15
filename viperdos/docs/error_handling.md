# ViperDOS Error Handling Strategy

This document describes the error handling conventions used throughout the ViperDOS kernel.

## Error Types

### 1. VError Codes (error.hpp)

The kernel uses a unified error code enum defined in `kernel/include/error.hpp`:

- `VOK (0)`: Success
- `VERR_*`: Negative error codes for various error conditions

These codes are used in:

- Syscall returns (x0 register)
- Internal kernel functions that need to propagate errors

### 2. Result<T, E> Type (lib/result.hpp)

For functions that return both a value and an error status, use `viper::Result<T, E>`:

```cpp
Result<int, Error> divide(int a, int b) {
    if (b == 0) return Result<int, Error>::Err(Error::InvalidArg);
    return Result<int, Error>::Ok(a / b);
}
```

### 3. Nullable Pointers

For allocation/lookup functions, returning `nullptr` on failure is acceptable when the only error condition is "not
found" or "allocation failed".

## Error Handling by Subsystem

### Syscalls (dispatch.cpp)

- Use `SYSCALL_RESULT()` macro for calls that return values
- Use `SYSCALL_VOID()` macro for calls that only return error codes
- Always validate user pointers before dereferencing
- Return `VERR_INVALID_ARG` for invalid arguments

### Memory Management (mm/)

- `pmm::alloc_page()`: Returns 0 on failure
- `kheap::kmalloc()`: Returns `nullptr` on failure
- Callers must check return values

### Filesystem (fs/)

- VFS functions return -1 on error, non-negative on success
- Inode operations return `nullptr` if inode not found
- Always release inodes after use

### Network (net/)

- Socket operations return negative error codes on failure
- TCP ISN is randomized using virtio-rng if available
- Fallback to timer-based ISN if RNG unavailable

### IPC (ipc/)

- Channel operations return error codes from error.hpp
- All channel table access is protected by spinlocks

## Synchronization and Safety

### Spinlocks

Global tables are protected by spinlocks:

- `channel_lock`: Protects channel table
- `tcp_lock`: Protects TCP socket table
- `cache_lock`: Protects block cache
- `tls_session_lock`: Protects TLS sessions

### User Pointer Validation

All syscalls that accept user pointers must validate them:

```cpp
if (validate_user_string(path, MAX_PATH) < 0) {
    verr = error::VERR_INVALID_ARG;
    break;
}
```

### RAII Wrappers

Use RAII wrappers to ensure resources are cleaned up on all code paths:

**kobj::Ref<T>** - Reference counting for kernel objects:

```cpp
kobj::Ref<Blob> blob = kobj::Blob::create(4096);
// blob is automatically released when it goes out of scope
```

**kheap::UniquePtr<T>** - Unique ownership for heap allocations:

```cpp
kheap::UniquePtr<u8> buffer(static_cast<u8*>(kheap::kmalloc(1024)));
// buffer is automatically freed when it goes out of scope
```

**fs::viperfs::InodeGuard** - Automatic inode release:

```cpp
InodeGuard guard(viperfs().read_inode(ino));
if (!guard) return -1;
// Use guard->field or guard.get() to access inode
// Inode is released when guard goes out of scope
```

**fs::CacheBlockGuard** - Automatic cache block release:

```cpp
CacheBlockGuard guard(cache().get(block_num));
if (!guard) return false;
// Use guard->data, guard.mark_dirty(), etc.
// Block is released when guard goes out of scope
```

**SpinlockGuard** - Automatic lock release:

```cpp
SpinlockGuard guard(channel_lock);
// Lock is released when guard goes out of scope
```

## Logging

Use the logging abstraction in `lib/log.hpp`:

```cpp
LOG_INFO("subsystem", "Initialization complete");
LOG_ERROR("subsystem", "Failed to allocate buffer");
LOG_FATAL("subsystem", "Unrecoverable error");  // halts system
```

## Best Practices

1. **Check all return values** from functions that can fail
2. **Prefer early returns** for error conditions
3. **Use RAII** (Ref<T>, SpinlockGuard) to ensure cleanup on all paths
4. **Document error conditions** in function comments
5. **Don't silently ignore errors** - log warnings or propagate them
6. **Validate input at system boundaries** (syscalls, external APIs)
