//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// This file declares the Instr struct, which represents a single IL instruction
// within a basic block. Instructions are the atomic units of computation in
// Viper IL, implementing operations ranging from arithmetic to control flow.
//
// The Instr struct uses a flexible design to accommodate the diverse needs of
// different instruction types:
// - Standard operations (add, mul, load, etc.) use the operands vector
// - Call instructions additionally store a callee name
// - Branch instructions store target labels and per-target arguments
// - All instructions can have an optional result temporary
//
// Instructions follow SSA form: each instruction that produces a value assigns
// it to a unique temporary ID within the function scope. The result field holds
// this temporary ID when present. Instructions without results (stores, branches)
// leave result empty.
//
// Special Instruction Types:
// - Calls: Use callee field for function name, CallAttr for optimization hints
// - Branches: Use labels vector for targets, brArgs for per-target arguments
// - Switch: Special operand/label/arg layout accessed via helper functions
//
// The source location field (loc) enables precise error reporting and debugging.
// Line and column numbers start at 1; {0,0} indicates unknown location.
//
// Ownership Model:
// - BasicBlock owns Instructions by value in a std::vector
// - Instruction owns all operands, labels, and branch arguments
// - String fields (callee, labels) use std::string value semantics
//
//===----------------------------------------------------------------------===//

#pragma once

#include "il/core/Opcode.hpp"
#include "il/core/Type.hpp"
#include "il/core/Value.hpp"
#include "support/source_location.hpp"
#include <optional>
#include <string>
#include <vector>

namespace il::core
{

/// @brief Attribute container for call-like instructions capturing semantic hints.
/// @details Stored on every instruction but only meaningful when
///          @ref Instr::op equals @ref Opcode::Call. Future passes may use these
///          attributes to reason about exception safety and memory effects
///          without re-deriving metadata from callee analysis.
struct CallAttrs
{
    /// @brief Call cannot throw an exception.
    bool nothrow = false;

    /// @brief Call may read from memory but performs no writes.
    bool readonly = false;

    /// @brief Call performs no memory access and has no observable side effects.
    bool pure = false;
};

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

    /// @brief Semantic attributes describing the behaviour of call-like instructions.
    CallAttrs CallAttr{};
};

/// @brief Access the scrutinee operand of a switch instruction.
const Value &switchScrutinee(const Instr &instr);

/// @brief Retrieve the default branch label for a switch instruction.
const std::string &switchDefaultLabel(const Instr &instr);

/// @brief Retrieve the default branch arguments for a switch instruction.
const std::vector<Value> &switchDefaultArgs(const Instr &instr);

/// @brief Count the number of explicit case arms in a switch instruction.
size_t switchCaseCount(const Instr &instr);

/// @brief Access the value guarding the @p index-th case arm.
const Value &switchCaseValue(const Instr &instr, size_t index);

/// @brief Access the branch label for the @p index-th case arm.
const std::string &switchCaseLabel(const Instr &instr, size_t index);

/// @brief Access the branch arguments for the @p index-th case arm.
const std::vector<Value> &switchCaseArgs(const Instr &instr, size_t index);

} // namespace il::core
