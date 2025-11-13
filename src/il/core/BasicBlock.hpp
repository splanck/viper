//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// This file declares the BasicBlock struct, which represents a maximal sequence
// of IL instructions with a single entry point and a single exit terminator.
// Basic blocks are the fundamental units of control flow in Viper IL functions.
//
// A BasicBlock consists of:
// - A unique label identifying the block within its function
// - Optional parameters (phi-node equivalents) for incoming SSA values
// - A sequence of instructions with the last being a terminator
// - A terminated flag indicating whether the block is properly closed
//
// Basic blocks follow standard compiler control flow graph (CFG) semantics:
// execution enters at the top, proceeds sequentially through instructions,
// and exits via a terminator (ret, br, cbr, or switch). Blocks with parameters
// receive values from predecessor blocks via branch arguments, implementing
// SSA phi-node semantics without explicit phi instructions.
//
// Key Invariants:
// - Labels must be non-empty and unique within the parent function
// - Parameter count and types must match incoming branch arguments
// - If terminated is true, the last instruction must be a terminator opcode
// - All instructions except the last must be non-terminator opcodes
//
// Ownership Model:
// - Function owns BasicBlocks by value in a std::vector
// - BasicBlock owns all Instructions and Params by value
// - Labels are stored as std::string values owned by the block
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
