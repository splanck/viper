---
title: VM Profiling (Opcode Counters)
status: active
audience: public
last-verified: 2025-11-10
---

# VM Profiling: Opcode Counters

The VM can count how many times each opcode executes. Build‑time default is
enabled; you can toggle at CMake configure time or at runtime:

- CMake: `-DVIPER_VM_OPCOUNTS=ON|OFF` (default ON)
- Env: `VIPER_ENABLE_OPCOUNTS=1|0` at process start

Reading counters:

```c++
il::vm::Runner runner(module, config);
runner.resetOpcodeCounts();
runner.run();
const auto &counts = runner.opcodeCounts();
// counts[static_cast<size_t>(il::core::Opcode::Add)]
```

When compiled without counters the accessors are inert.

