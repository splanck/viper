// File: src/il/verify/FunctionVerifier.cpp
// Purpose: Implements function-level verification by coordinating block and instruction checks.
// Key invariants: Functions must start with an entry block, maintain unique labels, and respect call signatures.
// Ownership/Lifetime: Operates on module-provided data; no allocations persist beyond verification.
// Links: docs/il-guide.md#reference

#include "il/verify/FunctionVerifier.hpp"

#include "il/core/BasicBlock.hpp"
#include "il/core/Extern.hpp"
#include "il/core/Function.hpp"
#include "il/core/Instr.hpp"
#include "il/core/Module.hpp"
#include "il/core/Type.hpp"
#include "il/verify/ControlFlowChecker.hpp"
#include "il/verify/InstructionChecker.hpp"
#include "il/verify/TypeInference.hpp"

#include <sstream>
#include <unordered_set>

using namespace il::core;

namespace il::verify
{
using il::support::Diag;
using il::support::Expected;
using il::support::makeError;

Expected<void> validateBlockParams_E(const Function &fn,
                                     const BasicBlock &bb,
                                     TypeInference &types,
                                     std::vector<unsigned> &paramIds);
Expected<void> checkBlockTerminators_E(const Function &fn, const BasicBlock &bb);
Expected<void> verifyOpcodeSignature_E(const Function &fn, const BasicBlock &bb, const Instr &instr);
Expected<void> verifyInstruction_E(const Function &fn,
                                   const BasicBlock &bb,
                                   const Instr &instr,
                                   const std::unordered_map<std::string, const Extern *> &externs,
                                   const std::unordered_map<std::string, const Function *> &funcs,
                                   TypeInference &types,
                                   DiagSink &sink);
Expected<void> verifyBr_E(const Function &fn,
                          const BasicBlock &bb,
                          const Instr &instr,
                          const std::unordered_map<std::string, const BasicBlock *> &blockMap,
                          TypeInference &types);
Expected<void> verifyCBr_E(const Function &fn,
                           const BasicBlock &bb,
                           const Instr &instr,
                           const std::unordered_map<std::string, const BasicBlock *> &blockMap,
                           TypeInference &types);
Expected<void> verifyRet_E(const Function &fn,
                           const BasicBlock &bb,
                           const Instr &instr,
                           TypeInference &types);

namespace
{

class ControlFlowStrategy final : public FunctionVerifier::InstructionStrategy
{
  public:
    bool matches(const Instr &instr) const override
    {
        return instr.op == Opcode::Br || instr.op == Opcode::CBr || instr.op == Opcode::Ret;
    }

    Expected<void> verify(const Function &fn,
                          const BasicBlock &bb,
                          const Instr &instr,
                          const std::unordered_map<std::string, const BasicBlock *> &blockMap,
                          const std::unordered_map<std::string, const Extern *> &,
                          const std::unordered_map<std::string, const Function *> &,
                          TypeInference &types,
                          DiagSink &) const override
    {
        switch (instr.op)
        {
            case Opcode::Br:
                return verifyBr_E(fn, bb, instr, blockMap, types);
            case Opcode::CBr:
                return verifyCBr_E(fn, bb, instr, blockMap, types);
            case Opcode::Ret:
                return verifyRet_E(fn, bb, instr, types);
            default:
                break;
        }
        return {};
    }
};

class DefaultInstructionStrategy final : public FunctionVerifier::InstructionStrategy
{
  public:
    bool matches(const Instr &) const override
    {
        return true;
    }

    Expected<void> verify(const Function &fn,
                          const BasicBlock &bb,
                          const Instr &instr,
                          const std::unordered_map<std::string, const BasicBlock *> &,
                          const std::unordered_map<std::string, const Extern *> &externs,
                          const std::unordered_map<std::string, const Function *> &funcs,
                          TypeInference &types,
                          DiagSink &sink) const override
    {
        return verifyInstruction_E(fn, bb, instr, externs, funcs, types, sink);
    }
};

} // namespace

FunctionVerifier::FunctionVerifier(const ExternMap &externs) : externs_(externs)
{
    strategies_.push_back(std::make_unique<ControlFlowStrategy>());
    strategies_.push_back(std::make_unique<DefaultInstructionStrategy>());
}

Expected<void> FunctionVerifier::run(const Module &module, DiagSink &sink)
{
    functionMap_.clear();

    for (const auto &fn : module.functions)
    {
        if (!functionMap_.emplace(fn.name, &fn).second)
            return Expected<void>{makeError({}, "duplicate function @" + fn.name)};
    }

    for (const auto &fn : module.functions)
        if (auto result = verifyFunction(fn, sink); !result)
            return result;

    return {};
}

Expected<void> FunctionVerifier::verifyFunction(const Function &fn, DiagSink &sink)
{
    if (fn.blocks.empty())
        return Expected<void>{makeError({}, formatFunctionDiag(fn, "function has no blocks"))};

    const std::string &firstLabel = fn.blocks.front().label;
    const bool isEntry = firstLabel == "entry" || firstLabel.rfind("entry_", 0) == 0;
    if (!isEntry)
        return Expected<void>{makeError({}, formatFunctionDiag(fn, "first block must be entry"))};

    if (auto it = externs_.find(fn.name); it != externs_.end())
    {
        const Extern *ext = it->second;
        bool sigOk = ext->retType.kind == fn.retType.kind && ext->params.size() == fn.params.size();
        if (sigOk)
        {
            for (size_t i = 0; i < ext->params.size(); ++i)
                if (ext->params[i].kind != fn.params[i].type.kind)
                    sigOk = false;
        }
        if (!sigOk)
            return Expected<void>{makeError({}, "function @" + fn.name + " signature mismatch with extern")};
    }

    std::unordered_set<std::string> labels;
    std::unordered_map<std::string, const BasicBlock *> blockMap;
    for (const auto &bb : fn.blocks)
    {
        if (!labels.insert(bb.label).second)
            return Expected<void>{makeError({}, formatFunctionDiag(fn, "duplicate label " + bb.label))};
        blockMap[bb.label] = &bb;
    }

    std::unordered_map<unsigned, Type> temps;
    for (const auto &param : fn.params)
        temps[param.id] = param.type;

    for (const auto &bb : fn.blocks)
        if (auto result = verifyBlock(fn, bb, blockMap, temps, sink); !result)
            return result;

    for (const auto &bb : fn.blocks)
        for (const auto &instr : bb.instructions)
            for (const auto &label : instr.labels)
                if (!labels.count(label))
                    return Expected<void>{makeError({}, formatFunctionDiag(fn, "unknown label " + label))};

    return {};
}

Expected<void> FunctionVerifier::verifyBlock(const Function &fn,
                                             const BasicBlock &bb,
                                             const std::unordered_map<std::string, const BasicBlock *> &blockMap,
                                             std::unordered_map<unsigned, Type> &temps,
                                             DiagSink &sink)
{
    std::unordered_set<unsigned> defined;
    for (const auto &entry : temps)
        defined.insert(entry.first);

    TypeInference types(temps, defined);

    std::vector<unsigned> paramIds;
    if (auto result = validateBlockParams_E(fn, bb, types, paramIds); !result)
        return result;

    for (const auto &instr : bb.instructions)
    {
        if (auto result = types.ensureOperandsDefined_E(fn, bb, instr); !result)
            return result;

        if (auto result = verifyInstruction(fn, bb, instr, blockMap, types, sink); !result)
            return result;

        if (isTerminator(instr.op))
            break;
    }

    if (auto result = checkBlockTerminators_E(fn, bb); !result)
        return result;

    for (unsigned id : paramIds)
        types.removeTemp(id);

    return {};
}

Expected<void> FunctionVerifier::verifyInstruction(const Function &fn,
                                                   const BasicBlock &bb,
                                                   const Instr &instr,
                                                   const std::unordered_map<std::string, const BasicBlock *> &blockMap,
                                                   TypeInference &types,
                                                   DiagSink &sink)
{
    if (auto result = verifyOpcodeSignature_E(fn, bb, instr); !result)
        return result;

    for (const auto &strategy : strategies_)
    {
        if (!strategy->matches(instr))
            continue;
        return strategy->verify(fn, bb, instr, blockMap, externs_, functionMap_, types, sink);
    }

    return Expected<void>{makeError({}, formatFunctionDiag(fn, "no instruction strategy for op"))};
}

std::string FunctionVerifier::formatFunctionDiag(const Function &fn, std::string_view message) const
{
    std::ostringstream oss;
    oss << fn.name;
    if (!message.empty())
        oss << ": " << message;
    return oss.str();
}

} // namespace il::verify
