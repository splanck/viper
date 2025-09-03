// File: lib/Passes/Mem2Reg.cpp
// Purpose: Implement alloca promotion to SSA using block parameters with the
// seal-and-rename algorithm (mem2reg v3).
// Key invariants: Handles i64/f64/i1 allocas whose addresses do not escape,
// supporting arbitrary control-flow including loops. Ownership/Lifetime:
// Mutates the module in place, introducing block params and branch args while
// removing allocas/loads/stores.
// Links: docs/passes/mem2reg.md

#include "Passes/Mem2Reg.h"
#include "Analysis/CFG.h"
#include <algorithm>
#include <functional>
#include <optional>
#include <queue>
#include <unordered_map>
#include <unordered_set>

using namespace il::core;

namespace viper::passes
{
namespace
{
struct AllocaInfo
{
    BasicBlock *block{nullptr};
    unsigned id{0};
    Type type{};
    bool addressTaken{false};
    bool hasStore{false};
};

static void replaceAllUses(Function &F, unsigned id, const Value &v)
{
    for (auto &B : F.blocks)
        for (auto &I : B.instructions)
        {
            for (auto &Op : I.operands)
                if (Op.kind == Value::Kind::Temp && Op.id == id)
                    Op = v;
            for (auto &argList : I.brArgs)
                for (auto &Arg : argList)
                    if (Arg.kind == Value::Kind::Temp && Arg.id == id)
                        Arg = v;
        }
}

static unsigned nextTempId(Function &F)
{
    unsigned next = 0;
    auto update = [&](unsigned v) { next = std::max(next, v + 1); };
    for (auto &p : F.params)
        update(p.id);
    for (auto &B : F.blocks)
    {
        for (auto &p : B.params)
            update(p.id);
        for (auto &I : B.instructions)
        {
            if (I.result)
                update(*I.result);
            for (auto &Op : I.operands)
                if (Op.kind == Value::Kind::Temp)
                    update(Op.id);
            for (auto &argList : I.brArgs)
                for (auto &Arg : argList)
                    if (Arg.kind == Value::Kind::Temp)
                        update(Arg.id);
        }
    }
    return next;
}
} // namespace

void mem2reg(Module &M, Mem2RegStats *stats)
{
    analysis::setModule(M);
    for (auto &F : M.functions)
    {
        // Gather promotable allocas
        std::unordered_map<unsigned, AllocaInfo> infos;
        for (auto &B : F.blocks)
            for (auto &I : B.instructions)
                if (I.op == Opcode::Alloca && I.result)
                    infos[*I.result] = AllocaInfo{&B, *I.result, Type{}, false, false};

        for (auto &B : F.blocks)
        {
            for (auto &I : B.instructions)
            {
                for (std::size_t oi = 0; oi < I.operands.size(); ++oi)
                {
                    Value &Op = I.operands[oi];
                    if (Op.kind != Value::Kind::Temp)
                        continue;
                    auto it = infos.find(Op.id);
                    if (it == infos.end())
                        continue;
                    AllocaInfo &AI = it->second;
                    if (I.op == Opcode::Store && oi == 0)
                    {
                        AI.hasStore = true;
                        AI.type = I.type;
                    }
                    else if (I.op == Opcode::Load && oi == 0)
                    {
                        AI.type = I.type;
                    }
                    else
                    {
                        AI.addressTaken = true;
                    }
                }
            }
        }

        if (stats)
            stats->promotedVars += infos.size();
        if (infos.size() != 1)
            continue; // TODO: handle multiple allocas

        struct VarState
        {
            Type type{};
            std::unordered_map<BasicBlock *, Value> defs;
        };

        std::unordered_map<unsigned, VarState> vars;
        for (auto &[id, AI] : infos)
        {
            if (AI.addressTaken || !AI.hasStore)
                continue;
            if (AI.type.kind != Type::Kind::I64 && AI.type.kind != Type::Kind::F64 &&
                AI.type.kind != Type::Kind::I1)
                continue;
            vars[id] = VarState{AI.type, {}};
        }

        if (vars.empty())
            continue;

        unsigned nextId = nextTempId(F);

        struct BlockState
        {
            bool sealed = false;
            unsigned totalPreds = 0;
            unsigned seenPreds = 0;
            std::unordered_map<unsigned, unsigned> params; // var -> param index
            std::unordered_set<unsigned> incomplete;       // vars needing fixup
        };

        std::unordered_map<BasicBlock *, BlockState> blocks;
        for (auto &B : F.blocks)
        {
            BlockState bs;
            bs.totalPreds = analysis::predecessors(F, B).size();
            bs.sealed = bs.totalPreds == 0;
            blocks[&B] = bs;
        }

        auto ensureParam = [&](BasicBlock *B, unsigned varId)
        {
            BlockState &BS = blocks[B];
            auto it = BS.params.find(varId);
            if (it != BS.params.end())
                return it->second;
            Param p;
            p.name = "a" + std::to_string(varId);
            p.type = vars[varId].type;
            p.id = nextId++;
            unsigned idx = B->params.size();
            B->params.push_back(p);
            BS.params[varId] = idx;
            return idx;
        };

        auto addIncoming = [&](BasicBlock *B, unsigned varId, BasicBlock *Pred, const Value &val)
        {
            unsigned pIdx = ensureParam(B, varId);
            Instr &term = Pred->instructions.back();
            std::size_t target = 0;
            for (; target < term.labels.size(); ++target)
                if (term.labels[target] == B->label)
                    break;
            if (term.brArgs.size() < term.labels.size())
                term.brArgs.resize(term.labels.size());
            auto &args = term.brArgs[target];
            if (args.size() <= pIdx)
                args.resize(pIdx + 1);
            args[pIdx] = val;
        };

        std::function<Value(BasicBlock *, unsigned)> readVariable;
        std::function<Value(BasicBlock *, unsigned)> readVariableFromPreds;
        std::function<void(BasicBlock *)> seal;

        readVariableFromPreds = [&](BasicBlock *B, unsigned varId) -> Value
        {
            auto preds = analysis::predecessors(F, *B);
            if (preds.empty())
            {
                const Type &ty = vars[varId].type;
                return ty.kind == Type::Kind::F64 ? Value::constFloat(0.0) : Value::constInt(0);
            }
            unsigned pIdx = ensureParam(B, varId);
            Value paramVal = Value::temp(B->params[pIdx].id);
            for (auto *P : preds)
            {
                Value arg = readVariable(P, varId);
                addIncoming(B, varId, P, arg);
            }
            return paramVal;
        };

        readVariable = [&](BasicBlock *B, unsigned varId) -> Value
        {
            VarState &VS = vars[varId];
            if (auto it = VS.defs.find(B); it != VS.defs.end())
                return it->second;
            BlockState &BS = blocks[B];
            if (!BS.sealed)
            {
                unsigned pIdx = ensureParam(B, varId);
                Value v = Value::temp(B->params[pIdx].id);
                VS.defs[B] = v;
                BS.incomplete.insert(varId);
                return v;
            }
            Value v = readVariableFromPreds(B, varId);
            VS.defs[B] = v;
            return v;
        };

        seal = [&](BasicBlock *B)
        {
            BlockState &BS = blocks[B];
            if (BS.sealed)
                return;
            for (unsigned varId : BS.incomplete)
                readVariableFromPreds(B, varId);
            BS.incomplete.clear();
            BS.sealed = true;
        };

        std::queue<BasicBlock *> work;
        std::unordered_set<BasicBlock *> queued;
        if (!F.blocks.empty())
        {
            work.push(&F.blocks.front());
            queued.insert(&F.blocks.front());
        }

        while (!work.empty())
        {
            BasicBlock *B = work.front();
            work.pop();

            for (std::size_t i = 0; i < B->instructions.size();)
            {
                Instr &I = B->instructions[i];
                if (I.op == Opcode::Alloca && I.result && vars.count(*I.result))
                {
                    B->instructions.erase(B->instructions.begin() + i);
                    continue;
                }
                if (I.op == Opcode::Load && I.operands.size() &&
                    I.operands[0].kind == Value::Kind::Temp && vars.count(I.operands[0].id))
                {
                    unsigned varId = I.operands[0].id;
                    Value v = readVariable(B, varId);
                    if (I.result)
                        replaceAllUses(F, *I.result, v);
                    B->instructions.erase(B->instructions.begin() + i);
                    if (stats)
                        stats->removedLoads++;
                    continue;
                }
                if (I.op == Opcode::Store && I.operands.size() > 1 &&
                    I.operands[0].kind == Value::Kind::Temp && vars.count(I.operands[0].id))
                {
                    unsigned varId = I.operands[0].id;
                    vars[varId].defs[B] = I.operands[1];
                    B->instructions.erase(B->instructions.begin() + i);
                    if (stats)
                        stats->removedStores++;
                    continue;
                }
                ++i;
            }

            auto succs = analysis::successors(*B);
            for (auto *S : succs)
            {
                BlockState &SS = blocks[S];
                SS.seenPreds++;
                if (!queued.count(S))
                {
                    work.push(S);
                    queued.insert(S);
                }
                if (SS.seenPreds == SS.totalPreds)
                    seal(S);
            }
        }
    }
}

} // namespace viper::passes
