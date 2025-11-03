# CODEMAP: IL Build

- **src/il/build/IRBuilder.cpp**

  Offers a stateful API for constructing modules, functions, and basic blocks while keeping SSA bookkeeping consistent. It caches known callee return types, allocates temporaries, tracks insertion points, and synthesizes terminators like `br`, `cbr`, and `ret` with argument validation. Convenience helpers materialize constants, manage block parameters, and append instructions while enforcing single-terminator invariants per block. The builder relies on `viper/il/IRBuilder.hpp`, IL core types (`Module`, `Function`, `BasicBlock`, `Instr`, `Type`, `Value`, `Opcode`), and `il::support::SourceLoc`, plus `<cassert>` and `<stdexcept>` for defensive checks.
