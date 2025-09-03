// File: src/il/core/BasicBlock.hpp
// Purpose: Represents a sequence of IL instructions with optional parameters.
// Key invariants: terminated is true when block ends with control flow.
//                 Parameter ids are unique within the function.
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
    std::string label;
    std::vector<Param> params;       ///< Block parameters
    std::vector<Instr> instructions; ///< Instruction list
    bool terminated = false;         ///< True if block ends with a terminator
};

} // namespace il::core
