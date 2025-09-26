// File: src/il/transform/ConstFold.cpp
// Purpose: Implements constant folding for integer ops and math intrinsics.
// Key invariants: Mirrors checked integer semantics and C math for f64.
// Ownership/Lifetime: Operates in place on the module.
// Links: docs/codemap.md
// License: MIT (see LICENSE)

#include "il/transform/ConstFold.hpp"
#include "il/core/Function.hpp"
#include "il/core/Instr.hpp"
#include "il/core/Module.hpp"
#include "il/core/Value.hpp"
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <optional>

using namespace il::core;

namespace il::transform
{

namespace
{

/**
 * @brief Extract a 64-bit integer constant operand.
 *
 * Recognises temporaries that already carry @ref Value::Kind::ConstInt and
 * exposes their signed 64-bit payload for folding. Conversion is lossless and
 * respects the IR's wraparound semantics by leaving the integer untouched. The
 * helper fails for all other operand categories, ensuring that folding does not
 * mix integers with floats or unresolved temporaries.
 *
 * @param v Operand to inspect.
 * @param out Receives the integer payload when recognised.
 * @returns True when @p v is a constant integer; false otherwise.
 */
static bool isConstInt(const Value &v, long long &out)
{
    if (v.kind == Value::Kind::ConstInt)
    {
        out = v.i64;
        return true;
    }
    return false;
}

/**
 * @brief Extract a floating-point value suitable for math intrinsic folding.
 *
 * Accepts both @ref Value::Kind::ConstFloat and @ref Value::Kind::ConstInt so
 * that integer literals can be promoted to double precision when folding
 * runtime math intrinsics. Promotion leverages the default C++ rules, meaning
 * large integers may lose precision while still matching the runtime helper's
 * semantics. The function rejects temporaries and references, signalling that
 * the fold must be deferred to runtime.
 *
 * @param v Operand to convert.
 * @param out Receives the double precision value when available.
 * @returns True when @p v encodes a usable floating-point quantity; false on
 *          unsupported operand kinds.
 */
static bool getConstFloat(const Value &v, double &out)
{
    if (v.kind == Value::Kind::ConstFloat)
    {
        out = v.f64;
        return true;
    }
    if (v.kind == Value::Kind::ConstInt)
    {
        out = static_cast<double>(v.i64);
        return true;
    }
    return false;
}

/**
 * @brief Perform checked addition mirroring the `.ovf` opcode semantics.
 *
 * Folding must trap on overflow, matching the runtime behaviour of
 * @ref Opcode::IAddOvf. The builtin overflow helpers report whether the
 * operation overflowed; if so the caller must bail out of folding so that the
 * verifier continues to reject the IR as trapping.
 */
static std::optional<long long> checkedAdd(long long a, long long b)
{
    long long result{};
    if (__builtin_add_overflow(a, b, &result))
        return std::nullopt;
    return result;
}

/**
 * @brief Perform checked subtraction mirroring the `.ovf` opcode semantics.
 */
static std::optional<long long> checkedSub(long long a, long long b)
{
    long long result{};
    if (__builtin_sub_overflow(a, b, &result))
        return std::nullopt;
    return result;
}

/**
 * @brief Perform checked multiplication mirroring the `.ovf` opcode semantics.
 */
static std::optional<long long> checkedMul(long long a, long long b)
{
    long long result{};
    if (__builtin_mul_overflow(a, b, &result))
        return std::nullopt;
    return result;
}

/**
 * @brief Substitute a folded value for all uses of a temporary.
 *
 * Iterates the function's blocks and instruction operands in-place, mutating
 * the IR so that every reference to the temporary identifier resolves to the
 * newly folded constant. Because the traversal rewrites operands directly, it
 * must run before the defining instruction is erased to avoid dangling
 * references.
 */
static void replaceAll(Function &f, unsigned id, const Value &v)
{
    for (auto &b : f.blocks)
        for (auto &in : b.instructions)
            for (auto &op : in.operands)
                if (op.kind == Value::Kind::Temp && op.id == id)
                    op = v;
}

/**
 * @brief Fold recognised math runtime calls into constants.
 *
 * Matches against intrinsics such as @c rt_abs_i64, @c rt_floor, @c rt_pow, and
 * trigonometric helpers. Each case documents the numeric preconditions it
 * enforces (e.g., @c rt_sqrt requires non-negative inputs, @c rt_pow only folds
 * small integral exponents) and relies on <cmath> routines like @c std::fabs,
 * @c std::pow, and friends to mirror runtime semantics. Folding fails when
 * operands are non-constant or violate domain restrictions, ensuring no change
 * to behaviour when C library edge cases (NaN propagation, domain errors) would
 * otherwise diverge from the runtime.
 *
 * @param in Instruction describing the call.
 * @param out Receives the constant result when folding succeeds.
 * @returns True when the call is replaced by a constant; false if no safe fold
 *          is available.
 */
static bool foldCall(const Instr &in, Value &out)
{
    if (in.op != Opcode::Call)
        return false;
    const std::string &c = in.callee;
    if (c == "rt_abs_i64" && in.operands.size() == 1)
    {
        long long v;
        if (isConstInt(in.operands[0], v) && v != std::numeric_limits<long long>::min())
        {
            out = Value::constInt(v < 0 ? -v : v);
            return true;
        }
        return false;
    }
    double a;
    if (c == "rt_abs_f64" && in.operands.size() == 1)
    {
        if (getConstFloat(in.operands[0], a))
        {
            out = Value::constFloat(std::fabs(a));
            return true;
        }
        return false;
    }
    if (c == "rt_floor" && in.operands.size() == 1)
    {
        if (getConstFloat(in.operands[0], a))
        {
            out = Value::constFloat(std::floor(a));
            return true;
        }
        return false;
    }
    if (c == "rt_ceil" && in.operands.size() == 1)
    {
        if (getConstFloat(in.operands[0], a))
        {
            out = Value::constFloat(std::ceil(a));
            return true;
        }
        return false;
    }
    if (c == "rt_sqrt" && in.operands.size() == 1)
    {
        if (getConstFloat(in.operands[0], a) && a >= 0.0)
        {
            out = Value::constFloat(std::sqrt(a));
            return true;
        }
        return false;
    }
    if (c == "rt_pow" && in.operands.size() == 2)
    {
        double base, expd;
        if (getConstFloat(in.operands[0], base) && getConstFloat(in.operands[1], expd))
        {
            double t = std::trunc(expd);
            if (expd == t)
            {
                long long exp = static_cast<long long>(t);
                if (std::llabs(exp) <= 16)
                {
                    out = Value::constFloat(std::pow(base, static_cast<double>(exp)));
                    return true;
                }
            }
        }
        return false;
    }
    if (c == "rt_sin" && in.operands.size() == 1)
    {
        if (getConstFloat(in.operands[0], a) && a == 0.0)
        {
            out = Value::constFloat(0.0);
            return true;
        }
        return false;
    }
    if (c == "rt_cos" && in.operands.size() == 1)
    {
        if (getConstFloat(in.operands[0], a) && a == 0.0)
        {
            out = Value::constFloat(1.0);
            return true;
        }
        return false;
    }
    return false;
}

} // namespace

/**
 * @brief Perform constant folding across a module in-place.
 *
 * Traverses each function block, opportunistically folding binary integer
 * operations and recognised runtime calls. Successful folds substitute the
 * computed value via @ref replaceAll before erasing the defining instruction,
 * so block traversal both mutates operand lists and shrinks instruction
 * sequences as it progresses. Wraparound helpers guarantee that integer
 * arithmetic matches the VM's modulo behaviour, while floating-point folds rely
 * on the C math library to honour runtime semantics and propagate failures when
 * inputs are out of domain or non-constant.
 *
 * @param m Module whose IR is rewritten in place.
 */
void constFold(Module &m)
{
    for (auto &f : m.functions)
    {
        for (auto &b : f.blocks)
        {
            for (size_t i = 0; i < b.instructions.size(); ++i)
            {
                Instr &in = b.instructions[i];
                if (!in.result)
                    continue;
                Value repl;
                bool folded = false;
                if (in.op == Opcode::Call)
                {
                    folded = foldCall(in, repl);
                }
                else if (in.operands.size() == 1)
                {
                    if (in.op == Opcode::CastFpToSiRteChk)
                    {
                        double operand;
                        if (getConstFloat(in.operands[0], operand) && std::isfinite(operand))
                        {
                            double rounded = std::nearbyint(operand);
                            if (std::isfinite(rounded))
                            {
                                constexpr double kMin =
                                    static_cast<double>(std::numeric_limits<long long>::min());
                                constexpr double kMax =
                                    static_cast<double>(std::numeric_limits<long long>::max());
                                if (rounded >= kMin && rounded <= kMax)
                                {
                                    repl = Value::constInt(static_cast<long long>(rounded));
                                    folded = true;
                                }
                            }
                        }
                    }
                }
                else if (in.operands.size() == 2)
                {
                    long long lhs, rhs, res = 0;
                    if (isConstInt(in.operands[0], lhs) && isConstInt(in.operands[1], rhs))
                    {
                        folded = true;
                        switch (in.op)
                        {
                            case Opcode::IAddOvf:
                                if (const auto sum = checkedAdd(lhs, rhs))
                                {
                                    res = *sum;
                                }
                                else
                                {
                                    folded = false;
                                }
                                break;
                            case Opcode::ISubOvf:
                                if (const auto diff = checkedSub(lhs, rhs))
                                {
                                    res = *diff;
                                }
                                else
                                {
                                    folded = false;
                                }
                                break;
                            case Opcode::IMulOvf:
                                if (const auto prod = checkedMul(lhs, rhs))
                                {
                                    res = *prod;
                                }
                                else
                                {
                                    folded = false;
                                }
                                break;
                            case Opcode::And:
                                res = lhs & rhs;
                                break;
                            case Opcode::Or:
                                res = lhs | rhs;
                                break;
                            case Opcode::Xor:
                                res = lhs ^ rhs;
                                break;
                            default:
                                folded = false;
                                break;
                        }
                        if (folded)
                            repl = Value::constInt(res);
                    }
                }
                if (folded)
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
