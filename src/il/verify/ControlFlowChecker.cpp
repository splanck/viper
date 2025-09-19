// File: src/il/verify/ControlFlowChecker.cpp
// Purpose: Implements control-flow specific IL verification helpers.
// Key invariants: Ensures terminators and branch arguments satisfy structural rules.
// Ownership/Lifetime: Operates with caller-provided verifier state.
// Links: docs/il-spec.md

#include "il/verify/ControlFlowChecker.hpp"
#include "il/core/BasicBlock.hpp"
#include "il/core/Extern.hpp"
#include "il/core/Function.hpp"
#include "il/core/Instr.hpp"
#include "il/core/Param.hpp"
#include <unordered_set>

using namespace il::core;

namespace il::verify
{

bool isTerminator(Opcode op)
{
    return op == Opcode::Br || op == Opcode::CBr || op == Opcode::Ret || op == Opcode::Trap;
}

bool validateBlockParams(const Function &fn,
                         const BasicBlock &bb,
                         TypeInference &types,
                         std::vector<unsigned> &paramIds,
                         std::ostream &err)
{
    bool ok = true;
    std::unordered_set<std::string> paramNames;
    for (const auto &param : bb.params)
    {
        if (!paramNames.insert(param.name).second)
        {
            err << fn.name << ":" << bb.label << ": duplicate param %" << param.name << "\n";
            ok = false;
        }
        if (param.type.kind == Type::Kind::Void)
        {
            err << fn.name << ":" << bb.label << ": param %" << param.name << " has void type\n";
            ok = false;
        }
        types.addTemp(param.id, param.type);
        paramIds.push_back(param.id);
    }
    return ok;
}

bool iterateBlockInstructions(VerifyInstrFn verifyInstrFn,
                              const Function &fn,
                              const BasicBlock &bb,
                              const std::unordered_map<std::string, const BasicBlock *> &blockMap,
                              const std::unordered_map<std::string, const Extern *> &externs,
                              const std::unordered_map<std::string, const Function *> &funcs,
                              TypeInference &types,
                              std::ostream &err)
{
    bool ok = true;
    for (const auto &instr : bb.instructions)
    {
        ok &= types.ensureOperandsDefined(fn, bb, instr, err);
        ok &= verifyInstrFn(fn, bb, instr, blockMap, externs, funcs, types, err);
        if (isTerminator(instr.op))
            break;
    }
    return ok;
}

bool checkBlockTerminators(const Function &fn, const BasicBlock &bb, std::ostream &err)
{
    if (bb.instructions.empty())
    {
        err << fn.name << ":" << bb.label << ": empty block\n";
        return false;
    }

    bool ok = true;
    bool seenTerm = false;
    for (const auto &instr : bb.instructions)
    {
        if (isTerminator(instr.op))
        {
            if (seenTerm)
            {
                err << fn.name << ":" << bb.label << ": " << makeSnippet(instr) << ": multiple terminators\n";
                ok = false;
                break;
            }
            seenTerm = true;
        }
        else if (seenTerm)
        {
            err << fn.name << ":" << bb.label << ": " << makeSnippet(instr) << ": instruction after terminator\n";
            ok = false;
            break;
        }
    }

    if (ok && !isTerminator(bb.instructions.back().op))
    {
        err << fn.name << ":" << bb.label << ": missing terminator\n";
        ok = false;
    }

    return ok;
}

bool verifyBr(const Function &fn,
              const BasicBlock &bb,
              const Instr &instr,
              const std::unordered_map<std::string, const BasicBlock *> &blockMap,
              TypeInference &types,
              std::ostream &err)
{
    bool ok = true;
    bool argsOk = instr.operands.empty() && instr.labels.size() == 1;
    if (!argsOk)
    {
        err << fn.name << ":" << bb.label << ": " << makeSnippet(instr) << ": branch mismatch\n";
        return false;
    }
    auto itB = blockMap.find(instr.labels[0]);
    if (itB != blockMap.end())
    {
        const BasicBlock &target = *itB->second;
        const std::vector<Value> *argsVec = instr.brArgs.size() > 0 ? &instr.brArgs[0] : nullptr;
        size_t argCount = argsVec ? argsVec->size() : 0;
        if (argCount != target.params.size())
        {
            err << fn.name << ":" << bb.label << ": " << makeSnippet(instr)
                << ": branch arg count mismatch for label " << instr.labels[0] << "\n";
            ok = false;
        }
        else
        {
            for (size_t i = 0; i < argCount; ++i)
                if (types.valueType((*argsVec)[i]).kind != target.params[i].type.kind)
                {
                    err << fn.name << ":" << bb.label << ": " << makeSnippet(instr)
                        << ": arg type mismatch for label " << instr.labels[0] << "\n";
                    ok = false;
                    break;
                }
        }
    }
    return ok;
}

bool verifyCBr(const Function &fn,
               const BasicBlock &bb,
               const Instr &instr,
               const std::unordered_map<std::string, const BasicBlock *> &blockMap,
               TypeInference &types,
               std::ostream &err)
{
    bool ok = true;
    bool condOk = instr.operands.size() == 1 && instr.labels.size() == 2 &&
                  types.valueType(instr.operands[0]).kind == Type::Kind::I1;
    if (!condOk)
    {
        err << fn.name << ":" << bb.label << ": " << makeSnippet(instr) << ": conditional branch mismatch\n";
        return false;
    }
    for (size_t t = 0; t < 2; ++t)
    {
        auto itB = blockMap.find(instr.labels[t]);
        if (itB == blockMap.end())
            continue;
        const BasicBlock &target = *itB->second;
        const std::vector<Value> *argsVec = instr.brArgs.size() > t ? &instr.brArgs[t] : nullptr;
        size_t argCount = argsVec ? argsVec->size() : 0;
        if (argCount != target.params.size())
        {
            err << fn.name << ":" << bb.label << ": " << makeSnippet(instr)
                << ": branch arg count mismatch for label " << instr.labels[t] << "\n";
            ok = false;
        }
        else
        {
            for (size_t i = 0; i < argCount; ++i)
                if (types.valueType((*argsVec)[i]).kind != target.params[i].type.kind)
                {
                    err << fn.name << ":" << bb.label << ": " << makeSnippet(instr)
                        << ": arg type mismatch for label " << instr.labels[t] << "\n";
                    ok = false;
                    break;
                }
        }
    }
    return ok;
}

bool verifyRet(const Function &fn,
               const BasicBlock &bb,
               const Instr &instr,
               TypeInference &types,
               std::ostream &err)
{
    bool ok = true;
    if (fn.retType.kind == Type::Kind::Void)
    {
        if (!instr.operands.empty())
        {
            err << fn.name << ":" << bb.label << ": " << makeSnippet(instr) << ": ret void with value\n";
            ok = false;
        }
    }
    else
    {
        if (instr.operands.size() != 1 || types.valueType(instr.operands[0]).kind != fn.retType.kind)
        {
            err << fn.name << ":" << bb.label << ": " << makeSnippet(instr) << ": ret value type mismatch\n";
            ok = false;
        }
    }
    return ok;
}

} // namespace il::verify
