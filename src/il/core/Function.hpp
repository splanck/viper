//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// This file declares the Function struct, which represents an IL function
// definition along with its parameters and basic blocks. Functions are the
// primary unit of code organization in Viper IL, containing executable
// instructions organized into a control flow graph.
//
// Each Function consists of:
// - A unique name within its containing Module
// - A return type and ordered parameter list
// - A sequence of BasicBlocks forming the function body
// - Optional metadata for diagnostics (SSA value names)
// - Semantic attributes (nothrow, readonly, pure) for optimization
//
// Functions use Static Single Assignment (SSA) form for values. Each instruction
// that produces a value is assigned a unique SSA ID within its function scope.
// The valueNames vector provides optional debug information mapping SSA IDs
// to source-level variable names.
//
// Key Invariants:
// - Functions must contain at least one basic block
// - Block labels must be unique within the function
// - Parameter types and count must match the function signature
// - All control flow paths must terminate with proper terminators
//
// Ownership Model:
// - Module owns Functions by value in a std::vector
// - Function owns all BasicBlocks, Params, and metadata
// - Functions can be moved but are expensive to copy (deep copy of all blocks)
//
//===----------------------------------------------------------------------===//

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
