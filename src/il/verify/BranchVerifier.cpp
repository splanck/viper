// File: src/il/verify/BranchVerifier.cpp
// Purpose: Implement branch and return verification helpers shared by the IL verifier.
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

#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

using namespace il::core;

namespace il::verify
{
using il::support::Expected;
using il::support::makeError;

namespace
{

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
