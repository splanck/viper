//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// This file provides forward declarations for the core IL types to reduce
// compilation dependencies and improve build times. Headers that only need
// pointer or reference types can include this file instead of the full
// definitions, breaking circular dependencies and minimizing header bloat.
//
// Forward-declared types include the fundamental IL data structures:
// - Module: Top-level compilation unit container
// - Function: Callable unit with parameters and basic blocks
// - BasicBlock: Sequence of instructions with CFG semantics
// - Instr: Individual IL instruction
// - Value: Operand or constant value
//
// Usage Guidelines:
// - Include this header when you only need pointers/references to IL types
// - Use full headers (Module.hpp, Function.hpp, etc.) when you need to:
//   * Access struct members or methods
//   * Construct or copy objects
//   * Use types in std::vector or other containers requiring complete types
//
// This pattern is standard in large C++ projects to minimize recompilation
// cascades when core headers change. It's particularly important for IL core
// types since they're referenced throughout the compiler infrastructure.
//
//===----------------------------------------------------------------------===//

#pragma once

namespace il::core
{
struct Module;
struct Function;
struct BasicBlock;
struct Instr;
struct Value;
} // namespace il::core
