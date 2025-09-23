// File: src/il/transform/DCE.cpp
// Purpose: Implement trivial dead-code elimination and block param cleanup.
// Key invariants: Simplifications preserve verifier correctness.
// Ownership/Lifetime: Mutates the module in place.
// Links: docs/codemap.md

#include "il/transform/DCE.hpp"
#include "il/core/Function.hpp"
#include "il/core/Instr.hpp"
#include "il/core/Module.hpp"
#include "il/core/Value.hpp"
#include <unordered_map>
#include <unordered_set>

using namespace il::core;

namespace il::transform
{
/// \brief Count temporary identifier uses within a function.
/// \param F Function whose temporaries are inspected.
/// \return Map from temporary id to number of references.
/// Algorithm: seeds the map with block parameters then scans every
/// instruction and branch argument, bumping the count for each temporary
/// operand encountered.
/// Data structures: std::unordered_map for the id→count mapping.
/// Mutations: none; purely analyzes \p F.
/// Limitations: purely syntactic and only considers temporaries.
static std::unordered_map<unsigned, size_t> countUses(Function &F)
{
    std::unordered_map<unsigned, size_t> uses;
    auto touch = [&](unsigned id) { uses[id]++; };
    for (auto &B : F.blocks)
    {
        for (auto &p : B.params)
            uses[p.id];
        for (auto &I : B.instructions)
        {
            for (auto &Op : I.operands)
                if (Op.kind == Value::Kind::Temp)
                    touch(Op.id);
            for (auto &ArgList : I.brArgs)
                for (auto &Arg : ArgList)
                    if (Arg.kind == Value::Kind::Temp)
                        touch(Arg.id);
        }
    }
    return uses;
}

/// \brief Eliminate trivially dead code from a module.
/// \param M Module simplified in place.
/// Steps:
/// 1. Count temp uses via countUses().
/// 2. Track allocas and whether a load exists for each.
/// 3. Remove loads with unused results, stores to never-loaded temps, and
///    allocas never loaded.
/// 4. Erase block parameters that have zero uses and adjust branch args.
/// Data structures: unordered maps for temp use counts and load presence.
/// Mutation: erases instructions and block params within \p M.
/// Limitations: handles only local, trivially dead loads/stores/allocas and
/// unused block params; no global or side-effect analysis.
void dce(Module &M)
{
    for (auto &F : M.functions)
    {
        auto uses = countUses(F);
        // Gather allocas and whether they have loads
        std::unordered_map<unsigned, bool> hasLoad;
        for (auto &B : F.blocks)
            for (auto &I : B.instructions)
            {
                if (I.op == Opcode::Alloca && I.result)
                    hasLoad[*I.result] = false;
                if (I.op == Opcode::Load && !I.operands.empty() &&
                    I.operands[0].kind == Value::Kind::Temp)
                    hasLoad[I.operands[0].id] = true;
            }

        // Remove dead loads/stores/allocas
        for (auto &B : F.blocks)
        {
            for (std::size_t i = 0; i < B.instructions.size();)
            {
                Instr &I = B.instructions[i];
                if (I.op == Opcode::Load && I.result && uses[*I.result] == 0)
                {
                    B.instructions.erase(B.instructions.begin() + i);
                    continue;
                }
                if (I.op == Opcode::Store && !I.operands.empty() &&
                    I.operands[0].kind == Value::Kind::Temp &&
                    hasLoad.find(I.operands[0].id) != hasLoad.end() && !hasLoad[I.operands[0].id])
                {
                    B.instructions.erase(B.instructions.begin() + i);
                    continue;
                }
                if (I.op == Opcode::Alloca && I.result &&
                    hasLoad.find(*I.result) != hasLoad.end() && !hasLoad[*I.result])
                {
                    B.instructions.erase(B.instructions.begin() + i);
                    continue;
                }
                ++i;
            }
        }

        // Remove unused block params
        for (auto &B : F.blocks)
        {
            for (int idx = static_cast<int>(B.params.size()) - 1; idx >= 0; --idx)
            {
                unsigned id = B.params[idx].id;
                if (uses[id] != 0)
                    continue;
                B.params.erase(B.params.begin() + idx);
                for (auto &PB : F.blocks)
                    for (auto &I : PB.instructions)
                        if ((I.op == Opcode::Br || I.op == Opcode::CBr))
                        {
                            for (std::size_t l = 0; l < I.labels.size(); ++l)
                                if (I.labels[l] == B.label && I.brArgs.size() > l &&
                                    I.brArgs[l].size() > static_cast<std::size_t>(idx))
                                    I.brArgs[l].erase(I.brArgs[l].begin() + idx);
                        }
            }
        }
    }
}

} // namespace il::transform
