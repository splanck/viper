// File: src/il/core/Function.hpp
// Purpose: Represents an IL function definition and its basic blocks.
// Key invariants: Parameter list matches return type; blocks form a valid CFG.
// Ownership/Lifetime: Module owns Function; Function owns parameters, blocks, and value names.
// Links: docs/il-guide.md#reference#functions
#pragma once

#include "il/core/BasicBlock.hpp"
#include "il/core/Param.hpp"
#include "il/core/Type.hpp"
#include <string>
#include <vector>

namespace il::core
{

/// @brief Container describing semantic attributes for a function.
/// @details These attributes do not currently influence lowering but allow
///          later optimisation passes to query summarised behaviour such as
///          exception safety or memory side effects.
struct FunctionAttrs
{
    /// @brief Function is guaranteed not to throw.
    bool nothrow = false;

    /// @brief Function may read memory but performs no writes.
    bool readonly = false;

    /// @brief Function is free of observable side effects and memory access.
    bool pure = false;
};

/// @brief Definition of an IL function with parameters and basic blocks.
/// @see docs/il-guide.md#reference
struct Function
{
    /// Human-readable identifier for the function.
    /// @ownership Stored by the containing Module; immutable after insertion.
    /// @constraint Unique within its Module.
    std::string name;

    /// Return type declared for the function.
    /// @ownership Value copied by Function.
    /// @constraint Must match verifier rules and caller expectations.
    Type retType;

    /// Ordered list of parameters.
    /// @ownership Function owns the container and its Param elements.
    /// @constraint Size and types must match the function type.
    std::vector<Param> params;

    /// Basic blocks comprising the function body.
    /// @ownership Function owns all blocks; each block's parent is this function.
    /// @constraint Contains at least one block; labels unique within the function.
    std::vector<BasicBlock> blocks;

    /// Mapping from SSA value IDs to their original names for diagnostics.
    /// @ownership Function owns this vector.
    /// @constraint Index aligns with SSA value numbering; entries may be empty.
    std::vector<std::string> valueNames;

    /// @brief Mutable attribute bundle describing semantic hints for the function.
    FunctionAttrs Attrs{};

    /// @brief Access mutable attribute bundle for the function.
    /// @return Reference to the owned attribute container.
    [[nodiscard]] FunctionAttrs &attrs()
    {
        return Attrs;
    }

    /// @brief Access read-only function attributes.
    /// @return Const reference to the attribute container.
    [[nodiscard]] const FunctionAttrs &attrs() const
    {
        return Attrs;
    }
};

} // namespace il::core
