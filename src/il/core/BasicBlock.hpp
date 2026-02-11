//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: il/core/BasicBlock.hpp
// Purpose: Declares the BasicBlock struct -- a maximal sequence of IL
//          instructions with a single entry point, optional block parameters
//          (phi-node equivalents), and a single exit terminator. Basic blocks
//          are the fundamental units of control flow in Viper IL functions.
// Key invariants:
//   - Labels must be non-empty and unique within the parent function.
//   - Parameter count and types must match incoming branch arguments.
//   - If terminated is true, the last instruction must be a terminator opcode.
//   - All instructions except the last must be non-terminator opcodes.
// Ownership/Lifetime: Function owns BasicBlocks by value in a std::vector.
//          BasicBlock owns all Instructions and Params by value. Labels are
//          stored as std::string values owned by the block.
// Links: docs/il-guide.md#reference, il/core/Instr.hpp, il/core/Param.hpp
//
//===----------------------------------------------------------------------===//

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
