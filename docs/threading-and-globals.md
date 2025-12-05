# Threading Model and Global State

This document describes the threading model for the Viper VM and the scope of process-global state shared across VM instances.

---

## Overview

Viper supports running multiple VM instances concurrently, with the following design principles:

1. **One thread per VM**: Each VM instance must be used by exactly one thread at a time.
2. **Multiple VMs allowed**: Different threads may run different VM instances in parallel.
3. **Shared extern registry**: External function registrations are process-global.
4. **Thread-local active VM**: The "current" VM is tracked via thread-local storage.

---

## VM Threading Model

### Thread-Local Active VM

The VM uses thread-local storage (TLS) to track the currently active VM instance on each thread. This is managed via `ActiveVMGuard` in `src/vm/VMContext.cpp`:

```cpp
thread_local VM *tlsActiveVM = nullptr;
```

**Key behaviors:**

- `ActiveVMGuard` installs a VM as the thread-local active VM on construction.
- The destructor restores the previous active VM (supporting nested guards).
- `VM::activeInstance()` returns the currently active VM for the calling thread.
- In debug builds, assertions catch attempts to activate a *different* VM while one is already active.

**Why this matters:**

The runtime bridge (`RuntimeBridge`) uses the active VM to route traps. When a runtime function traps, it queries `VM::activeInstance()` to find the correct VM for error handling. Without an active VM, traps go directly to `rt_abort()`.

### Safe Multi-VM Usage

To run multiple VMs on different threads:

```cpp
// Thread 1
void runVM1() {
    VM vm1;
    vm1.loadModule(module1);
    vm1.run();  // ActiveVMGuard installed internally
}

// Thread 2
void runVM2() {
    VM vm2;
    vm2.loadModule(module2);
    vm2.run();  // Separate TLS slot, no conflict
}

// Safe: different threads, different VMs
std::thread t1(runVM1);
std::thread t2(runVM2);
```

**Unsafe patterns:**

```cpp
// UNSAFE: Same VM from multiple threads
VM sharedVM;
std::thread t1([&]{ sharedVM.run(); });
std::thread t2([&]{ sharedVM.run(); });  // Data race!

// UNSAFE: Switching VMs on same thread without proper cleanup
VM vm1, vm2;
ActiveVMGuard g1(&vm1);
ActiveVMGuard g2(&vm2);  // Assertion failure in debug builds
```

---

## Process-Global State

### Extern Registry

The extern registry maps external function names to their implementations. It is **process-global** and shared by all VM instances.

**Location:** `src/vm/RuntimeBridge.cpp`

```cpp
ExternRegistry &processGlobalExternRegistry();  // The singleton
ExternRegistry &currentExternRegistry();        // Currently always returns global
```

**Characteristics:**

| Property | Value |
|----------|-------|
| Scope | Process-global singleton |
| Thread safety | Mutex-protected |
| Lifetime | Process lifetime |
| Visibility | All VMs see all registrations |

**Implications:**

1. An external function registered by VM1 is callable from VM2.
2. Unregistering a function affects all VMs.
3. There is no isolation between VMs for external functions.

**Usage:**

```cpp
// Register an external function (visible to all VMs)
ExternDesc desc;
desc.name = "my_extern";
desc.signature = { /* ... */ };
desc.fn = my_extern_impl;
RuntimeBridge::registerExtern(desc);

// Later, any VM can call "my_extern"
```

**Future direction:**

The code includes comments about per-VM extern registries:

```cpp
// FUTURE: Per-VM Extern Registry
// To support per-VM scoping, the following changes would be needed:
// 1. Add an `ExternRegistry*` member to the VM class.
// 2. Modify `currentExternRegistry()` to check for an active VM...
```

### BASIC Frontend Options

The BASIC frontend exposes process-global feature flags for controlling compilation behavior.

**Location:** `src/frontends/basic/Options.hpp`, `Options.cpp`

**Available flags:**

| Flag | Default | Description |
|------|---------|-------------|
| `enableRuntimeNamespaces` | true | Allow `USING Viper.*` imports |
| `enableRuntimeTypeBridging` | true | Direct runtime type constructors |
| `enableSelectCaseConstLabels` | true | CONST labels in SELECT CASE |

**Thread safety:**

All flags use `std::atomic<bool>` with relaxed memory ordering:

```cpp
static std::atomic<bool> g_enableRuntimeNamespaces{true};

bool FrontendOptions::enableRuntimeNamespaces() {
    return g_enableRuntimeNamespaces.load(std::memory_order_relaxed);
}
```

**Guarantees:**

- **No data races**: Concurrent reads and writes are safe.
- **No synchronization**: Changes may not be immediately visible to other threads.
- **No notification**: No callbacks when options change.

**Recommended usage:**

```cpp
// Configure options BEFORE spawning compilation threads
FrontendOptions::setEnableRuntimeNamespaces(false);
FrontendOptions::setEnableSelectCaseConstLabels(true);

// Now spawn threads that will compile BASIC code
std::vector<std::thread> workers;
for (int i = 0; i < numThreads; ++i) {
    workers.emplace_back(compileBasicFile, files[i]);
}
```

**Runtime option changes:**

Changing options while workers are active is safe (no undefined behavior) but may cause inconsistent behavior:

```cpp
// Thread 1: Compiling file A
// Reads enableRuntimeNamespaces = true

// Main thread: Changes option
FrontendOptions::setEnableRuntimeNamespaces(false);

// Thread 2: Compiling file B
// May see enableRuntimeNamespaces = false

// Result: File A and B compiled with different settings
```

---

## Embedder Guidelines

### Starting Multiple VMs

1. Create separate `VM` instances for each thread.
2. Do not share a single `VM` across threads.
3. External functions registered are visible to all VMs.

### Configuring Options

1. Set all `FrontendOptions` before starting worker threads.
2. Avoid changing options during active compilation.
3. If dynamic reconfiguration is needed, drain active compilations first.

### External Function Isolation

If you need isolated external function sets:

1. **Current workaround**: Use naming conventions (e.g., `tenant1_myfunc`, `tenant2_myfunc`).
2. **Future**: Per-VM registries may be added (see source code comments).

### Trap Handling

1. Traps are routed through the thread-local active VM.
2. If no VM is active, traps call `rt_abort()` directly.
3. Embedders can override `vm_trap()` (weak symbol) for custom handling.

---

## Source Code References

| Component | Location | Purpose |
|-----------|----------|---------|
| Active VM TLS | `src/vm/VMContext.cpp:38` | Thread-local active VM pointer |
| ActiveVMGuard | `src/vm/VMContext.cpp:72-114` | RAII guard for VM activation |
| Extern Registry | `src/vm/RuntimeBridge.cpp:309-425` | Process-global function registry |
| Frontend Options | `src/frontends/basic/Options.hpp` | Atomic feature flags |

---

## Summary

| State | Scope | Thread Safety | Notes |
|-------|-------|---------------|-------|
| Active VM | Thread-local | Automatic via TLS | One VM per thread |
| Extern Registry | Process-global | Mutex-protected | Shared by all VMs |
| Frontend Options | Process-global | Atomic (relaxed) | Configure before threading |
