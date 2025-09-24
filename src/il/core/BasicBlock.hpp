// File: src/il/core/BasicBlock.hpp
// Purpose: Represents a sequence of IL instructions and optional parameters.
// Key invariants: terminated is true when block ends with control flow.
// Ownership/Lifetime: Functions own blocks by value.
// Links: docs/il-guide.md#reference
#pragma once

#include "il/core/Instr.hpp"
#include "il/core/Param.hpp"
#include <string>
#include <vector>

namespace il::core
{

/// @brief Sequence of instructions terminated by a control-flow instruction.
struct BasicBlock
{
    /// Human-readable identifier for the block within its function.
    ///
    /// @invariant Non-empty and unique in the parent function.
    std::string label;

    /// Parameters representing incoming SSA values.
    ///
    /// @invariant Count and types match each predecessor edge.
    std::vector<Param> params;

    /// Ordered list of IL instructions belonging to this block.
    ///
    /// @invariant If @c terminated is true, the last instruction must be a terminator.
    std::vector<Instr> instructions;

    /// Indicates whether the block ends with a control-flow instruction.
    ///
    /// @invariant Reflects whether the last instruction is a terminator.
    bool terminated = false;
};

} // namespace il::core
