---
title: VM Runtime Bridge and Externs
status: active
audience: public
last-verified: 2025-11-10
---

# VM Runtime Bridge and Externs

The VM invokes C runtime helpers and user externs through a documented bridge.
Built‑in helpers are listed in `docs/runtime-vm.md#vm-externs`. You can also
register your own externs at runtime.

## Registering an extern

```c++
#include "viper/vm/RuntimeBridge.hpp"

static void times2_handler(void **args, void *result) {
  auto x = *reinterpret_cast<const int64_t *>(args[0]);
  *reinterpret_cast<int64_t *>(result) = x * 2;
}

int main() {
  il::vm::ExternDesc ext;
  ext.name = "times2";
  ext.fn = reinterpret_cast<void *>(&times2_handler);

  // Register in the process-global registry (shared by all VMs):
  il::vm::registerExternIn(il::vm::processGlobalExternRegistry(), ext);

  // Or register in a per-VM registry for isolation:
  // auto reg = il::vm::createExternRegistry();
  // il::vm::registerExternIn(*reg, ext);
}
```

When IL code calls `@times2`, the bridge validates arity and marshals
arguments/results. Unknown names or mismatched arity trap with a clear message.

See: `examples/externs/register_times2.cpp` and unit tests under `src/tests/unit/VM_ExternRegistryTests.cpp`.
