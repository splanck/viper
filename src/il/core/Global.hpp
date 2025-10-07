// File: src/il/core/Global.hpp
// Purpose: Represents global variables in IL modules.
// Key invariants: Initialized values match declared type.
// Ownership/Lifetime: Module owns global variables.
// Links: docs/il-guide.md#reference
#pragma once

#include "il/core/Type.hpp"
#include "il/core/Value.hpp"
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

    /// @brief Whether the binding is immutable.
    /// @note Serialisation emits the `const` keyword when this flag is true.
    bool isConst = false;

    /// @brief Initial value associated with the binding.
    /// @invariant Kind matches @ref type (e.g. `ConstStr` for `str`).
    Value init = Value::null();
};

} // namespace il::core
