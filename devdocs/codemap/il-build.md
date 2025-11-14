# CODEMAP: IL Build

- **src/il/build/IRBuilder.cpp**

  Offers a stateful API for constructing modules, functions, and basic blocks while keeping SSA bookkeeping consistent. It caches known callee return types, allocates temporaries, tracks insertion points, and synthesizes terminators like `br`, `cbr`, and `ret` with argument validation. Convenience helpers materialize constants, manage block parameters, and append instructions while enforcing single-terminator invariants per block. The builder relies on `viper/il/IRBuilder.hpp`, IL core types (`Module`, `Function`, `BasicBlock`, `Instr`, `Type`, `Value`, `Opcode`), and `il::support::SourceLoc`, plus `<cassert>` and `<stdexcept>` for defensive checks.

- **src/il/build/IRBuilder.hpp**

  Declares the builder façade that front ends and tools use to construct IL programmatically. The header exposes insertion-point management, instruction creators, and helpers for constants, block parameters, and structured control flow. It avoids leaking internal container choices by relying on IL core types. Dependencies include `il/core` headers and `support/source_location.hpp`.

- **include/viper/il/IRBuilder.hpp**

  Public umbrella header that forwards to `il/build/IRBuilder.hpp`, providing a stable include path for external clients. Keeps implementation details in `src/il/build` while exposing only the supported surface. No additional dependencies beyond the forwarded header.
