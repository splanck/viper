//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE in the project root for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/il/verify/BranchVerifier.cpp
// Purpose: Provide reusable validation routines for branch-like terminators
//          and return instructions used by the IL verifier.
// Key invariants: Successor argument lists must match block parameter arity and
//                 types, switch cases remain unique, and return instructions
//                 honour their surrounding function signature.
// Ownership/Lifetime: Operates on caller-owned IL data structures and emits
//                     diagnostics through Expected objects without retaining
//                     references.
// Links: docs/il-guide.md#reference, docs/architecture.md#il-verify
//
//===----------------------------------------------------------------------===//

#include "il/verify/BranchVerifier.hpp"

#include "il/core/BasicBlock.hpp"
#include "il/core/Function.hpp"
#include "il/core/Instr.hpp"
#include "il/core/Type.hpp"
#include "il/verify/DiagFormat.hpp"
#include "il/verify/TypeInference.hpp"

#include <limits>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

using namespace il::core;

namespace il::verify
{
using il::support::Expected;
using il::support::makeError;

namespace
{

/// @brief Validates that a branch transfers arguments compatible with its target block.
///
/// @details Ensures that the count of forwarded operands equals the destination
///          block parameter list and that each operand's inferred type matches
///          the declared parameter kind. Diagnostics are routed through the
///          @ref il::support::Expected channel so callers can propagate
///          structured errors to the verifier's sink.
///
/// @param fn Function containing the branch instruction.
/// @param bb Basic block holding @p instr.
/// @param instr Branch-like instruction whose arguments are validated.
/// @param target Destination block receiving the branch arguments.
/// @param args Optional branch argument vector aligned with @p target params.
/// @param label Label string associated with the evaluated branch edge.
/// @param types Inference context used to obtain operand types for comparison.
Expected<void> verifyBranchArgs(const Function &fn,
                                const BasicBlock &bb,
                                const Instr &instr,
                                const BasicBlock &target,
                                const std::vector<Value> *args,
                                std::string_view label,
                                TypeInference &types)
{
    size_t argCount = args ? args->size() : 0;
    if (argCount != target.params.size())
        return Expected<void>{makeError(
            instr.loc,
            formatInstrDiag(
                fn, bb, instr, "branch arg count mismatch for label " + std::string(label)))};

    for (size_t i = 0; i < argCount; ++i)
    {
        if (types.valueType((*args)[i]).kind != target.params[i].type.kind)
            return Expected<void>{
                makeError(instr.loc,
                          formatInstrDiag(
                              fn, bb, instr, "arg type mismatch for label " + std::string(label)))};
    }
    return {};
}

} // namespace

/// @brief Verifies an unconditional branch instruction forwards correct operands and target.
///
/// @details Checks that unconditional branches specify exactly one successor
///          label and no immediate operands, then delegates to
///          @ref verifyBranchArgs to validate argument forwarding. Any mismatch
///          results in a diagnostic identifying the offending branch edge.
///
/// @param fn Function currently being verified.
/// @param bb Block containing the unconditional branch.
/// @param instr The branch instruction to verify.
/// @param blockMap Lookup table mapping block labels to their definitions.
/// @param types Type inference cache supplying operand type information.
Expected<void> verifyBr_E(const Function &fn,
                          const BasicBlock &bb,
                          const Instr &instr,
                          const std::unordered_map<std::string, const BasicBlock *> &blockMap,
                          TypeInference &types)
{
    bool argsOk = instr.operands.empty() && instr.labels.size() == 1;
    if (!argsOk)
        return Expected<void>{
            makeError(instr.loc, formatInstrDiag(fn, bb, instr, "branch mismatch"))};

    if (auto it = blockMap.find(instr.labels[0]); it != blockMap.end())
    {
        const BasicBlock &target = *it->second;
        const std::vector<Value> *argsVec = !instr.brArgs.empty() ? &instr.brArgs[0] : nullptr;
        if (auto result = verifyBranchArgs(fn, bb, instr, target, argsVec, instr.labels[0], types);
            !result)
            return result;
    }

    return {};
}

/// @brief Verifies a conditional branch instruction's condition and successor arguments.
///
/// @details Ensures the condition operand exists and infers to @c i1, verifies
///          that two successor labels accompany the opcode, and checks branch
///          arguments for each edge using @ref verifyBranchArgs. Diagnostics
///          highlight whether the condition or successor metadata is malformed.
///
/// @param fn Function currently being verified.
/// @param bb Block containing the conditional branch.
/// @param instr Conditional branch instruction under validation.
/// @param blockMap Mapping from block labels to their resolved blocks.
/// @param types Type inference cache used for operand type queries.
Expected<void> verifyCBr_E(const Function &fn,
                           const BasicBlock &bb,
                           const Instr &instr,
                           const std::unordered_map<std::string, const BasicBlock *> &blockMap,
                           TypeInference &types)
{
    bool condOk = instr.operands.size() == 1 && instr.labels.size() == 2 &&
                  types.valueType(instr.operands[0]).kind == Type::Kind::I1;
    if (condOk)
    {
        for (size_t t = 0; t < 2; ++t)
        {
            auto it = blockMap.find(instr.labels[t]);
            if (it == blockMap.end())
                continue;
            const BasicBlock &target = *it->second;
            const std::vector<Value> *argsVec =
                instr.brArgs.size() > t ? &instr.brArgs[t] : nullptr;
            if (auto result =
                    verifyBranchArgs(fn, bb, instr, target, argsVec, instr.labels[t], types);
                !result)
                return result;
        }
        return {};
    }

    return Expected<void>{
        makeError(instr.loc, formatInstrDiag(fn, bb, instr, "conditional branch mismatch"))};
}

/// @brief Validates switch.i32 structure, operand types, and branch argument conformity.
///
/// @details Validates scrutinee presence and type, ensures a default label and
///          matching argument vectors exist, and enforces unique 32-bit case
///          values. Each successor receives argument validation through
///          @ref verifyBranchArgs. Duplicate cases or mismatched signatures
///          surface as structured diagnostics.
///
/// @param fn Function currently being verified.
/// @param bb Block containing the switch instruction.
/// @param instr switch.i32 instruction whose structure is examined.
/// @param blockMap Lookup for resolving target blocks referenced by labels.
/// @param types Type inference context providing operand type data.
Expected<void> verifySwitchI32_E(const Function &fn,
                                 const BasicBlock &bb,
                                 const Instr &instr,
                                 const std::unordered_map<std::string, const BasicBlock *> &blockMap,
                                 TypeInference &types)
{
    if (instr.operands.empty())
    {
        return Expected<void>{
            makeError(instr.loc, formatInstrDiag(fn, bb, instr, "switch.i32 missing scrutinee"))};
    }

    if (types.valueType(switchScrutinee(instr)).kind != Type::Kind::I32)
    {
        return Expected<void>{
            makeError(instr.loc, formatInstrDiag(fn, bb, instr, "switch.i32 scrutinee must be i32"))};
    }

    if (instr.labels.empty())
    {
        return Expected<void>{
            makeError(instr.loc, formatInstrDiag(fn, bb, instr, "switch.i32 missing default"))};
    }

    if (instr.brArgs.size() != instr.labels.size())
    {
        return Expected<void>{makeError(instr.loc,
                                        formatInstrDiag(fn,
                                                        bb,
                                                        instr,
                                                        "switch.i32 branch argument vector count mismatch"))};
    }

    const std::vector<Value> *defaultArgs = nullptr;
    if (!instr.brArgs.empty())
        defaultArgs = &instr.brArgs.front();

    const size_t caseCount = switchCaseCount(instr);
    if (instr.operands.size() != caseCount + 1)
    {
        return Expected<void>{makeError(instr.loc,
                                        formatInstrDiag(fn,
                                                        bb,
                                                        instr,
                                                        "switch.i32 operands mismatch cases"))};
    }

    if (auto it = blockMap.find(switchDefaultLabel(instr)); it != blockMap.end())
    {
        if (auto result =
                verifyBranchArgs(fn, bb, instr, *it->second, defaultArgs, switchDefaultLabel(instr), types);
            !result)
            return result;
    }

    std::unordered_set<int32_t> seen;
    seen.reserve(caseCount);

    for (size_t idx = 0; idx < caseCount; ++idx)
    {
        const Value &caseValue = switchCaseValue(instr, idx);
        if (caseValue.kind != Value::Kind::ConstInt)
        {
            return Expected<void>{makeError(instr.loc,
                                            formatInstrDiag(fn,
                                                            bb,
                                                            instr,
                                                            "switch.i32 case must be const i32"))};
        }
        if (caseValue.i64 < std::numeric_limits<int32_t>::min() ||
            caseValue.i64 > std::numeric_limits<int32_t>::max())
        {
            return Expected<void>{makeError(instr.loc,
                                            formatInstrDiag(fn,
                                                            bb,
                                                            instr,
                                                            "switch.i32 case out of i32 range"))};
        }

        const int32_t key = static_cast<int32_t>(caseValue.i64);
        if (!seen.insert(key).second)
        {
            return Expected<void>{makeError(instr.loc,
                                            formatInstrDiag(fn,
                                                            bb,
                                                            instr,
                                                            "duplicate switch.i32 case"))};
        }

        const std::string &label = switchCaseLabel(instr, idx);
        auto it = blockMap.find(label);
        if (it == blockMap.end())
            continue;
        const std::vector<Value> *caseArgs = nullptr;
        if (!instr.brArgs.empty())
            caseArgs = &instr.brArgs[idx + 1];
        if (auto result = verifyBranchArgs(fn, bb, instr, *it->second, caseArgs, label, types); !result)
            return result;
    }

    return {};
}

/// @brief Validate that a return instruction matches its function signature.
///
/// @details Functions returning void must not carry operands, while
///          non-void functions require exactly one operand whose inferred type
///          equals the function's declared return kind. Violations are reported
///          using formatted diagnostics tied to the instruction's source
///          location.
///
/// @param fn Function owning the instruction.
/// @param bb Basic block containing the @c ret.
/// @param instr Return instruction being checked.
/// @param types Type inference cache that provides operand kinds.
/// @return Empty on success or a diagnostic describing the mismatch.
Expected<void> verifyRet_E(const Function &fn,
                           const BasicBlock &bb,
                           const Instr &instr,
                           TypeInference &types)
{
    if (fn.retType.kind == Type::Kind::Void)
    {
        if (!instr.operands.empty())
            return Expected<void>{
                makeError(instr.loc, formatInstrDiag(fn, bb, instr, "ret void with value"))};
        return {};
    }

    if (instr.operands.size() == 1 && types.valueType(instr.operands[0]).kind == fn.retType.kind)
        return {};

    return Expected<void>{
        makeError(instr.loc, formatInstrDiag(fn, bb, instr, "ret value type mismatch"))};
}

} // namespace il::verify
