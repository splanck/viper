// File: src/il/verify/BranchVerifier.cpp
// Purpose: Implement branch and return verification helpers shared by the IL verifier.
// License: Distributed under the MIT license alongside the main project license text.
// Key invariants: Branch instructions respect target parameter lists; returns match function
// signatures. Ownership/Lifetime: Operates on caller-owned IL data without persisting references
// beyond invocation. Links: docs/il-guide.md#reference

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
/// Ensures the provided @p args match @p target parameter arity and inferred types. When
/// mismatches occur, a formatted diagnostic tied to @p instr is returned via the
/// il::support::Expected error channel.
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
/// Validates operand/label counts for `br` instructions and checks arguments against the
/// resolved target block signature using verifyBranchArgs(). On any mismatch a diagnostic is
/// emitted through the Expected error result.
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
/// Ensures a single i1 condition operand, exactly two successor labels, and per-edge argument
/// compatibility as checked by verifyBranchArgs(). Errors propagate diagnostics describing the
/// offending edge or condition mismatch.
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
/// Checks that the scrutinee exists and is i32, that default and case labels are present, and
/// that each case operand is a unique 32-bit integer. Branch arguments for the default and each
/// case are compared against their targets using verifyBranchArgs(), returning diagnostics on
/// any inconsistencies.
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
