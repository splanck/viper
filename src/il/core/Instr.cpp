//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Provides helper methods for inspecting and manipulating IL instructions,
// particularly the switch instruction helpers that appear in multiple passes.
// Keeping the implementation here reduces header churn while documenting shared
// invariants.
//
//===----------------------------------------------------------------------===//
//
/// @file
/// @brief Instruction helper functions for the IL core library.
/// @details Supplies routines for inspecting switch operands and associated
///          metadata, providing a single source of truth for operand layout.

#include "il/core/Instr.hpp"

#include <cassert>

namespace il::core
{
namespace
{
/// @brief Verifies that a helper is operating on a switch instruction.
/// @param instr Instruction subjected to the helper.
/// @pre instr.op must be Opcode::SwitchI32.
void requireSwitch(const Instr &instr)
{
    assert(instr.op == Opcode::SwitchI32 && "expected switch instruction");
}

/// @brief Returns the branch arguments vector for a switch target.
/// @param instr Switch instruction providing the branch data.
/// @param index Zero-based index into instr.labels and instr.brArgs.
/// @return Reference to the argument list for the given branch label.
/// @pre instr must satisfy requireSwitch().
/// @pre index must be less than instr.labels.size() and instr.brArgs.size().
/// @post The returned reference remains valid for the lifetime of @p instr.
const std::vector<Value> &argsOrEmpty(const Instr &instr, size_t index)
{
    requireSwitch(instr);
    assert(index < instr.labels.size());
    assert(index < instr.brArgs.size());
    return instr.brArgs[index];
}
} // namespace

/// @brief Retrieves the value tested by the switch instruction.
/// @param instr Switch instruction providing the scrutinee.
/// @return Reference to the scrutinee value.
/// @pre instr must satisfy requireSwitch().
/// @pre instr.operands must contain at least one entry representing the scrutinee.
const Value &switchScrutinee(const Instr &instr)
{
    requireSwitch(instr);
    assert(!instr.operands.empty());
    return instr.operands.front();
}

/// @brief Retrieves the label for the default branch of a switch.
/// @param instr Switch instruction containing the default label.
/// @return Reference to the default branch label string.
/// @pre instr must satisfy requireSwitch().
/// @pre instr.labels must contain at least one entry representing the default label.
const std::string &switchDefaultLabel(const Instr &instr)
{
    requireSwitch(instr);
    assert(!instr.labels.empty());
    return instr.labels.front();
}

/// @brief Retrieves the argument vector associated with the default branch.
/// @param instr Switch instruction containing branch arguments.
/// @return Reference to the default branch argument list (possibly empty).
/// @pre instr must satisfy requireSwitch().
const std::vector<Value> &switchDefaultArgs(const Instr &instr)
{
    return argsOrEmpty(instr, 0);
}

/// @brief Determines how many non-default cases exist for a switch.
/// @param instr Switch instruction to inspect.
/// @return Number of explicit case labels in the instruction.
/// @pre instr must satisfy requireSwitch().
size_t switchCaseCount(const Instr &instr)
{
    requireSwitch(instr);
    if (instr.labels.empty())
        return 0;
    return instr.labels.size() - 1;
}

/// @brief Retrieves the scrutinee value matched by a specific case.
/// @param instr Switch instruction containing the case information.
/// @param index Zero-based index selecting the desired case.
/// @return Reference to the case's comparison value.
/// @pre instr must satisfy requireSwitch().
/// @pre index must be less than switchCaseCount(instr).
/// @pre instr.operands must contain the scrutinee followed by case values.
const Value &switchCaseValue(const Instr &instr, size_t index)
{
    requireSwitch(instr);
    assert(index < switchCaseCount(instr));
    assert(instr.operands.size() > index + 1);
    return instr.operands[index + 1];
}

/// @brief Retrieves the destination label for a specific switch case.
/// @param instr Switch instruction containing the target labels.
/// @param index Zero-based index selecting the desired case.
/// @return Reference to the label string associated with the case.
/// @pre instr must satisfy requireSwitch().
/// @pre index must be less than switchCaseCount(instr).
const std::string &switchCaseLabel(const Instr &instr, size_t index)
{
    requireSwitch(instr);
    assert(index < switchCaseCount(instr));
    return instr.labels[index + 1];
}

/// @brief Retrieves the argument vector passed to a specific switch case target.
/// @param instr Switch instruction containing branch arguments.
/// @param index Zero-based index selecting the desired case.
/// @return Reference to the argument list for the selected case (possibly empty).
/// @pre instr must satisfy requireSwitch().
/// @pre index must be less than switchCaseCount(instr).
const std::vector<Value> &switchCaseArgs(const Instr &instr, size_t index)
{
    return argsOrEmpty(instr, index + 1);
}

} // namespace il::core
