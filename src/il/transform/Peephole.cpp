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
// File Structure:
// ---------------
// This file is organized into the following sections:
//
// 1. Value Utilities
//    - isConstInt: Check if a value is an integer constant
//    - isConstEq: Check if a value equals a specific constant
//
// 2. Use Count Analysis
//    - UseCountMap: Type alias for temp ID to use count mapping
//    - buildUseCountMap: Build use counts for all temps in a function
//    - getUseCount: Query use count from precomputed map
//
// 3. Value Replacement
//    - replaceAll: Substitute all uses of a temp with a replacement value
//
// 4. Peephole Driver
//    - peephole(Module&): Main entry point
//      - Control flow simplification: CBr with known predicate -> Br
//      - Algebraic identity rules: Apply kRules table from header
//
// Rule Table (in Peephole.hpp):
// ----------------------------
// The kRules table defines algebraic identity and annihilation patterns:
// - Arithmetic identities: x + 0 = x, x * 1 = x, x - 0 = x
// - Arithmetic annihilations: x * 0 = 0, x - x = 0
// - Bitwise identities: x & -1 = x, x | 0 = x, x ^ 0 = x
// - Bitwise annihilations/reflexivity: x & 0 = 0, x ^ x = 0, x | x = x
// - Shift identities/annihilations: x << 0 = x, 0 << y = 0
// - Reflexive integer comparisons: icmp.eq x, x = true, scmp.lt x, x = false
//
//===----------------------------------------------------------------------===//

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

#include <cstdlib>
#include <iostream>
#include <unordered_map>

using namespace il::core;

namespace il::transform
{

namespace
{

//===----------------------------------------------------------------------===//
// Section 1: Value Utilities
//===----------------------------------------------------------------------===//

/// @brief Test whether a value is an integer constant and expose its payload.
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

/// @brief Determine whether an operand equals a specific integer literal.
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

/// @brief Determine whether two operands are identical.
///
/// Peephole rules occasionally rely on reflexivity (e.g. @c xor x, x -> 0).
/// This helper checks equality across the supported operand kinds to keep the
/// rule application loop concise.
static bool sameValue(const Value &a, const Value &b)
{
    if (a.kind != b.kind)
        return false;
    switch (a.kind)
    {
        case Value::Kind::Temp:
            return a.id == b.id;
        case Value::Kind::ConstInt:
            return a.i64 == b.i64 && a.isBool == b.isBool;
        case Value::Kind::ConstFloat:
            return a.f64 == b.f64;
        case Value::Kind::ConstStr:
        case Value::Kind::GlobalAddr:
            return a.str == b.str;
        case Value::Kind::NullPtr:
            return true;
    }
    return false;
}

//===----------------------------------------------------------------------===//
// Section 2: Use Count Analysis
//===----------------------------------------------------------------------===//

/// @brief Map from temporary id to its use count within a function.
using UseCountMap = std::unordered_map<unsigned, size_t>;

/// @brief Build a use-count map for all temporaries in a function.
///
/// Walks every instruction in every block once, counting how many times each
/// temporary identifier appears as an operand. This precomputation avoids
/// repeated O(n) scans when checking use counts for multiple temporaries,
/// reducing the overall complexity from O(n*k) to O(n) where k is the number
/// of temporaries whose counts are queried.
///
/// @param f   Function to analyze.
/// @returns Map from temporary id to use count.
static UseCountMap buildUseCountMap(const Function &f)
{
    UseCountMap counts;
    for (const auto &b : f.blocks)
        for (const auto &in : b.instructions)
            for (const auto &op : in.operands)
                if (op.kind == Value::Kind::Temp)
                    ++counts[op.id];
    return counts;
}

/// @brief Look up the use count for a temporary from a precomputed map.
///
/// @param counts  Precomputed use-count map.
/// @param id      Temporary identifier to query.
/// @returns Number of uses, or 0 if not present.
static size_t getUseCount(const UseCountMap &counts, unsigned id)
{
    auto it = counts.find(id);
    return it != counts.end() ? it->second : 0;
}

//===----------------------------------------------------------------------===//
// Section 3: Value Replacement
//===----------------------------------------------------------------------===//

/// @brief Substitute every use of a temporary with a replacement value.
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

//===----------------------------------------------------------------------===//
// Comparison Constant Folding (for CBr simplification)
//===----------------------------------------------------------------------===//

/// @brief Evaluate a comparison opcode with two constant operands.
/// @param op  The comparison opcode.
/// @param l   Left-hand side constant.
/// @param r   Right-hand side constant.
/// @param out Output boolean result if comparison is foldable.
/// @returns True if the comparison was evaluated, false if opcode not supported.
static bool evaluateComparison(Opcode op, long long l, long long r, long long &out)
{
    switch (op)
    {
        case Opcode::ICmpEq:
            out = (l == r);
            return true;
        case Opcode::ICmpNe:
            out = (l != r);
            return true;
        case Opcode::SCmpLT:
            out = (l < r);
            return true;
        case Opcode::SCmpLE:
            out = (l <= r);
            return true;
        case Opcode::SCmpGT:
            out = (l > r);
            return true;
        case Opcode::SCmpGE:
            out = (l >= r);
            return true;
        default:
            return false;
    }
}

static bool traceEnabled()
{
    static const bool enabled = std::getenv("VIPER_PEEPHOLE_TRACE") != nullptr;
    return enabled;
}

/// @brief Check if a value is a float constant with the expected value.
static bool isConstFloatEq(const Value &v, double target)
{
    if (v.kind == Value::Kind::ConstFloat)
    {
        return v.f64 == target;
    }
    return false;
}

/// @brief Determine whether @p in matches @p rule and compute the replacement.
///
/// @param rule Peephole rule to evaluate.
/// @param in Instruction to inspect (must be binary with a result).
/// @param out Receives the replacement value on match.
/// @returns True when the rule matched and @p out is valid.
static bool applyRule(const Rule &rule, const Instr &in, Value &out)
{
    if (in.op != rule.match.op || in.operands.size() != 2)
        return false;

    switch (rule.match.kind)
    {
        case Match::Kind::ConstOperand:
            if (!isConstEq(in.operands[rule.match.constIdx], rule.match.value))
                return false;
            break;
        case Match::Kind::ConstFloatOperand:
            if (!isConstFloatEq(in.operands[rule.match.constIdx], rule.match.floatValue))
                return false;
            break;
        case Match::Kind::SameOperands:
            if (!sameValue(in.operands[0], in.operands[1]))
                return false;
            break;
    }

    switch (rule.repl.kind)
    {
        case Replace::Kind::Operand:
            out = in.operands[rule.repl.operandIdx];
            return true;
        case Replace::Kind::Const:
            out = rule.repl.isBool ? Value::constBool(rule.repl.constValue != 0)
                                   : Value::constInt(rule.repl.constValue);
            return true;
        case Replace::Kind::ConstFloat:
            out = Value::constFloat(rule.repl.floatConstValue);
            return true;
    }
    return false;
}

} // namespace

//===----------------------------------------------------------------------===//
// Section 4: Peephole Driver
//===----------------------------------------------------------------------===//

/// @brief Apply local simplifications to all functions in a module.
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
    const bool trace = traceEnabled();
    for (auto &f : m.functions)
    {
        // Precompute use counts once per function to avoid repeated O(n) scans.
        UseCountMap useCounts = buildUseCountMap(f);

        for (auto &b : f.blocks)
        {
            for (size_t i = 0; i < b.instructions.size(); ++i)
            {
                Instr &in = b.instructions[i];

                //===----------------------------------------------------------===//
                // Control Flow Simplification: CBr -> Br
                //===----------------------------------------------------------===//
                // Simplify conditional branches where:
                // 1. Both targets are the same label (trivial case)
                // 2. The condition is a constant
                // 3. The condition is defined by a comparison with constant operands
                //===----------------------------------------------------------===//
                if (in.op == Opcode::CBr)
                {
                    // Case 1: Both targets identical -> unconditional branch
                    if (in.labels.size() == 2 && in.labels[0] == in.labels[1])
                    {
                        in.op = Opcode::Br;
                        in.labels = {in.labels[0]};
                        in.operands.clear();
                        if (in.brArgs.size() > 1)
                            in.brArgs.resize(1);
                        continue;
                    }

                    // Try to determine the branch condition value
                    long long v;
                    bool known = false;
                    size_t defIdx = static_cast<size_t>(-1);
                    size_t uses = 0;

                    // Case 2: Condition is a constant
                    if (isConstInt(in.operands[0], v))
                    {
                        known = true;
                    }
                    // Case 3: Condition is defined by a comparison in this block
                    else if (in.operands[0].kind == Value::Kind::Temp)
                    {
                        unsigned id = in.operands[0].id;
                        uses = getUseCount(useCounts, id);

                        // Search backwards for the defining instruction
                        for (size_t j = 0; j < i; ++j)
                        {
                            Instr &def = b.instructions[j];
                            if (def.result && *def.result == id && def.operands.size() == 2)
                            {
                                long long l, r;
                                if (isConstInt(def.operands[0], l) &&
                                    isConstInt(def.operands[1], r))
                                {
                                    if (evaluateComparison(def.op, l, r, v))
                                    {
                                        known = true;
                                        defIdx = j;
                                    }
                                }
                            }
                            if (known)
                                break;
                        }
                    }

                    // Rewrite CBr to Br if condition is known
                    if (known)
                    {
                        in.op = Opcode::Br;
                        const bool takeTrue = (v != 0);
                        in.labels = {takeTrue ? in.labels[0] : in.labels[1]};

                        // Preserve the correct branch arguments
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

                        // Remove the dead comparison if it was single-use
                        if (defIdx != static_cast<size_t>(-1) && uses == 1)
                        {
                            b.instructions.erase(b.instructions.begin() + defIdx);
                            --i;
                        }
                    }
                    continue;
                }

                //===----------------------------------------------------------===//
                // Algebraic Identity Rules
                //===----------------------------------------------------------===//
                // Apply rules from kRules table:
                // - Match: opcode with either a specific constant operand or
                //   equal operands (reflexive simplifications).
                // - Action: Forward an operand or replace with a literal.
                //===----------------------------------------------------------===//
                if (!in.result)
                    continue;

                Value repl{};
                bool match = false;
                for (const auto &r : kRules)
                {
                    if (applyRule(r, in, repl))
                    {
                        match = true;
                        if (trace && in.result)
                            std::cerr << "[peephole] rule " << r.name << " in %" << *in.result
                                      << " (func " << f.name << ")\n";
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
