// File: src/il/core/Instr.hpp
// Purpose: Defines IL instruction representation.
// Key invariants: Opcode determines operand layout.
// Ownership/Lifetime: Instructions stored by value in basic blocks.
// Links: docs/il-spec.md
#pragma once

#include "il/core/Opcode.hpp"
#include "il/core/Type.hpp"
#include "il/core/Value.hpp"
#include "support/source_manager.hpp"
#include <optional>
#include <string>
#include <vector>

namespace il::core
{

/// @brief Instruction within a basic block.
struct Instr
{
    std::optional<unsigned> result; ///< destination temp id
    Opcode op;
    Type type; ///< result type (or void)
    std::vector<Value> operands;
    std::string callee;              ///< for call
    std::vector<std::string> labels; ///< branch target labels
    std::vector<Value> targs;        ///< true branch or br arguments
    std::vector<Value> fargs;        ///< false branch arguments
    il::support::SourceLoc loc;      ///< source location
};

} // namespace il::core
