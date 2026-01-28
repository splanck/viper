// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.

#include "bytecode/BytecodeVM.hpp"
#include "il/core/Module.hpp"
#include "il/runtime/signatures/Registry.hpp"
#include "rt_threads.h"
#include "support/small_vector.hpp"
#include "viper/runtime/rt.h"
#include "vm/OpHandlerAccess.hpp"
#include "vm/RuntimeBridge.hpp"
#include "vm/VM.hpp"
#include "vm/VMContext.hpp"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <vector>

namespace viper
{
namespace bytecode
{

//===----------------------------------------------------------------------===//
// Thread-local active BytecodeVM tracking
//===----------------------------------------------------------------------===//

/// Thread-local pointer to the currently active BytecodeVM.
/// This enables runtime handlers (like Thread.Start) to detect when they're
/// being called from bytecode execution and handle threading correctly.
thread_local BytecodeVM *tlsActiveBytecodeVM = nullptr;

/// Thread-local pointer to the current BytecodeModule (for thread spawning).
thread_local const BytecodeModule *tlsActiveBytecodeModule = nullptr;

BytecodeVM *activeBytecodeVMInstance()
{
    return tlsActiveBytecodeVM;
}

const BytecodeModule *activeBytecodeModule()
{
    return tlsActiveBytecodeModule;
}

ActiveBytecodeVMGuard::ActiveBytecodeVMGuard(BytecodeVM *vm)
    : previous_(tlsActiveBytecodeVM), current_(vm)
{
    tlsActiveBytecodeVM = vm;
}

ActiveBytecodeVMGuard::~ActiveBytecodeVMGuard()
{
    tlsActiveBytecodeVM = previous_;
}

BytecodeVM::BytecodeVM()
    : module_(nullptr), state_(VMState::Ready), trapKind_(TrapKind::None), sp_(nullptr),
      fp_(nullptr), instrCount_(0), runtimeBridgeEnabled_(false),
      useThreadedDispatch_(true) // Default to faster threaded dispatch
      ,
      allocaTop_(0), singleStep_(false)
{
    // Pre-allocate reasonable stack size
    valueStack_.resize(kMaxStackSize * kMaxCallDepth);
    callStack_.reserve(kMaxCallDepth);

    // Pre-allocate alloca buffer (64KB should be sufficient for most cases)
    allocaBuffer_.resize(64 * 1024);
}

BytecodeVM::~BytecodeVM()
{
    // Release all cached rt_string objects
    for (rt_string s : stringCache_)
    {
        if (s)
        {
            rt_string_unref(s);
        }
    }
    stringCache_.clear();
}

/// @brief Initialize the string cache with runtime string objects.
///
/// Pre-creates rt_string objects for all strings in the module's string pool.
/// This is necessary because the runtime expects managed string pointers,
/// not raw C strings. The cache is reference-counted and released on
/// destruction or when a new module is loaded.
void BytecodeVM::initStringCache()
{
    // Release any existing cache
    for (rt_string s : stringCache_)
    {
        if (s)
        {
            rt_string_unref(s);
        }
    }
    stringCache_.clear();

    if (!module_)
        return;

    // Pre-create rt_string objects for all strings in the pool
    // The runtime expects rt_string (pointer to rt_string_impl), not raw C strings
    stringCache_.reserve(module_->stringPool.size());
    for (const auto &str : module_->stringPool)
    {
        // Use rt_const_cstr to create a proper runtime string object
        // This matches what the standard VM does in VMInit.cpp
        rt_string rtStr = rt_const_cstr(str.c_str());
        stringCache_.push_back(rtStr);
    }
}

void BytecodeVM::registerNativeHandler(const std::string &name, NativeHandler handler)
{
    nativeHandlers_[name] = std::move(handler);
}

void BytecodeVM::load(const BytecodeModule *module)
{
    module_ = module;
    state_ = VMState::Ready;
    trapKind_ = TrapKind::None;
    trapMessage_.clear();
    callStack_.clear();
    ehStack_.clear();
    sp_ = valueStack_.data();
    fp_ = nullptr;

    // Initialize string cache with proper rt_string objects
    initStringCache();
}

BCSlot BytecodeVM::exec(const std::string &funcName, const std::vector<BCSlot> &args)
{
    if (!module_)
    {
        trap(TrapKind::RuntimeError, "No module loaded");
        return BCSlot{};
    }

    const BytecodeFunction *func = module_->findFunction(funcName);
    if (!func)
    {
        trap(TrapKind::RuntimeError, "Function not found");
        return BCSlot{};
    }

    return exec(func, args);
}

BCSlot BytecodeVM::exec(const BytecodeFunction *func, const std::vector<BCSlot> &args)
{
    if (!module_)
    {
        trap(TrapKind::RuntimeError, "No module loaded");
        return BCSlot{};
    }

    // Set up thread-local context so Thread.Start handler can find us
    ActiveBytecodeVMGuard vmGuard(this);
    const BytecodeModule *prevModule = tlsActiveBytecodeModule;
    tlsActiveBytecodeModule = module_;

    // Reset state
    state_ = VMState::Ready;
    trapKind_ = TrapKind::None;
    callStack_.clear();
    sp_ = valueStack_.data();
    allocaTop_ = 0; // Reset alloca stack

    // Push arguments onto stack as initial locals
    for (const auto &arg : args)
    {
        *sp_++ = arg;
    }

    // Call the function
    call(func);

    // Check if call setup failed (e.g., stack overflow in first call)
    if (state_ == VMState::Trapped || !fp_)
    {
        if (!fp_ && state_ != VMState::Trapped)
        {
            trap(TrapKind::RuntimeError, "Frame setup failed");
        }
        tlsActiveBytecodeModule = prevModule;
        return BCSlot{};
    }

    // Run interpreter - use threaded dispatch if available and enabled
#if defined(__GNUC__) || defined(__clang__)
    if (useThreadedDispatch_)
    {
        runThreaded();
    }
    else
    {
        run();
    }
#else
    run();
#endif

    // Restore module thread-local
    tlsActiveBytecodeModule = prevModule;

    // Return result
    if (state_ == VMState::Halted && sp_ > valueStack_.data())
    {
        return *(sp_ - 1);
    }
    return BCSlot{};
}

void BytecodeVM::run()
{
    state_ = VMState::Running;

    while (state_ == VMState::Running)
    {
        // Fetch instruction
        uint32_t instr = fp_->func->code[fp_->pc++];
        BCOpcode op = decodeOpcode(instr);

        ++instrCount_;

        switch (op)
        {
            //==================================================================
            // Stack Operations
            //==================================================================
            case BCOpcode::NOP:
                break;

            case BCOpcode::DUP:
                *sp_ = *(sp_ - 1);
                sp_++;
                break;

            case BCOpcode::DUP2:
                sp_[0] = sp_[-2];
                sp_[1] = sp_[-1];
                sp_ += 2;
                break;

            case BCOpcode::POP:
                sp_--;
                break;

            case BCOpcode::POP2:
                sp_ -= 2;
                break;

            case BCOpcode::SWAP:
            {
                BCSlot tmp = sp_[-1];
                sp_[-1] = sp_[-2];
                sp_[-2] = tmp;
                break;
            }

            case BCOpcode::ROT3:
            {
                BCSlot tmp = sp_[-1];
                sp_[-1] = sp_[-2];
                sp_[-2] = sp_[-3];
                sp_[-3] = tmp;
                break;
            }

            //==================================================================
            // Local Variable Operations
            //==================================================================
            case BCOpcode::LOAD_LOCAL:
            {
                uint8_t idx = decodeArg8_0(instr);
                *sp_++ = fp_->locals[idx];
                break;
            }

            case BCOpcode::STORE_LOCAL:
            {
                uint8_t idx = decodeArg8_0(instr);
                fp_->locals[idx] = *--sp_;
                break;
            }

            case BCOpcode::LOAD_LOCAL_W:
            {
                uint16_t idx = decodeArg16(instr);
                *sp_++ = fp_->locals[idx];
                break;
            }

            case BCOpcode::STORE_LOCAL_W:
            {
                uint16_t idx = decodeArg16(instr);
                fp_->locals[idx] = *--sp_;
                break;
            }

            case BCOpcode::INC_LOCAL:
            {
                uint8_t idx = decodeArg8_0(instr);
                fp_->locals[idx].i64++;
                break;
            }

            case BCOpcode::DEC_LOCAL:
            {
                uint8_t idx = decodeArg8_0(instr);
                fp_->locals[idx].i64--;
                break;
            }

            //==================================================================
            // Constant Loading
            //==================================================================
            case BCOpcode::LOAD_I8:
            {
                int8_t val = decodeArgI8_0(instr);
                sp_->i64 = val;
                sp_++;
                break;
            }

            case BCOpcode::LOAD_I16:
            {
                int16_t val = decodeArgI16(instr);
                sp_->i64 = val;
                sp_++;
                break;
            }

            case BCOpcode::LOAD_I64:
            {
                uint16_t idx = decodeArg16(instr);
                sp_->i64 = module_->i64Pool[idx];
                sp_++;
                break;
            }

            case BCOpcode::LOAD_F64:
            {
                uint16_t idx = decodeArg16(instr);
                sp_->f64 = module_->f64Pool[idx];
                sp_++;
                break;
            }

            case BCOpcode::LOAD_NULL:
                sp_->ptr = nullptr;
                sp_++;
                break;

            case BCOpcode::LOAD_ZERO:
                sp_->i64 = 0;
                sp_++;
                break;

            case BCOpcode::LOAD_ONE:
                sp_->i64 = 1;
                sp_++;
                break;

            //==================================================================
            // Integer Arithmetic
            //==================================================================
            case BCOpcode::ADD_I64:
                sp_[-2].i64 = sp_[-2].i64 + sp_[-1].i64;
                sp_--;
                break;

            case BCOpcode::SUB_I64:
                sp_[-2].i64 = sp_[-2].i64 - sp_[-1].i64;
                sp_--;
                break;

            case BCOpcode::MUL_I64:
                sp_[-2].i64 = sp_[-2].i64 * sp_[-1].i64;
                sp_--;
                break;

            case BCOpcode::SDIV_I64:
                sp_[-2].i64 = sp_[-2].i64 / sp_[-1].i64;
                sp_--;
                break;

            case BCOpcode::UDIV_I64:
                sp_[-2].i64 = static_cast<int64_t>(static_cast<uint64_t>(sp_[-2].i64) /
                                                   static_cast<uint64_t>(sp_[-1].i64));
                sp_--;
                break;

            case BCOpcode::SREM_I64:
                sp_[-2].i64 = sp_[-2].i64 % sp_[-1].i64;
                sp_--;
                break;

            case BCOpcode::UREM_I64:
                sp_[-2].i64 = static_cast<int64_t>(static_cast<uint64_t>(sp_[-2].i64) %
                                                   static_cast<uint64_t>(sp_[-1].i64));
                sp_--;
                break;

            case BCOpcode::NEG_I64:
                sp_[-1].i64 = -sp_[-1].i64;
                break;

            case BCOpcode::ADD_I64_OVF:
            {
                // Target type encoded in arg: 0=I1, 1=I16, 2=I32, 3=I64
                uint8_t targetType = decodeArg8_0(instr);
                int64_t a = sp_[-2].i64, b = sp_[-1].i64;
                int64_t result = a + b;
                bool overflow = false;
                switch (targetType)
                {
                    case 1: // I16
                        overflow = (result < INT16_MIN || result > INT16_MAX);
                        break;
                    case 2: // I32
                        overflow = (result < INT32_MIN || result > INT32_MAX);
                        break;
                    default: // I64
                        overflow = addOverflow(a, b, result);
                        break;
                }
                if (overflow)
                {
                    if (!dispatchTrap(TrapKind::Overflow))
                    {
                        trap(TrapKind::Overflow, "Overflow: integer overflow in add");
                    }
                    break;
                }
                sp_[-2].i64 = result;
                sp_--;
                break;
            }

            case BCOpcode::SUB_I64_OVF:
            {
                // Target type encoded in arg: 0=I1, 1=I16, 2=I32, 3=I64
                uint8_t targetType = decodeArg8_0(instr);
                int64_t a = sp_[-2].i64, b = sp_[-1].i64;
                int64_t result = a - b;
                bool overflow = false;
                switch (targetType)
                {
                    case 1: // I16
                        overflow = (result < INT16_MIN || result > INT16_MAX);
                        break;
                    case 2: // I32
                        overflow = (result < INT32_MIN || result > INT32_MAX);
                        break;
                    default: // I64
                        overflow = subOverflow(a, b, result);
                        break;
                }
                if (overflow)
                {
                    if (!dispatchTrap(TrapKind::Overflow))
                    {
                        trap(TrapKind::Overflow, "Overflow: integer overflow in sub");
                    }
                    break;
                }
                sp_[-2].i64 = result;
                sp_--;
                break;
            }

            case BCOpcode::MUL_I64_OVF:
            {
                // Target type encoded in arg: 0=I1, 1=I16, 2=I32, 3=I64
                uint8_t targetType = decodeArg8_0(instr);
                int64_t a = sp_[-2].i64, b = sp_[-1].i64;
                int64_t result = a * b;
                bool overflow = false;
                switch (targetType)
                {
                    case 1: // I16
                        overflow = (result < INT16_MIN || result > INT16_MAX);
                        break;
                    case 2: // I32
                        overflow = (result < INT32_MIN || result > INT32_MAX);
                        break;
                    default: // I64
                        overflow = mulOverflow(a, b, result);
                        break;
                }
                if (overflow)
                {
                    if (!dispatchTrap(TrapKind::Overflow))
                    {
                        trap(TrapKind::Overflow, "Overflow: integer overflow in mul");
                    }
                    break;
                }
                sp_[-2].i64 = result;
                sp_--;
                break;
            }

            case BCOpcode::SDIV_I64_CHK:
                if (sp_[-1].i64 == 0)
                {
                    if (!dispatchTrap(TrapKind::DivisionByZero))
                    {
                        trap(TrapKind::DivisionByZero, "division by zero");
                    }
                    break;
                }
                sp_[-2].i64 = sp_[-2].i64 / sp_[-1].i64;
                sp_--;
                break;

            case BCOpcode::UDIV_I64_CHK:
                if (sp_[-1].i64 == 0)
                {
                    if (!dispatchTrap(TrapKind::DivisionByZero))
                    {
                        trap(TrapKind::DivisionByZero, "division by zero");
                    }
                    break;
                }
                sp_[-2].i64 = static_cast<int64_t>(static_cast<uint64_t>(sp_[-2].i64) /
                                                   static_cast<uint64_t>(sp_[-1].i64));
                sp_--;
                break;

            case BCOpcode::SREM_I64_CHK:
                if (sp_[-1].i64 == 0)
                {
                    if (!dispatchTrap(TrapKind::DivisionByZero))
                    {
                        trap(TrapKind::DivisionByZero, "division by zero");
                    }
                    break;
                }
                sp_[-2].i64 = sp_[-2].i64 % sp_[-1].i64;
                sp_--;
                break;

            case BCOpcode::UREM_I64_CHK:
                if (sp_[-1].i64 == 0)
                {
                    if (!dispatchTrap(TrapKind::DivisionByZero))
                    {
                        trap(TrapKind::DivisionByZero, "division by zero");
                    }
                    break;
                }
                sp_[-2].i64 = static_cast<int64_t>(static_cast<uint64_t>(sp_[-2].i64) %
                                                   static_cast<uint64_t>(sp_[-1].i64));
                sp_--;
                break;

            case BCOpcode::IDX_CHK:
            {
                // Stack: [idx, lo, hi]
                int64_t hi = sp_[-1].i64;
                int64_t lo = sp_[-2].i64;
                int64_t idx = sp_[-3].i64;
                if (idx < lo || idx >= hi)
                {
                    if (!dispatchTrap(TrapKind::IndexOutOfBounds))
                    {
                        trap(TrapKind::IndexOutOfBounds, "index out of bounds");
                    }
                    break;
                }
                sp_ -= 2; // Pop lo, hi; keep idx
                break;
            }

            //==================================================================
            // Float Arithmetic
            //==================================================================
            case BCOpcode::ADD_F64:
                sp_[-2].f64 = sp_[-2].f64 + sp_[-1].f64;
                sp_--;
                break;

            case BCOpcode::SUB_F64:
                sp_[-2].f64 = sp_[-2].f64 - sp_[-1].f64;
                sp_--;
                break;

            case BCOpcode::MUL_F64:
                sp_[-2].f64 = sp_[-2].f64 * sp_[-1].f64;
                sp_--;
                break;

            case BCOpcode::DIV_F64:
                sp_[-2].f64 = sp_[-2].f64 / sp_[-1].f64;
                sp_--;
                break;

            case BCOpcode::NEG_F64:
                sp_[-1].f64 = -sp_[-1].f64;
                break;

            //==================================================================
            // Bitwise Operations
            //==================================================================
            case BCOpcode::AND_I64:
                sp_[-2].i64 = sp_[-2].i64 & sp_[-1].i64;
                sp_--;
                break;

            case BCOpcode::OR_I64:
                sp_[-2].i64 = sp_[-2].i64 | sp_[-1].i64;
                sp_--;
                break;

            case BCOpcode::XOR_I64:
                sp_[-2].i64 = sp_[-2].i64 ^ sp_[-1].i64;
                sp_--;
                break;

            case BCOpcode::NOT_I64:
                sp_[-1].i64 = ~sp_[-1].i64;
                break;

            case BCOpcode::SHL_I64:
                sp_[-2].i64 = sp_[-2].i64 << (sp_[-1].i64 & 63);
                sp_--;
                break;

            case BCOpcode::LSHR_I64:
                sp_[-2].i64 =
                    static_cast<int64_t>(static_cast<uint64_t>(sp_[-2].i64) >> (sp_[-1].i64 & 63));
                sp_--;
                break;

            case BCOpcode::ASHR_I64:
                sp_[-2].i64 = sp_[-2].i64 >> (sp_[-1].i64 & 63);
                sp_--;
                break;

            //==================================================================
            // Integer Comparisons
            //==================================================================
            case BCOpcode::CMP_EQ_I64:
                sp_[-2].i64 = (sp_[-2].i64 == sp_[-1].i64) ? 1 : 0;
                sp_--;
                break;

            case BCOpcode::CMP_NE_I64:
                sp_[-2].i64 = (sp_[-2].i64 != sp_[-1].i64) ? 1 : 0;
                sp_--;
                break;

            case BCOpcode::CMP_SLT_I64:
                sp_[-2].i64 = (sp_[-2].i64 < sp_[-1].i64) ? 1 : 0;
                sp_--;
                break;

            case BCOpcode::CMP_SLE_I64:
                sp_[-2].i64 = (sp_[-2].i64 <= sp_[-1].i64) ? 1 : 0;
                sp_--;
                break;

            case BCOpcode::CMP_SGT_I64:
                sp_[-2].i64 = (sp_[-2].i64 > sp_[-1].i64) ? 1 : 0;
                sp_--;
                break;

            case BCOpcode::CMP_SGE_I64:
                sp_[-2].i64 = (sp_[-2].i64 >= sp_[-1].i64) ? 1 : 0;
                sp_--;
                break;

            case BCOpcode::CMP_ULT_I64:
                sp_[-2].i64 =
                    (static_cast<uint64_t>(sp_[-2].i64) < static_cast<uint64_t>(sp_[-1].i64)) ? 1
                                                                                              : 0;
                sp_--;
                break;

            case BCOpcode::CMP_ULE_I64:
                sp_[-2].i64 =
                    (static_cast<uint64_t>(sp_[-2].i64) <= static_cast<uint64_t>(sp_[-1].i64)) ? 1
                                                                                               : 0;
                sp_--;
                break;

            case BCOpcode::CMP_UGT_I64:
                sp_[-2].i64 =
                    (static_cast<uint64_t>(sp_[-2].i64) > static_cast<uint64_t>(sp_[-1].i64)) ? 1
                                                                                              : 0;
                sp_--;
                break;

            case BCOpcode::CMP_UGE_I64:
                sp_[-2].i64 =
                    (static_cast<uint64_t>(sp_[-2].i64) >= static_cast<uint64_t>(sp_[-1].i64)) ? 1
                                                                                               : 0;
                sp_--;
                break;

            //==================================================================
            // Float Comparisons
            //==================================================================
            case BCOpcode::CMP_EQ_F64:
                sp_[-2].i64 = (sp_[-2].f64 == sp_[-1].f64) ? 1 : 0;
                sp_--;
                break;

            case BCOpcode::CMP_NE_F64:
                sp_[-2].i64 = (sp_[-2].f64 != sp_[-1].f64) ? 1 : 0;
                sp_--;
                break;

            case BCOpcode::CMP_LT_F64:
                sp_[-2].i64 = (sp_[-2].f64 < sp_[-1].f64) ? 1 : 0;
                sp_--;
                break;

            case BCOpcode::CMP_LE_F64:
                sp_[-2].i64 = (sp_[-2].f64 <= sp_[-1].f64) ? 1 : 0;
                sp_--;
                break;

            case BCOpcode::CMP_GT_F64:
                sp_[-2].i64 = (sp_[-2].f64 > sp_[-1].f64) ? 1 : 0;
                sp_--;
                break;

            case BCOpcode::CMP_GE_F64:
                sp_[-2].i64 = (sp_[-2].f64 >= sp_[-1].f64) ? 1 : 0;
                sp_--;
                break;

            //==================================================================
            // Type Conversions
            //==================================================================
            case BCOpcode::I64_TO_F64:
                sp_[-1].f64 = static_cast<double>(sp_[-1].i64);
                break;

            case BCOpcode::U64_TO_F64:
                sp_[-1].f64 = static_cast<double>(static_cast<uint64_t>(sp_[-1].i64));
                break;

            case BCOpcode::F64_TO_I64:
                sp_[-1].i64 = static_cast<int64_t>(sp_[-1].f64);
                break;

            case BCOpcode::F64_TO_I64_CHK:
            {
                // Float to signed int64 with overflow check and round-to-even
                double val = sp_[-1].f64;
                // Check for NaN
                if (val != val)
                {
                    if (!dispatchTrap(TrapKind::InvalidCast))
                    {
                        trap(TrapKind::InvalidCast, "InvalidCast: float to int conversion of NaN");
                    }
                    break;
                }
                // Round to nearest, ties to even (banker's rounding)
                double rounded = std::rint(val);
                // Check for out of range (INT64_MIN to INT64_MAX)
                constexpr double maxI64 = 9223372036854775807.0;
                constexpr double minI64 = -9223372036854775808.0;
                if (rounded > maxI64 || rounded < minI64)
                {
                    if (!dispatchTrap(TrapKind::InvalidCast))
                    {
                        trap(TrapKind::InvalidCast,
                             "InvalidCast: float to int conversion overflow");
                    }
                    break;
                }
                sp_[-1].i64 = static_cast<int64_t>(rounded);
                break;
            }

            case BCOpcode::F64_TO_U64_CHK:
            {
                // Float to unsigned int64 with overflow check and round-to-even
                double val = sp_[-1].f64;
                // Check for NaN
                if (val != val)
                {
                    if (!dispatchTrap(TrapKind::InvalidCast))
                    {
                        trap(TrapKind::InvalidCast, "InvalidCast: float to uint conversion of NaN");
                    }
                    break;
                }
                // Round to nearest, ties to even (banker's rounding)
                double rounded = std::rint(val);
                // Check for out of range (0 to UINT64_MAX)
                constexpr double maxU64 = 18446744073709551615.0;
                if (rounded < 0.0 || rounded > maxU64)
                {
                    if (!dispatchTrap(TrapKind::InvalidCast))
                    {
                        trap(TrapKind::InvalidCast,
                             "InvalidCast: float to uint conversion overflow");
                    }
                    break;
                }
                sp_[-1].i64 = static_cast<int64_t>(static_cast<uint64_t>(rounded));
                break;
            }

            case BCOpcode::I64_NARROW_CHK:
            {
                // Signed narrow conversion with overflow check
                // Target type encoded in arg: 0=I1, 1=I16, 2=I32, 3=I64
                uint8_t targetType = decodeArg8_0(instr);
                int64_t val = sp_[-1].i64;
                bool inRange = true;
                switch (targetType)
                {
                    case 0: // I1 (boolean)
                        inRange = (val == 0 || val == 1);
                        break;
                    case 1: // I16
                        inRange = (val >= INT16_MIN && val <= INT16_MAX);
                        break;
                    case 2: // I32
                        inRange = (val >= INT32_MIN && val <= INT32_MAX);
                        break;
                    default: // I64 - always in range
                        break;
                }
                if (!inRange)
                {
                    if (!dispatchTrap(TrapKind::Overflow))
                    {
                        trap(TrapKind::Overflow, "Overflow: signed narrow conversion overflow");
                    }
                    break;
                }
                // Value stays the same (already narrowed semantically)
                break;
            }

            case BCOpcode::U64_NARROW_CHK:
            {
                // Unsigned narrow conversion with overflow check
                // Target type encoded in arg: 0=I1, 1=I16, 2=I32, 3=I64
                uint8_t targetType = decodeArg8_0(instr);
                uint64_t val = static_cast<uint64_t>(sp_[-1].i64);
                bool inRange = true;
                switch (targetType)
                {
                    case 0: // I1 (boolean)
                        inRange = (val <= 1);
                        break;
                    case 1: // I16
                        inRange = (val <= UINT16_MAX);
                        break;
                    case 2: // I32
                        inRange = (val <= UINT32_MAX);
                        break;
                    default: // I64 - always in range
                        break;
                }
                if (!inRange)
                {
                    if (!dispatchTrap(TrapKind::Overflow))
                    {
                        trap(TrapKind::Overflow, "Overflow: unsigned narrow conversion overflow");
                    }
                    break;
                }
                // Value stays the same (already narrowed semantically)
                break;
            }

            case BCOpcode::BOOL_TO_I64:
                // Already i64 with 0 or 1
                break;

            case BCOpcode::I64_TO_BOOL:
                sp_[-1].i64 = (sp_[-1].i64 != 0) ? 1 : 0;
                break;

            //==================================================================
            // Control Flow
            //==================================================================
            case BCOpcode::JUMP:
            {
                int16_t offset = decodeArgI16(instr);
                fp_->pc += offset;
                break;
            }

            case BCOpcode::JUMP_IF_TRUE:
            {
                int16_t offset = decodeArgI16(instr);
                if ((--sp_)->i64 != 0)
                {
                    fp_->pc += offset;
                }
                break;
            }

            case BCOpcode::JUMP_IF_FALSE:
            {
                int16_t offset = decodeArgI16(instr);
                if ((--sp_)->i64 == 0)
                {
                    fp_->pc += offset;
                }
                break;
            }

            case BCOpcode::JUMP_LONG:
            {
                int32_t offset = decodeArgI24(instr);
                fp_->pc += offset;
                break;
            }

            case BCOpcode::SWITCH:
            {
                // Format: SWITCH [numCases:u32] [defaultOffset:i32] [caseVal:i32 caseOffset:i32]...
                // Pop scrutinee from stack
                int32_t scrutinee = static_cast<int32_t>((--sp_)->i64);

                // pc currently points to the word after SWITCH opcode (numCases)
                const uint32_t *code = fp_->func->code.data();
                uint32_t numCases = code[fp_->pc++];

                // Position of default offset word
                uint32_t defaultOffsetPos = fp_->pc++;

                // Search for matching case
                bool found = false;
                for (uint32_t i = 0; i < numCases; ++i)
                {
                    int32_t caseVal = static_cast<int32_t>(code[fp_->pc++]);
                    uint32_t caseOffsetPos = fp_->pc++;

                    if (caseVal == scrutinee)
                    {
                        // Found matching case - jump to its target
                        // Offset is relative to the offset word position
                        int32_t caseOffset = static_cast<int32_t>(code[caseOffsetPos]);
                        fp_->pc = caseOffsetPos + caseOffset;
                        found = true;
                        break;
                    }
                }

                if (!found)
                {
                    // No match - use default offset
                    int32_t defaultOffset = static_cast<int32_t>(code[defaultOffsetPos]);
                    fp_->pc = defaultOffsetPos + defaultOffset;
                }
                break;
            }

            case BCOpcode::CALL:
            {
                uint16_t funcIdx = decodeArg16(instr);
                if (funcIdx < module_->functions.size())
                {
                    call(&module_->functions[funcIdx]);
                }
                else
                {
                    trap(TrapKind::RuntimeError, "Invalid function index");
                }
                break;
            }

            case BCOpcode::RETURN:
            {
                BCSlot result = *--sp_;
                if (!popFrame())
                {
                    // Return from main function
                    *sp_++ = result;
                    state_ = VMState::Halted;
                    return;
                }
                *sp_++ = result;
                break;
            }

            case BCOpcode::RETURN_VOID:
            {
                if (!popFrame())
                {
                    state_ = VMState::Halted;
                    return;
                }
                break;
            }

            case BCOpcode::CALL_NATIVE:
            {
                // Instruction format: CALL_NATIVE nativeIdx, argCount
                uint8_t nativeIdx = decodeArg8_0(instr);
                uint8_t argCount = decodeArg8_1(instr);

                if (nativeIdx >= module_->nativeFuncs.size())
                {
                    trap(TrapKind::RuntimeError, "Invalid native function index");
                    break;
                }

                const NativeFuncRef &ref = module_->nativeFuncs[nativeIdx];

                // Set up arguments (they're on the stack)
                BCSlot *args = sp_ - argCount;
                BCSlot result{};

                if (runtimeBridgeEnabled_)
                {
                    // Use RuntimeBridge for native function calls
                    // Convert BCSlot* to il::vm::Slot* (same layout)
                    il::vm::Slot *vmArgs = reinterpret_cast<il::vm::Slot *>(args);
                    std::vector<il::vm::Slot> argVec(vmArgs, vmArgs + argCount);

                    il::vm::RuntimeCallContext ctx;
                    il::vm::Slot vmResult = il::vm::RuntimeBridge::call(
                        ctx, ref.name, argVec, il::support::SourceLoc{}, "", "");

                    result.i64 = vmResult.i64;
                }
                else
                {
                    // Look up handler in local registry
                    auto it = nativeHandlers_.find(ref.name);
                    if (it == nativeHandlers_.end())
                    {
                        trap(TrapKind::RuntimeError, "Native function not registered");
                        break;
                    }
                    // Call the handler
                    it->second(args, argCount, &result);
                }

                // Pop arguments
                sp_ -= argCount;

                // Push result if function returns a value
                if (ref.hasReturn)
                {
                    *sp_++ = result;
                }
                break;
            }

            case BCOpcode::CALL_INDIRECT:
            {
                // Indirect call through function pointer
                // Stack layout: [callee][arg0][arg1]...[argN] <- sp
                uint8_t argCount = decodeArg8_0(instr);

                // Get callee from below the arguments
                BCSlot *callee = sp_ - argCount - 1;
                BCSlot *args = sp_ - argCount;

                // Check if callee is a tagged function pointer (high bit set)
                constexpr uint64_t kFuncPtrTag = 0x8000000000000000ULL;
                uint64_t calleeVal = static_cast<uint64_t>(callee->i64);

                if (calleeVal & kFuncPtrTag)
                {
                    // Tagged function index - extract and call
                    uint32_t funcIdx = static_cast<uint32_t>(calleeVal & 0x7FFFFFFFULL);
                    if (funcIdx >= module_->functions.size())
                    {
                        trap(TrapKind::RuntimeError, "Invalid indirect function index");
                        break;
                    }

                    // Shift arguments down to overwrite the callee slot
                    for (int i = 0; i < argCount; ++i)
                    {
                        callee[i] = args[i];
                    }
                    sp_ = callee + argCount; // Adjust stack pointer

                    call(&module_->functions[funcIdx]);
                }
                else if (calleeVal == 0)
                {
                    // Null function pointer
                    trap(TrapKind::NullPointer, "Null indirect callee");
                    break;
                }
                else
                {
                    // Unknown pointer format
                    trap(TrapKind::RuntimeError, "Invalid indirect call target");
                    break;
                }
                break;
            }

            //==================================================================
            // Memory Operations (basic support)
            //==================================================================
            case BCOpcode::ALLOCA:
            {
                // Allocate from the separate alloca buffer (not operand stack)
                // This ensures alloca'd memory survives across function calls
                int64_t size = (--sp_)->i64;
                // Align to 8 bytes
                size = (size + 7) & ~7;

                // Check for alloca overflow
                if (allocaTop_ + static_cast<size_t>(size) > allocaBuffer_.size())
                {
                    // Grow buffer if needed (up to 1MB limit)
                    size_t newSize = allocaBuffer_.size() * 2;
                    if (newSize > 1024 * 1024 || allocaTop_ + static_cast<size_t>(size) > newSize)
                    {
                        trap(TrapKind::StackOverflow, "alloca stack overflow");
                        break;
                    }
                    allocaBuffer_.resize(newSize);
                }

                // Return pointer to allocated memory
                void *ptr = allocaBuffer_.data() + allocaTop_;
                allocaTop_ += static_cast<size_t>(size);
                sp_->ptr = ptr;
                sp_++;
                break;
            }

            case BCOpcode::GEP:
            {
                int64_t offset = (--sp_)->i64;
                uint8_t *ptr = static_cast<uint8_t *>(sp_[-1].ptr);
                sp_[-1].ptr = ptr + offset;
                break;
            }

            case BCOpcode::LOAD_I64_MEM:
            {
                void *ptr = sp_[-1].ptr;
                int64_t val;
                std::memcpy(&val, ptr, sizeof(val));
                sp_[-1].i64 = val;
                break;
            }

            case BCOpcode::STORE_I64_MEM:
            {
                int64_t val = (--sp_)->i64;
                void *ptr = (--sp_)->ptr;
                std::memcpy(ptr, &val, sizeof(val));
                break;
            }

            case BCOpcode::LOAD_I8_MEM:
            {
                void *ptr = sp_[-1].ptr;
                int8_t val;
                std::memcpy(&val, ptr, sizeof(val));
                sp_[-1].i64 = val; // Sign extend
                break;
            }

            case BCOpcode::LOAD_I16_MEM:
            {
                void *ptr = sp_[-1].ptr;
                int16_t val;
                std::memcpy(&val, ptr, sizeof(val));
                sp_[-1].i64 = val; // Sign extend
                break;
            }

            case BCOpcode::LOAD_I32_MEM:
            {
                void *ptr = sp_[-1].ptr;
                int32_t val;
                std::memcpy(&val, ptr, sizeof(val));
                sp_[-1].i64 = val; // Sign extend
                break;
            }

            case BCOpcode::LOAD_F64_MEM:
            {
                void *ptr = sp_[-1].ptr;
                double val;
                std::memcpy(&val, ptr, sizeof(val));
                sp_[-1].f64 = val;
                break;
            }

            case BCOpcode::LOAD_PTR_MEM:
            {
                void *val;
                std::memcpy(&val, sp_[-1].ptr, sizeof(val));
                sp_[-1].ptr = val;
                break;
            }

            case BCOpcode::STORE_I8_MEM:
            {
                int8_t val = static_cast<int8_t>((--sp_)->i64);
                void *ptr = (--sp_)->ptr;
                std::memcpy(ptr, &val, sizeof(val));
                break;
            }

            case BCOpcode::STORE_I16_MEM:
            {
                int16_t val = static_cast<int16_t>((--sp_)->i64);
                void *ptr = (--sp_)->ptr;
                std::memcpy(ptr, &val, sizeof(val));
                break;
            }

            case BCOpcode::STORE_I32_MEM:
            {
                int32_t val = static_cast<int32_t>((--sp_)->i64);
                void *ptr = (--sp_)->ptr;
                std::memcpy(ptr, &val, sizeof(val));
                break;
            }

            case BCOpcode::STORE_F64_MEM:
            {
                double val = (--sp_)->f64;
                void *ptr = (--sp_)->ptr;
                std::memcpy(ptr, &val, sizeof(val));
                break;
            }

            case BCOpcode::STORE_PTR_MEM:
            {
                void *val = (--sp_)->ptr;
                void *ptr = (--sp_)->ptr;
                std::memcpy(ptr, &val, sizeof(val));
                break;
            }

            //==================================================================
            // Global Variables
            //==================================================================
            case BCOpcode::LOAD_GLOBAL:
            {
                // TODO: Implement global variable storage
                sp_->ptr = nullptr;
                sp_++;
                break;
            }

            case BCOpcode::STORE_GLOBAL:
            {
                // TODO: Implement global variable storage
                sp_--;
                break;
            }

            //==================================================================
            // String Operations
            //==================================================================
            case BCOpcode::LOAD_STR:
            {
                uint16_t idx = decodeArg16(instr);
                // Use the cached rt_string object (not raw C string!)
                // The runtime expects rt_string (pointer to rt_string_impl struct)
                sp_->ptr = (idx < stringCache_.size()) ? stringCache_[idx] : nullptr;
                sp_++;
                break;
            }

            case BCOpcode::STR_RETAIN:
                // For now, no-op (strings in constant pool don't need refcounting)
                break;

            case BCOpcode::STR_RELEASE:
                // For now, no-op (strings in constant pool don't need refcounting)
                sp_--;
                break;

            //==================================================================
            // Exception Handling
            //==================================================================
            case BCOpcode::EH_PUSH:
            {
                // Handler offset is in the next code word (raw i32 offset)
                const uint32_t *code = fp_->func->code.data();
                int32_t offset = static_cast<int32_t>(code[fp_->pc++]);
                uint32_t handlerPc =
                    static_cast<uint32_t>(static_cast<int32_t>(fp_->pc - 1) + offset);
                pushExceptionHandler(handlerPc);
                break;
            }

            case BCOpcode::EH_POP:
                popExceptionHandler();
                break;

            case BCOpcode::EH_ENTRY:
                // Handler entry marker - no-op, execution continues
                break;

            case BCOpcode::TRAP:
            {
                uint8_t kind = decodeArg8_0(instr);
                TrapKind trapKind = static_cast<TrapKind>(kind);
                if (!dispatchTrap(trapKind))
                {
                    trap(trapKind, "Unhandled trap");
                }
                break;
            }

            case BCOpcode::TRAP_FROM_ERR:
            {
                // Pop error code from stack and use as trap kind
                int64_t code = (--sp_)->i64;
                TrapKind trapKind = static_cast<TrapKind>(code);
                if (!dispatchTrap(trapKind))
                {
                    trap(trapKind, "Unhandled trap from error");
                }
                break;
            }

            case BCOpcode::ERR_GET_KIND:
            {
                // Pop error object from stack and return its kind
                // In our simple model, the error object IS the trap kind
                int64_t errVal = (--sp_)->i64;
                sp_->i64 = errVal; // Push the kind (same as error value)
                sp_++;
                break;
            }

            case BCOpcode::ERR_GET_CODE:
                // Get error code - maps trap kind to BASIC error code
                sp_->i64 = static_cast<int64_t>(currentErrorCode_);
                sp_++;
                break;

            case BCOpcode::ERR_GET_IP:
                // Get fault instruction pointer - return current PC
                sp_->i64 = static_cast<int64_t>(fp_->pc);
                sp_++;
                break;

            case BCOpcode::ERR_GET_LINE:
                // Get source line - we don't track this in bytecode VM, return -1
                sp_->i64 = -1;
                sp_++;
                break;

            case BCOpcode::RESUME_SAME:
                // Resume execution at the point of the trap
                // This requires the trap PC to be stored, which we don't track yet
                // For now, just continue (handler should set up return properly)
                break;

            case BCOpcode::RESUME_NEXT:
                // Resume execution after the faulting instruction
                // Similar to RESUME_SAME but skips the instruction
                break;

            case BCOpcode::RESUME_LABEL:
            {
                // Resume at a specific label (offset is in next code word)
                const uint32_t *code = fp_->func->code.data();
                int32_t offset = static_cast<int32_t>(code[fp_->pc++]);
                fp_->pc = static_cast<uint32_t>(static_cast<int32_t>(fp_->pc - 1) + offset);
                break;
            }

            //==================================================================
            // Default
            //==================================================================
            default:
                trap(TrapKind::InvalidOpcode, "Unknown opcode");
                break;
        }
    }
}

/// @brief Call a bytecode function, setting up a new stack frame.
/// @param func The function to call. Arguments must be pre-pushed on the stack.
///
/// Creates a new call frame with the function's parameters taken from the
/// operand stack. Non-parameter locals are zero-initialized.
void BytecodeVM::call(const BytecodeFunction *func)
{
    // Check stack overflow
    if (callStack_.size() >= kMaxCallDepth)
    {
        trap(TrapKind::StackOverflow, "call stack overflow");
        return;
    }

    // Save call site PC
    uint32_t callSitePc = fp_ ? fp_->pc - 1 : 0;

    // Arguments are already on stack - they become first N locals
    BCSlot *localsStart = sp_ - func->numParams;

    // Push new frame
    callStack_.push_back({});
    BCFrame &frame = callStack_.back();
    frame.func = func;
    frame.pc = 0;
    frame.locals = localsStart;
    frame.stackBase = localsStart + func->numLocals;
    frame.ehStackDepth = static_cast<uint32_t>(ehStack_.size());
    frame.callSitePc = callSitePc;
    frame.allocaBase = allocaTop_; // Save alloca position for cleanup on return

    // Zero non-parameter locals
    std::fill(localsStart + func->numParams, localsStart + func->numLocals, BCSlot{});

    // Update stack pointer past locals
    sp_ = frame.stackBase;

    // Switch to new frame
    fp_ = &callStack_.back();
}

/// @brief Pop the current call frame and return to the caller.
/// @return true if execution can continue in a parent frame, false if at top level.
///
/// Restores the previous frame's state including stack pointer and alloca
/// position. Any stack-allocated memory from the popped frame is released.
bool BytecodeVM::popFrame()
{
    // Restore alloca stack to the base of the popped frame
    // This releases all alloca'd memory from this function call
    allocaTop_ = callStack_.back().allocaBase;

    // Pop frame
    callStack_.pop_back();

    if (callStack_.empty())
    {
        fp_ = nullptr;
        return false;
    }

    // Restore previous frame
    fp_ = &callStack_.back();
    sp_ = fp_->stackBase;

    return true;
}

/// @brief Raise a trap, halting execution with an error.
/// @param kind The type of error that occurred.
/// @param message Human-readable description of the error.
void BytecodeVM::trap(TrapKind kind, const char *message)
{
    trapKind_ = kind;
    trapMessage_ = message;
    state_ = VMState::Trapped;
}

/// @brief Check for signed addition overflow.
/// @return true if overflow would occur, false if safe.
/// Uses compiler builtins when available for efficiency.
bool BytecodeVM::addOverflow(int64_t a, int64_t b, int64_t &result)
{
#if defined(__GNUC__) || defined(__clang__)
    return __builtin_add_overflow(a, b, &result);
#else
    if ((b > 0 && a > std::numeric_limits<int64_t>::max() - b) ||
        (b < 0 && a < std::numeric_limits<int64_t>::min() - b))
    {
        return true;
    }
    result = a + b;
    return false;
#endif
}

/// @brief Check for signed subtraction overflow.
/// @return true if overflow would occur, false if safe.
bool BytecodeVM::subOverflow(int64_t a, int64_t b, int64_t &result)
{
#if defined(__GNUC__) || defined(__clang__)
    return __builtin_sub_overflow(a, b, &result);
#else
    if ((b < 0 && a > std::numeric_limits<int64_t>::max() + b) ||
        (b > 0 && a < std::numeric_limits<int64_t>::min() + b))
    {
        return true;
    }
    result = a - b;
    return false;
#endif
}

/// @brief Check for signed multiplication overflow.
/// @return true if overflow would occur, false if safe.
bool BytecodeVM::mulOverflow(int64_t a, int64_t b, int64_t &result)
{
#if defined(__GNUC__) || defined(__clang__)
    return __builtin_mul_overflow(a, b, &result);
#else
    if (a > 0)
    {
        if (b > 0)
        {
            if (a > std::numeric_limits<int64_t>::max() / b)
                return true;
        }
        else
        {
            if (b < std::numeric_limits<int64_t>::min() / a)
                return true;
        }
    }
    else
    {
        if (b > 0)
        {
            if (a < std::numeric_limits<int64_t>::min() / b)
                return true;
        }
        else
        {
            if (a != 0 && b < std::numeric_limits<int64_t>::max() / a)
                return true;
        }
    }
    result = a * b;
    return false;
#endif
}

//==============================================================================
// Source Line Tracking
//==============================================================================

/// @brief Get the source line number at the current execution point.
/// @return The source line number, or 0 if not available.
uint32_t BytecodeVM::currentSourceLine() const
{
    if (!fp_ || !fp_->func)
        return 0;
    return getSourceLine(fp_->func, fp_->pc);
}

/// @brief Get the source line number for a specific PC in a function.
/// @param func The bytecode function.
/// @param pc The program counter offset.
/// @return The source line number, or 0 if not available.
///
/// Uses the function's line table to map bytecode offsets back to
/// source locations for debugging and error reporting.
uint32_t BytecodeVM::getSourceLine(const BytecodeFunction *func, uint32_t pc)
{
    if (!func || func->lineTable.empty())
        return 0;
    if (pc >= func->lineTable.size())
        return 0;
    return func->lineTable[pc];
}

//==============================================================================
// Exception Handling
//==============================================================================

/// @brief Push an exception handler onto the handler stack.
/// @param handlerPc The program counter of the handler entry point.
///
/// Captures the current frame index and stack pointer so the VM can unwind
/// to this state if a trap occurs within the protected region.
void BytecodeVM::pushExceptionHandler(uint32_t handlerPc)
{
    BCExceptionHandler eh;
    eh.handlerPc = handlerPc;
    eh.frameIndex = static_cast<uint32_t>(callStack_.size() - 1);
    eh.stackPointer = sp_;
    ehStack_.push_back(eh);
}

/// @brief Pop the most recently pushed exception handler.
///
/// Called when exiting a protected region normally (no exception occurred).
void BytecodeVM::popExceptionHandler()
{
    if (!ehStack_.empty())
    {
        ehStack_.pop_back();
    }
}

/// @brief Dispatch a trap to the nearest exception handler.
/// @param kind The type of trap that occurred.
/// @return true if a handler was found and jumped to, false if no handler exists.
///
/// Unwinds the call stack searching for a registered exception handler.
/// If found, restores the stack to the handler's saved state, pushes
/// error information onto the operand stack, and transfers control
/// to the handler. Returns false if the trap propagates to the top level.
bool BytecodeVM::dispatchTrap(TrapKind kind)
{
    // Search for a handler
    while (!ehStack_.empty())
    {
        BCExceptionHandler eh = ehStack_.back();
        ehStack_.pop_back();

        // Unwind call stack to the frame where handler was registered
        while (callStack_.size() > eh.frameIndex + 1)
        {
            callStack_.pop_back();
        }

        if (!callStack_.empty())
        {
            fp_ = &callStack_.back();
            sp_ = eh.stackPointer;

            // Store trap info for err.get_* introspection
            trapKind_ = kind;
            // Map trap kind to BASIC error code
            switch (kind)
            {
                case TrapKind::DivisionByZero:
                    currentErrorCode_ = 11;
                    break; // BASIC: Division by zero
                case TrapKind::Overflow:
                    currentErrorCode_ = 6;
                    break; // BASIC: Overflow
                case TrapKind::IndexOutOfBounds:
                    currentErrorCode_ = 9;
                    break; // BASIC: Subscript out of range
                case TrapKind::NullPointer:
                    currentErrorCode_ = 91;
                    break; // BASIC: Object variable not set
                default:
                    currentErrorCode_ = 0;
                    break;
            }

            // Push trap kind onto stack for handler to inspect (as error token)
            sp_->i64 = static_cast<int64_t>(kind);
            sp_++;
            // Push a dummy resume token (we don't really use it in bytecode VM)
            sp_->i64 = 0;
            sp_++;

            // Jump to handler
            fp_->pc = eh.handlerPc;
            state_ = VMState::Running;
            return true;
        }
    }

    // No handler found - trap propagates to top level
    return false;
}

//==============================================================================
// Debug Support
//==============================================================================

/// @brief Set a breakpoint at a specific location.
/// @param funcName The name of the function containing the breakpoint.
/// @param pc The program counter offset within the function.
void BytecodeVM::setBreakpoint(const std::string &funcName, uint32_t pc)
{
    breakpoints_[funcName].insert(pc);
}

/// @brief Clear a breakpoint at a specific location.
/// @param funcName The name of the function containing the breakpoint.
/// @param pc The program counter offset to clear.
void BytecodeVM::clearBreakpoint(const std::string &funcName, uint32_t pc)
{
    auto it = breakpoints_.find(funcName);
    if (it != breakpoints_.end())
    {
        it->second.erase(pc);
        if (it->second.empty())
        {
            breakpoints_.erase(it);
        }
    }
}

/// @brief Clear all breakpoints in all functions.
void BytecodeVM::clearAllBreakpoints()
{
    breakpoints_.clear();
}

/// @brief Check if execution should pause at the current location.
/// @return true if execution should pause (breakpoint hit or single-stepping).
///
/// Called at the start of each instruction. Invokes the debug callback
/// if a breakpoint is hit or single-step mode is enabled.
bool BytecodeVM::checkBreakpoint()
{
    if (!fp_ || !fp_->func)
        return false;

    bool isBreakpoint = false;
    auto it = breakpoints_.find(fp_->func->name);
    if (it != breakpoints_.end())
    {
        isBreakpoint = it->second.count(fp_->pc) > 0;
    }

    // Check if we should pause (breakpoint hit or single-stepping)
    if (isBreakpoint || singleStep_)
    {
        if (debugCallback_)
        {
            return !debugCallback_(*this, fp_->func, fp_->pc, isBreakpoint);
        }
        return true; // Pause if no callback but breakpoint/step triggered
    }
    return false;
}

//==============================================================================
// Threaded Interpreter (Computed Goto Dispatch)
//==============================================================================

#if defined(__GNUC__) || defined(__clang__)

void BytecodeVM::runThreaded()
{
    // Dispatch table for computed goto
    static void *dispatchTable[256] = {
        // Stack Operations (0x00-0x0F)
        [0x00] = &&L_NOP,
        [0x01] = &&L_DUP,
        [0x02] = &&L_DUP2,
        [0x03] = &&L_POP,
        [0x04] = &&L_POP2,
        [0x05] = &&L_SWAP,
        [0x06] = &&L_ROT3,
        // 0x07-0x0F -> default

        // Local Variable Operations (0x10-0x1F)
        [0x10] = &&L_LOAD_LOCAL,
        [0x11] = &&L_STORE_LOCAL,
        [0x12] = &&L_LOAD_LOCAL_W,
        [0x13] = &&L_STORE_LOCAL_W,
        [0x14] = &&L_INC_LOCAL,
        [0x15] = &&L_DEC_LOCAL,
        // 0x16-0x1F -> default

        // Constant Loading (0x20-0x2F)
        [0x20] = &&L_LOAD_I8,
        [0x21] = &&L_LOAD_I16,
        [0x22] = &&L_DEFAULT, // LOAD_I32 - not implemented
        [0x23] = &&L_LOAD_I64,
        [0x24] = &&L_LOAD_F64,
        [0x25] = &&L_LOAD_STR,
        [0x26] = &&L_LOAD_NULL,
        [0x27] = &&L_LOAD_ZERO,
        [0x28] = &&L_LOAD_ONE,
        [0x29] = &&L_LOAD_GLOBAL,
        [0x2A] = &&L_STORE_GLOBAL,

        // Integer Arithmetic (0x30-0x3F)
        [0x30] = &&L_ADD_I64,
        [0x31] = &&L_SUB_I64,
        [0x32] = &&L_MUL_I64,
        [0x33] = &&L_SDIV_I64,
        [0x34] = &&L_UDIV_I64,
        [0x35] = &&L_SREM_I64,
        [0x36] = &&L_UREM_I64,
        [0x37] = &&L_NEG_I64,
        [0x38] = &&L_ADD_I64_OVF,
        [0x39] = &&L_SUB_I64_OVF,
        [0x3A] = &&L_MUL_I64_OVF,
        [0x3B] = &&L_SDIV_I64_CHK,
        [0x3C] = &&L_UDIV_I64_CHK,
        [0x3D] = &&L_SREM_I64_CHK,
        [0x3E] = &&L_UREM_I64_CHK,
        [0x3F] = &&L_IDX_CHK,

        // Float Arithmetic (0x50-0x5F)
        [0x50] = &&L_ADD_F64,
        [0x51] = &&L_SUB_F64,
        [0x52] = &&L_MUL_F64,
        [0x53] = &&L_DIV_F64,
        [0x54] = &&L_NEG_F64,

        // Bitwise Operations (0x60-0x6F)
        [0x60] = &&L_AND_I64,
        [0x61] = &&L_OR_I64,
        [0x62] = &&L_XOR_I64,
        [0x63] = &&L_NOT_I64,
        [0x64] = &&L_SHL_I64,
        [0x65] = &&L_LSHR_I64,
        [0x66] = &&L_ASHR_I64,

        // Integer Comparisons (0x70-0x7F)
        [0x70] = &&L_CMP_EQ_I64,
        [0x71] = &&L_CMP_NE_I64,
        [0x72] = &&L_CMP_SLT_I64,
        [0x73] = &&L_CMP_SLE_I64,
        [0x74] = &&L_CMP_SGT_I64,
        [0x75] = &&L_CMP_SGE_I64,
        [0x76] = &&L_CMP_ULT_I64,
        [0x77] = &&L_CMP_ULE_I64,
        [0x78] = &&L_CMP_UGT_I64,
        [0x79] = &&L_CMP_UGE_I64,

        // Float Comparisons (0x80-0x8F)
        [0x80] = &&L_CMP_EQ_F64,
        [0x81] = &&L_CMP_NE_F64,
        [0x82] = &&L_CMP_LT_F64,
        [0x83] = &&L_CMP_LE_F64,
        [0x84] = &&L_CMP_GT_F64,
        [0x85] = &&L_CMP_GE_F64,

        // Type Conversions (0x90-0x9F)
        [0x90] = &&L_I64_TO_F64,
        [0x91] = &&L_U64_TO_F64,
        [0x92] = &&L_F64_TO_I64,
        [0x93] = &&L_F64_TO_I64_CHK,
        [0x94] = &&L_F64_TO_U64_CHK,
        [0x95] = &&L_I64_NARROW_CHK,
        [0x96] = &&L_U64_NARROW_CHK,
        [0x97] = &&L_BOOL_TO_I64,
        [0x98] = &&L_I64_TO_BOOL,

        // Memory Operations (0xA0-0xAF)
        [0xA0] = &&L_ALLOCA,
        [0xA1] = &&L_GEP,
        [0xA2] = &&L_LOAD_I8_MEM,
        [0xA3] = &&L_LOAD_I16_MEM,
        [0xA4] = &&L_LOAD_I32_MEM,
        [0xA5] = &&L_LOAD_I64_MEM,
        [0xA6] = &&L_LOAD_F64_MEM,
        [0xA7] = &&L_LOAD_PTR_MEM,
        [0xA8] = &&L_DEFAULT, // LOAD_STR_MEM
        [0xA9] = &&L_STORE_I8_MEM,
        [0xAA] = &&L_STORE_I16_MEM,
        [0xAB] = &&L_STORE_I32_MEM,
        [0xAC] = &&L_STORE_I64_MEM,
        [0xAD] = &&L_STORE_F64_MEM,
        [0xAE] = &&L_STORE_PTR_MEM,
        [0xAF] = &&L_DEFAULT, // STORE_STR_MEM

        // Control Flow (0xB0-0xBF)
        [0xB0] = &&L_JUMP,
        [0xB1] = &&L_JUMP_IF_TRUE,
        [0xB2] = &&L_JUMP_IF_FALSE,
        [0xB3] = &&L_JUMP_LONG,
        [0xB4] = &&L_SWITCH,
        [0xB5] = &&L_CALL,
        [0xB6] = &&L_CALL_NATIVE,
        [0xB7] = &&L_CALL_INDIRECT,
        [0xB8] = &&L_RETURN,
        [0xB9] = &&L_RETURN_VOID,
        [0xBA] = &&L_DEFAULT, // TAIL_CALL

        // Exception Handling (0xC0-0xCF)
        [0xC0] = &&L_EH_PUSH,
        [0xC1] = &&L_EH_POP,
        [0xC2] = &&L_EH_ENTRY,
        [0xC3] = &&L_TRAP,
        [0xC4] = &&L_TRAP_FROM_ERR,
        [0xC6] = &&L_ERR_GET_KIND,
        [0xC7] = &&L_ERR_GET_CODE,
        [0xC8] = &&L_ERR_GET_IP,
        [0xC9] = &&L_ERR_GET_LINE,
        [0xCA] = &&L_RESUME_SAME,
        [0xCB] = &&L_RESUME_NEXT,
        [0xCC] = &&L_RESUME_LABEL,
    };

    // Fill uninitialized entries with default handler (once only)
    static bool tableInitialized = false;
    if (!tableInitialized)
    {
        for (int i = 0; i < 256; i++)
        {
            if (dispatchTable[i] == nullptr)
            {
                dispatchTable[i] = &&L_DEFAULT;
            }
        }
        tableInitialized = true;
    }

    state_ = VMState::Running;

    // Local copies for performance
    const uint32_t *code = fp_->func->code.data();
    uint32_t pc = fp_->pc;
    BCSlot *sp = sp_;
    BCSlot *locals = fp_->locals;

    uint32_t instr;

#define DISPATCH()                                                                                 \
    do                                                                                             \
    {                                                                                              \
        instr = code[pc++];                                                                        \
        ++instrCount_;                                                                             \
        goto *dispatchTable[instr & 0xFF];                                                         \
    } while (0)

#define SYNC_STATE()                                                                               \
    do                                                                                             \
    {                                                                                              \
        fp_->pc = pc;                                                                              \
        sp_ = sp;                                                                                  \
    } while (0)

#define RELOAD_STATE()                                                                             \
    do                                                                                             \
    {                                                                                              \
        code = fp_->func->code.data();                                                             \
        pc = fp_->pc;                                                                              \
        sp = sp_;                                                                                  \
        locals = fp_->locals;                                                                      \
    } while (0)

    DISPATCH();

    // Stack Operations
L_NOP:
    DISPATCH();

L_DUP:
    *sp = *(sp - 1);
    sp++;
    DISPATCH();

L_DUP2:
    sp[0] = sp[-2];
    sp[1] = sp[-1];
    sp += 2;
    DISPATCH();

L_POP:
    sp--;
    DISPATCH();

L_POP2:
    sp -= 2;
    DISPATCH();

L_SWAP:
{
    BCSlot tmp = sp[-1];
    sp[-1] = sp[-2];
    sp[-2] = tmp;
    DISPATCH();
}

L_ROT3:
{
    BCSlot tmp = sp[-1];
    sp[-1] = sp[-2];
    sp[-2] = sp[-3];
    sp[-3] = tmp;
    DISPATCH();
}

    // Local Variable Operations
L_LOAD_LOCAL:
{
    uint8_t slot = decodeArg8_0(instr);
    *sp++ = locals[slot];
    DISPATCH();
}

L_STORE_LOCAL:
{
    uint8_t slot = decodeArg8_0(instr);
    locals[slot] = *--sp;
    DISPATCH();
}

L_LOAD_LOCAL_W:
    *sp++ = locals[decodeArg16(instr)];
    DISPATCH();

L_STORE_LOCAL_W:
    locals[decodeArg16(instr)] = *--sp;
    DISPATCH();

L_INC_LOCAL:
    locals[decodeArg8_0(instr)].i64++;
    DISPATCH();

L_DEC_LOCAL:
    locals[decodeArg8_0(instr)].i64--;
    DISPATCH();

    // Constant Loading
L_LOAD_I8:
    sp->i64 = decodeArgI8_0(instr);
    sp++;
    DISPATCH();

L_LOAD_I16:
    sp->i64 = decodeArgI16(instr);
    sp++;
    DISPATCH();

L_LOAD_I64:
    sp->i64 = module_->i64Pool[decodeArg16(instr)];
    sp++;
    DISPATCH();

L_LOAD_F64:
    sp->f64 = module_->f64Pool[decodeArg16(instr)];
    sp++;
    DISPATCH();

L_LOAD_STR:
{
    uint16_t idx = decodeArg16(instr);
    // Use the cached rt_string object (not raw C string!)
    // The runtime expects rt_string (pointer to rt_string_impl struct)
    sp->ptr = (idx < stringCache_.size()) ? stringCache_[idx] : nullptr;
    sp++;
    DISPATCH();
}

L_LOAD_NULL:
    sp->ptr = nullptr;
    sp++;
    DISPATCH();

L_LOAD_ZERO:
    sp->i64 = 0;
    sp++;
    DISPATCH();

L_LOAD_ONE:
    sp->i64 = 1;
    sp++;
    DISPATCH();

L_LOAD_GLOBAL:
    sp->ptr = nullptr; // TODO
    sp++;
    DISPATCH();

L_STORE_GLOBAL:
    sp--; // TODO
    DISPATCH();

    // Integer Arithmetic
L_ADD_I64:
    sp[-2].i64 += sp[-1].i64;
    sp--;
    DISPATCH();

L_SUB_I64:
    sp[-2].i64 -= sp[-1].i64;
    sp--;
    DISPATCH();

L_MUL_I64:
    sp[-2].i64 *= sp[-1].i64;
    sp--;
    DISPATCH();

L_SDIV_I64:
    sp[-2].i64 /= sp[-1].i64;
    sp--;
    DISPATCH();

L_UDIV_I64:
    sp[-2].i64 =
        static_cast<int64_t>(static_cast<uint64_t>(sp[-2].i64) / static_cast<uint64_t>(sp[-1].i64));
    sp--;
    DISPATCH();

L_SREM_I64:
    sp[-2].i64 %= sp[-1].i64;
    sp--;
    DISPATCH();

L_UREM_I64:
    sp[-2].i64 =
        static_cast<int64_t>(static_cast<uint64_t>(sp[-2].i64) % static_cast<uint64_t>(sp[-1].i64));
    sp--;
    DISPATCH();

L_NEG_I64:
    sp[-1].i64 = -sp[-1].i64;
    DISPATCH();

L_ADD_I64_OVF:
{
    // Target type encoded in arg: 0=I1, 1=I16, 2=I32, 3=I64
    uint8_t targetType = decodeArg8_0(instr);
    int64_t a = sp[-2].i64, b = sp[-1].i64;
    int64_t result = a + b;
    bool overflow = false;
    switch (targetType)
    {
        case 1: // I16
            overflow = (result < INT16_MIN || result > INT16_MAX);
            break;
        case 2: // I32
            overflow = (result < INT32_MIN || result > INT32_MAX);
            break;
        default: // I64
            overflow = addOverflow(a, b, result);
            break;
    }
    if (overflow)
    {
        SYNC_STATE();
        trap(TrapKind::Overflow, "Overflow: integer overflow in add");
        return;
    }
    sp[-2].i64 = result;
    sp--;
    DISPATCH();
}

L_SUB_I64_OVF:
{
    // Target type encoded in arg: 0=I1, 1=I16, 2=I32, 3=I64
    uint8_t targetType = decodeArg8_0(instr);
    int64_t a = sp[-2].i64, b = sp[-1].i64;
    int64_t result = a - b;
    bool overflow = false;
    switch (targetType)
    {
        case 1: // I16
            overflow = (result < INT16_MIN || result > INT16_MAX);
            break;
        case 2: // I32
            overflow = (result < INT32_MIN || result > INT32_MAX);
            break;
        default: // I64
            overflow = subOverflow(a, b, result);
            break;
    }
    if (overflow)
    {
        SYNC_STATE();
        trap(TrapKind::Overflow, "Overflow: integer overflow in sub");
        return;
    }
    sp[-2].i64 = result;
    sp--;
    DISPATCH();
}

L_MUL_I64_OVF:
{
    // Target type encoded in arg: 0=I1, 1=I16, 2=I32, 3=I64
    uint8_t targetType = decodeArg8_0(instr);
    int64_t a = sp[-2].i64, b = sp[-1].i64;
    int64_t result = a * b;
    bool overflow = false;
    switch (targetType)
    {
        case 1: // I16
            overflow = (result < INT16_MIN || result > INT16_MAX);
            break;
        case 2: // I32
            overflow = (result < INT32_MIN || result > INT32_MAX);
            break;
        default: // I64
            overflow = mulOverflow(a, b, result);
            break;
    }
    if (overflow)
    {
        SYNC_STATE();
        trap(TrapKind::Overflow, "Overflow: integer overflow in mul");
        return;
    }
    sp[-2].i64 = result;
    sp--;
    DISPATCH();
}

L_SDIV_I64_CHK:
    if (sp[-1].i64 == 0)
    {
        SYNC_STATE();
        sp_ = sp;
        if (!dispatchTrap(TrapKind::DivisionByZero))
        {
            trap(TrapKind::DivisionByZero, "division by zero");
            return;
        }
        RELOAD_STATE();
        DISPATCH();
    }
    sp[-2].i64 /= sp[-1].i64;
    sp--;
    DISPATCH();

L_UDIV_I64_CHK:
    if (sp[-1].i64 == 0)
    {
        SYNC_STATE();
        sp_ = sp;
        if (!dispatchTrap(TrapKind::DivisionByZero))
        {
            trap(TrapKind::DivisionByZero, "division by zero");
            return;
        }
        RELOAD_STATE();
        DISPATCH();
    }
    sp[-2].i64 =
        static_cast<int64_t>(static_cast<uint64_t>(sp[-2].i64) / static_cast<uint64_t>(sp[-1].i64));
    sp--;
    DISPATCH();

L_SREM_I64_CHK:
    if (sp[-1].i64 == 0)
    {
        SYNC_STATE();
        sp_ = sp;
        if (!dispatchTrap(TrapKind::DivisionByZero))
        {
            trap(TrapKind::DivisionByZero, "division by zero");
            return;
        }
        RELOAD_STATE();
        DISPATCH();
    }
    sp[-2].i64 %= sp[-1].i64;
    sp--;
    DISPATCH();

L_UREM_I64_CHK:
    if (sp[-1].i64 == 0)
    {
        SYNC_STATE();
        sp_ = sp;
        if (!dispatchTrap(TrapKind::DivisionByZero))
        {
            trap(TrapKind::DivisionByZero, "division by zero");
            return;
        }
        RELOAD_STATE();
        DISPATCH();
    }
    sp[-2].i64 =
        static_cast<int64_t>(static_cast<uint64_t>(sp[-2].i64) % static_cast<uint64_t>(sp[-1].i64));
    sp--;
    DISPATCH();

L_IDX_CHK:
{
    int64_t hi = sp[-1].i64;
    int64_t lo = sp[-2].i64;
    int64_t idx = sp[-3].i64;
    if (idx < lo || idx >= hi)
    {
        SYNC_STATE();
        trap(TrapKind::IndexOutOfBounds, "index out of bounds");
        return;
    }
    sp -= 2;
    DISPATCH();
}

    // Float Arithmetic
L_ADD_F64:
    sp[-2].f64 += sp[-1].f64;
    sp--;
    DISPATCH();

L_SUB_F64:
    sp[-2].f64 -= sp[-1].f64;
    sp--;
    DISPATCH();

L_MUL_F64:
    sp[-2].f64 *= sp[-1].f64;
    sp--;
    DISPATCH();

L_DIV_F64:
    sp[-2].f64 /= sp[-1].f64;
    sp--;
    DISPATCH();

L_NEG_F64:
    sp[-1].f64 = -sp[-1].f64;
    DISPATCH();

    // Bitwise Operations
L_AND_I64:
    sp[-2].i64 &= sp[-1].i64;
    sp--;
    DISPATCH();

L_OR_I64:
    sp[-2].i64 |= sp[-1].i64;
    sp--;
    DISPATCH();

L_XOR_I64:
    sp[-2].i64 ^= sp[-1].i64;
    sp--;
    DISPATCH();

L_NOT_I64:
    sp[-1].i64 = ~sp[-1].i64;
    DISPATCH();

L_SHL_I64:
    sp[-2].i64 <<= (sp[-1].i64 & 63);
    sp--;
    DISPATCH();

L_LSHR_I64:
    sp[-2].i64 = static_cast<int64_t>(static_cast<uint64_t>(sp[-2].i64) >> (sp[-1].i64 & 63));
    sp--;
    DISPATCH();

L_ASHR_I64:
    sp[-2].i64 >>= (sp[-1].i64 & 63);
    sp--;
    DISPATCH();

    // Integer Comparisons
L_CMP_EQ_I64:
    sp[-2].i64 = (sp[-2].i64 == sp[-1].i64) ? 1 : 0;
    sp--;
    DISPATCH();

L_CMP_NE_I64:
    sp[-2].i64 = (sp[-2].i64 != sp[-1].i64) ? 1 : 0;
    sp--;
    DISPATCH();

L_CMP_SLT_I64:
    sp[-2].i64 = (sp[-2].i64 < sp[-1].i64) ? 1 : 0;
    sp--;
    DISPATCH();

L_CMP_SLE_I64:
    sp[-2].i64 = (sp[-2].i64 <= sp[-1].i64) ? 1 : 0;
    sp--;
    DISPATCH();

L_CMP_SGT_I64:
    sp[-2].i64 = (sp[-2].i64 > sp[-1].i64) ? 1 : 0;
    sp--;
    DISPATCH();

L_CMP_SGE_I64:
    sp[-2].i64 = (sp[-2].i64 >= sp[-1].i64) ? 1 : 0;
    sp--;
    DISPATCH();

L_CMP_ULT_I64:
    sp[-2].i64 = (static_cast<uint64_t>(sp[-2].i64) < static_cast<uint64_t>(sp[-1].i64)) ? 1 : 0;
    sp--;
    DISPATCH();

L_CMP_ULE_I64:
    sp[-2].i64 = (static_cast<uint64_t>(sp[-2].i64) <= static_cast<uint64_t>(sp[-1].i64)) ? 1 : 0;
    sp--;
    DISPATCH();

L_CMP_UGT_I64:
    sp[-2].i64 = (static_cast<uint64_t>(sp[-2].i64) > static_cast<uint64_t>(sp[-1].i64)) ? 1 : 0;
    sp--;
    DISPATCH();

L_CMP_UGE_I64:
    sp[-2].i64 = (static_cast<uint64_t>(sp[-2].i64) >= static_cast<uint64_t>(sp[-1].i64)) ? 1 : 0;
    sp--;
    DISPATCH();

    // Float Comparisons
L_CMP_EQ_F64:
    sp[-2].i64 = (sp[-2].f64 == sp[-1].f64) ? 1 : 0;
    sp--;
    DISPATCH();

L_CMP_NE_F64:
    sp[-2].i64 = (sp[-2].f64 != sp[-1].f64) ? 1 : 0;
    sp--;
    DISPATCH();

L_CMP_LT_F64:
    sp[-2].i64 = (sp[-2].f64 < sp[-1].f64) ? 1 : 0;
    sp--;
    DISPATCH();

L_CMP_LE_F64:
    sp[-2].i64 = (sp[-2].f64 <= sp[-1].f64) ? 1 : 0;
    sp--;
    DISPATCH();

L_CMP_GT_F64:
    sp[-2].i64 = (sp[-2].f64 > sp[-1].f64) ? 1 : 0;
    sp--;
    DISPATCH();

L_CMP_GE_F64:
    sp[-2].i64 = (sp[-2].f64 >= sp[-1].f64) ? 1 : 0;
    sp--;
    DISPATCH();

    // Type Conversions
L_I64_TO_F64:
    sp[-1].f64 = static_cast<double>(sp[-1].i64);
    DISPATCH();

L_U64_TO_F64:
    sp[-1].f64 = static_cast<double>(static_cast<uint64_t>(sp[-1].i64));
    DISPATCH();

L_F64_TO_I64:
    sp[-1].i64 = static_cast<int64_t>(sp[-1].f64);
    DISPATCH();

L_BOOL_TO_I64:
    DISPATCH(); // No-op, already i64

L_I64_TO_BOOL:
    sp[-1].i64 = (sp[-1].i64 != 0) ? 1 : 0;
    DISPATCH();

L_I64_NARROW_CHK:
{
    // Signed narrow conversion with overflow check
    // Target type encoded in arg: 0=I1, 1=I16, 2=I32, 3=I64
    uint8_t targetType = decodeArg8_0(instr);
    int64_t val = sp[-1].i64;
    bool inRange = true;
    switch (targetType)
    {
        case 0: // I1 (boolean)
            inRange = (val == 0 || val == 1);
            break;
        case 1: // I16
            inRange = (val >= INT16_MIN && val <= INT16_MAX);
            break;
        case 2: // I32
            inRange = (val >= INT32_MIN && val <= INT32_MAX);
            break;
        default: // I64 - always in range
            break;
    }
    if (!inRange)
    {
        SYNC_STATE();
        trap(TrapKind::Overflow, "Overflow: signed narrow conversion overflow");
        return;
    }
    // Value stays the same (already narrowed semantically)
    DISPATCH();
}

L_U64_NARROW_CHK:
{
    // Unsigned narrow conversion with overflow check
    // Target type encoded in arg: 0=I1, 1=I16, 2=I32, 3=I64
    uint8_t targetType = decodeArg8_0(instr);
    uint64_t val = static_cast<uint64_t>(sp[-1].i64);
    bool inRange = true;
    switch (targetType)
    {
        case 0: // I1 (boolean)
            inRange = (val <= 1);
            break;
        case 1: // I16
            inRange = (val <= UINT16_MAX);
            break;
        case 2: // I32
            inRange = (val <= UINT32_MAX);
            break;
        default: // I64 - always in range
            break;
    }
    if (!inRange)
    {
        SYNC_STATE();
        trap(TrapKind::Overflow, "Overflow: unsigned narrow conversion overflow");
        return;
    }
    // Value stays the same (already narrowed semantically)
    DISPATCH();
}

L_F64_TO_I64_CHK:
{
    // Float to signed int64 with overflow check and round-to-even
    double val = sp[-1].f64;
    // Check for NaN
    if (val != val)
    {
        SYNC_STATE();
        trap(TrapKind::InvalidCast, "InvalidCast: float to int conversion of NaN");
        return;
    }
    // Round to nearest, ties to even (banker's rounding)
    double rounded = std::rint(val);
    // Check for out of range (INT64_MIN to INT64_MAX)
    // Note: comparing doubles, so we use slightly wider bounds
    constexpr double maxI64 = 9223372036854775807.0;  // INT64_MAX as double
    constexpr double minI64 = -9223372036854775808.0; // INT64_MIN as double
    if (rounded > maxI64 || rounded < minI64)
    {
        SYNC_STATE();
        trap(TrapKind::InvalidCast, "InvalidCast: float to int conversion overflow");
        return;
    }
    sp[-1].i64 = static_cast<int64_t>(rounded);
    DISPATCH();
}

L_F64_TO_U64_CHK:
{
    // Float to unsigned int64 with overflow check and round-to-even
    double val = sp[-1].f64;
    // Check for NaN
    if (val != val)
    {
        SYNC_STATE();
        trap(TrapKind::InvalidCast, "InvalidCast: float to uint conversion of NaN");
        return;
    }
    // Round to nearest, ties to even (banker's rounding)
    double rounded = std::rint(val);
    // Check for out of range (0 to UINT64_MAX)
    constexpr double maxU64 = 18446744073709551615.0; // UINT64_MAX as double
    if (rounded < 0.0 || rounded > maxU64)
    {
        SYNC_STATE();
        trap(TrapKind::InvalidCast, "InvalidCast: float to uint conversion overflow");
        return;
    }
    sp[-1].i64 = static_cast<int64_t>(static_cast<uint64_t>(rounded));
    DISPATCH();
}

    // Memory Operations
L_ALLOCA:
{
    // Allocate from the separate alloca buffer (not operand stack)
    int64_t size = (--sp)->i64;
    // Align to 8 bytes
    size = (size + 7) & ~7;

    // Check for alloca overflow
    if (allocaTop_ + static_cast<size_t>(size) > allocaBuffer_.size())
    {
        size_t newSize = allocaBuffer_.size() * 2;
        if (newSize > 1024 * 1024 || allocaTop_ + static_cast<size_t>(size) > newSize)
        {
            trap(TrapKind::StackOverflow, "alloca stack overflow");
            return;
        }
        allocaBuffer_.resize(newSize);
    }

    void *ptr = allocaBuffer_.data() + allocaTop_;
    allocaTop_ += static_cast<size_t>(size);
    sp->ptr = ptr;
    sp++;
    DISPATCH();
}

L_GEP:
{
    int64_t offset = (--sp)->i64;
    uint8_t *ptr = static_cast<uint8_t *>(sp[-1].ptr);
    // Null pointer check for debugging field access
    if (ptr == nullptr && offset != 0)
    {
        SYNC_STATE();
        fprintf(stderr, "*** NULL POINTER DEREFERENCE ***\n");
        fprintf(stderr, "Attempting to access field at offset %lld on null object\n",
                (long long)offset);
        fprintf(stderr, "PC: %u, Function: %s\n", pc - 1,
                fp_->func->name.empty() ? "<unknown>" : fp_->func->name.c_str());
        // Print call stack
        fprintf(stderr, "Call stack:\n");
        for (size_t i = 0; i < callStack_.size(); i++)
        {
            const auto &frame = callStack_[i];
            fprintf(stderr, "  [%zu] %s (PC: %u)\n", i,
                    frame.func->name.empty() ? "<unknown>" : frame.func->name.c_str(),
                    frame.pc);
        }
        fflush(stderr);
        trapKind_ = TrapKind::NullPointer;
        state_ = VMState::Trapped;
        return;
    }
    sp[-1].ptr = ptr + offset;
    DISPATCH();
}

L_LOAD_I8_MEM:
{
    int8_t val;
    std::memcpy(&val, sp[-1].ptr, sizeof(val));
    sp[-1].i64 = val;
    DISPATCH();
}

L_LOAD_I16_MEM:
{
    int16_t val;
    std::memcpy(&val, sp[-1].ptr, sizeof(val));
    sp[-1].i64 = val;
    DISPATCH();
}

L_LOAD_I32_MEM:
{
    int32_t val;
    std::memcpy(&val, sp[-1].ptr, sizeof(val));
    sp[-1].i64 = val;
    DISPATCH();
}

L_LOAD_I64_MEM:
{
    void *addr = sp[-1].ptr;
    // Null/invalid pointer check for debugging
    if (addr == nullptr || reinterpret_cast<uintptr_t>(addr) < 4096)
    {
        SYNC_STATE();
        fprintf(stderr, "*** NULL POINTER READ (i64) ***\n");
        fprintf(stderr, "Attempting to read from address %p\n", addr);
        fprintf(stderr, "PC: %u, Function: %s\n", pc - 1,
                fp_->func->name.empty() ? "<unknown>" : fp_->func->name.c_str());
        // Print call stack
        fprintf(stderr, "Call stack:\n");
        for (size_t i = 0; i < callStack_.size(); i++)
        {
            const auto &frame = callStack_[i];
            fprintf(stderr, "  [%zu] %s (PC: %u)\n", i,
                    frame.func->name.empty() ? "<unknown>" : frame.func->name.c_str(),
                    frame.pc);
        }
        trapKind_ = TrapKind::NullPointer;
        state_ = VMState::Trapped;
        return;
    }
    int64_t val;
    std::memcpy(&val, addr, sizeof(val));
    sp[-1].i64 = val;
    DISPATCH();
}

L_LOAD_F64_MEM:
{
    double val;
    std::memcpy(&val, sp[-1].ptr, sizeof(val));
    sp[-1].f64 = val;
    DISPATCH();
}

L_LOAD_PTR_MEM:
{
    void *addr = sp[-1].ptr;
    // Null/invalid pointer check for debugging
    if (addr == nullptr || reinterpret_cast<uintptr_t>(addr) < 4096)
    {
        SYNC_STATE();
        fprintf(stderr, "*** NULL POINTER READ (ptr) ***\n");
        fprintf(stderr, "Attempting to read from address %p\n", addr);
        fprintf(stderr, "PC: %u, Function: %s\n", pc - 1,
                fp_->func->name.empty() ? "<unknown>" : fp_->func->name.c_str());
        // Print call stack
        fprintf(stderr, "Call stack:\n");
        for (size_t i = 0; i < callStack_.size(); i++)
        {
            const auto &frame = callStack_[i];
            fprintf(stderr, "  [%zu] %s (PC: %u)\n", i,
                    frame.func->name.empty() ? "<unknown>" : frame.func->name.c_str(),
                    frame.pc);
        }
        trapKind_ = TrapKind::NullPointer;
        state_ = VMState::Trapped;
        return;
    }
    void *val;
    std::memcpy(&val, addr, sizeof(val));
    sp[-1].ptr = val;
    DISPATCH();
}

L_STORE_I8_MEM:
{
    int8_t val = static_cast<int8_t>((--sp)->i64);
    void *ptr = (--sp)->ptr;
    std::memcpy(ptr, &val, sizeof(val));
    DISPATCH();
}

L_STORE_I16_MEM:
{
    int16_t val = static_cast<int16_t>((--sp)->i64);
    void *ptr = (--sp)->ptr;
    std::memcpy(ptr, &val, sizeof(val));
    DISPATCH();
}

L_STORE_I32_MEM:
{
    int32_t val = static_cast<int32_t>((--sp)->i64);
    void *ptr = (--sp)->ptr;
    std::memcpy(ptr, &val, sizeof(val));
    DISPATCH();
}

L_STORE_I64_MEM:
{
    int64_t val = (--sp)->i64;
    void *ptr = (--sp)->ptr;
    std::memcpy(ptr, &val, sizeof(val));
    DISPATCH();
}

L_STORE_F64_MEM:
{
    double val = (--sp)->f64;
    void *ptr = (--sp)->ptr;
    std::memcpy(ptr, &val, sizeof(val));
    DISPATCH();
}

L_STORE_PTR_MEM:
{
    void *val = (--sp)->ptr;
    void *ptr = (--sp)->ptr;
    std::memcpy(ptr, &val, sizeof(val));
    DISPATCH();
}

    // Control Flow
L_JUMP:
    pc += decodeArgI16(instr);
    DISPATCH();

L_JUMP_IF_TRUE:
    if ((--sp)->i64 != 0)
    {
        pc += decodeArgI16(instr);
    }
    DISPATCH();

L_JUMP_IF_FALSE:
    if ((--sp)->i64 == 0)
    {
        pc += decodeArgI16(instr);
    }
    DISPATCH();

L_JUMP_LONG:
    pc += decodeArgI24(instr);
    DISPATCH();

L_SWITCH:
{
    // Format: SWITCH [numCases:u32] [defaultOffset:i32] [caseVal:i32 caseOffset:i32]...
    // Pop scrutinee from stack
    int32_t scrutinee = static_cast<int32_t>((--sp)->i64);

    // pc currently points to the word after SWITCH opcode (numCases)
    uint32_t numCases = code[pc++];

    // Position of default offset word
    uint32_t defaultOffsetPos = pc++;

    // Search for matching case
    bool found = false;
    for (uint32_t i = 0; i < numCases; ++i)
    {
        int32_t caseVal = static_cast<int32_t>(code[pc++]);
        uint32_t caseOffsetPos = pc++;

        if (caseVal == scrutinee)
        {
            // Found matching case - jump to its target
            // Offset is relative to the offset word position (same as EH_PUSH)
            int32_t caseOffset = static_cast<int32_t>(code[caseOffsetPos]);
            pc = caseOffsetPos + caseOffset;
            found = true;
            break;
        }
    }

    if (!found)
    {
        // No match - use default offset
        int32_t defaultOffset = static_cast<int32_t>(code[defaultOffsetPos]);
        pc = defaultOffsetPos + defaultOffset;
    }

    DISPATCH();
}

L_CALL:
{
    uint16_t funcIdx = decodeArg16(instr);
    if (funcIdx >= module_->functions.size())
    {
        SYNC_STATE();
        trap(TrapKind::RuntimeError, "Invalid function index");
        return;
    }
    SYNC_STATE();
    sp_ = sp;
    fp_->pc = pc;
    call(&module_->functions[funcIdx]);
    RELOAD_STATE();
    DISPATCH();
}

L_CALL_NATIVE:
{
    uint8_t nativeIdx = decodeArg8_0(instr);
    uint8_t argCount = decodeArg8_1(instr);

    if (nativeIdx >= module_->nativeFuncs.size())
    {
        SYNC_STATE();
        trap(TrapKind::RuntimeError, "Invalid native function index");
        return;
    }

    const NativeFuncRef &ref = module_->nativeFuncs[nativeIdx];
    BCSlot *args = sp - argCount;
    BCSlot result{};

    if (runtimeBridgeEnabled_)
    {
        // Use RuntimeBridge for native function calls
        il::vm::Slot *vmArgs = reinterpret_cast<il::vm::Slot *>(args);
        std::vector<il::vm::Slot> argVec(vmArgs, vmArgs + argCount);

        il::vm::RuntimeCallContext ctx;
        il::vm::Slot vmResult =
            il::vm::RuntimeBridge::call(ctx, ref.name, argVec, il::support::SourceLoc{}, "", "");

        result.i64 = vmResult.i64;
    }
    else
    {
        auto it = nativeHandlers_.find(ref.name);
        if (it == nativeHandlers_.end())
        {
            SYNC_STATE();
            trap(TrapKind::RuntimeError, "Native function not registered");
            return;
        }
        it->second(args, argCount, &result);
    }

    sp -= argCount;
    if (ref.hasReturn)
    {
        *sp++ = result;
    }
    DISPATCH();
}

L_CALL_INDIRECT:
{
    // Indirect call through function pointer
    // Stack layout: [callee][arg0][arg1]...[argN] <- sp
    uint8_t argCount = decodeArg8_0(instr);

    // Get callee from below the arguments
    BCSlot *callee = sp - argCount - 1;
    BCSlot *args = sp - argCount;

    // Check if callee is a tagged function pointer (high bit set)
    constexpr uint64_t kFuncPtrTag = 0x8000000000000000ULL;
    uint64_t calleeVal = static_cast<uint64_t>(callee->i64);

    if (calleeVal & kFuncPtrTag)
    {
        // Tagged function index - extract and call
        uint32_t funcIdx = static_cast<uint32_t>(calleeVal & 0x7FFFFFFFULL);
        if (funcIdx >= module_->functions.size())
        {
            SYNC_STATE();
            trap(TrapKind::RuntimeError, "Invalid indirect function index");
            return;
        }

        // Shift arguments down to overwrite the callee slot
        // This is needed because call() expects args at the top of stack
        for (int i = 0; i < argCount; ++i)
        {
            callee[i] = args[i];
        }
        sp = callee + argCount; // Adjust stack pointer

        SYNC_STATE();
        sp_ = sp;
        fp_->pc = pc;
        call(&module_->functions[funcIdx]);
        RELOAD_STATE();
    }
    else if (calleeVal == 0)
    {
        // Null function pointer
        SYNC_STATE();
        trap(TrapKind::NullPointer, "Null indirect callee");
        return;
    }
    else
    {
        // Unknown pointer format - try runtime bridge
        // (This handles cases where the function pointer came from runtime)
        SYNC_STATE();
        trap(TrapKind::RuntimeError, "Invalid indirect call target");
        return;
    }
    DISPATCH();
}

L_RETURN:
{
    BCSlot result = *--sp;
    SYNC_STATE();
    sp_ = sp;
    if (!popFrame())
    {
        *sp_++ = result;
        state_ = VMState::Halted;
        return;
    }
    RELOAD_STATE();
    *sp++ = result;
    DISPATCH();
}

L_RETURN_VOID:
{
    SYNC_STATE();
    sp_ = sp;
    if (!popFrame())
    {
        state_ = VMState::Halted;
        return;
    }
    RELOAD_STATE();
    DISPATCH();
}

    //==========================================================================
    // Exception Handling
    //==========================================================================

L_EH_PUSH:
{
    // Handler offset is in the next code word (raw i32 offset)
    int32_t offset = static_cast<int32_t>(code[pc++]);
    uint32_t handlerPc = static_cast<uint32_t>(static_cast<int32_t>(pc - 1) + offset);
    SYNC_STATE();
    sp_ = sp;
    pushExceptionHandler(handlerPc);
    DISPATCH();
}

L_EH_POP:
{
    SYNC_STATE();
    popExceptionHandler();
    DISPATCH();
}

L_EH_ENTRY:
{
    // Handler entry marker - no-op
    // The error and resume token are already on the stack from dispatchTrap
    DISPATCH();
}

L_ERR_GET_KIND:
{
    // Get trap kind from error object on stack (or use current trap kind)
    // If there's an operand, it's on the stack; otherwise use current
    // For simplicity, always return the current trap kind
    sp->i64 = static_cast<int64_t>(trapKind_);
    sp++;
    DISPATCH();
}

L_ERR_GET_CODE:
{
    // Get error code - maps trap kind to BASIC error code
    sp->i64 = static_cast<int64_t>(currentErrorCode_);
    sp++;
    DISPATCH();
}

L_ERR_GET_IP:
{
    // Get fault instruction pointer - return current PC
    sp->i64 = static_cast<int64_t>(fp_->pc);
    sp++;
    DISPATCH();
}

L_ERR_GET_LINE:
{
    // Get source line - we don't track this in bytecode VM, return -1
    sp->i64 = -1;
    sp++;
    DISPATCH();
}

L_TRAP:
{
    uint8_t kind = decodeArg8_0(instr);
    TrapKind trapKind = static_cast<TrapKind>(kind);
    SYNC_STATE();
    sp_ = sp;
    if (!dispatchTrap(trapKind))
    {
        // Include trap kind name in message
        const char *msg =
            (trapKind == TrapKind::Overflow) ? "Overflow: unhandled trap" : "Trap: unhandled trap";
        trap(trapKind, msg);
        return;
    }
    RELOAD_STATE();
    DISPATCH();
}

L_TRAP_FROM_ERR:
{
    int64_t errCode = (--sp)->i64;
    TrapKind trapKind = static_cast<TrapKind>(errCode);
    SYNC_STATE();
    sp_ = sp;
    if (!dispatchTrap(trapKind))
    {
        trap(trapKind, "Unhandled trap from error");
        return;
    }
    RELOAD_STATE();
    DISPATCH();
}

L_RESUME_SAME:
{
    // Resume at fault point - for now just continue
    DISPATCH();
}

L_RESUME_NEXT:
{
    // Resume after fault - for now just continue
    DISPATCH();
}

L_RESUME_LABEL:
{
    // Target offset is in the next code word (raw i32 offset)
    int32_t offset = static_cast<int32_t>(code[pc++]);
    pc = static_cast<uint32_t>(static_cast<int32_t>(pc - 1) + offset);
    DISPATCH();
}

L_DEFAULT:
    SYNC_STATE();
    trap(TrapKind::InvalidOpcode, "Unknown opcode");
    return;

#undef DISPATCH
#undef SYNC_STATE
#undef RELOAD_STATE
}

#endif // __GNUC__ || __clang__

//===----------------------------------------------------------------------===//
// Bytecode VM Thread.Start Handler
//===----------------------------------------------------------------------===//

namespace
{

/// Payload for spawning a new bytecode VM thread.
struct BytecodeThreadPayload
{
    const BytecodeModule *module;
    const BytecodeFunction *entry;
    void *arg;
    bool runtimeBridgeEnabled;
};

/// Thread entry trampoline for bytecode VM threads.
extern "C" void bytecode_thread_entry_trampoline(void *raw)
{
    BytecodeThreadPayload *payload = static_cast<BytecodeThreadPayload *>(raw);
    if (!payload || !payload->module || !payload->entry)
    {
        delete payload;
        rt_abort("Thread.Start: invalid bytecode entry");
        return;
    }

    // Create a new BytecodeVM for this thread
    BytecodeVM vm;
    vm.load(payload->module);
    vm.setRuntimeBridgeEnabled(payload->runtimeBridgeEnabled);

    // Set up argument
    std::vector<BCSlot> args;
    if (payload->entry->numParams > 0)
    {
        BCSlot argSlot{};
        argSlot.ptr = payload->arg;
        args.push_back(argSlot);
    }

    // Execute the entry function
    vm.exec(payload->entry, args);

    delete payload;
}

/// Resolve a bytecode function by pointer value.
/// The bytecode VM uses tagged function pointers: high bit set, lower bits are function index.
static const BytecodeFunction *resolveBytecodeEntry(const BytecodeModule *module, void *entry)
{
    if (!entry || !module)
        return nullptr;

    // Check if this is a tagged function pointer (high bit set)
    constexpr uint64_t kFuncPtrTag = 0x8000000000000000ULL;
    uint64_t val = reinterpret_cast<uint64_t>(entry);

    if (val & kFuncPtrTag)
    {
        // Extract function index from tagged pointer
        uint64_t funcIdx = val & ~kFuncPtrTag;
        if (funcIdx < module->functions.size())
        {
            return &module->functions[funcIdx];
        }
        return nullptr;
    }

    // Fallback: try to match as a raw pointer (for compatibility)
    const auto *candidate = static_cast<const BytecodeFunction *>(entry);
    for (const auto &fn : module->functions)
    {
        if (&fn == candidate)
        {
            return &fn;
        }
    }
    return nullptr;
}

/// Payload for standard VM thread spawning (duplicate of ThreadsRuntime.cpp)
struct VmThreadStartPayload
{
    const il::core::Module *module = nullptr;
    std::shared_ptr<il::vm::VM::ProgramState> program;
    const il::core::Function *entry = nullptr;
    void *arg = nullptr;
};

/// Standard VM thread entry trampoline
extern "C" void vm_thread_entry_trampoline_bc(void *raw)
{
    VmThreadStartPayload *payload = static_cast<VmThreadStartPayload *>(raw);
    if (!payload || !payload->module || !payload->entry)
    {
        delete payload;
        rt_abort("Thread.Start: invalid entry");
        return;
    }

    try
    {
        il::vm::VM vm(*payload->module, payload->program);
        il::support::SmallVector<il::vm::Slot, 2> args;
        if (payload->entry->params.size() == 1)
        {
            il::vm::Slot s{};
            s.ptr = payload->arg;
            args.push_back(s);
        }
        il::vm::detail::VMAccess::callFunction(vm, *payload->entry, args);
    }
    catch (...)
    {
        rt_abort("Thread.Start: unhandled exception");
    }
    delete payload;
}

/// Resolve IL function pointer to module function
static const il::core::Function *resolveILEntry(const il::core::Module &module, void *entry)
{
    if (!entry)
        return nullptr;
    const auto *candidate = static_cast<const il::core::Function *>(entry);
    for (const auto &fn : module.functions)
    {
        if (&fn == candidate)
            return &fn;
    }
    return nullptr;
}

/// Validate thread entry signature for standard VM
static void validateEntrySignature(const il::core::Function &fn)
{
    using Kind = il::core::Type::Kind;
    if (fn.retType.kind != Kind::Void)
        rt_trap("Thread.Start: invalid entry signature");
    if (fn.params.empty())
        return;
    if (fn.params.size() == 1 && fn.params[0].type.kind == Kind::Ptr)
        return;
    rt_trap("Thread.Start: invalid entry signature");
}

/// Handler for Viper.Threads.Thread.Start - handles both standard VM and BytecodeVM.
static void unified_thread_start_handler(void **args, void *result)
{
    void *entry = nullptr;
    void *arg = nullptr;
    if (args && args[0])
        entry = *reinterpret_cast<void **>(args[0]);
    if (args && args[1])
        arg = *reinterpret_cast<void **>(args[1]);

    if (!entry)
        rt_trap("Thread.Start: null entry");

    // Check for standard VM first
    il::vm::VM *stdVm = il::vm::activeVMInstance();
    if (stdVm)
    {
        std::shared_ptr<il::vm::VM::ProgramState> program = stdVm->programState();
        if (!program)
            rt_trap("Thread.Start: invalid runtime state");

        const il::core::Module &module = stdVm->module();
        const il::core::Function *entryFn = resolveILEntry(module, entry);
        if (!entryFn)
            rt_trap("Thread.Start: invalid entry");
        validateEntrySignature(*entryFn);

        auto *payload = new VmThreadStartPayload{&module, std::move(program), entryFn, arg};
        void *thread =
            rt_thread_start(reinterpret_cast<void *>(&vm_thread_entry_trampoline_bc), payload);
        if (result)
            *reinterpret_cast<void **>(result) = thread;
        return;
    }

    // Check for BytecodeVM
    BytecodeVM *bcVm = activeBytecodeVMInstance();
    const BytecodeModule *bcModule = activeBytecodeModule();
    if (bcVm && bcModule)
    {
        const BytecodeFunction *entryFn = resolveBytecodeEntry(bcModule, entry);
        if (!entryFn)
            rt_trap("Thread.Start: invalid bytecode entry");

        auto *payload =
            new BytecodeThreadPayload{bcModule, entryFn, arg, bcVm->runtimeBridgeEnabled()};
        void *thread =
            rt_thread_start(reinterpret_cast<void *>(&bytecode_thread_entry_trampoline), payload);
        if (result)
            *reinterpret_cast<void **>(result) = thread;
        return;
    }

    // No VM active - direct call (native code path)
    void *thread = rt_thread_start(entry, arg);
    if (result)
        *reinterpret_cast<void **>(result) = thread;
}

/// Static initializer to register the unified Thread.Start handler.
/// This overrides the standard VM handler when BytecodeVM is linked.
struct UnifiedThreadHandlerRegistrar
{
    UnifiedThreadHandlerRegistrar()
    {
        using il::runtime::signatures::make_signature;
        using il::runtime::signatures::SigParam;

        il::vm::ExternDesc ext;
        ext.name = "Viper.Threads.Thread.Start";
        ext.signature = make_signature(ext.name, {SigParam::Ptr, SigParam::Ptr}, {SigParam::Ptr});
        ext.fn = reinterpret_cast<void *>(&unified_thread_start_handler);
        il::vm::RuntimeBridge::registerExtern(ext);
    }
};

// Register the unified handler when the library is loaded
[[maybe_unused]] const UnifiedThreadHandlerRegistrar kUnifiedThreadHandlerRegistrar{};

} // anonymous namespace

} // namespace bytecode
} // namespace viper
