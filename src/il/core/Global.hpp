//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// This file declares the Global struct, which represents module-scope variables
// and constants in Viper IL. Globals provide named, statically-allocated storage
// that is accessible to all functions within a module, enabling shared state and
// constant data initialization.
//
// Globals serve several purposes:
// - String literals: Constant string data shared across multiple functions
// - Global variables: Module-level mutable state (rare in IL, common in frontends)
// - Constant tables: Lookup tables, magic numbers, configuration data
// - Runtime metadata: Type descriptors, vtables, reflection information
//
// Each Global has a unique name within its module, a declared type, and optional
// initializer data. For string literals, the initializer contains UTF-8 bytes.
// For numeric types, the initializer may be empty (zero-initialized) or contain
// serialized constant values.
//
// Common Usage Patterns:
// - String constants: `global const str @.L0 = "Hello, World!"`
// - Numeric constants: `global const i64 @kMaxSize = 1024`
// - Array tables: `global const ptr @vtable = ...`
//
// The IL supports both mutable and const globals, though most IL code uses only
// const globals for string literals. Mutable globals are typically lowered to
// alloca instructions in function entry blocks for better optimization.
//
// Ownership Model:
// - Module owns Global structs by value in a std::vector
// - Each Global owns its name string, type, and initializer data
// - Global lifetime matches the containing module's lifetime
//
//===----------------------------------------------------------------------===//

#pragma once

#include "il/core/Type.hpp"
#include <string>

namespace il::core
{
/// @brief Module-scope variable or constant.
///
/// Globals provide named storage that is accessible to all functions within a
/// module. Each global carries its own identifier, declared type, and optional
/// initializer for constant data. The owning `Module` manages the lifetime of
/// these objects.
struct Global
{
    /// @brief Identifier of the global within its module.
    /// @invariant Unique among all globals owned by the module.
    std::string name;

    /// @brief Declared IL type of the global.
    /// @invariant Must match the type of any provided initializer.
    Type type;

    /// @brief Serialized initializer data, if any.
    /// @invariant Non-empty only for globals with constant values (e.g. UTF-8
    /// string literals).
    std::string init;
};

} // namespace il::core
