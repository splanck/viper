//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Implements the peephole optimisations that simplify short instruction
// sequences inside individual basic blocks.  The pass walks each function in a
// module, applies algebraic identity rules, and rewrites conditional branches
// whose predicate collapses to a constant value.  Transformations preserve the
// observable semantics of the module while eagerly removing redundant
// instructions so later passes operate on a smaller IR.
//
//===----------------------------------------------------------------------===//
//
/// @file
/// @brief Peephole simplification utilities for IL modules.
/// @details Centralises the helpers that identify literal operands, count SSA
///          uses, and rewrite branch instructions so that the high-level
///          peephole driver can focus on pass orchestration.

#include "il/transform/Peephole.hpp"
#include "il/core/Function.hpp"
#include "il/core/Instr.hpp"
#include "il/core/Module.hpp"
#include "il/core/Value.hpp"

using namespace il::core;

namespace il::transform
{

namespace
{

/// \brief Test whether a value is an integer constant and expose its payload.
///
/// The peephole rules only reason about literal integers. This helper centralises
/// the check and extraction so pattern matching can simply compare the numeric
/// payload in subsequent rules.
///
/// @param v      Candidate value operand.
/// @param out    Populated with the constant when the check succeeds.
/// @returns True when @p v is a @c ConstInt.
static bool isConstInt(const Value &v, long long &out)
{
    if (v.kind == Value::Kind::ConstInt)
    {
        out = v.i64;
        return true;
    }
    return false;
}

/// \brief Determine whether an operand equals a specific integer literal.
///
/// The helper reuses @ref isConstInt to recognise literal integers and then
/// performs the comparison against @p target.  Centralising the logic allows the
/// peephole rule table to specify literal matches declaratively without in-line
/// conditionals at each call site.
///
/// @param v       Operand to classify.
/// @param target  Required constant value.
/// @returns True when the operand is an integer constant identical to
///          @p target.
static bool isConstEq(const Value &v, long long target)
{
    long long c;
    return isConstInt(v, c) && c == target;
}

/// \brief Count how many times a temporary identifier is referenced in @p f.
///
/// The routine walks every instruction in every block, iterating each operand
/// vector and incrementing a counter whenever it encounters the requested
/// temporary.  Because the nested loops cover both regular operands and branch
/// arguments, the resulting count accurately reflects all SSA uses inside the
/// function, which the pass uses to guard single-use simplifications.
///
/// @param f   Function being optimised.
/// @param id  Temporary identifier to count.
/// @returns Number of uses observed for the identifier.
static size_t countUses(const Function &f, unsigned id)
{
    size_t uses = 0;
    for (const auto &b : f.blocks)
        for (const auto &in : b.instructions)
            for (const auto &op : in.operands)
                if (op.kind == Value::Kind::Temp && op.id == id)
                    ++uses;
    return uses;
}

/// \brief Substitute every use of a temporary with a replacement value.
///
/// Arithmetic identity rules forward an existing operand in place of the
/// computed result. Once a rule matches, this helper rewrites all uses before
/// the defining instruction is removed, preserving SSA-style data flow without
/// altering block structure.
///
/// @param f   Function whose operands should be updated.
/// @param id  Temporary identifier to replace.
/// @param v   Replacement value propagated to all uses.
static void replaceAll(Function &f, unsigned id, const Value &v)
{
    for (auto &b : f.blocks)
        for (auto &in : b.instructions)
            for (auto &op : in.operands)
                if (op.kind == Value::Kind::Temp && op.id == id)
                    op = v;
}

} // namespace

/// \brief Apply local simplifications to all functions in a module.
///
/// The pass performs two kinds of optimisation:
///  - Apply algebraic identity rules from @c kRules, forwarding constant-folded
///    operands and erasing the now-dead producer instruction.
///  - Simplify conditional branches whose predicate collapses to a known boolean
///    value, rewriting them into unconditional jumps.
///
/// When a branch is rewritten, the routine also prunes the untaken successor's
/// block parameters by trimming @c brArgs so the surviving edge keeps its
/// argument arity in sync with the callee block. Additionally, a branch-condition
/// definition is erased when the predicate was produced in the same block and
/// had a single use, ensuring we do not leave dead instructions behind. The
/// implementation intentionally limits itself to integer comparisons with
/// literal operands and does not chase values across blocks or through
/// non-literal arithmetic.
void peephole(Module &m)
{
    for (auto &f : m.functions)
    {
        for (auto &b : f.blocks)
        {
            for (size_t i = 0; i < b.instructions.size(); ++i)
            {
                Instr &in = b.instructions[i];
                if (in.op == Opcode::CBr)
                {
                    if (in.labels.size() == 2 && in.labels[0] == in.labels[1])
                    {
                        in.op = Opcode::Br;
                        in.labels = {in.labels[0]};
                        in.operands.clear();
                        if (in.brArgs.size() > 1)
                            in.brArgs.resize(1);
                        continue;
                    }
                    long long v;
                    bool known = false;
                    size_t defIdx = static_cast<size_t>(-1);
                    size_t uses = 0;
                    if (isConstInt(in.operands[0], v))
                    {
                        known = true;
                    }
                    else if (in.operands[0].kind == Value::Kind::Temp)
                    {
                        unsigned id = in.operands[0].id;
                        uses = countUses(f, id);
                        for (size_t j = 0; j < i; ++j)
                        {
                            Instr &def = b.instructions[j];
                            if (def.result && *def.result == id && def.operands.size() == 2)
                            {
                                long long l, r;
                                if (isConstInt(def.operands[0], l) &&
                                    isConstInt(def.operands[1], r))
                                {
                                    switch (def.op)
                                    {
                                        case Opcode::ICmpEq:
                                            v = (l == r);
                                            known = true;
                                            break;
                                        case Opcode::ICmpNe:
                                            v = (l != r);
                                            known = true;
                                            break;
                                        case Opcode::SCmpLT:
                                            v = (l < r);
                                            known = true;
                                            break;
                                        case Opcode::SCmpLE:
                                            v = (l <= r);
                                            known = true;
                                            break;
                                        case Opcode::SCmpGT:
                                            v = (l > r);
                                            known = true;
                                            break;
                                        case Opcode::SCmpGE:
                                            v = (l >= r);
                                            known = true;
                                            break;
                                        default:
                                            break;
                                    }
                                    if (known)
                                        defIdx = j;
                                }
                            }
                            if (known)
                                break;
                        }
                    }
                    if (known)
                    {
                        in.op = Opcode::Br;
                        const bool takeTrue = (v != 0);
                        in.labels = {takeTrue ? in.labels[0] : in.labels[1]};
                        if (!in.brArgs.empty())
                        {
                            if (takeTrue)
                            {
                                in.brArgs = std::vector<std::vector<Value>>{in.brArgs.front()};
                            }
                            else if (in.brArgs.size() > 1)
                            {
                                in.brArgs = std::vector<std::vector<Value>>{in.brArgs[1]};
                            }
                            else
                            {
                                in.brArgs.clear();
                            }
                        }
                        in.operands.clear();
                        if (defIdx != static_cast<size_t>(-1) && uses == 1)
                        {
                            b.instructions.erase(b.instructions.begin() + defIdx);
                            --i;
                        }
                    }
                    continue;
                }
                if (!in.result || in.operands.size() != 2)
                    continue;
                Value repl{};
                bool match = false;
                for (const auto &r : kRules)
                {
                    if (in.op == r.match.op &&
                        isConstEq(in.operands[r.match.constIdx], r.match.value))
                    {
                        repl = in.operands[r.repl.operandIdx];
                        match = true;
                        break;
                    }
                }
                if (match)
                {
                    replaceAll(f, *in.result, repl);
                    b.instructions.erase(b.instructions.begin() + i);
                    --i;
                }
            }
        }
    }
}

} // namespace il::transform
