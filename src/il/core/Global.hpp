//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: il/core/Global.hpp
// Purpose: Declares the Global struct -- module-scope variables and constants
//          in Viper IL. Provides named, statically-allocated storage accessible
//          to all functions within a module (string literals, numeric constants,
//          lookup tables, runtime metadata).
// Key invariants:
//   - Global names must be unique among all globals owned by the module.
//   - Initializer type must match the declared type of the global.
//   - Non-empty init only for globals with constant values (e.g. UTF-8 strings).
// Ownership/Lifetime: Module owns Global structs by value in a std::vector.
//          Each Global owns its name string, type, and initializer data.
//          Global lifetime matches the containing module's lifetime.
// Links: docs/il-guide.md#reference, il/core/Type.hpp
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
