// File: src/il/core/fwd.hpp
// Purpose: Forward declarations for core IL data structures.
// Key invariants: None.
// Ownership/Lifetime: Declarations only; definitions live in respective headers.
// Links: docs/il-spec.md
#pragma once

namespace il::core
{
struct Module;
struct Function;
struct BasicBlock;
struct Instr;
struct Value;
struct Extern;
struct Global;
struct Param;
} // namespace il::core
