// File: src/il/core/BasicBlock.hpp
// Purpose: Represents a sequence of IL instructions.
// Key invariants: terminated is true when block ends with control flow.
// Ownership/Lifetime: Functions own blocks by value.
// Links: docs/il-spec.md
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
    std::string label;         ///< Block label
    std::vector<Param> params; ///< Block parameters
    std::vector<Instr> instructions;
    bool terminated = false;
};

} // namespace il::core
