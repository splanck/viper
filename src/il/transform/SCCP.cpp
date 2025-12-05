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
//    - LatticeValue struct and value comparison helpers
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
#include <limits>
#include <optional>
#include <queue>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

using namespace il::core;

namespace il::transform
{
namespace
{

//===----------------------------------------------------------------------===//
// Section 1: Lattice and Value Utilities
//===----------------------------------------------------------------------===//

/// @brief Three-point lattice for SCCP analysis.
/// @details Unknown < Constant < Overdefined
struct LatticeValue
{
    enum class Kind
    {
        Unknown,    ///< Value not yet determined
        Constant,   ///< Value is a known constant
        Overdefined ///< Value varies or cannot be determined
    };

    Kind kind = Kind::Unknown;
    Value constant{};
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
static std::optional<Value> foldIntegerArithmetic(Opcode op, const FoldContext &ctx)
{
    long long lhs{}, rhs{};
    if (!ctx.getConstIntOperand(0, lhs) || !ctx.getConstIntOperand(1, rhs))
        return std::nullopt;

    switch (op)
    {
        case Opcode::Add:
            return Value::constInt(lhs + rhs);
        case Opcode::Sub:
            return Value::constInt(lhs - rhs);
        case Opcode::Mul:
            return Value::constInt(lhs * rhs);
        case Opcode::And:
            return Value::constInt(lhs & rhs);
        case Opcode::Or:
            return Value::constInt(lhs | rhs);
        case Opcode::Xor:
            return Value::constInt(lhs ^ rhs);
        case Opcode::Shl:
            return Value::constInt(lhs << (rhs & 63));
        case Opcode::LShr:
            return Value::constInt(static_cast<unsigned long long>(lhs) >> (rhs & 63));
        case Opcode::AShr:
            return Value::constInt(lhs >> (rhs & 63));
        default:
            return std::nullopt;
    }
}

//===----------------------------------------------------------------------===//
// Overflow-Checked Arithmetic: IAddOvf, ISubOvf, IMulOvf
//===----------------------------------------------------------------------===//

/// @brief Fold overflow-checked arithmetic operations.
/// @details Returns nullopt if the operation would overflow at runtime.
static std::optional<Value> foldOverflowArithmetic(Opcode op, const FoldContext &ctx)
{
    long long lhs{}, rhs{};
    if (!ctx.getConstIntOperand(0, lhs) || !ctx.getConstIntOperand(1, rhs))
        return std::nullopt;

    switch (op)
    {
        case Opcode::IAddOvf:
            if (auto sum = checkedAdd(lhs, rhs))
                return Value::constInt(*sum);
            break;
        case Opcode::ISubOvf:
            if (auto diff = checkedSub(lhs, rhs))
                return Value::constInt(*diff);
            break;
        case Opcode::IMulOvf:
            if (auto prod = checkedMul(lhs, rhs))
                return Value::constInt(*prod);
            break;
        default:
            break;
    }
    return std::nullopt;
}

//===----------------------------------------------------------------------===//
// Division and Remainder: SDivChk0, SRemChk0, UDivChk0, URemChk0
//===----------------------------------------------------------------------===//

/// @brief Fold signed division/remainder with zero-check.
/// @details Returns nullopt for divide-by-zero or MIN/-1 overflow.
static std::optional<Value> foldSignedDivRem(Opcode op, const FoldContext &ctx)
{
    long long lhs{}, rhs{};
    if (!ctx.getConstIntOperand(0, lhs) || !ctx.getConstIntOperand(1, rhs))
        return std::nullopt;

    // Check for divide-by-zero and signed overflow (MIN / -1)
    if (rhs == 0 || (lhs == std::numeric_limits<long long>::min() && rhs == -1))
        return std::nullopt;

    if (op == Opcode::SDivChk0)
        return Value::constInt(lhs / rhs);
    return Value::constInt(lhs % rhs);
}

/// @brief Fold unsigned division/remainder with zero-check.
static std::optional<Value> foldUnsignedDivRem(Opcode op, const FoldContext &ctx)
{
    unsigned long long lhs{}, rhs{};
    if (!ctx.getConstUIntOperand(0, lhs) || !ctx.getConstUIntOperand(1, rhs))
        return std::nullopt;

    if (rhs == 0)
        return std::nullopt;

    if (op == Opcode::UDivChk0)
        return Value::constInt(static_cast<long long>(lhs / rhs));
    return Value::constInt(static_cast<long long>(lhs % rhs));
}

//===----------------------------------------------------------------------===//
// Floating-Point Arithmetic: FAdd, FSub, FMul, FDiv
//===----------------------------------------------------------------------===//

/// @brief Fold floating-point arithmetic operations.
static std::optional<Value> foldFloatArithmetic(Opcode op, const FoldContext &ctx)
{
    double lhs{}, rhs{};
    if (!ctx.getConstFloatOperand(0, lhs) || !ctx.getConstFloatOperand(1, rhs))
        return std::nullopt;

    switch (op)
    {
        case Opcode::FAdd:
            return Value::constFloat(lhs + rhs);
        case Opcode::FSub:
            return Value::constFloat(lhs - rhs);
        case Opcode::FMul:
            return Value::constFloat(lhs * rhs);
        case Opcode::FDiv:
            return rhs == 0.0 ? std::nullopt : std::optional<Value>(Value::constFloat(lhs / rhs));
        default:
            return std::nullopt;
    }
}

//===----------------------------------------------------------------------===//
// Integer Comparisons: ICmpEq, ICmpNe, SCmpLT, SCmpLE, SCmpGT, SCmpGE
//===----------------------------------------------------------------------===//

/// @brief Fold signed integer comparison operations.
static std::optional<Value> foldIntegerComparisons(Opcode op, const FoldContext &ctx)
{
    long long lhs{}, rhs{};
    if (!ctx.getConstIntOperand(0, lhs) || !ctx.getConstIntOperand(1, rhs))
        return std::nullopt;

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
            return std::nullopt;
    }
    return Value::constBool(result);
}

//===----------------------------------------------------------------------===//
// Unsigned Comparisons: UCmpLT, UCmpLE, UCmpGT, UCmpGE
//===----------------------------------------------------------------------===//

/// @brief Fold unsigned integer comparison operations.
static std::optional<Value> foldUnsignedComparisons(Opcode op, const FoldContext &ctx)
{
    unsigned long long lhs{}, rhs{};
    if (!ctx.getConstUIntOperand(0, lhs) || !ctx.getConstUIntOperand(1, rhs))
        return std::nullopt;

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
            return std::nullopt;
    }
    return Value::constBool(result);
}

//===----------------------------------------------------------------------===//
// Floating-Point Comparisons: FCmpEQ, FCmpNE, FCmpLT, FCmpLE, FCmpGT, FCmpGE
//===----------------------------------------------------------------------===//

/// @brief Fold floating-point comparison operations.
static std::optional<Value> foldFloatComparisons(Opcode op, const FoldContext &ctx)
{
    double lhs{}, rhs{};
    if (!ctx.getConstFloatOperand(0, lhs) || !ctx.getConstFloatOperand(1, rhs))
        return std::nullopt;

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
            return std::nullopt;
    }
    return Value::constBool(result);
}

//===----------------------------------------------------------------------===//
// Type Conversions: CastSiToFp, CastUiToFp, CastFpToSiRteChk, CastFpToUiRteChk
//===----------------------------------------------------------------------===//

/// @brief Fold signed integer to floating-point conversion.
static std::optional<Value> foldCastSiToFp(const FoldContext &ctx)
{
    long long operand{};
    if (!ctx.getConstIntOperand(0, operand))
        return std::nullopt;
    return Value::constFloat(static_cast<double>(operand));
}

/// @brief Fold unsigned integer to floating-point conversion.
static std::optional<Value> foldCastUiToFp(const FoldContext &ctx)
{
    unsigned long long operand{};
    if (!ctx.getConstUIntOperand(0, operand))
        return std::nullopt;
    return Value::constFloat(static_cast<double>(operand));
}

/// @brief Fold floating-point to signed integer conversion with range check.
static std::optional<Value> foldCastFpToSi(const FoldContext &ctx)
{
    double operand{};
    if (!ctx.getConstFloatOperand(0, operand) || !std::isfinite(operand))
        return std::nullopt;

    double rounded = std::nearbyint(operand);
    if (!std::isfinite(rounded))
        return std::nullopt;

    constexpr double kMin = static_cast<double>(std::numeric_limits<long long>::min());
    constexpr double kMax = static_cast<double>(std::numeric_limits<long long>::max());
    if (rounded < kMin || rounded > kMax)
        return std::nullopt;

    return Value::constInt(static_cast<long long>(rounded));
}

/// @brief Fold floating-point to unsigned integer conversion with range check.
static std::optional<Value> foldCastFpToUi(const FoldContext &ctx)
{
    double operand{};
    if (!ctx.getConstFloatOperand(0, operand) || !std::isfinite(operand))
        return std::nullopt;

    double rounded = std::nearbyint(operand);
    if (!std::isfinite(rounded))
        return std::nullopt;

    constexpr double kMin = 0.0;
    constexpr double kMax = static_cast<double>(std::numeric_limits<unsigned long long>::max());
    if (rounded < kMin || rounded > kMax)
        return std::nullopt;

    return Value::constInt(static_cast<long long>(static_cast<unsigned long long>(rounded)));
}

//===----------------------------------------------------------------------===//
// Boolean Operations: Zext1, Trunc1
//===----------------------------------------------------------------------===//

/// @brief Fold zero-extend from 1 bit (boolean to integer).
static std::optional<Value> foldZext1(const FoldContext &ctx)
{
    long long operand{};
    if (!ctx.getConstIntOperand(0, operand))
        return std::nullopt;
    return Value::constInt((operand & 1) != 0 ? 1 : 0);
}

/// @brief Fold truncate to 1 bit (integer to boolean).
static std::optional<Value> foldTrunc1(const FoldContext &ctx)
{
    long long operand{};
    if (!ctx.getConstIntOperand(0, operand))
        return std::nullopt;
    return Value::constBool((operand & 1) != 0);
}

//===----------------------------------------------------------------------===//
// Constant Materialization: ConstNull, ConstStr, AddrOf
//===----------------------------------------------------------------------===//

/// @brief Fold constant materialization instructions.
static std::optional<Value> foldConstantMaterialization(const Instr &instr)
{
    switch (instr.op)
    {
        case Opcode::ConstNull:
            return Value::null();
        case Opcode::ConstStr:
            if (!instr.operands.empty())
                return instr.operands[0];
            return std::nullopt;
        case Opcode::AddrOf:
            if (!instr.operands.empty())
                return instr.operands[0];
            return std::nullopt;
        default:
            return std::nullopt;
    }
}

//===----------------------------------------------------------------------===//
// Section 3: SCCPSolver Class
//===----------------------------------------------------------------------===//

class SCCPSolver
{
  public:
    explicit SCCPSolver(Function &function) : function_(function)
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
    std::unordered_map<unsigned, LatticeValue> values_;
    std::unordered_map<unsigned, std::vector<Instr *>> uses_;
    std::unordered_map<Instr *, size_t> instrBlock_;
    std::unordered_map<std::string, size_t> blockIndex_;
    std::vector<bool> blockExecutable_;
    std::queue<size_t> blockWorklist_;
    std::queue<Instr *> instrWorklist_;
    std::unordered_set<Instr *> inInstrWorklist_;

    //===------------------------------------------------------------------===//
    // Initialization
    //===------------------------------------------------------------------===//

    void initialiseStates()
    {
        blockExecutable_.assign(function_.blocks.size(), false);
        for (size_t bi = 0; bi < function_.blocks.size(); ++bi)
        {
            blockIndex_[function_.blocks[bi].label] = bi;
        }

        auto registerValue = [&](unsigned id, bool overdefined)
        {
            auto &entry = values_[id];
            if (overdefined)
                entry.kind = LatticeValue::Kind::Overdefined;
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

    LatticeValue &valueState(unsigned id)
    {
        return values_[id];
    }

    const LatticeValue &valueState(unsigned id) const
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
        blockWorklist_.push(index);
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
        LatticeValue &state = valueState(id);
        if (state.kind == LatticeValue::Kind::Unknown)
        {
            state.kind = LatticeValue::Kind::Constant;
            state.constant = v;
            enqueueUsers(id);
            return true;
        }
        if (state.kind == LatticeValue::Kind::Constant)
        {
            if (!valuesEqual(state.constant, v))
            {
                state.kind = LatticeValue::Kind::Overdefined;
                enqueueUsers(id);
                return true;
            }
            return false;
        }
        return false;
    }

    bool markOverdefined(unsigned id)
    {
        LatticeValue &state = valueState(id);
        if (state.kind == LatticeValue::Kind::Overdefined)
            return false;
        state.kind = LatticeValue::Kind::Overdefined;
        enqueueUsers(id);
        return true;
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
                const LatticeValue &state = it->second;
                if (state.kind == LatticeValue::Kind::Constant)
                {
                    out = state.constant;
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
        return it->second.kind == LatticeValue::Kind::Overdefined;
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
            visitInstruction(function_.blocks[bit->second], *instr, bit->second);
        }
    }

    //===------------------------------------------------------------------===//
    // Edge Propagation
    //===------------------------------------------------------------------===//

    void propagateEdge(size_t fromBlockIndex, Instr &terminator, size_t succSlot)
    {
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

    void visitInstruction(BasicBlock &, Instr &instr, size_t blockIndex)
    {
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
            default:
                visitComputational(instr);
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

    void visitComputational(Instr &instr)
    {
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

        std::optional<Value> folded = foldInstruction(instr);
        if (folded)
        {
            mergeConstant(*instr.result, *folded);
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
    std::optional<Value> foldInstruction(const Instr &instr) const
    {
        if (!instr.result)
            return std::nullopt;

        // Create fold context with operand resolver
        FoldContext ctx{
            instr,
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
                return std::nullopt;
        }
    }

    //===------------------------------------------------------------------===//
    // Rewriting Phase
    //===------------------------------------------------------------------===//

    void rewriteConstants()
    {
        for (auto &[id, state] : values_)
        {
            if (state.kind != LatticeValue::Kind::Constant)
                continue;
            replaceAllUses(id, state.constant);
        }
    }

    void replaceAllUses(unsigned id, const Value &replacement)
    {
        for (auto &block : function_.blocks)
        {
            for (auto &instr : block.instructions)
            {
                for (auto &operand : instr.operands)
                    if (operand.kind == Value::Kind::Temp && operand.id == id)
                        operand = replacement;
                for (auto &args : instr.brArgs)
                    for (auto &arg : args)
                        if (arg.kind == Value::Kind::Temp && arg.id == id)
                            arg = replacement;
            }
        }
    }

    //===------------------------------------------------------------------===//
    // Terminator Folding
    //===------------------------------------------------------------------===//

    void foldTerminators()
    {
        for (size_t bi = 0; bi < function_.blocks.size(); ++bi)
        {
            if (!blockExecutable_[bi])
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
