---
title: VM Runtime Bridge and Externs
status: active
audience: public
last-verified: 2026-02-17
---

# VM Runtime Bridge and Externs

The VM invokes C runtime helpers and user externs through a documented bridge.
Built‑in helpers are listed in `docs/runtime-vm.md#vm-externs`. You can also
register your own externs at runtime.

## Registering an extern

`ExternDesc` requires three fields: a name, a `Signature` (built with
`il::runtime::signatures::make_signature`), and a raw function pointer matching
the `void(void **args, void *result)` runtime handler ABI.

The preferred way to register externs for a single run is via `RunConfig.externs`.
For process-wide registration shared by all VM instances, use
`registerExternIn(processGlobalExternRegistry(), ext)`.

```c++
#include "viper/vm/RuntimeBridge.hpp"
#include "viper/vm/VM.hpp"
#include "il/runtime/signatures/Registry.hpp"

static void times2_handler(void **args, void *result) {
  const auto x = *reinterpret_cast<const int64_t *>(args[0]);
  *reinterpret_cast<int64_t *>(result) = x * 2;
}

int main() {
  using il::runtime::signatures::SigParam;

  il::vm::ExternDesc ext;
  ext.name = "times2";
  ext.signature = il::runtime::signatures::make_signature(
      "times2", {SigParam::Kind::I64}, {SigParam::Kind::I64});
  ext.fn = reinterpret_cast<void *>(&times2_handler);

  // Option A: register via RunConfig (scoped to this runner):
  il::vm::RunConfig cfg;
  cfg.externs.push_back(ext);
  // il::vm::Runner r(module, cfg);

  // Option B: register in the process-global registry (shared by all VMs):
  il::vm::registerExternIn(il::vm::processGlobalExternRegistry(), ext);

  // Option C: register in a per-VM registry for isolation:
  // auto reg = il::vm::createExternRegistry();
  // il::vm::registerExternIn(*reg, ext);
  // vm.setExternRegistry(reg.get());
}
```

When IL code calls `@times2`, the bridge validates arity and marshals
arguments/results. Unknown names or mismatched arity trap with a clear message.

See: `examples/externs/register_times2.cpp` and unit tests under `src/tests/unit/VM_ExternRegistryTests.cpp`.
