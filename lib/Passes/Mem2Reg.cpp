// File: lib/Passes/Mem2Reg.cpp
// Purpose: Implement alloca promotion to SSA using block parameters (mem2reg v2).
// Key invariants: Only handles i64/f64/i1 allocas with no escaped addresses and
// runs only on acyclic CFGs. Ownership/Lifetime: Mutates the module in place,
// introducing block params and branch args while removing allocas/loads/stores.
// Links: docs/passes/mem2reg.md

#include "Passes/Mem2Reg.h"
#include "Analysis/CFG.h"
#include <algorithm>
#include <optional>
#include <unordered_map>

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

void mem2reg(Module &M)
{
    analysis::setModule(M);
    for (auto &F : M.functions)
    {
        if (!analysis::isAcyclic(F))
            continue; // v2 handles only DAG CFGs

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

        if (infos.size() != 1)
            continue; // TODO: handle multiple allocas

        unsigned nextId = nextTempId(F);
        std::vector<unsigned> orderIds;
        for (auto &[id, _] : infos)
            orderIds.push_back(id);
        std::sort(orderIds.begin(), orderIds.end());

        for (unsigned id : orderIds)
        {
            AllocaInfo &AI = infos[id];
            if (AI.addressTaken || !AI.hasStore)
                continue;
            if (AI.type.kind != Type::Kind::I64 && AI.type.kind != Type::Kind::F64 &&
                AI.type.kind != Type::Kind::I1)
                continue;

            std::unordered_map<BasicBlock *, unsigned> paramIndex;
            auto topo = analysis::topoOrder(F);
            for (auto *B : topo)
            {
                std::optional<Value> current;
                if (auto it = paramIndex.find(B); it != paramIndex.end())
                    current = Value::temp(B->params[it->second].id);

                for (std::size_t i = 0; i < B->instructions.size();)
                {
                    Instr &I = B->instructions[i];
                    if (I.op == Opcode::Alloca && I.result && *I.result == id)
                    {
                        B->instructions.erase(B->instructions.begin() + i);
                        continue;
                    }
                    if (I.op == Opcode::Load && I.operands.size() &&
                        I.operands[0].kind == Value::Kind::Temp && I.operands[0].id == id)
                    {
                        if (current && I.result)
                            replaceAllUses(F, *I.result, *current);
                        B->instructions.erase(B->instructions.begin() + i);
                        continue;
                    }
                    if (I.op == Opcode::Store && I.operands.size() > 1 &&
                        I.operands[0].kind == Value::Kind::Temp && I.operands[0].id == id)
                    {
                        current = I.operands[1];
                        B->instructions.erase(B->instructions.begin() + i);
                        continue;
                    }
                    ++i;
                }

                if (!current)
                    continue;
                auto succs = analysis::successors(*B);
                for (auto *S : succs)
                {
                    unsigned pIdx;
                    if (auto it = paramIndex.find(S); it != paramIndex.end())
                        pIdx = it->second;
                    else
                    {
                        Param p;
                        p.name = "a" + std::to_string(id);
                        p.type = AI.type;
                        p.id = nextId++;
                        pIdx = S->params.size();
                        S->params.push_back(p);
                        paramIndex[S] = pIdx;
                    }

                    Instr &term = B->instructions.back();
                    std::size_t target = 0;
                    for (; target < term.labels.size(); ++target)
                        if (term.labels[target] == S->label)
                            break;
                    if (term.brArgs.size() < term.labels.size())
                        term.brArgs.resize(term.labels.size());
                    auto &args = term.brArgs[target];
                    if (args.size() <= pIdx)
                        args.resize(pIdx + 1);
                    args[pIdx] = *current;
                }
            }
        }
    }
}

} // namespace viper::passes
