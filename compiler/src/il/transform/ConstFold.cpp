//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE in the project root for license information.
//
// File: src/il/transform/ConstFold.cpp
// Purpose: Implement the IL constant-folding pass that reduces literal
//          expressions and recognised runtime calls.
// Key invariants: Folding must never change observable behaviour—operations
//                 that would trap at runtime or depend on non-constant data must
//                 be left untouched.  Integer folding respects overflow flags,
//                 and floating-point folding matches runtime helper semantics.
// Ownership/Lifetime: Operates on caller-owned Module/Function instances and
//                     mutates instructions in place without allocating global
//                     state.
// Links: docs/il-guide.md#reference, docs/codemap.md
//
//===----------------------------------------------------------------------===//
//
/// @file
/// @brief Constant folding implementation for IL modules.
/// @details Declares helper routines for extracting literal operands,
///          performing checked arithmetic, and substituting folded values before
///          exposing the public @ref constFold entry point.

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

/// @brief Extract a 64-bit integer constant operand.
/// @details Recognises @ref Value::Kind::ConstInt operands and exposes their
///          payload for folding without performing any conversions.  Other
///          operand kinds leave @p out untouched and cause the helper to fail so
///          callers can defer to runtime evaluation.
/// @param v Operand to inspect.
/// @param out Receives the integer payload when recognised.
/// @return True when @p v carries a constant integer value.
static bool isConstInt(const Value &v, long long &out)
{
    if (v.kind == Value::Kind::ConstInt)
    {
        out = v.i64;
        return true;
    }
    return false;
}

/// @brief Extract a floating-point value suitable for math intrinsic folding.
/// @details Accepts floating and integer literals, promoting integers to double
///          precision using the default C++ conversion so runtime semantics are
///          preserved.  Temporaries and references cause the helper to fail so
///          the call remains at runtime.
/// @param v Operand to convert.
/// @param out Receives the double-precision value when available.
/// @return True when @p v provides a foldable floating-point payload.
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

/// @brief Perform checked addition mirroring the `.ovf` opcode semantics.
/// @details Uses compiler builtins to detect overflow; when detected the helper
///          returns @c std::nullopt so folding can be skipped and runtime traps
///          are preserved.
/// @param a Left-hand integer operand.
/// @param b Right-hand integer operand.
/// @return Folded result or empty optional when overflow occurred.
static std::optional<long long> checkedAdd(long long a, long long b)
{
    long long result{};
    if (__builtin_add_overflow(a, b, &result))
        return std::nullopt;
    return result;
}

/// @brief Perform checked subtraction mirroring the `.ovf` opcode semantics.
/// @param a Left-hand integer operand.
/// @param b Right-hand integer operand.
/// @return Folded result or empty optional when overflow occurred.
static std::optional<long long> checkedSub(long long a, long long b)
{
    long long result{};
    if (__builtin_sub_overflow(a, b, &result))
        return std::nullopt;
    return result;
}

/// @brief Perform checked multiplication mirroring the `.ovf` opcode semantics.
/// @param a Left-hand integer operand.
/// @param b Right-hand integer operand.
/// @return Folded result or empty optional when overflow occurred.
static std::optional<long long> checkedMul(long long a, long long b)
{
    long long result{};
    if (__builtin_mul_overflow(a, b, &result))
        return std::nullopt;
    return result;
}

/// @brief Substitute a folded value for all uses of a temporary.
/// @details Walks every block and instruction operand to replace references to
///          the temporary identifier with the folded constant.  Must be invoked
///          before erasing the defining instruction to avoid dangling
///          references.
/// @param f Function containing the temporary.
/// @param id Temporary identifier to rewrite.
/// @param v Replacement value to substitute.
static void replaceAll(Function &f, unsigned id, const Value &v)
{
    for (auto &b : f.blocks)
        for (auto &in : b.instructions)
            for (auto &op : in.operands)
                if (op.kind == Value::Kind::Temp && op.id == id)
                    op = v;
}

/// @brief Fold recognised math runtime calls into constants.
/// @details Handles a curated set of runtime helpers (absolute value, rounding,
///          power, square root, trigonometry, etc.) when all operands are
///          literals that satisfy each helper's domain restrictions.  When a
///          domain precondition fails or an operand is non-constant the helper
///          returns @c false so the call remains in the IR.
/// @param in Instruction describing the runtime call.
/// @param out Receives the folded constant on success.
/// @return True when folding succeeded and @p out contains the result.
static bool foldCall(const Instr &in, Value &out)
{
    if (in.op != Opcode::Call)
        return false;
    const std::string &c = in.callee;
    if (c == "rt_val" || c == "rt_val_to_double" || c == "rt_int_to_str" || c == "rt_f64_to_str")
        return false;
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
    if (c == "rt_int_floor" && in.operands.size() == 1)
    {
        if (getConstFloat(in.operands[0], a))
        {
            out = Value::constFloat(std::floor(a));
            return true;
        }
        return false;
    }
    if (c == "rt_fix_trunc" && in.operands.size() == 1)
    {
        if (getConstFloat(in.operands[0], a))
        {
            out = Value::constFloat(std::trunc(a));
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
    if (c == "rt_pow_f64_chkdom" && in.operands.size() == 2)
    {
        double base = 0.0;
        double expd = 0.0;
        if (getConstFloat(in.operands[0], base) && getConstFloat(in.operands[1], expd))
        {
            const bool expIntegral = std::isfinite(expd) && (expd == std::trunc(expd));
            if (!(base < 0.0 && !expIntegral) && expIntegral)
            {
                const double truncated = std::trunc(expd);
                if (std::fabs(truncated) <= 16.0)
                {
                    const long long exp = static_cast<long long>(truncated);
                    const double value = std::pow(base, static_cast<double>(exp));
                    if (std::isfinite(value))
                    {
                        out = Value::constFloat(value);
                        return true;
                    }
                }
            }
        }
        return false;
    }
    if (c == "rt_round_even" && in.operands.size() == 2)
    {
        double value = 0.0;
        long long digits = 0;
        if (getConstFloat(in.operands[0], value) && isConstInt(in.operands[1], digits))
        {
            if (!std::isfinite(value))
            {
                out = Value::constFloat(value);
                return true;
            }
            if (digits == 0)
            {
                out = Value::constFloat(std::nearbyint(value));
                return true;
            }
            const double digitsAsDouble = static_cast<double>(digits);
            if (!std::isfinite(digitsAsDouble) || std::fabs(digitsAsDouble) > 308.0)
            {
                out = Value::constFloat(value);
                return true;
            }
            const double factor = std::pow(10.0, digitsAsDouble);
            if (!std::isfinite(factor) || factor == 0.0)
            {
                out = Value::constFloat(value);
                return true;
            }
            const double scaled = value * factor;
            if (!std::isfinite(scaled))
            {
                out = Value::constFloat(value);
                return true;
            }
            const double rounded = std::nearbyint(scaled);
            out = Value::constFloat(rounded / factor);
            return true;
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
    // Trigonometric functions with constant folding for specific values
    if (c == "rt_tan" && in.operands.size() == 1)
    {
        if (getConstFloat(in.operands[0], a) && a == 0.0)
        {
            out = Value::constFloat(0.0);
            return true;
        }
        return false;
    }
    if (c == "rt_asin" && in.operands.size() == 1)
    {
        if (getConstFloat(in.operands[0], a) && a == 0.0)
        {
            out = Value::constFloat(0.0);
            return true;
        }
        return false;
    }
    if (c == "rt_acos" && in.operands.size() == 1)
    {
        if (getConstFloat(in.operands[0], a) && a == 1.0)
        {
            out = Value::constFloat(0.0);
            return true;
        }
        return false;
    }
    if (c == "rt_atan" && in.operands.size() == 1)
    {
        if (getConstFloat(in.operands[0], a) && a == 0.0)
        {
            out = Value::constFloat(0.0);
            return true;
        }
        return false;
    }
    // Log/exp functions
    if (c == "rt_log" && in.operands.size() == 1)
    {
        if (getConstFloat(in.operands[0], a) && a == 1.0)
        {
            out = Value::constFloat(0.0);
            return true;
        }
        return false;
    }
    if (c == "rt_exp" && in.operands.size() == 1)
    {
        if (getConstFloat(in.operands[0], a) && a == 0.0)
        {
            out = Value::constFloat(1.0);
            return true;
        }
        return false;
    }
    // Min/max with constant arguments
    double b;
    if (c == "rt_min_f64" && in.operands.size() == 2)
    {
        if (getConstFloat(in.operands[0], a) && getConstFloat(in.operands[1], b))
        {
            out = Value::constFloat(a < b ? a : b);
            return true;
        }
        return false;
    }
    if (c == "rt_max_f64" && in.operands.size() == 2)
    {
        if (getConstFloat(in.operands[0], a) && getConstFloat(in.operands[1], b))
        {
            out = Value::constFloat(a > b ? a : b);
            return true;
        }
        return false;
    }
    // Integer min/max
    long long ai, bi;
    if (c == "rt_min_i64" && in.operands.size() == 2)
    {
        if (isConstInt(in.operands[0], ai) && isConstInt(in.operands[1], bi))
        {
            out = Value::constInt(ai < bi ? ai : bi);
            return true;
        }
        return false;
    }
    if (c == "rt_max_i64" && in.operands.size() == 2)
    {
        if (isConstInt(in.operands[0], ai) && isConstInt(in.operands[1], bi))
        {
            out = Value::constInt(ai > bi ? ai : bi);
            return true;
        }
        return false;
    }
    // Clamp
    if (c == "rt_clamp_f64" && in.operands.size() == 3)
    {
        double val, lo, hi;
        if (getConstFloat(in.operands[0], val) && getConstFloat(in.operands[1], lo) &&
            getConstFloat(in.operands[2], hi))
        {
            if (val < lo)
                out = Value::constFloat(lo);
            else if (val > hi)
                out = Value::constFloat(hi);
            else
                out = Value::constFloat(val);
            return true;
        }
        return false;
    }
    if (c == "rt_clamp_i64" && in.operands.size() == 3)
    {
        long long val, lo, hi;
        if (isConstInt(in.operands[0], val) && isConstInt(in.operands[1], lo) &&
            isConstInt(in.operands[2], hi))
        {
            if (val < lo)
                out = Value::constInt(lo);
            else if (val > hi)
                out = Value::constInt(hi);
            else
                out = Value::constInt(val);
            return true;
        }
        return false;
    }
    // Sign function
    if (c == "rt_sgn_i64" && in.operands.size() == 1)
    {
        if (isConstInt(in.operands[0], ai))
        {
            out = Value::constInt(ai > 0 ? 1 : (ai < 0 ? -1 : 0));
            return true;
        }
        return false;
    }
    if (c == "rt_sgn_f64" && in.operands.size() == 1)
    {
        if (getConstFloat(in.operands[0], a))
        {
            out = Value::constFloat(a > 0.0 ? 1.0 : (a < 0.0 ? -1.0 : 0.0));
            return true;
        }
        return false;
    }
    return false;
}

} // namespace

/// @brief Perform constant folding across a module in-place.
/// @details Visits every instruction, attempting to fold recognised runtime
///          calls, unary casts, and binary arithmetic operations.  Successful
///          folds update all uses of the temporary via @ref replaceAll and erase
///          the original instruction, shrinking the IR without altering
///          observable behaviour.
/// @param m Module whose functions are to be folded.
void constFold(Module &m)
{
    // Constant folding must be observationally equivalent to VM; where traps would
    // occur at runtime, folding must not change behavior.
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
                            case Opcode::SDivChk0:
                                if (rhs != 0 &&
                                    !(lhs == std::numeric_limits<long long>::min() && rhs == -1))
                                {
                                    res = lhs / rhs;
                                }
                                else
                                {
                                    folded = false;
                                }
                                break;
                            case Opcode::SRemChk0:
                                if (rhs != 0 &&
                                    !(lhs == std::numeric_limits<long long>::min() && rhs == -1))
                                {
                                    res = lhs % rhs;
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
                            // Shift operations
                            case Opcode::Shl:
                                if (rhs >= 0 && rhs < 64)
                                    res = lhs << rhs;
                                else
                                    folded = false;
                                break;
                            case Opcode::LShr:
                                if (rhs >= 0 && rhs < 64)
                                    res = static_cast<long long>(
                                        static_cast<unsigned long long>(lhs) >> rhs);
                                else
                                    folded = false;
                                break;
                            case Opcode::AShr:
                                if (rhs >= 0 && rhs < 64)
                                    res = lhs >> rhs;
                                else
                                    folded = false;
                                break;
                            // Integer comparisons - produce boolean results
                            case Opcode::ICmpEq:
                                repl = Value::constBool(lhs == rhs);
                                break;
                            case Opcode::ICmpNe:
                                repl = Value::constBool(lhs != rhs);
                                break;
                            case Opcode::SCmpLT:
                                repl = Value::constBool(lhs < rhs);
                                break;
                            case Opcode::SCmpLE:
                                repl = Value::constBool(lhs <= rhs);
                                break;
                            case Opcode::SCmpGT:
                                repl = Value::constBool(lhs > rhs);
                                break;
                            case Opcode::SCmpGE:
                                repl = Value::constBool(lhs >= rhs);
                                break;
                            // Unsigned comparisons (treat as unsigned) - produce boolean results
                            case Opcode::UCmpLT:
                                repl = Value::constBool(static_cast<unsigned long long>(lhs) <
                                                        static_cast<unsigned long long>(rhs));
                                break;
                            case Opcode::UCmpLE:
                                repl = Value::constBool(static_cast<unsigned long long>(lhs) <=
                                                        static_cast<unsigned long long>(rhs));
                                break;
                            case Opcode::UCmpGT:
                                repl = Value::constBool(static_cast<unsigned long long>(lhs) >
                                                        static_cast<unsigned long long>(rhs));
                                break;
                            case Opcode::UCmpGE:
                                repl = Value::constBool(static_cast<unsigned long long>(lhs) >=
                                                        static_cast<unsigned long long>(rhs));
                                break;
                            // Unsigned division and remainder
                            case Opcode::UDivChk0:
                                if (rhs != 0)
                                {
                                    res = static_cast<long long>(
                                        static_cast<unsigned long long>(lhs) /
                                        static_cast<unsigned long long>(rhs));
                                }
                                else
                                {
                                    folded = false;
                                }
                                break;
                            case Opcode::URemChk0:
                                if (rhs != 0)
                                {
                                    res = static_cast<long long>(
                                        static_cast<unsigned long long>(lhs) %
                                        static_cast<unsigned long long>(rhs));
                                }
                                else
                                {
                                    folded = false;
                                }
                                break;
                            default:
                                folded = false;
                                break;
                        }
                        // For non-comparison operations, synthesize the integer result
                        // Comparison operations already set repl directly
                        if (folded && in.op != Opcode::ICmpEq && in.op != Opcode::ICmpNe &&
                            in.op != Opcode::SCmpLT && in.op != Opcode::SCmpLE &&
                            in.op != Opcode::SCmpGT && in.op != Opcode::SCmpGE &&
                            in.op != Opcode::UCmpLT && in.op != Opcode::UCmpLE &&
                            in.op != Opcode::UCmpGT && in.op != Opcode::UCmpGE)
                        {
                            repl = Value::constInt(res);
                        }
                    }
                    // Try floating-point folding
                    if (!folded)
                    {
                        double flhs = 0.0;
                        double frhs = 0.0;
                        if (getConstFloat(in.operands[0], flhs) &&
                            getConstFloat(in.operands[1], frhs))
                        {
                            double fres = 0.0;
                            folded = true;
                            switch (in.op)
                            {
                                case Opcode::FAdd:
                                    fres = flhs + frhs;
                                    break;
                                case Opcode::FSub:
                                    fres = flhs - frhs;
                                    break;
                                case Opcode::FMul:
                                    fres = flhs * frhs;
                                    break;
                                case Opcode::FDiv:
                                    // Don't fold division by zero
                                    if (frhs != 0.0)
                                        fres = flhs / frhs;
                                    else
                                        folded = false;
                                    break;
                                // Float comparisons produce boolean (int) results
                                case Opcode::FCmpEQ:
                                    repl = Value::constBool(flhs == frhs);
                                    break;
                                case Opcode::FCmpNE:
                                    repl = Value::constBool(flhs != frhs);
                                    break;
                                case Opcode::FCmpLT:
                                    repl = Value::constBool(flhs < frhs);
                                    break;
                                case Opcode::FCmpLE:
                                    repl = Value::constBool(flhs <= frhs);
                                    break;
                                case Opcode::FCmpGT:
                                    repl = Value::constBool(flhs > frhs);
                                    break;
                                case Opcode::FCmpGE:
                                    repl = Value::constBool(flhs >= frhs);
                                    break;
                                default:
                                    folded = false;
                                    break;
                            }
                            // For arithmetic operations, produce float result
                            if (folded && in.op != Opcode::FCmpEQ && in.op != Opcode::FCmpNE &&
                                in.op != Opcode::FCmpLT && in.op != Opcode::FCmpLE &&
                                in.op != Opcode::FCmpGT && in.op != Opcode::FCmpGE)
                            {
                                if (std::isfinite(fres))
                                    repl = Value::constFloat(fres);
                                else
                                    folded = false; // Don't fold to inf/nan
                            }
                        }
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
