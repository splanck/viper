// File: src/il/core/Instr.hpp
// Purpose: Defines IL instruction representation.
// Key invariants: Opcode determines operand layout.
// Ownership/Lifetime: Instructions stored by value in basic blocks.
// Links: docs/il-spec.md
#pragma once

#include "il/core/Opcode.hpp"
#include "il/core/Type.hpp"
#include "il/core/Value.hpp"
#include "support/source_loc.hpp"
#include <optional>
#include <string>
#include <vector>

namespace il::core
{

/// @brief Instruction within a basic block.
struct Instr
{
    /// Destination temporary id.
    /// Owned by the instruction.
    /// Non-negative; disengaged if the instruction has no result.
    std::optional<unsigned> result;

    /// Operation code selecting semantics.
    /// Owned by the instruction.
    /// Must be a valid Opcode enumerator.
    Opcode op;

    /// Result type or void.
    /// Owned by the instruction.
    /// Must be void when result is absent.
    Type type;

    /// General operands.
    /// Vector owns each Value element.
    /// Size and content depend on opcode.
    std::vector<Value> operands;

    /// Callee name for call instructions.
    /// Owned by the instruction.
    /// Must be non-empty when op == Opcode::Call.
    std::string callee;

    /// Branch target labels.
    /// Vector owns each label string.
    /// Each must correspond to a basic block in the same function.
    std::vector<std::string> labels;

    /// Branch arguments per target.
    /// Outer vector matches labels in size; inner vectors own their Value elements.
    /// Types must match the parameters of the corresponding block.
    std::vector<std::vector<Value>> brArgs;

    /// Source location.
    /// Owned by the instruction.
    /// Line and column are >=1 when known; {0,0} denotes unknown.
    il::support::SourceLoc loc;
};

} // namespace il::core
