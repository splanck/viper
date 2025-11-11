---
title: VM Runtime Bridge and Externs
status: active
audience: public
last-verified: 2025-11-10
---

# VM Runtime Bridge and Externs

The VM invokes C runtime helpers and user externs through a stable bridge.
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
  ext.signature = il::vm::signatures::make_signature("times2",
      {il::vm::signatures::SigParam::Kind::I64},
      {il::vm::signatures::SigParam::Kind::I64});
  ext.fn = reinterpret_cast<void *>(&times2_handler);
  il::vm::RuntimeBridge::registerExtern(ext);
}
```

When IL code calls `@times2`, the bridge validates arity and marshals
arguments/results. Unknown names or mismatched arity trap with a clear message.

See: `examples/externs/register_times2.cpp` and unit tests under `tests/unit/VM_ExternRegistryTests.cpp`.

