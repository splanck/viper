//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Implements the trivial dead-code elimination pass used by the IL optimizer.
// The pass performs syntactic use counting, removes instructions whose results
// are never consumed, and prunes unused block parameters together with their
// corresponding branch arguments.  All mutations happen in place so callers can
// run DCE over a fully materialised module without rebuilding auxiliary data
// structures.
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Implements the dead-code elimination (DCE) pass for the IL pipeline.
/// @details The pass complements the lightweight optimiser by erasing trivially
///          dead temporaries, redundant stack traffic, and unused block
///          parameters.  It relies solely on syntactic information so that it
///          can run quickly and deterministically even in debug builds where
///          richer analysis infrastructure may be disabled.  The implementation
///          lives out of line to keep the public header minimal while providing a
///          single, well-documented reference for the pass' heuristics.

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
/// @brief Count how many times each temporary identifier is referenced.
///
/// @details The pass seeds the map with entries for block parameters so they
/// receive a zero count when unused, then walks every operand and branch
/// argument inside @p F.  Each encounter with a temporary identifier bumps the
/// associated slot, yielding an @c O(n) summary where @c n equals the total
/// number of operands scanned.  No control-flow reasoning is performed; the
/// counts purely reflect syntactic uses, which is sufficient for pruning dead
/// temporaries and block parameters later in the pass.
///
/// @param F Function whose temporaries are inspected.
/// @return Map from temporary id to number of references.
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

/// @brief Eliminate trivially dead instructions and block parameters.
///
/// @details The transformation iterates over every function in @p M and:
///   1. Builds use counts for temporaries via @ref countUses so later stages can
///      determine which results are never observed.
///   2. Records which @c alloca results are observed by @c load instructions to
///      distinguish genuine stack slots from those that only ever receive
///      stores.
///   3. Deletes loads whose results have zero uses, stores that write to never
///      loaded allocas, and allocas that are never read.  The instruction
///      traversal keeps indices stable by only advancing when no erasure
///      occurred.
///   4. Walks block parameters in reverse order, removing unused entries and
///      erasing the corresponding operands from predecessor branch argument
///      lists so SSA form remains consistent.
/// Instructions that may observe side effects (for example calls) are
/// intentionally untouched; the pass focuses solely on obvious dead temporaries
/// to keep compilation fast and predictable.  The in-place updates mean callers
/// can reuse existing module objects without re-running expensive analyses.
///
/// @param M Module simplified in place.
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
                        if (I.op == Opcode::Br || I.op == Opcode::CBr || I.op == Opcode::SwitchI32)
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
