// File: src/il/core/Global.hpp
// Purpose: Represents global variables in IL modules.
// Key invariants: Initialized values match declared type.
// Ownership/Lifetime: Module owns global variables.
// Links: docs/il-guide.md#reference
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
