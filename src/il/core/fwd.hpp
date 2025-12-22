//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
/// @file
/// @brief Forward declarations for core IL types.
/// @details This header reduces compile-time dependencies by exposing only
///          incomplete type declarations for the IL core structures. Consumers
///          that need pointers or references can include this file instead of
///          the full definitions, which helps break cycles and reduces rebuild
///          fan-out when the core headers change.
///
/// Usage guidelines:
/// - Include this header when you only need pointers/references to IL types.
/// - Include the full headers (Module.hpp, Function.hpp, etc.) when you need to:
///   - access members or methods,
///   - construct or copy objects, or
///   - place the type in containers that require a complete definition.
//
//===----------------------------------------------------------------------===//

#pragma once

namespace il::core
{
/// @brief Forward declaration for the module-level IL container.
struct Module;
/// @brief Forward declaration for an IL function definition.
struct Function;
/// @brief Forward declaration for a basic block in the IL control-flow graph.
struct BasicBlock;
/// @brief Forward declaration for a single IL instruction.
struct Instr;
/// @brief Forward declaration for an IL operand or constant value.
struct Value;
} // namespace il::core
