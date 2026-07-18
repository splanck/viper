//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
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
#include "il/core/Module.hpp"

#include <cassert>
#include <stdexcept>
#include <utility>

namespace il::core {
namespace {
/// @brief Verifies that a helper is operating on a switch instruction.
/// @param instr Instruction subjected to the helper.
/// @pre instr.op must be Opcode::SwitchI32.
bool requireSwitch(const Instr &instr) {
    assert(instr.op == Opcode::SwitchI32 && "expected switch instruction");
    if (instr.op != Opcode::SwitchI32)
        throw std::logic_error("expected switch instruction");
    return true;
}

/// @brief Returns the branch arguments vector for a switch target.
/// @param instr Switch instruction providing the branch data.
/// @param index Zero-based index into instr.labels and instr.brArgs.
/// @return Reference to the argument list for the given branch label.
/// @pre instr must satisfy requireSwitch().
/// @pre index must be less than instr.labels.size() and instr.brArgs.size().
/// @post The returned reference remains valid for the lifetime of @p instr.
const std::vector<Value> &argsOrEmpty(const Instr &instr, size_t index) {
    static const std::vector<Value> kEmptyArgs;
    if (!requireSwitch(instr))
        return kEmptyArgs;
    if (index >= instr.labels.size() || index >= instr.brArgs.size())
        throw std::out_of_range("switch branch argument index out of range");
    assert(index < instr.labels.size());
    assert(index < instr.brArgs.size());
    return instr.brArgs[index];
}

/// @brief Append branch arguments while preserving all-or-none brArgs shape.
/// @details The append API historically exposes @c brArgs.back() immediately
///          after adding a target. Therefore appended targets always receive a
///          bundle, and older no-bundle edges are backfilled with explicit empty
///          lists so @ref Instr::brArgs remains aligned with @ref Instr::labels.
/// @param instr Instruction whose branch-argument vector is updated.
/// @param args Argument list for the newly appended edge.
/// @pre The new branch label has already been appended to @ref Instr::labels.
void appendBranchArgs(Instr &instr, std::vector<Value> args) {
    if (instr.brArgs.empty() && !instr.labels.empty())
        instr.brArgs.resize(instr.labels.size() - 1);

    instr.brArgs.push_back(std::move(args));
}
} // namespace

/// @brief Determine whether this instruction uses call metadata.
/// @return True for direct and indirect call opcodes.
bool Instr::isCallLike() const noexcept {
    return op == Opcode::Call || op == Opcode::CallIndirect;
}

/// @brief Determine whether this instruction names a direct callee.
/// @return True for well-formed direct calls; false for indirect or malformed calls.
bool Instr::isDirectCall() const noexcept {
    return op == Opcode::Call && !callee.empty();
}

/// @brief Determine whether the instruction carries branch target labels.
/// @return True when at least one successor label is stored.
bool Instr::hasBranchTargets() const noexcept {
    return !labels.empty();
}

/// @brief Determine whether explicit call.indirect signature fields are active.
/// @return True when @ref hasIndirectSignature is set.
bool Instr::hasIndirectCallSignature() const noexcept {
    return hasIndirectSignature;
}

/// @brief Replace the direct callee string without module interning.
/// @param name Callee identifier to store.
/// @post @ref calleeSymbol is invalidated because no Module was supplied.
void Instr::setDirectCallee(std::string name) {
    callee = std::move(name);
    calleeSymbol = {};
}

/// @brief Replace the direct callee string and update its symbol sidecar.
/// @param module Module whose interner owns the resulting symbol.
/// @param name Callee identifier to store.
/// @post @ref calleeSymbol mirrors @ref callee when the name is non-empty.
void Instr::setDirectCallee(Module &module, std::string name) {
    callee = std::move(name);
    calleeSymbol = callee.empty() ? il::support::Symbol{} : module.internIdentifier(callee);
}

/// @brief Clear direct-call metadata.
/// @post @ref callee is empty and @ref calleeSymbol is invalid.
void Instr::clearDirectCallee() {
    callee.clear();
    calleeSymbol = {};
}

/// @brief Replace branch labels and arguments without module interning.
/// @param targets Successor labels to store in terminator order.
/// @param args Optional per-successor branch argument lists.
/// @throws std::invalid_argument When non-empty @p args does not match target count.
/// @post @ref labelSymbols is cleared because labels have no owning Module.
void Instr::setBranchTargets(std::vector<std::string> targets,
                             std::vector<std::vector<Value>> args) {
    if (!args.empty() && args.size() != targets.size())
        throw std::invalid_argument("branch argument list count must match branch target count");
    labels = std::move(targets);
    brArgs = std::move(args);
    labelSymbols.clear();
}

/// @brief Replace branch labels and arguments and update symbol sidecars.
/// @param module Module whose interner owns every non-empty target label.
/// @param targets Successor labels to store in terminator order.
/// @param args Optional per-successor branch argument lists.
/// @throws std::invalid_argument When non-empty @p args does not match target count.
/// @post @ref labelSymbols is aligned one-for-one with @ref labels.
void Instr::setBranchTargets(Module &module,
                             std::vector<std::string> targets,
                             std::vector<std::vector<Value>> args) {
    if (!args.empty() && args.size() != targets.size())
        throw std::invalid_argument("branch argument list count must match branch target count");
    labels = std::move(targets);
    brArgs = std::move(args);
    labelSymbols.clear();
    labelSymbols.reserve(labels.size());
    for (const auto &label : labels) {
        labelSymbols.push_back(label.empty() ? il::support::Symbol{}
                                             : module.internIdentifier(label));
    }
}

/// @brief Append a branch target without module interning.
/// @param target Successor label to append.
/// @param args Values passed to the successor block parameters.
/// @post @ref labelSymbols is cleared because the appended label has no symbol.
void Instr::addBranchTarget(std::string target, std::vector<Value> args) {
    labels.push_back(std::move(target));
    appendBranchArgs(*this, std::move(args));
    labelSymbols.clear();
}

/// @brief Append a branch target and update its symbol sidecar.
/// @param module Module whose interner owns the target label symbol.
/// @param target Successor label to append.
/// @param args Values passed to the successor block parameters.
/// @post @ref labelSymbols remains aligned with @ref labels.
void Instr::addBranchTarget(Module &module, std::string target, std::vector<Value> args) {
    labels.push_back(std::move(target));
    appendBranchArgs(*this, std::move(args));
    if (labelSymbols.size() + 1 != labels.size()) {
        labelSymbols.clear();
        labelSymbols.reserve(labels.size());
        for (const auto &label : labels)
            labelSymbols.push_back(label.empty() ? il::support::Symbol{}
                                                 : module.internIdentifier(label));
        return;
    }
    labelSymbols.push_back(labels.back().empty() ? il::support::Symbol{}
                                                 : module.internIdentifier(labels.back()));
}

/// @brief Remove all branch target labels, arguments, and symbol sidecars.
/// @post @ref labels, @ref brArgs, and @ref labelSymbols are empty.
void Instr::clearBranchTargets() {
    labels.clear();
    brArgs.clear();
    labelSymbols.clear();
}

/// @brief Store explicit signature metadata for a call.indirect instruction.
/// @param retType Declared indirect-call return type.
/// @param paramTypes Declared indirect-call parameter types.
/// @param isVarArg True when the callee accepts variadic arguments.
/// @post Indirect signature fields are active and own the supplied values.
void Instr::setIndirectSignature(Type retType, std::vector<Type> paramTypes, bool isVarArg) {
    hasIndirectSignature = true;
    indirectRetType = std::move(retType);
    indirectParamTypes = std::move(paramTypes);
    indirectIsVarArg = isVarArg;
}

/// @brief Clear explicit call.indirect signature metadata.
/// @post Signature fields are reset to their inactive defaults.
void Instr::clearIndirectSignature() {
    hasIndirectSignature = false;
    indirectRetType = Type{};
    indirectParamTypes.clear();
    indirectIsVarArg = false;
}

/// @brief Clear direct-call, indirect-call, and call-attribute metadata.
/// @post Operand, result, opcode, type, and source-location fields are unchanged.
void Instr::clearCallMetadata() {
    clearDirectCallee();
    CallAttr = {};
    clearIndirectSignature();
}

/// @brief Retrieves the value tested by the switch instruction.
/// @param instr Switch instruction providing the scrutinee.
/// @return Reference to the scrutinee value.
/// @pre instr must satisfy requireSwitch().
/// @pre instr.operands must contain at least one entry representing the scrutinee.
const Value &switchScrutinee(const Instr &instr) {
    static const Value kInvalidValue = Value::constInt(0);
    if (!requireSwitch(instr))
        return kInvalidValue;
    assert(!instr.operands.empty());
    if (instr.operands.empty())
        throw std::logic_error("switch instruction is missing scrutinee");
    return instr.operands.front();
}

/// @brief Retrieves the label for the default branch of a switch.
/// @param instr Switch instruction containing the default label.
/// @return Reference to the default branch label string.
/// @pre instr must satisfy requireSwitch().
/// @pre instr.labels must contain at least one entry representing the default label.
const std::string &switchDefaultLabel(const Instr &instr) {
    static const std::string kEmptyLabel;
    if (!requireSwitch(instr))
        return kEmptyLabel;
    assert(!instr.labels.empty());
    if (instr.labels.empty())
        throw std::logic_error("switch instruction is missing default label");
    return instr.labels.front();
}

/// @brief Retrieves the argument vector associated with the default branch.
/// @param instr Switch instruction containing branch arguments.
/// @return Reference to the default branch argument list (possibly empty).
/// @pre instr must satisfy requireSwitch().
const std::vector<Value> &switchDefaultArgs(const Instr &instr) {
    return argsOrEmpty(instr, 0);
}

/// @brief Determines how many non-default cases exist for a switch.
/// @param instr Switch instruction to inspect.
/// @return Number of explicit case labels in the instruction.
/// @pre instr must satisfy requireSwitch().
size_t switchCaseCount(const Instr &instr) {
    if (!requireSwitch(instr))
        return 0;
    if (instr.labels.empty())
        throw std::logic_error("switch instruction is missing default label");
    return instr.labels.size() - 1;
}

/// @brief Retrieves the scrutinee value matched by a specific case.
/// @param instr Switch instruction containing the case information.
/// @param index Zero-based index selecting the desired case.
/// @return Reference to the case's comparison value.
/// @pre instr must satisfy requireSwitch().
/// @pre index must be less than switchCaseCount(instr).
/// @pre instr.operands must contain the scrutinee followed by case values.
const Value &switchCaseValue(const Instr &instr, size_t index) {
    static const Value kInvalidValue = Value::constInt(0);
    if (!requireSwitch(instr))
        return kInvalidValue;
    if (index >= switchCaseCount(instr) || instr.operands.size() <= index + 1)
        throw std::out_of_range("switch case value index out of range");
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
const std::string &switchCaseLabel(const Instr &instr, size_t index) {
    static const std::string kEmptyLabel;
    if (!requireSwitch(instr))
        return kEmptyLabel;
    if (index >= switchCaseCount(instr) || instr.labels.size() <= index + 1)
        throw std::out_of_range("switch case label index out of range");
    assert(index < switchCaseCount(instr));
    return instr.labels[index + 1];
}

/// @brief Retrieves the argument vector passed to a specific switch case target.
/// @param instr Switch instruction containing branch arguments.
/// @param index Zero-based index selecting the desired case.
/// @return Reference to the argument list for the selected case (possibly empty).
/// @pre instr must satisfy requireSwitch().
/// @pre index must be less than switchCaseCount(instr).
const std::vector<Value> &switchCaseArgs(const Instr &instr, size_t index) {
    return argsOrEmpty(instr, index + 1);
}

} // namespace il::core
