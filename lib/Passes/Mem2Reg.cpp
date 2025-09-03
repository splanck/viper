// File: lib/Passes/Mem2Reg.cpp
// Purpose: Implement simple alloca promotion to SSA (mem2reg v1).
// Key invariants: Only promotes allocas of i64, f64, or i1 with a single store dominating all loads
// and no escaped addresses. Ownership/Lifetime: Operates in place on the module, removing dead
// allocas and stores. Links: docs/passes/mem2reg.md

#include "Passes/Mem2Reg.h"
#include "Analysis/Dominators.h"
#include <algorithm>
#include <unordered_map>

using namespace il::core;

namespace viper::passes
{
namespace
{
struct LoadRef
{
    BasicBlock *block;
    std::size_t index;
    unsigned result;
};

struct AllocaInfo
{
    BasicBlock *allocaBlock{nullptr};
    unsigned allocaId{0};
    BasicBlock *storeBlock{nullptr};
    std::size_t storeIndex{0};
    Value storedValue{};
    Type storedType{};
    std::vector<LoadRef> loads;
    bool addressTaken{false};
    bool hasStore{false};
    bool multipleStores{false};
};

static void replaceAllUses(Function &F, unsigned id, const Value &v)
{
    for (auto &B : F.blocks)
        for (auto &I : B.instructions)
            for (auto &Op : I.operands)
                if (Op.kind == Value::Kind::Temp && Op.id == id)
                    Op = v;
}
} // namespace

void mem2reg(Module &M)
{
    for (auto &F : M.functions)
    {
        analysis::DomTree DT = analysis::computeDominatorTree(F);
        std::unordered_map<unsigned, AllocaInfo> infos;

        for (auto &B : F.blocks)
        {
            for (std::size_t i = 0; i < B.instructions.size(); ++i)
            {
                Instr &I = B.instructions[i];
                if (I.op == Opcode::Alloca && I.result)
                {
                    AllocaInfo AI;
                    AI.allocaBlock = &B;
                    AI.allocaId = *I.result;
                    infos[AI.allocaId] = AI;
                }

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
                        if (AI.hasStore)
                            AI.multipleStores = true;
                        else
                        {
                            AI.hasStore = true;
                            AI.storeBlock = &B;
                            AI.storeIndex = i;
                            AI.storedValue = I.operands[1];
                            AI.storedType = I.type;
                        }
                    }
                    else if (I.op == Opcode::Load && oi == 0)
                    {
                        AI.loads.push_back(LoadRef{&B, i, *I.result});
                        AI.storedType = I.type;
                    }
                    else
                    {
                        AI.addressTaken = true;
                    }
                }
            }
        }

        for (auto &[id, AI] : infos)
        {
            if (AI.addressTaken || AI.multipleStores || !AI.hasStore)
                continue;
            if (AI.storedType.kind != Type::Kind::I64 && AI.storedType.kind != Type::Kind::F64 &&
                AI.storedType.kind != Type::Kind::I1)
                continue;

            bool domOk = true;
            for (const auto &LR : AI.loads)
            {
                if (LR.block == AI.storeBlock)
                {
                    if (AI.storeIndex >= LR.index)
                    {
                        domOk = false;
                        break;
                    }
                }
                else if (!DT.dominates(AI.storeBlock, LR.block))
                {
                    domOk = false;
                    break;
                }
            }
            if (!domOk)
                continue;

            // replace loads
            std::unordered_map<BasicBlock *, std::vector<std::size_t>> perBlock;
            for (const auto &LR : AI.loads)
                perBlock[LR.block].push_back(LR.index);
            for (auto &[B, idxs] : perBlock)
            {
                std::sort(idxs.rbegin(), idxs.rend());
                for (std::size_t idx : idxs)
                {
                    Instr &LI = B->instructions[idx];
                    if (LI.result)
                        replaceAllUses(F, *LI.result, AI.storedValue);
                    B->instructions.erase(B->instructions.begin() + idx);
                }
            }

            // remove store
            for (std::size_t i = 0; i < AI.storeBlock->instructions.size(); ++i)
            {
                Instr &S = AI.storeBlock->instructions[i];
                if (S.op == Opcode::Store && S.operands.size() >= 1 &&
                    S.operands[0].kind == Value::Kind::Temp && S.operands[0].id == id)
                {
                    AI.storeBlock->instructions.erase(AI.storeBlock->instructions.begin() + i);
                    break;
                }
            }

            // remove alloca
            for (std::size_t i = 0; i < AI.allocaBlock->instructions.size(); ++i)
            {
                Instr &A = AI.allocaBlock->instructions[i];
                if (A.op == Opcode::Alloca && A.result && *A.result == id)
                {
                    AI.allocaBlock->instructions.erase(AI.allocaBlock->instructions.begin() + i);
                    break;
                }
            }
        }
    }
}

} // namespace viper::passes
