//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Provides the branch-specific verification helpers used by the IL verifier.
// The routines in this translation unit ensure that branch operands match the
// parameter lists of their destination blocks, that conditional branches have
// the proper predicate shape, and that return instructions honour the
// function's signature.  Collecting the logic here keeps the main verifier
// pipeline focused on structural traversal while these helpers enforce
// control-flow invariants.
//
//===----------------------------------------------------------------------===//
//
// @file
// @brief Branch, switch, and return validation for the IL verifier.
// @details Each helper operates on caller-owned IL objects and reports
//          diagnostics through @ref il::support::Expected so the verifier can
//          decide how to surface the failure.  The routines never cache
//          references beyond their call to avoid lifetime surprises.
//
// Links: docs/il-guide.md#reference

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

/// @brief Validate that the arguments attached to a branch edge match the
///        destination block signature.
///
/// @details Checks both the number of arguments and their inferred types.  When
///          a mismatch is observed the helper produces a diagnostic tied to the
///          offending instruction, allowing the caller to surface the failure via
///          @ref il::support::Expected.
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

/// @brief Verify that an unconditional `br` instruction is well-formed.
///
/// @details Ensures the instruction carries exactly one successor label, no
///          operand payload, and that any attached branch arguments match the
///          destination block parameters via @ref verifyBranchArgs.
///
/// @param fn Function currently being verified.
/// @param bb Block containing the unconditional branch.
/// @param instr Branch instruction to verify.
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

/// @brief Validate the operands and successor edges of a `cbr` instruction.
///
/// @details Confirms the condition operand exists and is an @c i1 value, checks
///          that exactly two labels are present, and verifies that each edge's
///          arguments match the destination signature through
///          @ref verifyBranchArgs.
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

/// @brief Validate the structure and operands of a `switch.i32` instruction.
///
/// @details Checks the scrutinee type, ensures the default label is present,
///          verifies that each case value is a unique signed 32-bit integer, and
///          confirms that branch arguments match the parameter lists of their
///          destination blocks.
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
        const auto &args = switchDefaultArgs(instr);
        if (auto result = verifyBranchArgs(fn, bb, instr, *it->second, &args, switchDefaultLabel(instr), types);
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
        const auto &args = switchCaseArgs(instr, idx);
        if (auto result = verifyBranchArgs(fn, bb, instr, *it->second, &args, label, types); !result)
            return result;
    }

    return {};
}

/// @brief Validate that a return instruction matches the function signature.
///
/// @details Void functions must omit operands, while functions with a return
///          type must supply exactly one operand whose inferred type matches the
///          declaration.
///
/// @param fn Function currently being verified.
/// @param bb Block containing the return.
/// @param instr Return instruction to check.
/// @param types Type inference helper describing operand types.
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
