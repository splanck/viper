//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Implements sparse conditional constant propagation for the IL.  The solver
// tracks executable blocks and edges, models block parameters as phi nodes, and
// rewrites instructions whose results collapse to constants.  Terminators with
// known outcomes are simplified to unconditional branches, leaving further CFG
// clean-up to SimplifyCFG.
//
//===----------------------------------------------------------------------===//
//
// File Structure:
// ---------------
// This file is organized into the following sections:
//
// 1. Lattice and Value Utilities
//    - ValueLattice struct and value comparison helpers
//    - Constant extraction helpers (getConstInt, getConstFloat, etc.)
//    - Overflow-checked arithmetic helpers
//
// 2. Constant Folding by Opcode Family
//    - foldIntegerArithmetic: Add, Sub, Mul, And, Or, Xor, Shl, LShr, AShr
//    - foldOverflowArithmetic: IAddOvf, ISubOvf, IMulOvf
//    - foldDivisionRemainder: SDivChk0, SRemChk0, UDivChk0, URemChk0
//    - foldFloatArithmetic: FAdd, FSub, FMul, FDiv
//    - foldIntegerComparisons: ICmpEq, ICmpNe, SCmpLT, SCmpLE, SCmpGT, SCmpGE
//    - foldUnsignedComparisons: UCmpLT, UCmpLE, UCmpGT, UCmpGE
//    - foldFloatComparisons: FCmpEQ, FCmpNE, FCmpLT, FCmpLE, FCmpGT, FCmpGE
//    - foldTypeConversions: CastSiToFp, CastUiToFp, CastFpToSiRteChk, etc.
//    - foldBooleanOps: Zext1, Trunc1
//    - foldConstantMaterialization: ConstNull, ConstStr, AddrOf
//
// 3. SCCPSolver Class
//    - Lattice state management
//    - Worklist processing
//    - Terminator handling (CBr, SwitchI32)
//    - Rewriting phase
//
// 4. Public API
//    - sccp(Module&) entry point
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Sparse conditional constant propagation for IL functions.
/// @details Provides a lattice-based solver that runs per function.  The solver
///          propagates constants only along executable edges, merges block
///          parameter values using the classic three-point lattice, and rewrites
///          instructions and terminators once fixed points are reached.

#include "il/transform/SCCP.hpp"

#include "il/core/Function.hpp"
#include "il/core/Instr.hpp"
#include "il/core/Module.hpp"
#include "il/core/Opcode.hpp"
#include "il/core/Type.hpp"
#include "il/core/Value.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <limits>
#include <optional>
#include <queue>
#include <sstream>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#ifdef _MSC_VER
// MSVC doesn't have __builtin_*_overflow, so we implement our own for long long
namespace
{
inline bool msvc_add_overflow_ll(long long a, long long b, long long *result)
{
    *result = static_cast<long long>(static_cast<unsigned long long>(a) +
                                     static_cast<unsigned long long>(b));
    if (b >= 0)
        return *result < a;
    else
        return *result > a;
}

inline bool msvc_sub_overflow_ll(long long a, long long b, long long *result)
{
    *result = static_cast<long long>(static_cast<unsigned long long>(a) -
                                     static_cast<unsigned long long>(b));
    if (b >= 0)
        return *result > a;
    else
        return *result < a;
}

inline bool msvc_mul_overflow_ll(long long a, long long b, long long *result)
{
    if (a == 0 || b == 0)
    {
        *result = 0;
        return false;
    }
    if (a == (std::numeric_limits<long long>::min)() && b == -1)
    {
        *result = (std::numeric_limits<long long>::min)();
        return true;
    }
    if (b == (std::numeric_limits<long long>::min)() && a == -1)
    {
        *result = (std::numeric_limits<long long>::min)();
        return true;
    }
    long long abs_a = a < 0 ? -a : a;
    long long abs_b = b < 0 ? -b : b;
    if (abs_a > (std::numeric_limits<long long>::max)() / abs_b)
    {
        *result = static_cast<long long>(static_cast<unsigned long long>(a) *
                                         static_cast<unsigned long long>(b));
        return true;
    }
    *result = a * b;
    return false;
}
} // namespace

#define __builtin_add_overflow(a, b, r) msvc_add_overflow_ll(a, b, r)
#define __builtin_sub_overflow(a, b, r) msvc_sub_overflow_ll(a, b, r)
#define __builtin_mul_overflow(a, b, r) msvc_mul_overflow_ll(a, b, r)
#endif // _MSC_VER

using namespace il::core;

namespace il::transform
{
namespace
{

//===----------------------------------------------------------------------===//
// Section 1: Lattice and Value Utilities
//===----------------------------------------------------------------------===//

bool valuesEqual(const Value &lhs, const Value &rhs);

/// @brief Three-point lattice for SCCP analysis.
/// @details Unknown < Constant < Overdefined.  Trap-like instructions are
///          modelled separately during folding so we never turn a known trap
///          into an executable edge.
struct ValueLattice
{
    enum class Kind
    {
        Unknown,
        Constant,
        Overdefined
    };

    static ValueLattice unknown()
    {
        return ValueLattice{Kind::Unknown, {}};
    }

    static ValueLattice fromConstant(Value v)
    {
        return ValueLattice{Kind::Constant, v};
    }

    static ValueLattice overdefined()
    {
        return ValueLattice{Kind::Overdefined, {}};
    }

    bool isUnknown() const
    {
        return kind == Kind::Unknown;
    }

    bool isConstant() const
    {
        return kind == Kind::Constant;
    }

    bool isOverdefined() const
    {
        return kind == Kind::Overdefined;
    }

    /// @brief Merge a constant into the lattice state.
    /// @return True if the state changed.
    bool mergeConstant(const Value &v)
    {
        if (kind == Kind::Unknown)
        {
            kind = Kind::Constant;
            value = v;
            return true;
        }
        if (kind == Kind::Constant && !valuesEqual(value, v))
        {
            kind = Kind::Overdefined;
            value = {};
            return true;
        }
        return false;
    }

    /// @brief Raise the lattice element to Overdefined.
    /// @return True if the state changed.
    bool markOverdefined()
    {
        if (kind == Kind::Overdefined)
            return false;
        kind = Kind::Overdefined;
        value = {};
        return true;
    }

    Kind kind = Kind::Unknown;
    Value value{};
};

/// @brief Folding outcome classification used during evaluation.
struct FoldResult
{
    enum class Kind
    {
        Unknown,
        Constant,
        Trap
    };

    static FoldResult unknown()
    {
        return FoldResult{Kind::Unknown, {}};
    }

    static FoldResult trap()
    {
        return FoldResult{Kind::Trap, {}};
    }

    static FoldResult constant(Value v)
    {
        return FoldResult{Kind::Constant, v};
    }

    bool isTrap() const
    {
        return kind == Kind::Trap;
    }

    bool isConstant() const
    {
        return kind == Kind::Constant;
    }

    Kind kind;
    Value value;
};

/// @brief Compare two IL values for equality.
bool valuesEqual(const Value &lhs, const Value &rhs)
{
    if (lhs.kind != rhs.kind)
        return false;
    switch (lhs.kind)
    {
        case Value::Kind::ConstInt:
            return lhs.i64 == rhs.i64 && lhs.isBool == rhs.isBool;
        case Value::Kind::ConstFloat:
            return lhs.f64 == rhs.f64;
        case Value::Kind::ConstStr:
        case Value::Kind::GlobalAddr:
            return lhs.str == rhs.str;
        case Value::Kind::Temp:
            return lhs.id == rhs.id;
        case Value::Kind::NullPtr:
            return true;
    }
    return false;
}

std::string describeValue(const Value &value)
{
    std::ostringstream oss;
    switch (value.kind)
    {
        case Value::Kind::ConstInt:
            oss << value.i64;
            if (value.isBool)
                oss << " (bool)";
            break;
        case Value::Kind::ConstFloat:
            oss << value.f64;
            break;
        case Value::Kind::ConstStr:
            oss << "str(" << value.str << ")";
            break;
        case Value::Kind::GlobalAddr:
            oss << "addr(" << value.str << ")";
            break;
        case Value::Kind::NullPtr:
            oss << "null";
            break;
        case Value::Kind::Temp:
            oss << "%" << value.id;
            break;
    }
    return oss.str();
}

//===----------------------------------------------------------------------===//
// Constant Extraction Helpers
//===----------------------------------------------------------------------===//

/// @brief Extract a signed integer constant from a value.
bool getConstInt(const Value &value, long long &out)
{
    if (value.kind != Value::Kind::ConstInt)
        return false;
    out = value.i64;
    return true;
}

/// @brief Extract an unsigned integer constant from a value.
bool getConstUInt(const Value &value, unsigned long long &out)
{
    if (value.kind != Value::Kind::ConstInt)
        return false;
    out = static_cast<unsigned long long>(value.i64);
    return true;
}

/// @brief Extract a floating-point constant from a value.
/// @details Also handles ConstInt by converting to double.
bool getConstFloat(const Value &value, double &out)
{
    if (value.kind == Value::Kind::ConstFloat)
    {
        out = value.f64;
        return true;
    }
    if (value.kind == Value::Kind::ConstInt)
    {
        out = static_cast<double>(value.i64);
        return true;
    }
    return false;
}

/// @brief Extract a boolean from a constant value.
/// @details Handles ConstInt, ConstFloat, NullPtr, ConstStr, GlobalAddr.
bool getConstBool(const Value &value, bool &out)
{
    switch (value.kind)
    {
        case Value::Kind::ConstInt:
            out = value.i64 != 0;
            return true;
        case Value::Kind::ConstFloat:
            out = value.f64 != 0.0;
            return true;
        case Value::Kind::NullPtr:
            out = false;
            return true;
        case Value::Kind::ConstStr:
        case Value::Kind::GlobalAddr:
            out = true;
            return true;
        default:
            return false;
    }
}

//===----------------------------------------------------------------------===//
// Overflow-Checked Arithmetic Helpers
//===----------------------------------------------------------------------===//

/// @brief Checked signed addition that returns nullopt on overflow.
std::optional<long long> checkedAdd(long long lhs, long long rhs)
{
    long long result{};
    if (__builtin_add_overflow(lhs, rhs, &result))
        return std::nullopt;
    return result;
}

/// @brief Checked signed subtraction that returns nullopt on overflow.
std::optional<long long> checkedSub(long long lhs, long long rhs)
{
    long long result{};
    if (__builtin_sub_overflow(lhs, rhs, &result))
        return std::nullopt;
    return result;
}

/// @brief Checked signed multiplication that returns nullopt on overflow.
std::optional<long long> checkedMul(long long lhs, long long rhs)
{
    long long result{};
    if (__builtin_mul_overflow(lhs, rhs, &result))
        return std::nullopt;
    return result;
}

//===----------------------------------------------------------------------===//
// Section 2: Constant Folding by Opcode Family
//===----------------------------------------------------------------------===//
//
// Each fold function takes the instruction and a resolver for operand values,
// returning an optional constant if the operation can be folded.
//

/// @brief Context for resolving instruction operands during folding.
struct FoldContext
{
    const Instr &instr;
    std::function<bool(size_t, Value &)> resolveOperand;

    bool getConstIntOperand(size_t index, long long &out) const
    {
        if (index >= instr.operands.size())
            return false;
        Value resolved;
        if (!resolveOperand(index, resolved))
            return false;
        return getConstInt(resolved, out);
    }

    bool getConstUIntOperand(size_t index, unsigned long long &out) const
    {
        if (index >= instr.operands.size())
            return false;
        Value resolved;
        if (!resolveOperand(index, resolved))
            return false;
        return getConstUInt(resolved, out);
    }

    bool getConstFloatOperand(size_t index, double &out) const
    {
        if (index >= instr.operands.size())
            return false;
        Value resolved;
        if (!resolveOperand(index, resolved))
            return false;
        return getConstFloat(resolved, out);
    }
};

//===----------------------------------------------------------------------===//
// Integer Arithmetic: Add, Sub, Mul, And, Or, Xor, Shl, LShr, AShr
//===----------------------------------------------------------------------===//

/// @brief Fold basic integer arithmetic operations.
/// @details Handles non-overflow-checked integer operations.
static FoldResult foldIntegerArithmetic(Opcode op, const FoldContext &ctx)
{
    long long lhs{}, rhs{};
    if (!ctx.getConstIntOperand(0, lhs) || !ctx.getConstIntOperand(1, rhs))
        return FoldResult::unknown();

    switch (op)
    {
        case Opcode::Add:
            return FoldResult::constant(Value::constInt(lhs + rhs));
        case Opcode::Sub:
            return FoldResult::constant(Value::constInt(lhs - rhs));
        case Opcode::Mul:
            return FoldResult::constant(Value::constInt(lhs * rhs));
        case Opcode::And:
            return FoldResult::constant(Value::constInt(lhs & rhs));
        case Opcode::Or:
            return FoldResult::constant(Value::constInt(lhs | rhs));
        case Opcode::Xor:
            return FoldResult::constant(Value::constInt(lhs ^ rhs));
        case Opcode::Shl:
            return FoldResult::constant(Value::constInt(lhs << (rhs & 63)));
        case Opcode::LShr:
            return FoldResult::constant(
                Value::constInt(static_cast<unsigned long long>(lhs) >> (rhs & 63)));
        case Opcode::AShr:
            return FoldResult::constant(Value::constInt(lhs >> (rhs & 63)));
        default:
            return FoldResult::unknown();
    }
}

//===----------------------------------------------------------------------===//
// Overflow-Checked Arithmetic: IAddOvf, ISubOvf, IMulOvf
//===----------------------------------------------------------------------===//

/// @brief Fold overflow-checked arithmetic operations.
/// @details Returns nullopt if the operation would overflow at runtime.
static FoldResult foldOverflowArithmetic(Opcode op, const FoldContext &ctx)
{
    long long lhs{}, rhs{};
    if (!ctx.getConstIntOperand(0, lhs) || !ctx.getConstIntOperand(1, rhs))
        return FoldResult::unknown();

    switch (op)
    {
        case Opcode::IAddOvf:
            if (auto sum = checkedAdd(lhs, rhs))
                return FoldResult::constant(Value::constInt(*sum));
            return FoldResult::trap();
        case Opcode::ISubOvf:
            if (auto diff = checkedSub(lhs, rhs))
                return FoldResult::constant(Value::constInt(*diff));
            return FoldResult::trap();
        case Opcode::IMulOvf:
            if (auto prod = checkedMul(lhs, rhs))
                return FoldResult::constant(Value::constInt(*prod));
            return FoldResult::trap();
        default:
            break;
    }
    return FoldResult::unknown();
}

//===----------------------------------------------------------------------===//
// Division and Remainder: SDivChk0, SRemChk0, UDivChk0, URemChk0
//===----------------------------------------------------------------------===//

/// @brief Fold signed division/remainder with zero-check.
/// @details Returns nullopt for divide-by-zero or MIN/-1 overflow.
static FoldResult foldSignedDivRem(Opcode op, const FoldContext &ctx)
{
    long long lhs{}, rhs{};
    if (!ctx.getConstIntOperand(0, lhs) || !ctx.getConstIntOperand(1, rhs))
        return FoldResult::unknown();

    // Check for divide-by-zero and signed overflow (MIN / -1)
    if (rhs == 0 || (lhs == std::numeric_limits<long long>::min() && rhs == -1))
        return FoldResult::trap();

    if (op == Opcode::SDivChk0)
        return FoldResult::constant(Value::constInt(lhs / rhs));
    return FoldResult::constant(Value::constInt(lhs % rhs));
}

/// @brief Fold unsigned division/remainder with zero-check.
static FoldResult foldUnsignedDivRem(Opcode op, const FoldContext &ctx)
{
    unsigned long long lhs{}, rhs{};
    if (!ctx.getConstUIntOperand(0, lhs) || !ctx.getConstUIntOperand(1, rhs))
        return FoldResult::unknown();

    if (rhs == 0)
        return FoldResult::trap();

    if (op == Opcode::UDivChk0)
        return FoldResult::constant(Value::constInt(static_cast<long long>(lhs / rhs)));
    return FoldResult::constant(Value::constInt(static_cast<long long>(lhs % rhs)));
}

//===----------------------------------------------------------------------===//
// Floating-Point Arithmetic: FAdd, FSub, FMul, FDiv
//===----------------------------------------------------------------------===//

/// @brief Fold floating-point arithmetic operations.
static FoldResult foldFloatArithmetic(Opcode op, const FoldContext &ctx)
{
    double lhs{}, rhs{};
    if (!ctx.getConstFloatOperand(0, lhs) || !ctx.getConstFloatOperand(1, rhs))
        return FoldResult::unknown();

    switch (op)
    {
        case Opcode::FAdd:
            return FoldResult::constant(Value::constFloat(lhs + rhs));
        case Opcode::FSub:
            return FoldResult::constant(Value::constFloat(lhs - rhs));
        case Opcode::FMul:
            return FoldResult::constant(Value::constFloat(lhs * rhs));
        case Opcode::FDiv:
            // IEEE 754: x/0 produces ±inf or NaN; let the C++ runtime handle it.
            return FoldResult::constant(Value::constFloat(lhs / rhs));
        default:
            return FoldResult::unknown();
    }
}

//===----------------------------------------------------------------------===//
// Integer Comparisons: ICmpEq, ICmpNe, SCmpLT, SCmpLE, SCmpGT, SCmpGE
//===----------------------------------------------------------------------===//

/// @brief Fold signed integer comparison operations.
static FoldResult foldIntegerComparisons(Opcode op, const FoldContext &ctx)
{
    long long lhs{}, rhs{};
    if (!ctx.getConstIntOperand(0, lhs) || !ctx.getConstIntOperand(1, rhs))
        return FoldResult::unknown();

    bool result = false;
    switch (op)
    {
        case Opcode::ICmpEq:
            result = lhs == rhs;
            break;
        case Opcode::ICmpNe:
            result = lhs != rhs;
            break;
        case Opcode::SCmpLT:
            result = lhs < rhs;
            break;
        case Opcode::SCmpLE:
            result = lhs <= rhs;
            break;
        case Opcode::SCmpGT:
            result = lhs > rhs;
            break;
        case Opcode::SCmpGE:
            result = lhs >= rhs;
            break;
        default:
            return FoldResult::unknown();
    }
    return FoldResult::constant(Value::constBool(result));
}

//===----------------------------------------------------------------------===//
// Unsigned Comparisons: UCmpLT, UCmpLE, UCmpGT, UCmpGE
//===----------------------------------------------------------------------===//

/// @brief Fold unsigned integer comparison operations.
static FoldResult foldUnsignedComparisons(Opcode op, const FoldContext &ctx)
{
    unsigned long long lhs{}, rhs{};
    if (!ctx.getConstUIntOperand(0, lhs) || !ctx.getConstUIntOperand(1, rhs))
        return FoldResult::unknown();

    bool result = false;
    switch (op)
    {
        case Opcode::UCmpLT:
            result = lhs < rhs;
            break;
        case Opcode::UCmpLE:
            result = lhs <= rhs;
            break;
        case Opcode::UCmpGT:
            result = lhs > rhs;
            break;
        case Opcode::UCmpGE:
            result = lhs >= rhs;
            break;
        default:
            return FoldResult::unknown();
    }
    return FoldResult::constant(Value::constBool(result));
}

//===----------------------------------------------------------------------===//
// Floating-Point Comparisons: FCmpEQ, FCmpNE, FCmpLT, FCmpLE, FCmpGT, FCmpGE
//===----------------------------------------------------------------------===//

/// @brief Fold floating-point comparison operations.
static FoldResult foldFloatComparisons(Opcode op, const FoldContext &ctx)
{
    double lhs{}, rhs{};
    if (!ctx.getConstFloatOperand(0, lhs) || !ctx.getConstFloatOperand(1, rhs))
        return FoldResult::unknown();

    bool result = false;
    switch (op)
    {
        case Opcode::FCmpEQ:
            result = lhs == rhs;
            break;
        case Opcode::FCmpNE:
            result = lhs != rhs;
            break;
        case Opcode::FCmpLT:
            result = lhs < rhs;
            break;
        case Opcode::FCmpLE:
            result = lhs <= rhs;
            break;
        case Opcode::FCmpGT:
            result = lhs > rhs;
            break;
        case Opcode::FCmpGE:
            result = lhs >= rhs;
            break;
        default:
            return FoldResult::unknown();
    }
    return FoldResult::constant(Value::constBool(result));
}

//===----------------------------------------------------------------------===//
// Type Conversions: CastSiToFp, CastUiToFp, CastFpToSiRteChk, CastFpToUiRteChk
//===----------------------------------------------------------------------===//

/// @brief Fold signed integer to floating-point conversion.
static FoldResult foldCastSiToFp(const FoldContext &ctx)
{
    long long operand{};
    if (!ctx.getConstIntOperand(0, operand))
        return FoldResult::unknown();
    return FoldResult::constant(Value::constFloat(static_cast<double>(operand)));
}

/// @brief Fold unsigned integer to floating-point conversion.
static FoldResult foldCastUiToFp(const FoldContext &ctx)
{
    unsigned long long operand{};
    if (!ctx.getConstUIntOperand(0, operand))
        return FoldResult::unknown();
    return FoldResult::constant(Value::constFloat(static_cast<double>(operand)));
}

/// @brief Fold floating-point to signed integer conversion with range check.
static FoldResult foldCastFpToSi(const FoldContext &ctx)
{
    double operand{};
    if (!ctx.getConstFloatOperand(0, operand) || !std::isfinite(operand))
        return FoldResult::unknown();

    double rounded = std::nearbyint(operand);
    if (!std::isfinite(rounded))
        return FoldResult::trap();

    constexpr double kMin = static_cast<double>(std::numeric_limits<long long>::min());
    constexpr double kMax = static_cast<double>(std::numeric_limits<long long>::max());
    if (rounded < kMin || rounded > kMax)
        return FoldResult::trap();

    return FoldResult::constant(Value::constInt(static_cast<long long>(rounded)));
}

/// @brief Fold floating-point to unsigned integer conversion with range check.
static FoldResult foldCastFpToUi(const FoldContext &ctx)
{
    double operand{};
    if (!ctx.getConstFloatOperand(0, operand) || !std::isfinite(operand))
        return FoldResult::unknown();

    double rounded = std::nearbyint(operand);
    if (!std::isfinite(rounded))
        return FoldResult::trap();

    constexpr double kMin = 0.0;
    constexpr double kMax = static_cast<double>(std::numeric_limits<unsigned long long>::max());
    if (rounded < kMin || rounded > kMax)
        return FoldResult::trap();

    return FoldResult::constant(
        Value::constInt(static_cast<long long>(static_cast<unsigned long long>(rounded))));
}

//===----------------------------------------------------------------------===//
// Boolean Operations: Zext1, Trunc1
//===----------------------------------------------------------------------===//

/// @brief Fold zero-extend from 1 bit (boolean to integer).
static FoldResult foldZext1(const FoldContext &ctx)
{
    long long operand{};
    if (!ctx.getConstIntOperand(0, operand))
        return FoldResult::unknown();
    return FoldResult::constant(Value::constInt((operand & 1) != 0 ? 1 : 0));
}

/// @brief Fold truncate to 1 bit (integer to boolean).
static FoldResult foldTrunc1(const FoldContext &ctx)
{
    long long operand{};
    if (!ctx.getConstIntOperand(0, operand))
        return FoldResult::unknown();
    return FoldResult::constant(Value::constBool((operand & 1) != 0));
}

//===----------------------------------------------------------------------===//
// Constant Materialization: ConstNull, ConstStr, AddrOf
//===----------------------------------------------------------------------===//

/// @brief Fold constant materialization instructions.
static FoldResult foldConstantMaterialization(const Instr &instr)
{
    switch (instr.op)
    {
        case Opcode::ConstNull:
            return FoldResult::constant(Value::null());
        case Opcode::ConstStr:
            if (!instr.operands.empty())
                return FoldResult::constant(instr.operands[0]);
            return FoldResult::unknown();
        case Opcode::AddrOf:
            if (!instr.operands.empty())
                return FoldResult::constant(instr.operands[0]);
            return FoldResult::unknown();
        default:
            return FoldResult::unknown();
    }
}

//===----------------------------------------------------------------------===//
// Section 3: SCCPSolver Class
//===----------------------------------------------------------------------===//

class SCCPSolver
{
  public:
    explicit SCCPSolver(Function &function)
        : function_(function), debug_(std::getenv("VIPER_SCCP_DEBUG") != nullptr)
    {
        initialiseStates();
    }

    void run()
    {
        if (function_.blocks.empty())
            return;

        markBlockExecutable(0);
        process();
        rewriteConstants();
        foldTerminators();
    }

  private:
    Function &function_;
    std::unordered_map<unsigned, ValueLattice> values_;
    std::unordered_map<unsigned, std::vector<Instr *>> uses_;
    std::unordered_map<Instr *, size_t> instrBlock_;
    std::unordered_map<std::string, size_t> blockIndex_;
    std::vector<bool> blockExecutable_;
    std::vector<bool> blockTraps_;
    bool debug_ = false;
    std::queue<size_t> blockWorklist_;
    std::queue<Instr *> instrWorklist_;
    std::unordered_set<Instr *> inInstrWorklist_;

    //===------------------------------------------------------------------===//
    // Initialization
    //===------------------------------------------------------------------===//

    void initialiseStates()
    {
        blockExecutable_.assign(function_.blocks.size(), false);
        blockTraps_.assign(function_.blocks.size(), false);
        for (size_t bi = 0; bi < function_.blocks.size(); ++bi)
        {
            blockIndex_[function_.blocks[bi].label] = bi;
        }

        auto registerValue = [&](unsigned id, bool overdefined)
        {
            auto &entry = values_[id];
            if (overdefined && !entry.isOverdefined())
                entry = ValueLattice::overdefined();
        };

        for (auto &param : function_.params)
            registerValue(param.id, true);

        for (size_t bi = 0; bi < function_.blocks.size(); ++bi)
        {
            BasicBlock &block = function_.blocks[bi];
            for (auto &param : block.params)
                registerValue(param.id, false);

            for (auto &instr : block.instructions)
            {
                instrBlock_[&instr] = bi;
                if (instr.result)
                    registerValue(*instr.result, false);

                for (auto &operand : instr.operands)
                {
                    if (operand.kind == Value::Kind::Temp)
                        uses_[operand.id].push_back(&instr);
                }
                for (auto &args : instr.brArgs)
                {
                    for (auto &arg : args)
                        if (arg.kind == Value::Kind::Temp)
                            uses_[arg.id].push_back(&instr);
                }
            }
        }
    }

    //===------------------------------------------------------------------===//
    // Lattice State Management
    //===------------------------------------------------------------------===//

    ValueLattice &valueState(unsigned id)
    {
        return values_[id];
    }

    const ValueLattice &valueState(unsigned id) const
    {
        auto it = values_.find(id);
        assert(it != values_.end());
        return it->second;
    }

    void markBlockExecutable(size_t index)
    {
        if (blockExecutable_[index])
            return;
        blockExecutable_[index] = true;
        if (debug_)
            std::cerr << "[sccp] executable block " << function_.blocks[index].label << "\n";
        blockWorklist_.push(index);
    }

    void markBlockTrap(size_t index)
    {
        if (blockTraps_[index])
            return;
        blockTraps_[index] = true;
        if (debug_)
            std::cerr << "[sccp] block " << function_.blocks[index].label << " known to trap\n";
    }

    void traceValueChange(unsigned id, std::string_view action, const Value *v = nullptr)
    {
        if (!debug_)
            return;
        std::cerr << "[sccp] " << action << " %" << id;
        if (v)
            std::cerr << " -> " << describeValue(*v);
        std::cerr << "\n";
    }

    void enqueueInstr(Instr &instr)
    {
        if (inInstrWorklist_.insert(&instr).second)
            instrWorklist_.push(&instr);
    }

    void enqueueUsers(unsigned id)
    {
        auto it = uses_.find(id);
        if (it == uses_.end())
            return;
        for (Instr *user : it->second)
        {
            auto bit = instrBlock_.find(user);
            if (bit == instrBlock_.end())
                continue;
            if (blockExecutable_[bit->second])
                enqueueInstr(*user);
        }
    }

    bool mergeConstant(unsigned id, const Value &v)
    {
        ValueLattice &state = valueState(id);
        if (state.mergeConstant(v))
        {
            traceValueChange(id, "const", &v);
            enqueueUsers(id);
            return true;
        }
        return false;
    }

    bool markOverdefined(unsigned id)
    {
        ValueLattice &state = valueState(id);
        if (state.markOverdefined())
        {
            traceValueChange(id, "overdefined");
            enqueueUsers(id);
            return true;
        }
        return false;
    }

    //===------------------------------------------------------------------===//
    // Value Resolution
    //===------------------------------------------------------------------===//

    bool resolveValue(const Value &operand, Value &out) const
    {
        switch (operand.kind)
        {
            case Value::Kind::ConstInt:
            case Value::Kind::ConstFloat:
            case Value::Kind::ConstStr:
            case Value::Kind::GlobalAddr:
            case Value::Kind::NullPtr:
                out = operand;
                return true;
            case Value::Kind::Temp:
            {
                auto it = values_.find(operand.id);
                if (it == values_.end())
                    return false;
                const ValueLattice &state = it->second;
                if (state.isConstant())
                {
                    out = state.value;
                    return true;
                }
                return false;
            }
        }
        return false;
    }

    bool operandOverdefined(const Value &operand) const
    {
        if (operand.kind != Value::Kind::Temp)
            return false;
        auto it = values_.find(operand.id);
        if (it == values_.end())
            return false;
        return it->second.isOverdefined();
    }

    //===------------------------------------------------------------------===//
    // Worklist Processing
    //===------------------------------------------------------------------===//

    void process()
    {
        while (!blockWorklist_.empty() || !instrWorklist_.empty())
        {
            if (!blockWorklist_.empty())
            {
                size_t blockIndex = blockWorklist_.front();
                blockWorklist_.pop();
                BasicBlock &block = function_.blocks[blockIndex];
                for (auto &instr : block.instructions)
                    enqueueInstr(instr);
                continue;
            }

            Instr *instr = instrWorklist_.front();
            instrWorklist_.pop();
            inInstrWorklist_.erase(instr);
            auto bit = instrBlock_.find(instr);
            if (bit == instrBlock_.end())
                continue;
            if (!blockExecutable_[bit->second])
                continue;
            visitInstruction(*instr, bit->second);
        }
    }

    //===------------------------------------------------------------------===//
    // Edge Propagation
    //===------------------------------------------------------------------===//

    void propagateEdge(size_t fromBlockIndex, Instr &terminator, size_t succSlot)
    {
        if (blockTraps_[fromBlockIndex])
            return;
        if (succSlot >= terminator.labels.size())
            return;
        const std::string &targetLabel = terminator.labels[succSlot];
        auto it = blockIndex_.find(targetLabel);
        if (it == blockIndex_.end())
            return;
        size_t succIndex = it->second;
        markBlockExecutable(succIndex);
        BasicBlock &succ = function_.blocks[succIndex];
        if (succSlot >= terminator.brArgs.size())
            return;
        const auto &args = terminator.brArgs[succSlot];
        for (size_t pi = 0; pi < succ.params.size() && pi < args.size(); ++pi)
        {
            const Value &arg = args[pi];
            Value resolved;
            if (resolveValue(arg, resolved))
            {
                mergeConstant(succ.params[pi].id, resolved);
            }
            else if (operandOverdefined(arg))
            {
                markOverdefined(succ.params[pi].id);
            }
        }
    }

    //===------------------------------------------------------------------===//
    // Instruction Visitors
    //===------------------------------------------------------------------===//

    void visitInstruction(Instr &instr, size_t blockIndex)
    {
        if (blockTraps_[blockIndex])
            return;

        switch (instr.op)
        {
            case Opcode::Br:
                propagateEdge(blockIndex, instr, 0);
                break;
            case Opcode::CBr:
                visitCBr(blockIndex, instr);
                break;
            case Opcode::SwitchI32:
                visitSwitch(blockIndex, instr);
                break;
            case Opcode::Trap:
            case Opcode::TrapFromErr:
            case Opcode::TrapErr:
            case Opcode::ResumeSame:
            case Opcode::ResumeNext:
            case Opcode::ResumeLabel:
                markBlockTrap(blockIndex);
                break;
            default:
                visitComputational(instr, blockIndex);
                break;
        }
    }

    void visitCBr(size_t blockIndex, Instr &instr)
    {
        if (instr.operands.empty())
            return;
        Value cond;
        if (resolveValue(instr.operands[0], cond))
        {
            bool truth = false;
            if (!getConstBool(cond, truth))
                return;
            if (truth)
                propagateEdge(blockIndex, instr, 0);
            else
                propagateEdge(blockIndex, instr, 1);
        }
        else if (operandOverdefined(instr.operands[0]))
        {
            propagateEdge(blockIndex, instr, 0);
            propagateEdge(blockIndex, instr, 1);
        }
    }

    void visitSwitch(size_t blockIndex, Instr &instr)
    {
        if (instr.operands.empty())
            return;
        Value scrut;
        if (resolveValue(instr.operands[0], scrut) && scrut.kind == Value::Kind::ConstInt)
        {
            bool matched = false;
            for (size_t ci = 0; ci < switchCaseCount(instr); ++ci)
            {
                const Value &caseVal = switchCaseValue(instr, ci);
                if (caseVal.kind == Value::Kind::ConstInt && caseVal.i64 == scrut.i64)
                {
                    propagateEdge(blockIndex, instr, ci + 1);
                    matched = true;
                    break;
                }
            }
            if (!matched)
                propagateEdge(blockIndex, instr, 0);
        }
        else if (operandOverdefined(instr.operands[0]))
        {
            for (size_t li = 0; li < instr.labels.size(); ++li)
                propagateEdge(blockIndex, instr, li);
        }
    }

    void visitComputational(Instr &instr, size_t blockIndex)
    {
        FoldResult folded = foldInstruction(instr);
        if (folded.isTrap())
        {
            markBlockTrap(blockIndex);
            return;
        }

        if (!instr.result)
            return;

        bool anyOverdefined = false;
        bool allConstants = !instr.operands.empty();
        for (auto &operand : instr.operands)
        {
            Value resolved;
            if (!resolveValue(operand, resolved))
            {
                allConstants = false;
                if (operandOverdefined(operand))
                    anyOverdefined = true;
            }
        }

        if (folded.isConstant())
        {
            mergeConstant(*instr.result, folded.value);
            return;
        }

        if (isAlwaysOverdefined(instr.op) || anyOverdefined || allConstants)
            markOverdefined(*instr.result);
    }

    //===------------------------------------------------------------------===//
    // Overdefined Classification
    //===------------------------------------------------------------------===//

    /// @brief Check if an opcode always produces overdefined results.
    /// @details Side-effecting operations and operations with external
    ///          dependencies cannot be constant-folded.
    bool isAlwaysOverdefined(Opcode op) const
    {
        switch (op)
        {
            // Memory operations
            case Opcode::Load:
            case Opcode::Alloca:
            case Opcode::GEP:
            case Opcode::Store:
            // Calls
            case Opcode::Call:
            // Exception handling
            case Opcode::ResumeSame:
            case Opcode::ResumeNext:
            case Opcode::ResumeLabel:
            case Opcode::EhPush:
            case Opcode::EhPop:
            case Opcode::Trap:
            case Opcode::TrapFromErr:
            case Opcode::TrapErr:
            case Opcode::ErrGetKind:
            case Opcode::ErrGetCode:
            case Opcode::ErrGetIp:
            case Opcode::ErrGetLine:
            // Runtime checks
            case Opcode::IdxChk:
                return true;
            default:
                return false;
        }
    }

    //===------------------------------------------------------------------===//
    // Main Folding Dispatch
    //===------------------------------------------------------------------===//

    /// @brief Attempt to fold an instruction to a constant value.
    /// @details Dispatches to family-specific fold functions based on opcode.
    FoldResult foldInstruction(const Instr &instr) const
    {
        // Create fold context with operand resolver
        FoldContext ctx{instr,
                        [this, &instr](size_t index, Value &out) -> bool
                        {
                            if (index >= instr.operands.size())
                                return false;
                            return resolveValue(instr.operands[index], out);
                        }};

        switch (instr.op)
        {
            //===--------------------------------------------------------------===//
            // Integer Arithmetic
            //===--------------------------------------------------------------===//
            case Opcode::Add:
            case Opcode::Sub:
            case Opcode::Mul:
            case Opcode::And:
            case Opcode::Or:
            case Opcode::Xor:
            case Opcode::Shl:
            case Opcode::LShr:
            case Opcode::AShr:
                return foldIntegerArithmetic(instr.op, ctx);

            //===--------------------------------------------------------------===//
            // Overflow-Checked Arithmetic
            //===--------------------------------------------------------------===//
            case Opcode::IAddOvf:
            case Opcode::ISubOvf:
            case Opcode::IMulOvf:
                return foldOverflowArithmetic(instr.op, ctx);

            //===--------------------------------------------------------------===//
            // Division and Remainder
            //===--------------------------------------------------------------===//
            case Opcode::SDivChk0:
            case Opcode::SRemChk0:
                return foldSignedDivRem(instr.op, ctx);

            case Opcode::UDivChk0:
            case Opcode::URemChk0:
                return foldUnsignedDivRem(instr.op, ctx);

            //===--------------------------------------------------------------===//
            // Floating-Point Arithmetic
            //===--------------------------------------------------------------===//
            case Opcode::FAdd:
            case Opcode::FSub:
            case Opcode::FMul:
            case Opcode::FDiv:
                return foldFloatArithmetic(instr.op, ctx);

            //===--------------------------------------------------------------===//
            // Integer Comparisons
            //===--------------------------------------------------------------===//
            case Opcode::ICmpEq:
            case Opcode::ICmpNe:
            case Opcode::SCmpLT:
            case Opcode::SCmpLE:
            case Opcode::SCmpGT:
            case Opcode::SCmpGE:
                return foldIntegerComparisons(instr.op, ctx);

            //===--------------------------------------------------------------===//
            // Unsigned Comparisons
            //===--------------------------------------------------------------===//
            case Opcode::UCmpLT:
            case Opcode::UCmpLE:
            case Opcode::UCmpGT:
            case Opcode::UCmpGE:
                return foldUnsignedComparisons(instr.op, ctx);

            //===--------------------------------------------------------------===//
            // Floating-Point Comparisons
            //===--------------------------------------------------------------===//
            case Opcode::FCmpEQ:
            case Opcode::FCmpNE:
            case Opcode::FCmpLT:
            case Opcode::FCmpLE:
            case Opcode::FCmpGT:
            case Opcode::FCmpGE:
                return foldFloatComparisons(instr.op, ctx);

            //===--------------------------------------------------------------===//
            // Type Conversions
            //===--------------------------------------------------------------===//
            case Opcode::CastSiToFp:
                return foldCastSiToFp(ctx);
            case Opcode::CastUiToFp:
                return foldCastUiToFp(ctx);
            case Opcode::CastFpToSiRteChk:
                return foldCastFpToSi(ctx);
            case Opcode::CastFpToUiRteChk:
                return foldCastFpToUi(ctx);

            //===--------------------------------------------------------------===//
            // Boolean Operations
            //===--------------------------------------------------------------===//
            case Opcode::Zext1:
                return foldZext1(ctx);
            case Opcode::Trunc1:
                return foldTrunc1(ctx);

            //===--------------------------------------------------------------===//
            // Constant Materialization
            //===--------------------------------------------------------------===//
            case Opcode::ConstNull:
            case Opcode::ConstStr:
            case Opcode::AddrOf:
                return foldConstantMaterialization(instr);

            default:
                return FoldResult::unknown();
        }
    }

    //===------------------------------------------------------------------===//
    // Rewriting Phase
    //===------------------------------------------------------------------===//

    void rewriteConstants()
    {
        for (auto &[id, state] : values_)
        {
            if (!state.isConstant())
                continue;
            replaceAllUses(id, state.value);
        }
    }

    /// @brief Replace all uses of a value with a constant using the pre-built use map.
    /// @details Uses the `uses_` map built during initialization for O(uses) replacement
    ///          instead of O(blocks × instructions) full traversal.
    void replaceAllUses(unsigned id, const Value &replacement)
    {
        auto usesIt = uses_.find(id);
        if (usesIt == uses_.end())
            return;

        for (Instr *instr : usesIt->second)
        {
            for (auto &operand : instr->operands)
                if (operand.kind == Value::Kind::Temp && operand.id == id)
                    operand = replacement;
            for (auto &args : instr->brArgs)
                for (auto &arg : args)
                    if (arg.kind == Value::Kind::Temp && arg.id == id)
                        arg = replacement;
        }
    }

    //===------------------------------------------------------------------===//
    // Terminator Folding
    //===------------------------------------------------------------------===//

    void foldTerminators()
    {
        for (size_t bi = 0; bi < function_.blocks.size(); ++bi)
        {
            if (!blockExecutable_[bi] || blockTraps_[bi])
                continue;
            BasicBlock &block = function_.blocks[bi];
            if (block.instructions.empty())
                continue;
            Instr &term = block.instructions.back();
            if (term.op == Opcode::CBr)
                rewriteConditional(term);
            else if (term.op == Opcode::SwitchI32)
                rewriteSwitch(term);
        }
    }

    void rewriteConditional(Instr &instr)
    {
        if (instr.operands.empty())
            return;
        Value cond;
        if (!resolveValue(instr.operands[0], cond))
            return;
        bool truth = false;
        if (!getConstBool(cond, truth))
            return;
        if (truth)
            convertToBranch(instr, 0);
        else
            convertToBranch(instr, 1);
    }

    void convertToBranch(Instr &instr, size_t succSlot)
    {
        if (succSlot >= instr.labels.size())
            return;
        std::string label = instr.labels[succSlot];
        std::vector<std::vector<Value>> args;
        if (succSlot < instr.brArgs.size())
            args.push_back(instr.brArgs[succSlot]);
        else
            args.emplace_back();
        instr.op = Opcode::Br;
        instr.operands.clear();
        instr.labels.clear();
        instr.labels.push_back(std::move(label));
        instr.brArgs = std::move(args);
        instr.type = Type(Type::Kind::Void);
    }

    void rewriteSwitch(Instr &instr)
    {
        if (instr.operands.empty())
            return;
        Value scrut;
        if (!resolveValue(instr.operands[0], scrut) || scrut.kind != Value::Kind::ConstInt)
            return;
        for (size_t ci = 0; ci < switchCaseCount(instr); ++ci)
        {
            const Value &caseVal = switchCaseValue(instr, ci);
            if (caseVal.kind == Value::Kind::ConstInt && caseVal.i64 == scrut.i64)
            {
                convertToBranch(instr, ci + 1);
                return;
            }
        }
        convertToBranch(instr, 0);
    }
};

void runSCCP(Function &function)
{
    SCCPSolver solver(function);
    solver.run();
}

} // namespace

//===----------------------------------------------------------------------===//
// Section 4: Public API
//===----------------------------------------------------------------------===//

void sccp(Module &module)
{
    for (auto &function : module.functions)
        runSCCP(function);
}

} // namespace il::transform
