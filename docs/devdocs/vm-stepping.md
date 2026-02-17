---
title: VM Stepping API
status: active
audience: public
last-verified: 2026-02-17
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
  using namespace il::core;
  Module m;
  il::build::IRBuilder b(m);
  auto &fn = b.startFunction("main", Type(Type::Kind::I64), {});
  auto &bb = b.addBlock(fn, "entry");
  b.setInsertPoint(bb);

  // Build: t0 = add 40, 2 ; ret t0
  Instr add;
  add.result = b.reserveTempId();
  add.op = Opcode::Add;
  add.type = Type(Type::Kind::I64);
  add.operands.push_back(Value::constInt(40));
  add.operands.push_back(Value::constInt(2));
  bb.instructions.push_back(add);

  Instr ret;
  ret.op = Opcode::Ret;
  ret.type = Type(Type::Kind::Void);
  ret.operands.push_back(Value::temp(*add.result));
  bb.instructions.push_back(ret);
  bb.terminated = true;

  il::vm::RunConfig cfg;
  il::vm::Runner r(m, cfg);
  auto s1 = r.step();          // executes add
  auto s2 = r.continueRun();   // runs to halt
  (void)s1; (void)s2;
}
```

See also: `examples/stepping/stepping_example.cpp`.

