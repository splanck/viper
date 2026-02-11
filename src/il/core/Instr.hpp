//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: il/core/Instr.hpp
// Purpose: Declares the Instr struct -- a single IL instruction within a basic
//          block. Covers arithmetic, memory, control flow, calls, and switch
//          operations with optional SSA result, operands, branch labels, and
//          call attributes.
// Key invariants:
//   - result is disengaged when the instruction produces no value.
//   - op must be a valid Opcode enumerator.
//   - Branch labels must correspond to basic blocks in the same function.
//   - brArgs outer vector size must match labels vector size.
//   - Source location {0,0} denotes unknown.
// Ownership/Lifetime: BasicBlock owns Instructions by value in a std::vector.
//          Instruction owns all operands, labels, and branch arguments. String
//          fields (callee, labels) use std::string value semantics.
// Links: docs/il-guide.md#reference, il/core/Opcode.hpp, il/core/Type.hpp,
//        il/core/Value.hpp
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
