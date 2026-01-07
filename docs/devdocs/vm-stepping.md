---
title: VM Stepping API
status: active
audience: public
last-verified: 2025-11-10
---

# VM Stepping API

This page shows how to single‑step IL with the public `il::vm::Runner` façade.

- Construct a `Runner` with a module and optional `RunConfig`.
- Use `step()` to execute exactly one instruction; use `continueRun()` to run
  until a terminal state (halt/trap/breakpoint/pause).
- Breakpoints can be set by source location, and a step budget can be applied via
  `RunConfig.maxSteps` or `Runner::setMaxSteps`.

Minimal example:

```c++
#include "il/build/IRBuilder.hpp"
#include "viper/vm/VM.hpp"

int main() {
  il::core::Module m;
  il::build::IRBuilder b(m);
  auto &fn = b.startFunction("main", il::core::Type(il::core::Type::Kind::I64), {});
  auto &bb = b.addBlock(fn, "entry");
  b.setInsertPoint(bb);
  auto t0 = b.emitAdd(il::core::Value::constInt(1), il::core::Value::constInt(2));
  b.emitRet(t0);

  il::vm::Runner r(m, {});
  auto s1 = r.step();          // executes add
  auto s2 = r.continueRun();   // runs to halt
  (void)s1; (void)s2;
}
```

See also: `examples/stepping/stepping_example.cpp`.

