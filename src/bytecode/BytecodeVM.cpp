// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.

#include "bytecode/BytecodeVM.hpp"
#include "il/core/Module.hpp"
#include "il/runtime/RuntimeSignatures.hpp"
#include "il/runtime/signatures/Registry.hpp"
#include "rt_async.h"
#include "rt_future.h"
#include "rt_http_server.h"
#include "rt_object.h"
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
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <limits>
#include <mutex>
#include <string_view>
#include <vector>

namespace viper {
namespace bytecode {

namespace {

const il::runtime::RuntimeSignature *lookupRuntimeSignature(std::string_view name) {
    return il::runtime::findRuntimeSignature(name);
}

bool runtimeParamIsString(const il::runtime::RuntimeSignature *sig, size_t index) {
    return sig && index < sig->paramTypes.size() &&
           sig->paramTypes[index].kind == il::core::Type::Kind::Str;
}

void registerUnifiedVmRuntimeHandlers();

void bytecodeRuntimeTrapPassthrough(const il::vm::RuntimeTrapSignal &, void *) {}

using UnifiedRuntimeHandler = void (*)(void **, void *);
std::once_flag gUnifiedRuntimeHandlersOnce;
UnifiedRuntimeHandler gPriorThreadStartHandler = nullptr;
UnifiedRuntimeHandler gPriorThreadStartSafeHandler = nullptr;
UnifiedRuntimeHandler gPriorAsyncRunHandler = nullptr;
UnifiedRuntimeHandler gPriorHttpBindHandler = nullptr;

} // namespace

//===----------------------------------------------------------------------===//
// Thread-local active BytecodeVM tracking
//===----------------------------------------------------------------------===//

/// Thread-local pointer to the currently active BytecodeVM.
/// This enables runtime handlers (like Thread.Start) to detect when they're
/// being called from bytecode execution and handle threading correctly.
thread_local BytecodeVM *tlsActiveBytecodeVM = nullptr;

/// Thread-local pointer to the current BytecodeModule (for thread spawning).
thread_local const BytecodeModule *tlsActiveBytecodeModule = nullptr;

BytecodeVM *activeBytecodeVMInstance() {
    return tlsActiveBytecodeVM;
}

const BytecodeModule *activeBytecodeModule() {
    return tlsActiveBytecodeModule;
}

ActiveBytecodeVMGuard::ActiveBytecodeVMGuard(BytecodeVM *vm)
    : previous_(tlsActiveBytecodeVM), current_(vm) {
    tlsActiveBytecodeVM = vm;
}

ActiveBytecodeVMGuard::~ActiveBytecodeVMGuard() {
    tlsActiveBytecodeVM = previous_;
}

BytecodeVM::BytecodeVM()
    : module_(nullptr), state_(VMState::Ready), trapKind_(TrapKind::None), currentErrorCode_(0),
      sp_(nullptr), fp_(nullptr), instrCount_(0), runtimeBridgeEnabled_(false),
      useThreadedDispatch_(true) // Default to faster threaded dispatch
      ,
      allocaTop_(0), singleStep_(false) {
    // Pre-allocate reasonable stack size
    valueStack_.resize(kMaxStackSize * kMaxCallDepth);
    valueStackStringOwned_.assign(valueStack_.size(), 0);
    callStack_.reserve(kMaxCallDepth);

    // Pre-allocate alloca buffer and reserve maximum capacity upfront.
    // The buffer MUST NOT reallocate during execution because alloca pointers
    // stored in registers and operand stack would become dangling.
    // Reserve the 16MB maximum so resize() never triggers reallocation.
    allocaBuffer_.reserve(16 * 1024 * 1024);
    allocaBuffer_.resize(64 * 1024);
}

BytecodeVM::~BytecodeVM() {
    resetExecutionState();
    releaseOwnedGlobals();
    clearTrapRecord();
    // Release all cached rt_string objects
    for (rt_string s : stringCache_) {
        if (s) {
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
void BytecodeVM::initStringCache() {
    // Release any existing cache
    for (rt_string s : stringCache_) {
        if (s) {
            rt_string_unref(s);
        }
    }
    stringCache_.clear();

    if (!module_)
        return;

    // Pre-create rt_string objects for all strings in the pool
    // The runtime expects rt_string (pointer to rt_string_impl), not raw C strings
    stringCache_.reserve(module_->stringPool.size());
    for (const auto &str : module_->stringPool) {
        // Use rt_const_cstr to create a proper runtime string object
        // This matches what the standard VM does in VMInit.cpp
        rt_string rtStr = rt_const_cstr(str.c_str());
        stringCache_.push_back(rtStr);
    }
}

bool BytecodeVM::runtimeCallConsumesClonedStringArgs(std::string_view name) {
    // Keep this list aligned with rtgen's needsConsumingStringHandler().
    return name == "rt_str_concat" || name == "Viper.String.Concat" ||
           name == "Viper.String.ConcatSelf";
}

bool BytecodeVM::runtimeCallConsumesOwnedStringArgs(std::string_view name) {
    return name == "rt_str_release_maybe";
}

std::vector<BCSlot> BytecodeVM::cloneRuntimeStringArgs(std::string_view name,
                                                       const BCSlot *args,
                                                       size_t argCount) const {
    if (!args || argCount == 0)
        return {};
    if (!runtimeCallConsumesClonedStringArgs(name))
        return {};

    const auto *sig = lookupRuntimeSignature(name);
    if (!sig)
        return {};

    std::vector<BCSlot> cloned(args, args + argCount);
    for (size_t i = 0; i < argCount; ++i) {
        if (runtimeParamIsString(sig, i))
            rt_str_retain_maybe(static_cast<rt_string>(cloned[i].ptr));
    }
    return cloned;
}

void BytecodeVM::releaseRuntimeStringArgs(std::string_view name, std::vector<BCSlot> &args) const {
    if (args.empty())
        return;
    if (!runtimeCallConsumesClonedStringArgs(name))
        return;

    const auto *sig = lookupRuntimeSignature(name);
    if (!sig)
        return;
    for (size_t i = 0; i < args.size(); ++i) {
        if (runtimeParamIsString(sig, i))
            rt_str_release_maybe(static_cast<rt_string>(args[i].ptr));
    }
}

bool BytecodeVM::invokeRuntimeBridgeNative(const NativeFuncRef &ref,
                                           BCSlot *args,
                                           uint8_t argCount,
                                           BCSlot &result) {
    std::vector<BCSlot> preservedArgs =
        cloneRuntimeStringArgs(ref.name, args, static_cast<size_t>(argCount));
    BCSlot *callArgs = preservedArgs.empty() ? args : preservedArgs.data();
    il::vm::Slot *vmArgs = reinterpret_cast<il::vm::Slot *>(callArgs);
    std::vector<il::vm::Slot> argVec(vmArgs, vmArgs + argCount);

    il::vm::RuntimeCallContext ctx;
    try {
        il::vm::ScopedRuntimeTrapInterceptor trapInterceptor(&bytecodeRuntimeTrapPassthrough, this);
        il::vm::Slot vmResult = il::vm::RuntimeBridge::call(
            ctx,
            ref.name,
            argVec,
            il::support::SourceLoc{},
            fp_ && fp_->func ? fp_->func->name : std::string{},
            "");
        result.i64 = vmResult.i64;
    } catch (const il::vm::RuntimeTrapSignal &signal) {
        releaseRuntimeStringArgs(ref.name, preservedArgs);
        if (!dispatchTrap(static_cast<TrapKind>(signal.kind), signal.code, signal.message.c_str()))
            trap(static_cast<TrapKind>(signal.kind), signal.message.c_str());
        return false;
    }

    releaseRuntimeStringArgs(ref.name, preservedArgs);
    return true;
}

void BytecodeVM::dismissConsumedStringArgs(std::string_view name, BCSlot *args, uint8_t argCount) {
    if (!args || argCount == 0)
        return;
    if (!runtimeCallConsumesOwnedStringArgs(name))
        return;

    const auto *sig = lookupRuntimeSignature(name);
    if (!sig)
        return;

    for (uint8_t i = 0; i < argCount; ++i) {
        if (runtimeParamIsString(sig, i))
            setSlotOwnsString(args + i, false);
    }
}

void BytecodeVM::registerNativeHandler(const std::string &name, NativeHandler handler) {
    nativeHandlers_[name] = std::move(handler);
}

BytecodeVM::ExecutionEnvironment BytecodeVM::captureExecutionEnvironment() const {
    ExecutionEnvironment env;
    env.runtimeBridgeEnabled = runtimeBridgeEnabled_;
    env.useThreadedDispatch = useThreadedDispatch_;
    env.nativeHandlers = nativeHandlers_;
    return env;
}

void BytecodeVM::applyExecutionEnvironment(const ExecutionEnvironment &env) {
    runtimeBridgeEnabled_ = env.runtimeBridgeEnabled;
    useThreadedDispatch_ = env.useThreadedDispatch;
    nativeHandlers_ = env.nativeHandlers;
}

void BytecodeVM::copyExecutionEnvironmentFrom(const BytecodeVM &other) {
    applyExecutionEnvironment(other.captureExecutionEnvironment());
}

void BytecodeVM::load(const BytecodeModule *module) {
    registerUnifiedVmRuntimeHandlers();
    resetExecutionState();
    releaseOwnedGlobals();
    clearTrapRecord();

    module_ = module;
    state_ = VMState::Ready;
    trapKind_ = TrapKind::None;
    currentErrorCode_ = 0;
    trapMessage_.clear();

    // Initialize global variable storage
    globals_.clear();
    globalsStringOwned_.clear();
    if (module_) {
        globals_.resize(module_->globals.size());
        globalsStringOwned_.assign(module_->globals.size(), 0);
        for (size_t i = 0; i < module_->globals.size(); ++i) {
            globals_[i].i64 = 0; // Zero-initialize
            const auto &gi = module_->globals[i];
            if (!gi.initData.empty()) {
                size_t copySize = std::min<size_t>(gi.initData.size(), sizeof(BCSlot));
                std::memcpy(&globals_[i], gi.initData.data(), copySize);
            }
        }
    }

    // Initialize string cache with proper rt_string objects
    initStringCache();
}

size_t BytecodeVM::slotIndex(const BCSlot *slot) const {
    assert(slot >= valueStack_.data());
    assert(slot < valueStack_.data() + valueStack_.size());
    return static_cast<size_t>(slot - valueStack_.data());
}

bool BytecodeVM::slotOwnsString(const BCSlot *slot) const {
    return valueStackStringOwned_[slotIndex(slot)] != 0;
}

void BytecodeVM::setSlotOwnsString(const BCSlot *slot, bool owns) {
    valueStackStringOwned_[slotIndex(slot)] = owns ? 1 : 0;
}

bool BytecodeVM::localIsString(const BCFrame &frame, uint32_t idx) const {
    return idx < frame.func->localIsString.size() && frame.func->localIsString[idx] != 0;
}

bool BytecodeVM::validateStringHandle(const void *ptr, const char *site) {
    if (!ptr || rt_string_is_handle(ptr))
        return true;

    const char *functionName = (fp_ && fp_->func) ? fp_->func->name.c_str() : "<none>";
    const uint32_t pc = fp_ ? fp_->pc : 0;
    trap(TrapKind::RuntimeError,
         (std::string(site) + ": invalid runtime string handle in " + functionName +
          " @pc=" + std::to_string(pc))
             .c_str());
    return false;
}

void BytecodeVM::releaseOwnedString(BCSlot *slot) {
    if (!slotOwnsString(slot))
        return;
    if (!validateStringHandle(slot->ptr, "BytecodeVM::releaseOwnedString")) {
        slot->ptr = nullptr;
        setSlotOwnsString(slot, false);
        return;
    }
    rt_str_release_maybe(static_cast<rt_string>(slot->ptr));
    slot->ptr = nullptr;
    setSlotOwnsString(slot, false);
}

void BytecodeVM::pushLocal(uint32_t idx) {
    BCSlot *dst = sp_++;
    *dst = fp_->locals[idx];
    if (localIsString(*fp_, idx) && dst->ptr) {
        if (!validateStringHandle(dst->ptr, "BytecodeVM::pushLocal")) {
            sp_--;
            return;
        }
        rt_str_retain_maybe(static_cast<rt_string>(dst->ptr));
        setSlotOwnsString(dst, true);
    } else {
        setSlotOwnsString(dst, false);
    }
}

void BytecodeVM::storeLocal(uint32_t idx) {
    BCSlot *src = --sp_;
    BCSlot *dst = fp_->locals + idx;
    const bool srcOwns = slotOwnsString(src);
    const BCSlot value = *src;

    if (localIsString(*fp_, idx)) {
        releaseOwnedString(dst);
        *dst = value;
        if (value.ptr) {
            if (!validateStringHandle(value.ptr, "BytecodeVM::storeLocal")) {
                setSlotOwnsString(src, false);
                setSlotOwnsString(dst, false);
                return;
            }
            if (!srcOwns)
                rt_str_retain_maybe(static_cast<rt_string>(value.ptr));
            setSlotOwnsString(dst, true);
        } else {
            setSlotOwnsString(dst, false);
        }
    } else {
        *dst = value;
        setSlotOwnsString(dst, false);
    }

    setSlotOwnsString(src, false);
}

void BytecodeVM::releaseCallArgs(BCSlot *args, uint8_t argCount) {
    for (uint8_t i = 0; i < argCount; ++i)
        releaseOwnedString(args + i);
}

void BytecodeVM::releaseFrameLocals(const BCFrame &frame) {
    for (uint32_t i = 0; i < frame.func->numLocals; ++i) {
        if (localIsString(frame, i))
            releaseOwnedString(frame.locals + i);
        else
            setSlotOwnsString(frame.locals + i, false);
    }
}

void BytecodeVM::releaseOwnedValueStack() {
    for (size_t i = 0; i < valueStack_.size(); ++i) {
        if (valueStackStringOwned_[i] == 0)
            continue;
        releaseOwnedString(valueStack_.data() + i);
    }
}

void BytecodeVM::releaseOwnedGlobals() {
    for (size_t i = 0; i < globals_.size(); ++i) {
        if (i >= globalsStringOwned_.size() || globalsStringOwned_[i] == 0)
            continue;
        if (globals_[i].ptr && validateStringHandle(globals_[i].ptr, "BytecodeVM::globals")) {
            rt_str_release_maybe(static_cast<rt_string>(globals_[i].ptr));
        }
        globals_[i].ptr = nullptr;
        globalsStringOwned_[i] = 0;
    }
}

void BytecodeVM::clearTrapRecord() {
    for (size_t i = 0; i < trapRecord_.valueSlots.size() && i < trapRecord_.valueOwned.size(); ++i) {
        if (trapRecord_.valueOwned[i] == 0)
            continue;
        if (trapRecord_.valueSlots[i].ptr &&
            validateStringHandle(trapRecord_.valueSlots[i].ptr, "BytecodeVM::clearTrapRecord")) {
            rt_str_release_maybe(static_cast<rt_string>(trapRecord_.valueSlots[i].ptr));
        }
    }
    trapRecord_ = TrapRecord{};
}

void BytecodeVM::resetExecutionState() {
    releaseOwnedValueStack();
    clearTrapRecord();
    callStack_.clear();
    ehStack_.clear();
    sp_ = valueStack_.data();
    fp_ = nullptr;
    allocaTop_ = 0;
    std::fill(valueStackStringOwned_.begin(), valueStackStringOwned_.end(), 0);
}

uint32_t BytecodeVM::operandDepth(const BCFrame &frame, const BCSlot *sp) const {
    return static_cast<uint32_t>(sp - frame.stackBase);
}

bool BytecodeVM::ensurePcInRange(const BytecodeFunction &func, uint32_t pc, const char *site) {
    if (pc < func.code.size())
        return true;
    trap(TrapKind::InvalidOpcode, (std::string(site) + ": program counter out of range").c_str());
    return false;
}

bool BytecodeVM::ensureWordsAvailable(const BytecodeFunction &func,
                                      uint32_t pc,
                                      uint32_t words,
                                      const char *site) {
    const uint64_t end = static_cast<uint64_t>(pc) + static_cast<uint64_t>(words);
    if (end <= func.code.size())
        return true;
    trap(TrapKind::InvalidOpcode, (std::string(site) + ": instruction data out of range").c_str());
    return false;
}

bool BytecodeVM::ensureBranchTarget(const BytecodeFunction &func,
                                    uint32_t target,
                                    const char *site) {
    if (target < func.code.size())
        return true;
    trap(TrapKind::InvalidOpcode, (std::string(site) + ": branch target out of range").c_str());
    return false;
}

bool BytecodeVM::ensureStackForInstruction(const BCFrame &frame,
                                           const BCSlot *sp,
                                           uint32_t instr,
                                           const char *site) {
    uint32_t required = 0;
    int32_t delta = 0;
    const BCOpcode op = decodeOpcode(instr);
    switch (op) {
        case BCOpcode::NOP:
        case BCOpcode::INC_LOCAL:
        case BCOpcode::DEC_LOCAL:
        case BCOpcode::EH_PUSH:
        case BCOpcode::EH_POP:
        case BCOpcode::EH_ENTRY:
        case BCOpcode::TRAP:
            break;
        case BCOpcode::DUP:
        case BCOpcode::POP:
        case BCOpcode::NEG_I64:
        case BCOpcode::NEG_F64:
        case BCOpcode::NOT_I64:
        case BCOpcode::BOOL_TO_I64:
        case BCOpcode::I64_TO_F64:
        case BCOpcode::U64_TO_F64:
        case BCOpcode::F64_TO_I64:
        case BCOpcode::F64_TO_I64_CHK:
        case BCOpcode::F64_TO_U64_CHK:
        case BCOpcode::I64_TO_BOOL:
        case BCOpcode::STR_RETAIN:
        case BCOpcode::STR_RELEASE:
            required = 1;
            delta = (op == BCOpcode::DUP) ? 1 : 0;
            break;
        case BCOpcode::DUP2:
        case BCOpcode::POP2:
            required = 2;
            delta = (op == BCOpcode::DUP2) ? 2 : -2;
            break;
        case BCOpcode::SWAP:
            required = 2;
            break;
        case BCOpcode::ROT3:
            required = 3;
            break;
        case BCOpcode::ADD_I64:
        case BCOpcode::SUB_I64:
        case BCOpcode::MUL_I64:
        case BCOpcode::SDIV_I64:
        case BCOpcode::UDIV_I64:
        case BCOpcode::SREM_I64:
        case BCOpcode::UREM_I64:
        case BCOpcode::ADD_I64_OVF:
        case BCOpcode::SUB_I64_OVF:
        case BCOpcode::MUL_I64_OVF:
        case BCOpcode::SDIV_I64_CHK:
        case BCOpcode::UDIV_I64_CHK:
        case BCOpcode::SREM_I64_CHK:
        case BCOpcode::UREM_I64_CHK:
        case BCOpcode::ADD_F64:
        case BCOpcode::SUB_F64:
        case BCOpcode::MUL_F64:
        case BCOpcode::DIV_F64:
        case BCOpcode::AND_I64:
        case BCOpcode::OR_I64:
        case BCOpcode::XOR_I64:
        case BCOpcode::SHL_I64:
        case BCOpcode::LSHR_I64:
        case BCOpcode::ASHR_I64:
        case BCOpcode::CMP_EQ_I64:
        case BCOpcode::CMP_NE_I64:
        case BCOpcode::CMP_SLT_I64:
        case BCOpcode::CMP_SLE_I64:
        case BCOpcode::CMP_SGT_I64:
        case BCOpcode::CMP_SGE_I64:
        case BCOpcode::CMP_ULT_I64:
        case BCOpcode::CMP_ULE_I64:
        case BCOpcode::CMP_UGT_I64:
        case BCOpcode::CMP_UGE_I64:
        case BCOpcode::CMP_EQ_F64:
        case BCOpcode::CMP_NE_F64:
        case BCOpcode::CMP_LT_F64:
        case BCOpcode::CMP_LE_F64:
        case BCOpcode::CMP_GT_F64:
        case BCOpcode::CMP_GE_F64:
        case BCOpcode::I64_NARROW_CHK:
        case BCOpcode::U64_NARROW_CHK:
        case BCOpcode::STORE_LOCAL:
        case BCOpcode::STORE_LOCAL_W:
        case BCOpcode::STORE_GLOBAL:
        case BCOpcode::JUMP_IF_TRUE:
        case BCOpcode::JUMP_IF_FALSE:
        case BCOpcode::ALLOCA:
        case BCOpcode::ERR_GET_KIND:
        case BCOpcode::ERR_GET_CODE:
        case BCOpcode::ERR_GET_IP:
        case BCOpcode::ERR_GET_LINE:
        case BCOpcode::TRAP_FROM_ERR:
        case BCOpcode::RESUME_SAME:
        case BCOpcode::RESUME_NEXT:
        case BCOpcode::RESUME_LABEL:
            required = 1;
            delta = (op == BCOpcode::STORE_LOCAL || op == BCOpcode::STORE_LOCAL_W ||
                     op == BCOpcode::STORE_GLOBAL || op == BCOpcode::JUMP_IF_TRUE ||
                     op == BCOpcode::JUMP_IF_FALSE || op == BCOpcode::TRAP_FROM_ERR ||
                     op == BCOpcode::RESUME_SAME || op == BCOpcode::RESUME_NEXT ||
                     op == BCOpcode::RESUME_LABEL)
                        ? -1
                        : 0;
            break;
        case BCOpcode::GEP:
        case BCOpcode::STORE_I8_MEM:
        case BCOpcode::STORE_I16_MEM:
        case BCOpcode::STORE_I32_MEM:
        case BCOpcode::STORE_I64_MEM:
        case BCOpcode::STORE_F64_MEM:
        case BCOpcode::STORE_PTR_MEM:
        case BCOpcode::STORE_STR_MEM:
            required = 2;
            delta = -1;
            break;
        case BCOpcode::IDX_CHK:
            required = 3;
            delta = -2;
            break;
        case BCOpcode::LOAD_LOCAL:
        case BCOpcode::LOAD_LOCAL_W:
        case BCOpcode::LOAD_I8:
        case BCOpcode::LOAD_I16:
        case BCOpcode::LOAD_I32:
        case BCOpcode::LOAD_I64:
        case BCOpcode::LOAD_F64:
        case BCOpcode::LOAD_STR:
        case BCOpcode::LOAD_NULL:
        case BCOpcode::LOAD_ZERO:
        case BCOpcode::LOAD_ONE:
        case BCOpcode::LOAD_GLOBAL:
        case BCOpcode::TRAP_KIND:
            delta = 1;
            break;
        case BCOpcode::LOAD_I8_MEM:
        case BCOpcode::LOAD_I16_MEM:
        case BCOpcode::LOAD_I32_MEM:
        case BCOpcode::LOAD_I64_MEM:
        case BCOpcode::LOAD_F64_MEM:
        case BCOpcode::LOAD_PTR_MEM:
        case BCOpcode::LOAD_STR_MEM:
        case BCOpcode::RETURN:
        case BCOpcode::SWITCH:
            required = 1;
            delta = (op == BCOpcode::RETURN || op == BCOpcode::SWITCH) ? -1 : 0;
            break;
        case BCOpcode::CALL: {
            const uint16_t funcIdx = decodeArg16(instr);
            if (funcIdx >= module_->functions.size()) {
                trap(TrapKind::RuntimeError, "Invalid function index");
                return false;
            }
            const BytecodeFunction &callee = module_->functions[funcIdx];
            required = callee.numParams;
            delta = callee.hasReturn ? (1 - static_cast<int32_t>(required))
                                     : -static_cast<int32_t>(required);
            break;
        }
        case BCOpcode::CALL_NATIVE: {
            const uint16_t nativeIdx = decodeArg16_1(instr);
            if (nativeIdx >= module_->nativeFuncs.size()) {
                trap(TrapKind::RuntimeError, "Invalid native function index");
                return false;
            }
            const NativeFuncRef &ref = module_->nativeFuncs[nativeIdx];
            required = ref.paramCount;
            delta = ref.hasReturn ? (1 - static_cast<int32_t>(required))
                                  : -static_cast<int32_t>(required);
            break;
        }
        case BCOpcode::CALL_INDIRECT:
            required = static_cast<uint32_t>(decodeArg8_0(instr)) + 1;
            delta = 0;
            break;
        case BCOpcode::RETURN_VOID:
            delta = 0;
            break;
        case BCOpcode::JUMP:
        case BCOpcode::JUMP_LONG:
            break;
        default:
            break;
    }

    const uint32_t depth = operandDepth(frame, sp);
    if (depth < required) {
        trap(TrapKind::StackOverflow,
             (std::string(site) + ": operand stack underflow at " + opcodeName(op) + " in " +
              frame.func->name + " @pc=" + std::to_string(frame.pc) + " depth=" +
              std::to_string(depth) + " required=" + std::to_string(required) + " max=" +
              std::to_string(frame.func->maxStack))
                 .c_str());
        return false;
    }

    if (delta > 0 && depth + static_cast<uint32_t>(delta) > frame.func->maxStack) {
        trap(TrapKind::StackOverflow,
             (std::string(site) + ": operand stack overflow at " + opcodeName(op) + " in " +
              frame.func->name + " @pc=" + std::to_string(frame.pc) + " depth=" +
              std::to_string(depth) + " delta=" + std::to_string(delta) + " max=" +
              std::to_string(frame.func->maxStack))
                 .c_str());
        return false;
    }

    return true;
}

bool BytecodeVM::ensureCallArity(const BytecodeFunction *func,
                                 const BCFrame *caller,
                                 const BCSlot *sp,
                                 const char *site) {
    if (!func) {
        trap(TrapKind::RuntimeError, (std::string(site) + ": null callee").c_str());
        return false;
    }

    const uint32_t depth =
        caller ? operandDepth(*caller, sp) : static_cast<uint32_t>(sp - valueStack_.data());
    if (depth != func->numParams) {
        trap(TrapKind::RuntimeError, (std::string(site) + ": call arity mismatch").c_str());
        return false;
    }

    return true;
}

bool BytecodeVM::ensureFrameFootprint(const BytecodeFunction *func,
                                      const BCSlot *sp,
                                      const char *site) {
    if (!func) {
        trap(TrapKind::RuntimeError, (std::string(site) + ": null callee").c_str());
        return false;
    }
    if (func->numLocals < func->numParams) {
        trap(TrapKind::InvalidOpcode, (std::string(site) + ": invalid frame metadata").c_str());
        return false;
    }
    BCSlot *localsStart = const_cast<BCSlot *>(sp) - func->numParams;
    BCSlot *stackBase = localsStart + func->numLocals;
    BCSlot *stackLimit = stackBase + func->maxStack;
    if (localsStart < valueStack_.data() || stackLimit > valueStack_.data() + valueStack_.size()) {
        trap(TrapKind::StackOverflow, (std::string(site) + ": frame exceeds value stack").c_str());
        return false;
    }
    return true;
}

BCSlot BytecodeVM::exec(const std::string &funcName, const std::vector<BCSlot> &args) {
    if (!module_) {
        trap(TrapKind::RuntimeError, "No module loaded");
        return BCSlot{};
    }

    const BytecodeFunction *func = module_->findFunction(funcName);
    if (!func) {
        trap(TrapKind::RuntimeError, "Function not found");
        return BCSlot{};
    }

    return exec(func, args);
}

BCSlot BytecodeVM::exec(const BytecodeFunction *func, const std::vector<BCSlot> &args) {
    registerUnifiedVmRuntimeHandlers();
    if (!module_) {
        trap(TrapKind::RuntimeError, "No module loaded");
        return BCSlot{};
    }
    if (!func) {
        trap(TrapKind::RuntimeError, "Null function entry");
        return BCSlot{};
    }
    if (args.size() != func->numParams) {
        trap(TrapKind::RuntimeError, "Function entry arity mismatch");
        return BCSlot{};
    }

    // Set up thread-local context so Thread.Start handler can find us
    ActiveBytecodeVMGuard vmGuard(this);
    const BytecodeModule *prevModule = tlsActiveBytecodeModule;
    tlsActiveBytecodeModule = module_;

    // Reset state
    state_ = VMState::Ready;
    trapKind_ = TrapKind::None;
    currentErrorCode_ = 0;
    trapMessage_.clear();
    resetExecutionState();

    // Push arguments onto stack as initial locals
    for (const auto &arg : args) {
        *sp_++ = arg;
        setSlotOwnsString(sp_ - 1, false);
    }

    // Call the function
    call(func);

    // Check if call setup failed (e.g., stack overflow in first call)
    if (state_ == VMState::Trapped || !fp_) {
        if (!fp_ && state_ != VMState::Trapped) {
            trap(TrapKind::RuntimeError, "Frame setup failed");
        }
        tlsActiveBytecodeModule = prevModule;
        return BCSlot{};
    }

    // Run interpreter - use threaded dispatch if available and enabled
#if defined(__GNUC__) || defined(__clang__)
    if (useThreadedDispatch_) {
        runThreaded();
    } else {
        run();
    }
#else
    run();
#endif

    // Restore module thread-local
    tlsActiveBytecodeModule = prevModule;

    // Return result
    if (state_ == VMState::Halted && sp_ > valueStack_.data()) {
        return *(sp_ - 1);
    }
    return BCSlot{};
}

void BytecodeVM::run() {
    state_ = VMState::Running;

    while (state_ == VMState::Running) {
        if (!fp_ || !fp_->func || !ensurePcInRange(*fp_->func, fp_->pc, "BytecodeVM::run(fetch)"))
            return;

        if (!ensureStackForInstruction(*fp_, sp_, fp_->func->code[fp_->pc], "BytecodeVM::run"))
            return;

        // Fetch instruction
        uint32_t instr = fp_->func->code[fp_->pc++];
        BCOpcode op = decodeOpcode(instr);

        ++instrCount_;

        switch (op) {
            //==================================================================
            // Stack Operations
            //==================================================================
            case BCOpcode::NOP:
                break;

            case BCOpcode::DUP:
                *sp_ = *(sp_ - 1);
                setSlotOwnsString(sp_, false);
                if (slotOwnsString(sp_ - 1) && sp_->ptr) {
                    rt_str_retain_maybe(static_cast<rt_string>(sp_->ptr));
                    setSlotOwnsString(sp_, true);
                }
                sp_++;
                break;

            case BCOpcode::DUP2:
                sp_[0] = sp_[-2];
                sp_[1] = sp_[-1];
                setSlotOwnsString(sp_, false);
                setSlotOwnsString(sp_ + 1, false);
                if (slotOwnsString(sp_ - 2) && sp_[0].ptr) {
                    rt_str_retain_maybe(static_cast<rt_string>(sp_[0].ptr));
                    setSlotOwnsString(sp_, true);
                }
                if (slotOwnsString(sp_ - 1) && sp_[1].ptr) {
                    rt_str_retain_maybe(static_cast<rt_string>(sp_[1].ptr));
                    setSlotOwnsString(sp_ + 1, true);
                }
                sp_ += 2;
                break;

            case BCOpcode::POP:
                releaseOwnedString(sp_ - 1);
                sp_--;
                break;

            case BCOpcode::POP2:
                releaseOwnedString(sp_ - 1);
                releaseOwnedString(sp_ - 2);
                sp_ -= 2;
                break;

            case BCOpcode::SWAP: {
                BCSlot tmp = sp_[-1];
                const bool tmpOwns = slotOwnsString(sp_ - 1);
                sp_[-1] = sp_[-2];
                sp_[-2] = tmp;
                const bool lowerOwns = slotOwnsString(sp_ - 2);
                setSlotOwnsString(sp_ - 1, lowerOwns);
                setSlotOwnsString(sp_ - 2, tmpOwns);
                break;
            }

            case BCOpcode::ROT3: {
                BCSlot tmp = sp_[-1];
                const bool tmpOwns = slotOwnsString(sp_ - 1);
                sp_[-1] = sp_[-2];
                sp_[-2] = sp_[-3];
                sp_[-3] = tmp;
                const bool secondOwns = slotOwnsString(sp_ - 2);
                const bool firstOwns = slotOwnsString(sp_ - 3);
                setSlotOwnsString(sp_ - 1, secondOwns);
                setSlotOwnsString(sp_ - 2, firstOwns);
                setSlotOwnsString(sp_ - 3, tmpOwns);
                break;
            }

            //==================================================================
            // Local Variable Operations
            //==================================================================
            case BCOpcode::LOAD_LOCAL: {
                uint8_t idx = decodeArg8_0(instr);
                pushLocal(idx);
                break;
            }

            case BCOpcode::STORE_LOCAL: {
                uint8_t idx = decodeArg8_0(instr);
                storeLocal(idx);
                break;
            }

            case BCOpcode::LOAD_LOCAL_W: {
                uint16_t idx = decodeArg16(instr);
                pushLocal(idx);
                break;
            }

            case BCOpcode::STORE_LOCAL_W: {
                uint16_t idx = decodeArg16(instr);
                storeLocal(idx);
                break;
            }

            case BCOpcode::INC_LOCAL: {
                uint8_t idx = decodeArg8_0(instr);
                fp_->locals[idx].i64++;
                break;
            }

            case BCOpcode::DEC_LOCAL: {
                uint8_t idx = decodeArg8_0(instr);
                fp_->locals[idx].i64--;
                break;
            }

            //==================================================================
            // Constant Loading
            //==================================================================
            case BCOpcode::LOAD_I8: {
                int8_t val = decodeArgI8_0(instr);
                sp_->i64 = val;
                setSlotOwnsString(sp_, false);
                sp_++;
                break;
            }

            case BCOpcode::LOAD_I16: {
                int16_t val = decodeArgI16(instr);
                sp_->i64 = val;
                setSlotOwnsString(sp_, false);
                sp_++;
                break;
            }

            case BCOpcode::LOAD_I32: {
                if (!ensureWordsAvailable(*fp_->func, fp_->pc, 1, "BytecodeVM::LOAD_I32"))
                    return;
                int32_t val = static_cast<int32_t>(fp_->func->code[fp_->pc++]);
                sp_->i64 = val;
                setSlotOwnsString(sp_, false);
                sp_++;
                break;
            }

            case BCOpcode::LOAD_I64: {
                uint16_t idx = decodeArg16(instr);
                if (idx >= module_->i64Pool.size()) {
                    trap(TrapKind::InvalidOpcode, "LOAD_I64 constant index out of range");
                    break;
                }
                sp_->i64 = module_->i64Pool[idx];
                setSlotOwnsString(sp_, false);
                sp_++;
                break;
            }

            case BCOpcode::LOAD_F64: {
                uint16_t idx = decodeArg16(instr);
                if (idx >= module_->f64Pool.size()) {
                    trap(TrapKind::InvalidOpcode, "LOAD_F64 constant index out of range");
                    break;
                }
                sp_->f64 = module_->f64Pool[idx];
                setSlotOwnsString(sp_, false);
                sp_++;
                break;
            }

            case BCOpcode::LOAD_NULL:
                sp_->ptr = nullptr;
                setSlotOwnsString(sp_, false);
                sp_++;
                break;

            case BCOpcode::LOAD_ZERO:
                sp_->i64 = 0;
                setSlotOwnsString(sp_, false);
                sp_++;
                break;

            case BCOpcode::LOAD_ONE:
                sp_->i64 = 1;
                setSlotOwnsString(sp_, false);
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
                {
                    int64_t result = 0;
                    TrapKind fault = TrapKind::None;
                    if (!safeSignedDiv(sp_[-2].i64, sp_[-1].i64, result, fault)) {
                        if (!dispatchTrap(fault)) {
                            trap(fault,
                                 fault == TrapKind::DivideByZero ? "division by zero"
                                                                  : "Overflow: integer division overflow");
                        }
                        break;
                    }
                    sp_[-2].i64 = result;
                    sp_--;
                }
                break;

            case BCOpcode::UDIV_I64:
                {
                    int64_t result = 0;
                    TrapKind fault = TrapKind::None;
                    if (!safeUnsignedDiv(sp_[-2].i64, sp_[-1].i64, result, fault)) {
                        if (!dispatchTrap(fault)) {
                            trap(fault, "division by zero");
                        }
                        break;
                    }
                    sp_[-2].i64 = result;
                    sp_--;
                }
                break;

            case BCOpcode::SREM_I64:
                {
                    int64_t result = 0;
                    TrapKind fault = TrapKind::None;
                    if (!safeSignedRem(sp_[-2].i64, sp_[-1].i64, result, fault)) {
                        if (!dispatchTrap(fault)) {
                            trap(fault,
                                 fault == TrapKind::DivideByZero ? "division by zero"
                                                                  : "Overflow: integer remainder overflow");
                        }
                        break;
                    }
                    sp_[-2].i64 = result;
                    sp_--;
                }
                break;

            case BCOpcode::UREM_I64:
                {
                    int64_t result = 0;
                    TrapKind fault = TrapKind::None;
                    if (!safeUnsignedRem(sp_[-2].i64, sp_[-1].i64, result, fault)) {
                        if (!dispatchTrap(fault)) {
                            trap(fault, "division by zero");
                        }
                        break;
                    }
                    sp_[-2].i64 = result;
                    sp_--;
                }
                break;

            case BCOpcode::NEG_I64:
                {
                    int64_t result = 0;
                    TrapKind fault = TrapKind::None;
                    if (!safeNegate(sp_[-1].i64, result, fault)) {
                        if (!dispatchTrap(fault)) {
                            trap(fault, "Overflow: integer negation overflow");
                        }
                        break;
                    }
                    sp_[-1].i64 = result;
                }
                break;

            case BCOpcode::ADD_I64_OVF: {
                // Target type encoded in arg: 0=I1, 1=I16, 2=I32, 3=I64
                uint8_t targetType = decodeArg8_0(instr);
                int64_t a = sp_[-2].i64, b = sp_[-1].i64;
                int64_t result = 0;
                bool overflow = addOverflow(a, b, result);
                switch (targetType) {
                    case 1: // I16
                        overflow = overflow || result < INT16_MIN || result > INT16_MAX;
                        break;
                    case 2: // I32
                        overflow = overflow || result < INT32_MIN || result > INT32_MAX;
                        break;
                    default:
                        break;
                }
                if (overflow) {
                    if (!dispatchTrap(TrapKind::Overflow)) {
                        trap(TrapKind::Overflow, "Overflow: integer overflow in add");
                    }
                    break;
                }
                sp_[-2].i64 = result;
                sp_--;
                break;
            }

            case BCOpcode::SUB_I64_OVF: {
                // Target type encoded in arg: 0=I1, 1=I16, 2=I32, 3=I64
                uint8_t targetType = decodeArg8_0(instr);
                int64_t a = sp_[-2].i64, b = sp_[-1].i64;
                int64_t result = 0;
                bool overflow = subOverflow(a, b, result);
                switch (targetType) {
                    case 1: // I16
                        overflow = overflow || result < INT16_MIN || result > INT16_MAX;
                        break;
                    case 2: // I32
                        overflow = overflow || result < INT32_MIN || result > INT32_MAX;
                        break;
                    default:
                        break;
                }
                if (overflow) {
                    if (!dispatchTrap(TrapKind::Overflow)) {
                        trap(TrapKind::Overflow, "Overflow: integer overflow in sub");
                    }
                    break;
                }
                sp_[-2].i64 = result;
                sp_--;
                break;
            }

            case BCOpcode::MUL_I64_OVF: {
                // Target type encoded in arg: 0=I1, 1=I16, 2=I32, 3=I64
                uint8_t targetType = decodeArg8_0(instr);
                int64_t a = sp_[-2].i64, b = sp_[-1].i64;
                int64_t result = 0;
                bool overflow = mulOverflow(a, b, result);
                switch (targetType) {
                    case 1: // I16
                        overflow = overflow || result < INT16_MIN || result > INT16_MAX;
                        break;
                    case 2: // I32
                        overflow = overflow || result < INT32_MIN || result > INT32_MAX;
                        break;
                    default:
                        break;
                }
                if (overflow) {
                    if (!dispatchTrap(TrapKind::Overflow)) {
                        trap(TrapKind::Overflow, "Overflow: integer overflow in mul");
                    }
                    break;
                }
                sp_[-2].i64 = result;
                sp_--;
                break;
            }

            case BCOpcode::SDIV_I64_CHK:
                {
                    int64_t result = 0;
                    TrapKind fault = TrapKind::None;
                    if (!safeSignedDiv(sp_[-2].i64, sp_[-1].i64, result, fault)) {
                        if (!dispatchTrap(fault)) {
                            trap(fault,
                                 fault == TrapKind::DivideByZero ? "division by zero"
                                                                  : "Overflow: integer overflow in div");
                        }
                        break;
                    }
                    sp_[-2].i64 = result;
                    sp_--;
                }
                break;

            case BCOpcode::UDIV_I64_CHK:
                {
                    int64_t result = 0;
                    TrapKind fault = TrapKind::None;
                    if (!safeUnsignedDiv(sp_[-2].i64, sp_[-1].i64, result, fault)) {
                        if (!dispatchTrap(fault)) {
                            trap(fault, "division by zero");
                        }
                        break;
                    }
                    sp_[-2].i64 = result;
                    sp_--;
                }
                break;

            case BCOpcode::SREM_I64_CHK:
                {
                    int64_t result = 0;
                    TrapKind fault = TrapKind::None;
                    if (!safeSignedRem(sp_[-2].i64, sp_[-1].i64, result, fault)) {
                        if (!dispatchTrap(fault)) {
                            trap(fault,
                                 fault == TrapKind::DivideByZero ? "division by zero"
                                                                  : "Overflow: integer overflow in rem");
                        }
                        break;
                    }
                    sp_[-2].i64 = result;
                    sp_--;
                }
                break;

            case BCOpcode::UREM_I64_CHK:
                {
                    int64_t result = 0;
                    TrapKind fault = TrapKind::None;
                    if (!safeUnsignedRem(sp_[-2].i64, sp_[-1].i64, result, fault)) {
                        if (!dispatchTrap(fault)) {
                            trap(fault, "division by zero");
                        }
                        break;
                    }
                    sp_[-2].i64 = result;
                    sp_--;
                }
                break;

            case BCOpcode::IDX_CHK: {
                // Stack: [idx, lo, hi]
                int64_t hi = sp_[-1].i64;
                int64_t lo = sp_[-2].i64;
                int64_t idx = sp_[-3].i64;
                if (idx < lo || idx >= hi) {
                    if (!dispatchTrap(TrapKind::Bounds)) {
                        trap(TrapKind::Bounds, "index out of bounds");
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

            case BCOpcode::F64_TO_I64_CHK: {
                // Float to signed int64 with overflow check and round-to-even
                double val = sp_[-1].f64;
                // Check for NaN
                if (val != val) {
                    if (!dispatchTrap(TrapKind::InvalidCast)) {
                        trap(TrapKind::InvalidCast, "InvalidCast: float to int conversion of NaN");
                    }
                    break;
                }
                // Round to nearest, ties to even (banker's rounding)
                double rounded = std::rint(val);
                // Check for out of range (INT64_MIN to INT64_MAX)
                constexpr double maxI64 = 9223372036854775807.0;
                constexpr double minI64 = -9223372036854775808.0;
                if (rounded > maxI64 || rounded < minI64) {
                    if (!dispatchTrap(TrapKind::InvalidCast)) {
                        trap(TrapKind::InvalidCast,
                             "InvalidCast: float to int conversion overflow");
                    }
                    break;
                }
                sp_[-1].i64 = static_cast<int64_t>(rounded);
                break;
            }

            case BCOpcode::F64_TO_U64_CHK: {
                // Float to unsigned int64 with overflow check and round-to-even
                double val = sp_[-1].f64;
                // Check for NaN
                if (val != val) {
                    if (!dispatchTrap(TrapKind::InvalidCast)) {
                        trap(TrapKind::InvalidCast, "InvalidCast: float to uint conversion of NaN");
                    }
                    break;
                }
                // Round to nearest, ties to even (banker's rounding)
                double rounded = std::rint(val);
                // Check for out of range (0 to UINT64_MAX)
                constexpr double maxU64 = 18446744073709551615.0;
                if (rounded < 0.0 || rounded > maxU64) {
                    if (!dispatchTrap(TrapKind::InvalidCast)) {
                        trap(TrapKind::InvalidCast,
                             "InvalidCast: float to uint conversion overflow");
                    }
                    break;
                }
                sp_[-1].i64 = static_cast<int64_t>(static_cast<uint64_t>(rounded));
                break;
            }

            case BCOpcode::I64_NARROW_CHK: {
                // Signed narrow conversion with overflow check
                // Target type encoded in arg: 0=I1, 1=I16, 2=I32, 3=I64
                uint8_t targetType = decodeArg8_0(instr);
                int64_t val = sp_[-1].i64;
                bool inRange = true;
                switch (targetType) {
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
                if (!inRange) {
                    if (!dispatchTrap(TrapKind::Overflow)) {
                        trap(TrapKind::Overflow, "Overflow: signed narrow conversion overflow");
                    }
                    break;
                }
                // Value stays the same (already narrowed semantically)
                break;
            }

            case BCOpcode::U64_NARROW_CHK: {
                // Unsigned narrow conversion with overflow check
                // Target type encoded in arg: 0=I1, 1=I16, 2=I32, 3=I64
                uint8_t targetType = decodeArg8_0(instr);
                uint64_t val = static_cast<uint64_t>(sp_[-1].i64);
                bool inRange = true;
                switch (targetType) {
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
                if (!inRange) {
                    if (!dispatchTrap(TrapKind::Overflow)) {
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
            case BCOpcode::JUMP: {
                int16_t offset = decodeArgI16(instr);
                fp_->pc += offset;
                if (!ensureBranchTarget(*fp_->func, fp_->pc, "BytecodeVM::JUMP"))
                    return;
                break;
            }

            case BCOpcode::JUMP_IF_TRUE: {
                int16_t offset = decodeArgI16(instr);
                if ((--sp_)->i64 != 0) {
                    fp_->pc += offset;
                    if (!ensureBranchTarget(*fp_->func, fp_->pc, "BytecodeVM::JUMP_IF_TRUE"))
                        return;
                }
                break;
            }

            case BCOpcode::JUMP_IF_FALSE: {
                int16_t offset = decodeArgI16(instr);
                if ((--sp_)->i64 == 0) {
                    fp_->pc += offset;
                    if (!ensureBranchTarget(*fp_->func, fp_->pc, "BytecodeVM::JUMP_IF_FALSE"))
                        return;
                }
                break;
            }

            case BCOpcode::JUMP_LONG: {
                int32_t offset = decodeArgI24(instr);
                fp_->pc += offset;
                if (!ensureBranchTarget(*fp_->func, fp_->pc, "BytecodeVM::JUMP_LONG"))
                    return;
                break;
            }

            case BCOpcode::SWITCH: {
                // Format: SWITCH [numCases:u32] [defaultOffset:i32] [caseVal:i32 caseOffset:i32]...
                // Pop scrutinee from stack
                int32_t scrutinee = static_cast<int32_t>((--sp_)->i64);

                // pc currently points to the word after SWITCH opcode (numCases)
                const uint32_t *code = fp_->func->code.data();
                if (!ensureWordsAvailable(*fp_->func, fp_->pc, 2, "BytecodeVM::SWITCH(header)"))
                    return;
                uint32_t numCases = code[fp_->pc++];

                // Position of default offset word
                uint32_t defaultOffsetPos = fp_->pc++;
                const uint64_t caseWords = static_cast<uint64_t>(numCases) * 2u;
                if (caseWords > std::numeric_limits<uint32_t>::max() ||
                    !ensureWordsAvailable(*fp_->func,
                                          fp_->pc,
                                          static_cast<uint32_t>(caseWords),
                                          "BytecodeVM::SWITCH(cases)")) {
                    return;
                }

                // Search for matching case
                bool found = false;
                for (uint32_t i = 0; i < numCases; ++i) {
                    int32_t caseVal = static_cast<int32_t>(code[fp_->pc++]);
                    uint32_t caseOffsetPos = fp_->pc++;

                    if (caseVal == scrutinee) {
                        // Found matching case - jump to its target
                        // Offset is relative to the offset word position
                        int32_t caseOffset = static_cast<int32_t>(code[caseOffsetPos]);
                        fp_->pc = caseOffsetPos + caseOffset;
                        if (!ensureBranchTarget(*fp_->func, fp_->pc, "BytecodeVM::SWITCH(case)"))
                            return;
                        found = true;
                        break;
                    }
                }

                if (!found) {
                    // No match - use default offset
                    int32_t defaultOffset = static_cast<int32_t>(code[defaultOffsetPos]);
                    fp_->pc = defaultOffsetPos + defaultOffset;
                    if (!ensureBranchTarget(*fp_->func, fp_->pc, "BytecodeVM::SWITCH(default)"))
                        return;
                }
                break;
            }

            case BCOpcode::CALL: {
                uint16_t funcIdx = decodeArg16(instr);
                if (funcIdx < module_->functions.size()) {
                    call(&module_->functions[funcIdx]);
                } else {
                    trap(TrapKind::RuntimeError, "Invalid function index");
                }
                break;
            }

            case BCOpcode::RETURN: {
                BCSlot *resultSlot = --sp_;
                BCSlot result = *resultSlot;
                const bool resultOwnsString = slotOwnsString(resultSlot);
                setSlotOwnsString(resultSlot, false);
                if (!popFrame()) {
                    // Return from main function
                    *sp_++ = result;
                    setSlotOwnsString(sp_ - 1, resultOwnsString);
                    state_ = VMState::Halted;
                    return;
                }
                *sp_++ = result;
                setSlotOwnsString(sp_ - 1, resultOwnsString);
                break;
            }

            case BCOpcode::RETURN_VOID: {
                if (!popFrame()) {
                    state_ = VMState::Halted;
                    return;
                }
                break;
            }

            case BCOpcode::CALL_NATIVE: {
                // Instruction format: CALL_NATIVE [argCount:8][nativeIdx:16]
                uint8_t argCount = decodeArg8_0(instr);
                uint16_t nativeIdx = decodeArg16_1(instr);

                if (nativeIdx >= module_->nativeFuncs.size()) {
                    trap(TrapKind::RuntimeError, "Invalid native function index");
                    break;
                }

                const NativeFuncRef &ref = module_->nativeFuncs[nativeIdx];

                // Set up arguments (they're on the stack)
                BCSlot *args = sp_ - argCount;
                BCSlot result{};

                if (runtimeBridgeEnabled_) {
                    if (!invokeRuntimeBridgeNative(ref, args, argCount, result))
                        break;
                } else {
                    // Look up handler in local registry
                    auto it = nativeHandlers_.find(ref.name);
                    if (it == nativeHandlers_.end()) {
                        trap(TrapKind::RuntimeError, "Native function not registered");
                        break;
                    }
                    // Call the handler
                    it->second(args, argCount, &result);
                }

                dismissConsumedStringArgs(ref.name, args, argCount);
                releaseCallArgs(args, argCount);

                // Pop arguments
                sp_ -= argCount;

                // Push result if function returns a value
                if (ref.hasReturn) {
                    *sp_++ = result;
                    if (lookupRuntimeSignature(ref.name) &&
                        lookupRuntimeSignature(ref.name)->retType.kind ==
                            il::core::Type::Kind::Str &&
                        result.ptr) {
                        setSlotOwnsString(sp_ - 1, true);
                    } else {
                        setSlotOwnsString(sp_ - 1, false);
                    }
                }
                break;
            }

            case BCOpcode::CALL_INDIRECT: {
                // Indirect call through function pointer
                // Stack layout: [callee][arg0][arg1]...[argN] <- sp
                uint8_t argCount = decodeArg8_0(instr);

                // Get callee from below the arguments
                BCSlot *callee = sp_ - argCount - 1;
                BCSlot *args = sp_ - argCount;

                // Check if callee is a tagged function pointer (high bit set)
                constexpr uint64_t kFuncPtrTag = 0x8000000000000000ULL;
                uint64_t calleeVal = static_cast<uint64_t>(callee->i64);

                if (calleeVal & kFuncPtrTag) {
                    // Tagged function index - extract and call
                    uint32_t funcIdx = static_cast<uint32_t>(calleeVal & 0x7FFFFFFFULL);
                    if (funcIdx >= module_->functions.size()) {
                        trap(TrapKind::RuntimeError, "Invalid indirect function index");
                        break;
                    }

                    // Shift arguments down to overwrite the callee slot
                    for (int i = 0; i < argCount; ++i) {
                        callee[i] = args[i];
                        setSlotOwnsString(callee + i, slotOwnsString(args + i));
                        if (callee + i != args + i)
                            setSlotOwnsString(args + i, false);
                    }
                    sp_ = callee + argCount; // Adjust stack pointer

                    call(&module_->functions[funcIdx]);
                } else if (calleeVal == 0) {
                    // Null function pointer
                    trap(TrapKind::NullPointer, "Null indirect callee");
                    break;
                } else {
                    // Unknown pointer format
                    trap(TrapKind::RuntimeError, "Invalid indirect call target");
                    break;
                }
                break;
            }

            //==================================================================
            // Memory Operations (basic support)
            //==================================================================
            case BCOpcode::ALLOCA: {
                // Allocate from the separate alloca buffer (not operand stack)
                // This ensures alloca'd memory survives across function calls
                int64_t size = (--sp_)->i64;
                // Align to 8 bytes
                size = (size + 7) & ~7;

                // Check for alloca overflow
                if (allocaTop_ + static_cast<size_t>(size) > allocaBuffer_.size()) {
                    // Grow buffer if needed (up to 16MB limit)
                    size_t newSize = allocaBuffer_.size() * 2;
                    if (newSize > 16 * 1024 * 1024 ||
                        allocaTop_ + static_cast<size_t>(size) > newSize) {
                        trap(TrapKind::StackOverflow, "alloca stack overflow");
                        break;
                    }
                    allocaBuffer_.resize(newSize);
                }

                // Return pointer to allocated memory
                void *ptr = allocaBuffer_.data() + allocaTop_;
                std::memset(ptr, 0, static_cast<size_t>(size));
                allocaTop_ += static_cast<size_t>(size);
                sp_->ptr = ptr;
                setSlotOwnsString(sp_, false);
                sp_++;
                break;
            }

            case BCOpcode::GEP: {
                int64_t offset = (--sp_)->i64;
                uint8_t *ptr = static_cast<uint8_t *>(sp_[-1].ptr);
                sp_[-1].ptr = ptr + offset;
                break;
            }

            case BCOpcode::LOAD_I64_MEM: {
                void *ptr = sp_[-1].ptr;
                int64_t val;
                std::memcpy(&val, ptr, sizeof(val));
                sp_[-1].i64 = val;
                break;
            }

            case BCOpcode::STORE_I64_MEM: {
                int64_t val = (--sp_)->i64;
                void *ptr = (--sp_)->ptr;
                std::memcpy(ptr, &val, sizeof(val));
                break;
            }

            case BCOpcode::LOAD_I8_MEM: {
                void *ptr = sp_[-1].ptr;
                int8_t val;
                std::memcpy(&val, ptr, sizeof(val));
                sp_[-1].i64 = val; // Sign extend
                break;
            }

            case BCOpcode::LOAD_I16_MEM: {
                void *ptr = sp_[-1].ptr;
                int16_t val;
                std::memcpy(&val, ptr, sizeof(val));
                sp_[-1].i64 = val; // Sign extend
                break;
            }

            case BCOpcode::LOAD_I32_MEM: {
                void *ptr = sp_[-1].ptr;
                int32_t val;
                std::memcpy(&val, ptr, sizeof(val));
                sp_[-1].i64 = val; // Sign extend
                break;
            }

            case BCOpcode::LOAD_F64_MEM: {
                void *ptr = sp_[-1].ptr;
                double val;
                std::memcpy(&val, ptr, sizeof(val));
                sp_[-1].f64 = val;
                break;
            }

            case BCOpcode::LOAD_PTR_MEM: {
                void *val;
                std::memcpy(&val, sp_[-1].ptr, sizeof(val));
                sp_[-1].ptr = val;
                setSlotOwnsString(sp_ - 1, false);
                break;
            }

            case BCOpcode::LOAD_STR_MEM: {
                rt_string val = nullptr;
                std::memcpy(&val, sp_[-1].ptr, sizeof(val));
                sp_[-1].ptr = val;
                if (val) {
                    if (!validateStringHandle(val, "BytecodeVM::LOAD_STR_MEM"))
                        break;
                    rt_str_retain_maybe(val);
                    setSlotOwnsString(sp_ - 1, true);
                } else {
                    setSlotOwnsString(sp_ - 1, false);
                }
                break;
            }

            case BCOpcode::STORE_I8_MEM: {
                int8_t val = static_cast<int8_t>((--sp_)->i64);
                void *ptr = (--sp_)->ptr;
                std::memcpy(ptr, &val, sizeof(val));
                break;
            }

            case BCOpcode::STORE_I16_MEM: {
                int16_t val = static_cast<int16_t>((--sp_)->i64);
                void *ptr = (--sp_)->ptr;
                std::memcpy(ptr, &val, sizeof(val));
                break;
            }

            case BCOpcode::STORE_I32_MEM: {
                int32_t val = static_cast<int32_t>((--sp_)->i64);
                void *ptr = (--sp_)->ptr;
                std::memcpy(ptr, &val, sizeof(val));
                break;
            }

            case BCOpcode::STORE_F64_MEM: {
                double val = (--sp_)->f64;
                void *ptr = (--sp_)->ptr;
                std::memcpy(ptr, &val, sizeof(val));
                break;
            }

            case BCOpcode::STORE_PTR_MEM: {
                void *val = (--sp_)->ptr;
                void *ptr = (--sp_)->ptr;
                std::memcpy(ptr, &val, sizeof(val));
                setSlotOwnsString(sp_, false);
                setSlotOwnsString(sp_ + 1, false);
                break;
            }

            case BCOpcode::STORE_STR_MEM: {
                BCSlot *valueSlot = --sp_;
                rt_string incoming = static_cast<rt_string>(valueSlot->ptr);
                const bool incomingOwns = slotOwnsString(valueSlot);
                void *ptr = (--sp_)->ptr;
                rt_string current = nullptr;
                std::memcpy(&current, ptr, sizeof(current));
                if (current && !validateStringHandle(current, "BytecodeVM::STORE_STR_MEM(current)"))
                    break;
                rt_str_release_maybe(current);
                if (incoming && !validateStringHandle(incoming, "BytecodeVM::STORE_STR_MEM"))
                    break;
                if (incoming && !incomingOwns)
                    rt_str_retain_maybe(incoming);
                std::memcpy(ptr, &incoming, sizeof(incoming));
                setSlotOwnsString(valueSlot, false);
                setSlotOwnsString(sp_, false);
                break;
            }

            //==================================================================
            // Global Variables
            //==================================================================
            case BCOpcode::LOAD_GLOBAL: {
                uint16_t idx = decodeArg16(instr);
                if (idx >= globals_.size()) {
                    trap(TrapKind::InvalidOpcode, "LOAD_GLOBAL index out of range");
                    break;
                }
                *sp_++ = globals_[idx];
                if (idx < globalsStringOwned_.size() && globalsStringOwned_[idx] &&
                    globals_[idx].ptr) {
                    if (!validateStringHandle(globals_[idx].ptr, "BytecodeVM::LOAD_GLOBAL"))
                        return;
                    rt_str_retain_maybe(static_cast<rt_string>(globals_[idx].ptr));
                    setSlotOwnsString(sp_ - 1, true);
                } else {
                    setSlotOwnsString(sp_ - 1, false);
                }
                break;
            }

            case BCOpcode::STORE_GLOBAL: {
                uint16_t idx = decodeArg16(instr);
                BCSlot val = *--sp_;
                if (idx >= globals_.size()) {
                    setSlotOwnsString(sp_, false);
                    trap(TrapKind::InvalidOpcode, "STORE_GLOBAL index out of range");
                    break;
                }
                if (idx < globalsStringOwned_.size() && globalsStringOwned_[idx] &&
                    globals_[idx].ptr) {
                    if (validateStringHandle(globals_[idx].ptr, "BytecodeVM::STORE_GLOBAL"))
                        rt_str_release_maybe(static_cast<rt_string>(globals_[idx].ptr));
                }
                globals_[idx] = val;
                if (idx < globalsStringOwned_.size()) {
                    globalsStringOwned_[idx] = slotOwnsString(sp_) ? 1 : 0;
                }
                setSlotOwnsString(sp_, false);
                break;
            }

            //==================================================================
            // String Operations
            //==================================================================
            case BCOpcode::LOAD_STR: {
                uint16_t idx = decodeArg16(instr);
                // Use the cached rt_string object (not raw C string!)
                // The runtime expects rt_string (pointer to rt_string_impl struct)
                sp_->ptr = (idx < stringCache_.size()) ? stringCache_[idx] : nullptr;
                if (sp_->ptr) {
                    if (!validateStringHandle(sp_->ptr, "BytecodeVM::LOAD_STR"))
                        break;
                    rt_str_retain_maybe(static_cast<rt_string>(sp_->ptr));
                    setSlotOwnsString(sp_, true);
                } else {
                    setSlotOwnsString(sp_, false);
                }
                sp_++;
                break;
            }

            case BCOpcode::STR_RETAIN:
                if (sp_[-1].ptr) {
                    if (!validateStringHandle(sp_[-1].ptr, "BytecodeVM::STR_RETAIN"))
                        break;
                    rt_str_retain_maybe(static_cast<rt_string>(sp_[-1].ptr));
                    setSlotOwnsString(sp_ - 1, true);
                }
                break;

            case BCOpcode::STR_RELEASE:
                releaseOwnedString(sp_ - 1);
                sp_--;
                break;

            //==================================================================
            // Exception Handling
            //==================================================================
            case BCOpcode::EH_PUSH: {
                // Handler offset is in the next code word (raw i32 offset)
                const uint32_t *code = fp_->func->code.data();
                if (!ensureWordsAvailable(*fp_->func, fp_->pc, 1, "BytecodeVM::EH_PUSH"))
                    return;
                int32_t offset = static_cast<int32_t>(code[fp_->pc++]);
                uint32_t handlerPc =
                    static_cast<uint32_t>(static_cast<int32_t>(fp_->pc - 1) + offset);
                if (!ensureBranchTarget(*fp_->func, handlerPc, "BytecodeVM::EH_PUSH"))
                    return;
                pushExceptionHandler(handlerPc);
                break;
            }

            case BCOpcode::EH_POP:
                popExceptionHandler();
                break;

            case BCOpcode::EH_ENTRY:
                // Handler entry marker - no-op, execution continues
                break;

            case BCOpcode::TRAP: {
                uint8_t kind = decodeArg8_0(instr);
                TrapKind trapKind = static_cast<TrapKind>(kind);
                if (!dispatchTrap(trapKind)) {
                    trap(trapKind, "Unhandled trap");
                }
                break;
            }

            case BCOpcode::TRAP_FROM_ERR: {
                // Pop error code from stack and use as trap kind
                int64_t code = (--sp_)->i64;
                TrapKind trapKind = static_cast<TrapKind>(code);
                if (!dispatchTrap(trapKind)) {
                    trap(trapKind, "Unhandled trap from error");
                }
                break;
            }

            case BCOpcode::ERR_GET_KIND: {
                // Replace the error token with its trap discriminator.
                // The IL form always provides one Error operand, so this is a
                // consume-one / produce-one transform rather than an extra push.
                sp_[-1].i64 = sp_[-1].i64;
                break;
            }

            case BCOpcode::ERR_GET_CODE:
                // Replace the error token with the extracted code.
                sp_[-1].i64 = static_cast<int64_t>(currentErrorCode_);
                break;

            case BCOpcode::ERR_GET_IP:
                sp_[-1].i64 = trapRecord_.valid ? static_cast<int64_t>(trapRecord_.faultPc)
                                                : static_cast<int64_t>(fp_ ? fp_->pc : 0);
                break;

            case BCOpcode::ERR_GET_LINE:
                sp_[-1].i64 = trapRecord_.valid ? static_cast<int64_t>(trapRecord_.faultLine) : -1;
                break;

            case BCOpcode::RESUME_SAME:
                if (!resumeTrap(false))
                    trap(TrapKind::InvalidOperation, "resume.same: invalid resume token");
                break;

            case BCOpcode::RESUME_NEXT:
                if (!resumeTrap(true))
                    trap(TrapKind::InvalidOperation, "resume.next: invalid resume token");
                break;

            case BCOpcode::RESUME_LABEL: {
                // Resume at a specific label in the current frame. The IL
                // form supplies an explicit resume token operand, so the
                // bytecode op must consume and validate that token before
                // continuing to the destination label.
                BCSlot token = *--sp_;
                setSlotOwnsString(sp_, false);
                if (token.ptr != &trapRecord_ || !trapRecord_.valid) {
                    trap(TrapKind::InvalidOperation, "resume.label: invalid resume token");
                    break;
                }
                clearTrapRecord();
                const uint32_t *code = fp_->func->code.data();
                if (!ensureWordsAvailable(*fp_->func, fp_->pc, 1, "BytecodeVM::RESUME_LABEL"))
                    return;
                int32_t offset = static_cast<int32_t>(code[fp_->pc++]);
                fp_->pc = static_cast<uint32_t>(static_cast<int32_t>(fp_->pc - 1) + offset);
                if (!ensureBranchTarget(*fp_->func, fp_->pc, "BytecodeVM::RESUME_LABEL"))
                    return;
                break;
            }

            case BCOpcode::TRAP_KIND: {
                // Push the current trap kind as an I64 for typed-catch comparison.
                // Values 0-11 are aligned with il::vm::TrapKind (vm/Trap.hpp).
                // BC-specific kinds (100+) map to RuntimeError(9) as catch-all.
                uint8_t raw = static_cast<uint8_t>(trapKind_);
                int64_t ilKind = (raw <= 11) ? static_cast<int64_t>(raw) : 9;
                sp_->i64 = ilKind;
                sp_++;
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
void BytecodeVM::call(const BytecodeFunction *func) {
    // Check stack overflow
    if (callStack_.size() >= kMaxCallDepth) {
        trap(TrapKind::StackOverflow, "call stack overflow");
        return;
    }
    if (!ensureCallArity(func, fp_, sp_, "BytecodeVM::call"))
        return;
    if (!ensureFrameFootprint(func, sp_, "BytecodeVM::call"))
        return;

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

    // Ensure parameter locals own any incoming string handles.
    for (uint32_t i = 0; i < func->numParams; ++i) {
        BCSlot *slot = localsStart + i;
        if (localIsString(frame, i) && slot->ptr) {
            if (!validateStringHandle(slot->ptr, "BytecodeVM::call(param)")) {
                setSlotOwnsString(slot, false);
                continue;
            }
            if (!slotOwnsString(slot))
                rt_str_retain_maybe(static_cast<rt_string>(slot->ptr));
            setSlotOwnsString(slot, true);
        } else {
            setSlotOwnsString(slot, false);
        }
    }

    // Zero non-parameter locals
    std::fill(localsStart + func->numParams, localsStart + func->numLocals, BCSlot{});
    for (uint32_t i = func->numParams; i < func->numLocals; ++i)
        setSlotOwnsString(localsStart + i, false);

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
bool BytecodeVM::popFrame() {
    releaseFrameLocals(callStack_.back());

    // Restore alloca stack to the base of the popped frame
    // This releases all alloca'd memory from this function call
    allocaTop_ = callStack_.back().allocaBase;

    // Pop frame
    callStack_.pop_back();

    if (callStack_.empty()) {
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
void BytecodeVM::trap(TrapKind kind, const char *message) {
    trapKind_ = kind;
    trapMessage_ = message;
    state_ = VMState::Trapped;
}

/// @brief Check for signed addition overflow.
/// @return true if overflow would occur, false if safe.
/// Uses compiler builtins when available for efficiency.
bool BytecodeVM::addOverflow(int64_t a, int64_t b, int64_t &result) {
#if defined(__GNUC__) || defined(__clang__)
    return __builtin_add_overflow(a, b, &result);
#else
    if ((b > 0 && a > std::numeric_limits<int64_t>::max() - b) ||
        (b < 0 && a < std::numeric_limits<int64_t>::min() - b)) {
        return true;
    }
    result = a + b;
    return false;
#endif
}

/// @brief Check for signed subtraction overflow.
/// @return true if overflow would occur, false if safe.
bool BytecodeVM::subOverflow(int64_t a, int64_t b, int64_t &result) {
#if defined(__GNUC__) || defined(__clang__)
    return __builtin_sub_overflow(a, b, &result);
#else
    if ((b < 0 && a > std::numeric_limits<int64_t>::max() + b) ||
        (b > 0 && a < std::numeric_limits<int64_t>::min() + b)) {
        return true;
    }
    result = a - b;
    return false;
#endif
}

/// @brief Check for signed multiplication overflow.
/// @return true if overflow would occur, false if safe.
bool BytecodeVM::mulOverflow(int64_t a, int64_t b, int64_t &result) {
#if defined(__GNUC__) || defined(__clang__)
    return __builtin_mul_overflow(a, b, &result);
#else
    if (a > 0) {
        if (b > 0) {
            if (a > std::numeric_limits<int64_t>::max() / b)
                return true;
        } else {
            if (b < std::numeric_limits<int64_t>::min() / a)
                return true;
        }
    } else {
        if (b > 0) {
            if (a < std::numeric_limits<int64_t>::min() / b)
                return true;
        } else {
            if (a != 0 && b < std::numeric_limits<int64_t>::max() / a)
                return true;
        }
    }
    result = a * b;
    return false;
#endif
}

bool BytecodeVM::safeSignedDiv(int64_t a,
                               int64_t b,
                               int64_t &result,
                               TrapKind &fault) const {
    if (b == 0) {
        fault = TrapKind::DivideByZero;
        return false;
    }
    if (a == std::numeric_limits<int64_t>::min() && b == -1) {
        fault = TrapKind::Overflow;
        return false;
    }
    result = a / b;
    return true;
}

bool BytecodeVM::safeUnsignedDiv(int64_t a,
                                 int64_t b,
                                 int64_t &result,
                                 TrapKind &fault) const {
    if (b == 0) {
        fault = TrapKind::DivideByZero;
        return false;
    }
    result = static_cast<int64_t>(static_cast<uint64_t>(a) / static_cast<uint64_t>(b));
    return true;
}

bool BytecodeVM::safeSignedRem(int64_t a,
                               int64_t b,
                               int64_t &result,
                               TrapKind &fault) const {
    if (b == 0) {
        fault = TrapKind::DivideByZero;
        return false;
    }
    if (a == std::numeric_limits<int64_t>::min() && b == -1) {
        fault = TrapKind::Overflow;
        return false;
    }
    result = a % b;
    return true;
}

bool BytecodeVM::safeUnsignedRem(int64_t a,
                                 int64_t b,
                                 int64_t &result,
                                 TrapKind &fault) const {
    if (b == 0) {
        fault = TrapKind::DivideByZero;
        return false;
    }
    result = static_cast<int64_t>(static_cast<uint64_t>(a) % static_cast<uint64_t>(b));
    return true;
}

bool BytecodeVM::safeNegate(int64_t value, int64_t &result, TrapKind &fault) const {
    if (value == std::numeric_limits<int64_t>::min()) {
        fault = TrapKind::Overflow;
        return false;
    }
    result = -value;
    return true;
}

//==============================================================================
// Source Line Tracking
//==============================================================================

/// @brief Get the source line number at the current execution point.
/// @return The source line number, or 0 if not available.
uint32_t BytecodeVM::currentSourceLine() const {
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
uint32_t BytecodeVM::getSourceLine(const BytecodeFunction *func, uint32_t pc) {
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
void BytecodeVM::pushExceptionHandler(uint32_t handlerPc) {
    BCExceptionHandler eh;
    eh.handlerPc = handlerPc;
    eh.frameIndex = static_cast<uint32_t>(callStack_.size() - 1);
    eh.stackPointer = sp_;
    ehStack_.push_back(eh);
}

/// @brief Pop the most recently pushed exception handler.
///
/// Called when exiting a protected region normally (no exception occurred).
void BytecodeVM::popExceptionHandler() {
    if (!ehStack_.empty()) {
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
bool BytecodeVM::dispatchTrap(TrapKind kind, int32_t errorCode, const char *message) {
    clearTrapRecord();
    trapKind_ = kind;
    if (message)
        trapMessage_ = message;

    auto mapBasicErrorCode = [](TrapKind trapKind) -> int32_t {
        switch (trapKind) {
            case TrapKind::DivideByZero:
                return 11;
            case TrapKind::Overflow:
                return 6;
            case TrapKind::Bounds:
                return 9;
            case TrapKind::NullPointer:
                return 91;
            default:
                return 0;
        }
    };

    // Search for a handler and auto-pop it from the EH stack on dispatch.
    // Handler blocks always start with a clean EH stack — if catch body throws,
    // the trap propagates to the next outer handler rather than re-entering
    // the same one. Normal-path cleanup uses explicit eh.pop in IL.
    while (!ehStack_.empty()) {
        BCExceptionHandler eh = ehStack_.back();
        ehStack_.pop_back();
        if (eh.frameIndex >= callStack_.size())
            continue;

        clearTrapRecord();
        trapRecord_.valid = true;
        trapRecord_.kind = kind;
        trapRecord_.errorCode = errorCode;
        trapRecord_.faultPc = (fp_ && fp_->pc > 0) ? (fp_->pc - 1) : 0;
        trapRecord_.nextPc = fp_ ? fp_->pc : 0;
        const uint32_t line = (fp_ && fp_->func) ? getSourceLine(fp_->func, trapRecord_.faultPc) : 0;
        trapRecord_.faultLine = line ? static_cast<int32_t>(line) : -1;
        trapRecord_.valueCount = static_cast<size_t>(sp_ - valueStack_.data());
        trapRecord_.stackPointerIndex = trapRecord_.valueCount;
        trapRecord_.resumeStackPointerIndex =
            static_cast<size_t>(eh.stackPointer - valueStack_.data());
        trapRecord_.valueSlots.assign(valueStack_.begin(),
                                      valueStack_.begin() + trapRecord_.valueCount);
        trapRecord_.valueOwned.assign(valueStackStringOwned_.begin(),
                                      valueStackStringOwned_.begin() + trapRecord_.valueCount);
        for (size_t i = 0; i < trapRecord_.valueCount; ++i) {
            if (trapRecord_.valueOwned[i] == 0 || !trapRecord_.valueSlots[i].ptr)
                continue;
            if (validateStringHandle(trapRecord_.valueSlots[i].ptr,
                                     "BytecodeVM::dispatchTrap(snapshot)")) {
                rt_str_retain_maybe(static_cast<rt_string>(trapRecord_.valueSlots[i].ptr));
            }
        }
        trapRecord_.callStack = callStack_;
        trapRecord_.ehStack = ehStack_;
        trapRecord_.allocaSize = allocaTop_;
        trapRecord_.allocaBytes.assign(allocaBuffer_.begin(), allocaBuffer_.begin() + allocaTop_);

        // Unwind call stack to the frame where handler was registered
        while (callStack_.size() > eh.frameIndex + 1) {
            BCFrame &unwound = callStack_.back();
            while (sp_ > unwound.stackBase)
                releaseOwnedString(--sp_);
            releaseFrameLocals(unwound);
            allocaTop_ = unwound.allocaBase;
            callStack_.pop_back();
            if (!callStack_.empty())
                sp_ = callStack_.back().stackBase;
        }

        if (!callStack_.empty()) {
            fp_ = &callStack_.back();
            while (sp_ > eh.stackPointer)
                releaseOwnedString(--sp_);
            sp_ = eh.stackPointer;

            // Store trap info for err.get_* introspection
            trapKind_ = kind;
            currentErrorCode_ = errorCode >= 0 ? errorCode : mapBasicErrorCode(kind);

            // Push trap kind onto stack for handler to inspect (as error token)
            sp_->i64 = static_cast<int64_t>(kind);
            sp_++;
            // Push an opaque resume token pointing at the retained trap record.
            sp_->ptr = &trapRecord_;
            setSlotOwnsString(sp_, false);
            sp_++;

            // Jump to handler
            fp_->pc = eh.handlerPc;
            state_ = VMState::Running;
            return true;
        }

        // Frame for this handler no longer exists — already popped above, try next
        clearTrapRecord();
    }

    // No handler found - trap propagates to top level
    return false;
}

bool BytecodeVM::resumeTrap(bool useNextPc) {
    BCSlot token = *--sp_;
    setSlotOwnsString(sp_, false);
    if (token.ptr != &trapRecord_ || !trapRecord_.valid)
        return false;

    releaseOwnedValueStack();
    callStack_.clear();
    ehStack_.clear();
    std::fill(valueStackStringOwned_.begin(), valueStackStringOwned_.end(), 0);

    const size_t restoredCount =
        useNextPc ? trapRecord_.resumeStackPointerIndex : trapRecord_.stackPointerIndex;
    if (trapRecord_.valueCount > valueStack_.size() || restoredCount > trapRecord_.valueCount)
        return false;
    std::fill(valueStackStringOwned_.begin(), valueStackStringOwned_.end(), 0);
    std::copy(trapRecord_.valueSlots.begin(),
              trapRecord_.valueSlots.begin() + restoredCount,
              valueStack_.begin());
    std::copy(trapRecord_.valueOwned.begin(),
              trapRecord_.valueOwned.begin() + restoredCount,
              valueStackStringOwned_.begin());
    sp_ = valueStack_.data() + restoredCount;

    callStack_ = trapRecord_.callStack;
    ehStack_ = trapRecord_.ehStack;
    fp_ = callStack_.empty() ? nullptr : &callStack_.back();

    if (trapRecord_.allocaSize > allocaBuffer_.size())
        allocaBuffer_.resize(trapRecord_.allocaSize);
    std::copy(trapRecord_.allocaBytes.begin(), trapRecord_.allocaBytes.end(), allocaBuffer_.begin());
    allocaTop_ = trapRecord_.allocaSize;

    if (!fp_ || !fp_->func)
        return false;
    fp_->pc = useNextPc ? trapRecord_.nextPc : trapRecord_.faultPc;
    state_ = VMState::Running;
    for (size_t i = 0; i < restoredCount && i < trapRecord_.valueOwned.size(); ++i)
        trapRecord_.valueOwned[i] = 0;
    clearTrapRecord();
    return true;
}

//==============================================================================
// Debug Support
//==============================================================================

/// @brief Set a breakpoint at a specific location.
/// @param funcName The name of the function containing the breakpoint.
/// @param pc The program counter offset within the function.
void BytecodeVM::setBreakpoint(const std::string &funcName, uint32_t pc) {
    breakpoints_[funcName].insert(pc);
}

/// @brief Clear a breakpoint at a specific location.
/// @param funcName The name of the function containing the breakpoint.
/// @param pc The program counter offset to clear.
void BytecodeVM::clearBreakpoint(const std::string &funcName, uint32_t pc) {
    auto it = breakpoints_.find(funcName);
    if (it != breakpoints_.end()) {
        it->second.erase(pc);
        if (it->second.empty()) {
            breakpoints_.erase(it);
        }
    }
}

/// @brief Clear all breakpoints in all functions.
void BytecodeVM::clearAllBreakpoints() {
    breakpoints_.clear();
}

/// @brief Check if execution should pause at the current location.
/// @return true if execution should pause (breakpoint hit or single-stepping).
///
/// Called at the start of each instruction. Invokes the debug callback
/// if a breakpoint is hit or single-step mode is enabled.
bool BytecodeVM::checkBreakpoint() {
    if (!fp_ || !fp_->func)
        return false;

    bool isBreakpoint = false;
    auto it = breakpoints_.find(fp_->func->name);
    if (it != breakpoints_.end()) {
        isBreakpoint = it->second.count(fp_->pc) > 0;
    }

    // Check if we should pause (breakpoint hit or single-stepping)
    if (isBreakpoint || singleStep_) {
        if (debugCallback_) {
            return !debugCallback_(*this, fp_->func, fp_->pc, isBreakpoint);
        }
        return true; // Pause if no callback but breakpoint/step triggered
    }
    return false;
}

//===----------------------------------------------------------------------===//
// Bytecode VM Thread.Start Handler
//===----------------------------------------------------------------------===//

namespace {

/// Payload for spawning a new bytecode VM thread.
struct BytecodeThreadPayload {
    const BytecodeModule *module;
    const BytecodeFunction *entry;
    void *arg;
    BytecodeVM::ExecutionEnvironment environment;
};

/// Payload for bytecode-backed Async.Run.
struct BytecodeAsyncPayload {
    const BytecodeModule *module;
    const BytecodeFunction *entry;
    void *arg;
    BytecodeVM::ExecutionEnvironment environment;
    void *promise;
};

static bool runBytecodeThreadPayload(BytecodeThreadPayload *payload,
                                     char *errorBuf,
                                     size_t errorBufSize) {
    if (errorBuf && errorBufSize > 0)
        errorBuf[0] = '\0';
    if (!payload || !payload->module || !payload->entry) {
        if (errorBuf && errorBufSize > 0)
            std::snprintf(errorBuf, errorBufSize, "%s", "Thread.StartSafe: invalid bytecode entry");
        delete payload;
        return false;
    }

    BytecodeVM vm;
    vm.load(payload->module);
    vm.applyExecutionEnvironment(payload->environment);

    std::vector<BCSlot> args;
    if (payload->entry->numParams > 0) {
        BCSlot argSlot{};
        argSlot.ptr = payload->arg;
        args.push_back(argSlot);
    }

    vm.exec(payload->entry, args);
    if (vm.state() == VMState::Trapped) {
        const std::string &message = vm.trapMessage();
        if (errorBuf && errorBufSize > 0) {
            std::snprintf(errorBuf,
                          errorBufSize,
                          "%s",
                          message.empty() ? "Thread.StartSafe: trapped bytecode worker"
                                          : message.c_str());
        }
        delete payload;
        return false;
    }

    delete payload;
    return true;
}

/// Thread entry trampoline for bytecode VM threads.
extern "C" void bytecode_thread_entry_trampoline(void *raw) {
    char error[512];
    if (!runBytecodeThreadPayload(static_cast<BytecodeThreadPayload *>(raw), error, sizeof(error)))
        rt_abort(error[0] ? error : "Thread.Start: trapped bytecode worker");
}

/// Safe thread entry trampoline for bytecode VM threads.
extern "C" void bytecode_thread_safe_entry_trampoline(void *raw) {
    // rt_thread_start_safe uses setjmp/longjmp for recovery, so this trampoline
    // must not hold live C++ objects across rt_trap().
    char error[512];
    if (!runBytecodeThreadPayload(static_cast<BytecodeThreadPayload *>(raw), error, sizeof(error)))
        rt_trap(error[0] ? error : "Thread.StartSafe: trapped bytecode worker");
}

/// Resolve a bytecode function by pointer value.
/// The bytecode VM uses tagged function pointers: high bit set, lower bits are function index.
static const BytecodeFunction *resolveBytecodeEntry(const BytecodeModule *module, void *entry) {
    if (!entry || !module)
        return nullptr;

    // Check if this is a tagged function pointer (high bit set)
    constexpr uint64_t kFuncPtrTag = 0x8000000000000000ULL;
    uint64_t val = reinterpret_cast<uint64_t>(entry);

    if (val & kFuncPtrTag) {
        // Extract function index from tagged pointer
        uint64_t funcIdx = val & ~kFuncPtrTag;
        if (funcIdx < module->functions.size()) {
            return &module->functions[funcIdx];
        }
        return nullptr;
    }

    // Fallback: try to match as a raw pointer (for compatibility)
    const auto *candidate = static_cast<const BytecodeFunction *>(entry);
    for (const auto &fn : module->functions) {
        if (&fn == candidate) {
            return &fn;
        }
    }
    return nullptr;
}

/// Payload for standard VM thread spawning (duplicate of ThreadsRuntime.cpp)
struct VmThreadStartPayload {
    const il::core::Module *module = nullptr;
    std::shared_ptr<il::vm::VM::ProgramState> program;
    il::vm::ExternRegistry *externRegistry = nullptr;
    const il::core::Function *entry = nullptr;
    void *arg = nullptr;
};

struct VmAsyncRunPayload {
    const il::core::Module *module = nullptr;
    std::shared_ptr<il::vm::VM::ProgramState> program;
    il::vm::ExternRegistry *externRegistry = nullptr;
    const il::core::Function *entry = nullptr;
    void *arg = nullptr;
    void *promise = nullptr;
};

struct VmHttpHandlerPayload {
    const il::core::Module *module = nullptr;
    std::shared_ptr<il::vm::VM::ProgramState> program;
    il::vm::ExternRegistry *externRegistry = nullptr;
    const il::core::Function *entry = nullptr;
};

struct BytecodeHttpHandlerPayload {
    const BytecodeModule *module = nullptr;
    const BytecodeFunction *entry = nullptr;
    BytecodeVM::ExecutionEnvironment environment;
};

/// Standard VM thread entry trampoline
extern "C" void vm_thread_entry_trampoline_bc(void *raw) {
    VmThreadStartPayload *payload = static_cast<VmThreadStartPayload *>(raw);
    if (!payload || !payload->module || !payload->entry) {
        if (payload)
            il::vm::releaseExternRegistry(payload->externRegistry);
        delete payload;
        rt_abort("Thread.Start: invalid entry");
        return;
    }

    try {
        il::vm::VM vm(*payload->module, payload->program);
        vm.setExternRegistry(payload->externRegistry);
        il::support::SmallVector<il::vm::Slot, 2> args;
        if (payload->entry->params.size() == 1) {
            il::vm::Slot s{};
            s.ptr = payload->arg;
            args.push_back(s);
        }
        il::vm::detail::VMAccess::callFunction(vm, *payload->entry, args);
    } catch (...) {
        il::vm::releaseExternRegistry(payload->externRegistry);
        payload->externRegistry = nullptr;
        rt_abort("Thread.Start: unhandled exception");
    }
    il::vm::releaseExternRegistry(payload->externRegistry);
    delete payload;
}

/// Standard VM safe thread entry trampoline.
extern "C" void vm_thread_safe_entry_trampoline_bc(void *raw) {
    VmThreadStartPayload *payload = static_cast<VmThreadStartPayload *>(raw);
    if (!payload || !payload->module || !payload->entry) {
        if (payload)
            il::vm::releaseExternRegistry(payload->externRegistry);
        delete payload;
        rt_abort("Thread.StartSafe: invalid entry");
        return;
    }

    try {
        il::vm::VM vm(*payload->module, payload->program);
        vm.setExternRegistry(payload->externRegistry);
        il::support::SmallVector<il::vm::Slot, 2> args;
        if (payload->entry->params.size() == 1) {
            il::vm::Slot s{};
            s.ptr = payload->arg;
            args.push_back(s);
        }
        il::vm::detail::VMAccess::callFunction(vm, *payload->entry, args);
    } catch (...) {
        il::vm::releaseExternRegistry(payload->externRegistry);
        payload->externRegistry = nullptr;
        delete payload;
        rt_abort("Thread.StartSafe: unhandled exception in thread entry");
        return;
    }
    il::vm::releaseExternRegistry(payload->externRegistry);
    delete payload;
}

/// Resolve IL function pointer to module function
static const il::core::Function *resolveILEntry(const il::core::Module &module, void *entry) {
    if (!entry)
        return nullptr;
    const auto *candidate = static_cast<const il::core::Function *>(entry);
    for (const auto &fn : module.functions) {
        if (&fn == candidate)
            return &fn;
    }
    return nullptr;
}

/// Validate thread entry signature for standard VM
static void validateEntrySignature(const il::core::Function &fn) {
    using Kind = il::core::Type::Kind;
    if (fn.retType.kind != Kind::Void)
        rt_trap("Thread.Start: invalid entry signature");
    if (fn.params.empty())
        return;
    if (fn.params.size() == 1 && fn.params[0].type.kind == Kind::Ptr)
        return;
    rt_trap("Thread.Start: invalid entry signature");
}

static void validateAsyncEntrySignature(const il::core::Function &fn) {
    using Kind = il::core::Type::Kind;
    if (fn.retType.kind != Kind::Ptr)
        rt_trap("Async.Run: invalid entry signature");
    if (fn.params.size() == 1 && fn.params[0].type.kind == Kind::Ptr)
        return;
    rt_trap("Async.Run: invalid entry signature");
}

static void validateHttpHandlerSignature(const il::core::Function &fn) {
    using Kind = il::core::Type::Kind;
    if (fn.retType.kind != Kind::Void || fn.params.size() != 2 ||
        fn.params[0].type.kind != Kind::Ptr || fn.params[1].type.kind != Kind::Ptr) {
        rt_trap("HttpServer.BindHandler: invalid entry signature");
    }
}

static void validateBytecodeThreadEntrySignature(const BytecodeFunction &fn) {
    if (fn.hasReturn)
        rt_trap("Thread.Start: invalid bytecode entry signature");
    if (fn.numParams == 0 || fn.numParams == 1)
        return;
    rt_trap("Thread.Start: invalid bytecode entry signature");
}

static void validateBytecodeAsyncEntrySignature(const BytecodeFunction &fn) {
    if (!fn.hasReturn || fn.numParams != 1)
        rt_trap("Async.Run: invalid bytecode entry signature");
}

static void validateBytecodeHttpHandlerSignature(const BytecodeFunction &fn) {
    if (fn.hasReturn || fn.numParams != 2)
        rt_trap("HttpServer.BindHandler: invalid bytecode entry signature");
}

/// Handler for Viper.Threads.Thread.Start - handles both standard VM and BytecodeVM.
static void unified_thread_start_handler(void **args, void *result) {
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
    if (stdVm) {
        std::shared_ptr<il::vm::VM::ProgramState> program = stdVm->programState();
        if (!program)
            rt_trap("Thread.Start: invalid runtime state");

        const il::core::Module &module = stdVm->module();
        const il::core::Function *entryFn = resolveILEntry(module, entry);
        if (!entryFn)
            rt_trap("Thread.Start: invalid entry");
        validateEntrySignature(*entryFn);

        auto *payload = new VmThreadStartPayload{
            &module, std::move(program), stdVm->externRegistry(), entryFn, arg};
        il::vm::retainExternRegistry(payload->externRegistry);
        void *thread =
            rt_thread_start(reinterpret_cast<void *>(&vm_thread_entry_trampoline_bc), payload);
        if (!thread) {
            il::vm::releaseExternRegistry(payload->externRegistry);
            delete payload;
            rt_trap("Thread.Start: failed to create thread");
        }
        if (result)
            *reinterpret_cast<void **>(result) = thread;
        return;
    }

    // Check for BytecodeVM
    BytecodeVM *bcVm = activeBytecodeVMInstance();
    const BytecodeModule *bcModule = activeBytecodeModule();
    if (bcVm && bcModule) {
        const BytecodeFunction *entryFn = resolveBytecodeEntry(bcModule, entry);
        if (!entryFn)
            rt_trap("Thread.Start: invalid bytecode entry");
        validateBytecodeThreadEntrySignature(*entryFn);

        auto *payload =
            new BytecodeThreadPayload{bcModule, entryFn, arg, bcVm->captureExecutionEnvironment()};
        void *thread =
            rt_thread_start(reinterpret_cast<void *>(&bytecode_thread_entry_trampoline), payload);
        if (result)
            *reinterpret_cast<void **>(result) = thread;
        return;
    }

    // No VM active - direct call (native code path)
    if (gPriorThreadStartHandler) {
        gPriorThreadStartHandler(args, result);
        return;
    }
    void *thread = rt_thread_start(entry, arg);
    if (result)
        *reinterpret_cast<void **>(result) = thread;
}

/// Handler for Viper.Threads.Thread.StartSafe - handles both standard VM and BytecodeVM.
/// Uses rt_thread_start_safe to wrap execution in trap recovery via setjmp/longjmp.
static void unified_thread_start_safe_handler(void **args, void *result) {
    void *entry = nullptr;
    void *arg = nullptr;
    if (args && args[0])
        entry = *reinterpret_cast<void **>(args[0]);
    if (args && args[1])
        arg = *reinterpret_cast<void **>(args[1]);

    if (!entry)
        rt_trap("Thread.StartSafe: null entry");

    // Check for standard VM first
    il::vm::VM *stdVm = il::vm::activeVMInstance();
    if (stdVm) {
        std::shared_ptr<il::vm::VM::ProgramState> program = stdVm->programState();
        if (!program)
            rt_trap("Thread.StartSafe: invalid runtime state");

        const il::core::Module &module = stdVm->module();
        const il::core::Function *entryFn = resolveILEntry(module, entry);
        if (!entryFn)
            rt_trap("Thread.StartSafe: invalid entry");
        validateEntrySignature(*entryFn);

        auto *payload = new VmThreadStartPayload{
            &module, std::move(program), stdVm->externRegistry(), entryFn, arg};
        il::vm::retainExternRegistry(payload->externRegistry);
        void *thread = rt_thread_start_safe(
            reinterpret_cast<void *>(&vm_thread_safe_entry_trampoline_bc), payload);
        if (!thread) {
            il::vm::releaseExternRegistry(payload->externRegistry);
            delete payload;
            rt_trap("Thread.StartSafe: failed to create thread");
        }
        if (result)
            *reinterpret_cast<void **>(result) = thread;
        return;
    }

    // Check for BytecodeVM
    BytecodeVM *bcVm = activeBytecodeVMInstance();
    const BytecodeModule *bcModule = activeBytecodeModule();
    if (bcVm && bcModule) {
        const BytecodeFunction *entryFn = resolveBytecodeEntry(bcModule, entry);
        if (!entryFn)
            rt_trap("Thread.StartSafe: invalid bytecode entry");
        validateBytecodeThreadEntrySignature(*entryFn);

        auto *payload =
            new BytecodeThreadPayload{bcModule, entryFn, arg, bcVm->captureExecutionEnvironment()};
        void *thread = rt_thread_start_safe(
            reinterpret_cast<void *>(&bytecode_thread_safe_entry_trampoline), payload);
        if (result)
            *reinterpret_cast<void **>(result) = thread;
        return;
    }

    // No VM active - direct call (native code path)
    if (gPriorThreadStartSafeHandler) {
        gPriorThreadStartSafeHandler(args, result);
        return;
    }
    void *thread = rt_thread_start_safe(entry, arg);
    if (result)
        *reinterpret_cast<void **>(result) = thread;
}

extern "C" void vm_async_run_entry_trampoline_bc(void *raw) {
    VmAsyncRunPayload *payload = static_cast<VmAsyncRunPayload *>(raw);
    if (!payload || !payload->module || !payload->entry || !payload->promise) {
        if (payload)
            il::vm::releaseExternRegistry(payload->externRegistry);
        delete payload;
        rt_abort("Async.Run: invalid entry");
        return;
    }

    try {
        il::vm::VM vm(*payload->module, payload->program);
        vm.setExternRegistry(payload->externRegistry);
        il::support::SmallVector<il::vm::Slot, 2> args;
        il::vm::Slot s{};
        s.ptr = payload->arg;
        args.push_back(s);
        il::vm::Slot result = il::vm::detail::VMAccess::callFunction(vm, *payload->entry, args);
        rt_promise_set(payload->promise, result.ptr);
    } catch (...) {
        rt_promise_set_error(payload->promise, rt_const_cstr("Async.Run: unhandled exception"));
    }

    if (rt_obj_release_check0(payload->promise))
        rt_obj_free(payload->promise);
    il::vm::releaseExternRegistry(payload->externRegistry);
    delete payload;
}

extern "C" void bytecode_async_entry_trampoline(void *raw) {
    BytecodeAsyncPayload *payload = static_cast<BytecodeAsyncPayload *>(raw);
    if (!payload || !payload->module || !payload->entry || !payload->promise) {
        delete payload;
        rt_abort("Async.Run: invalid bytecode entry");
        return;
    }

    BytecodeVM vm;
    vm.load(payload->module);
    vm.applyExecutionEnvironment(payload->environment);

    std::vector<BCSlot> args;
    BCSlot argSlot{};
    argSlot.ptr = payload->arg;
    args.push_back(argSlot);

    BCSlot result = vm.exec(payload->entry, args);
    if (vm.state() == VMState::Trapped) {
        const std::string &message = vm.trapMessage();
        rt_promise_set_error(payload->promise,
                             message.empty()
                                 ? rt_const_cstr("Async.Run: trapped")
                                 : rt_string_from_bytes(message.data(), message.size()));
    } else {
        rt_promise_set(payload->promise, result.ptr);
    }

    if (rt_obj_release_check0(payload->promise))
        rt_obj_free(payload->promise);
    delete payload;
}

extern "C" void vm_http_handler_dispatch_bc(void *raw, void *req, void *res) {
    auto *payload = static_cast<VmHttpHandlerPayload *>(raw);
    if (!payload || !payload->module || !payload->entry) {
        rt_abort("HttpServer.BindHandler: invalid entry");
        return;
    }

    try {
        il::vm::VM vm(*payload->module, payload->program);
        vm.setExternRegistry(payload->externRegistry);
        il::support::SmallVector<il::vm::Slot, 2> args;
        il::vm::Slot reqSlot{};
        reqSlot.ptr = req;
        args.push_back(reqSlot);
        il::vm::Slot resSlot{};
        resSlot.ptr = res;
        args.push_back(resSlot);
        il::vm::detail::VMAccess::callFunction(vm, *payload->entry, args);
    } catch (...) {
        rt_abort("HttpServer.BindHandler: unhandled exception");
    }
}

extern "C" void bytecode_http_handler_dispatch(void *raw, void *req, void *res) {
    auto *payload = static_cast<BytecodeHttpHandlerPayload *>(raw);
    if (!payload || !payload->module || !payload->entry) {
        rt_abort("HttpServer.BindHandler: invalid bytecode entry");
        return;
    }

    BytecodeVM vm;
    vm.load(payload->module);
    vm.applyExecutionEnvironment(payload->environment);

    std::vector<BCSlot> args;
    BCSlot reqSlot{};
    reqSlot.ptr = req;
    args.push_back(reqSlot);
    BCSlot resSlot{};
    resSlot.ptr = res;
    args.push_back(resSlot);
    vm.exec(payload->entry, args);
    if (vm.state() == VMState::Trapped) {
        const std::string message = vm.trapMessage();
        rt_abort(message.empty() ? "HttpServer.BindHandler: trapped bytecode handler"
                                 : message.c_str());
    }
}

extern "C" void destroy_vm_http_handler_payload_bc(void *raw) {
    auto *payload = static_cast<VmHttpHandlerPayload *>(raw);
    if (!payload)
        return;
    il::vm::releaseExternRegistry(payload->externRegistry);
    delete payload;
}

extern "C" void destroy_bytecode_http_handler_payload(void *raw) {
    delete static_cast<BytecodeHttpHandlerPayload *>(raw);
}

static void unified_http_server_bind_handler(void **args, void *result) {
    (void)result;

    void *server = nullptr;
    rt_string tag = nullptr;
    void *entry = nullptr;
    if (args && args[0])
        server = *reinterpret_cast<void **>(args[0]);
    if (args && args[1])
        tag = *reinterpret_cast<rt_string *>(args[1]);
    if (args && args[2])
        entry = *reinterpret_cast<void **>(args[2]);

    if (!entry)
        rt_trap("HttpServer.BindHandler: null entry");

    il::vm::VM *stdVm = il::vm::activeVMInstance();
    if (stdVm) {
        std::shared_ptr<il::vm::VM::ProgramState> program = stdVm->programState();
        if (!program)
            rt_trap("HttpServer.BindHandler: invalid runtime state");

        const il::core::Module &module = stdVm->module();
        const il::core::Function *entryFn = resolveILEntry(module, entry);
        if (!entryFn)
            rt_trap("HttpServer.BindHandler: invalid entry");
        validateHttpHandlerSignature(*entryFn);

        auto *payload =
            new VmHttpHandlerPayload{&module, std::move(program), stdVm->externRegistry(), entryFn};
        il::vm::retainExternRegistry(payload->externRegistry);
        rt_http_server_bind_handler_dispatch(
            server,
            tag,
            reinterpret_cast<void *>(&vm_http_handler_dispatch_bc),
            payload,
            reinterpret_cast<void *>(&destroy_vm_http_handler_payload_bc));
        return;
    }

    BytecodeVM *bcVm = activeBytecodeVMInstance();
    const BytecodeModule *bcModule = activeBytecodeModule();
    if (bcVm && bcModule) {
        const BytecodeFunction *entryFn = resolveBytecodeEntry(bcModule, entry);
        if (!entryFn)
            rt_trap("HttpServer.BindHandler: invalid bytecode entry");
        validateBytecodeHttpHandlerSignature(*entryFn);

        auto *payload =
            new BytecodeHttpHandlerPayload{bcModule, entryFn, bcVm->captureExecutionEnvironment()};
        rt_http_server_bind_handler_dispatch(
            server,
            tag,
            reinterpret_cast<void *>(&bytecode_http_handler_dispatch),
            payload,
            reinterpret_cast<void *>(&destroy_bytecode_http_handler_payload));
        return;
    }

    if (gPriorHttpBindHandler) {
        gPriorHttpBindHandler(args, result);
        return;
    }
    rt_http_server_bind_handler(server, tag, entry);
}

static void unified_async_run_handler(void **args, void *result) {
    void *entry = nullptr;
    void *arg = nullptr;
    if (args && args[0])
        entry = *reinterpret_cast<void **>(args[0]);
    if (args && args[1])
        arg = *reinterpret_cast<void **>(args[1]);

    if (!entry)
        rt_trap("Async.Run: null entry");

    // Standard VM path
    il::vm::VM *stdVm = il::vm::activeVMInstance();
    if (stdVm) {
        std::shared_ptr<il::vm::VM::ProgramState> program = stdVm->programState();
        if (!program)
            rt_trap("Async.Run: invalid runtime state");

        const il::core::Module &module = stdVm->module();
        const il::core::Function *entryFn = resolveILEntry(module, entry);
        if (!entryFn)
            rt_trap("Async.Run: invalid entry");
        validateAsyncEntrySignature(*entryFn);

        void *promise = rt_promise_new();
        void *future = rt_promise_get_future(promise);
        auto *payload = new VmAsyncRunPayload{
            &module, std::move(program), stdVm->externRegistry(), entryFn, arg, promise};
        il::vm::retainExternRegistry(payload->externRegistry);
        void *thread =
            rt_thread_start(reinterpret_cast<void *>(&vm_async_run_entry_trampoline_bc), payload);
        if (!thread) {
            il::vm::releaseExternRegistry(payload->externRegistry);
            delete payload;
            rt_promise_set_error(promise, rt_const_cstr("Async.Run: failed to create thread"));
            if (result)
                *reinterpret_cast<void **>(result) = future;
            return;
        }

        if (rt_obj_release_check0(thread))
            rt_obj_free(thread);
        if (result)
            *reinterpret_cast<void **>(result) = future;
        return;
    }

    // Bytecode VM path
    BytecodeVM *bcVm = activeBytecodeVMInstance();
    const BytecodeModule *bcModule = activeBytecodeModule();
    if (bcVm && bcModule) {
        const BytecodeFunction *entryFn = resolveBytecodeEntry(bcModule, entry);
        if (!entryFn)
            rt_trap("Async.Run: invalid bytecode entry");
        validateBytecodeAsyncEntrySignature(*entryFn);

        void *promise = rt_promise_new();
        void *future = rt_promise_get_future(promise);
        auto *payload = new BytecodeAsyncPayload{
            bcModule, entryFn, arg, bcVm->captureExecutionEnvironment(), promise};
        void *thread =
            rt_thread_start(reinterpret_cast<void *>(&bytecode_async_entry_trampoline), payload);
        if (!thread) {
            delete payload;
            rt_promise_set_error(promise, rt_const_cstr("Async.Run: failed to create thread"));
            if (result)
                *reinterpret_cast<void **>(result) = future;
            return;
        }

        if (rt_obj_release_check0(thread))
            rt_obj_free(thread);
        if (result)
            *reinterpret_cast<void **>(result) = future;
        return;
    }

    if (gPriorAsyncRunHandler) {
        gPriorAsyncRunHandler(args, result);
        return;
    }
    void *future = rt_async_run(entry, arg);
    if (result)
        *reinterpret_cast<void **>(result) = future;
}

void registerUnifiedVmRuntimeHandlers() {
    std::call_once(gUnifiedRuntimeHandlersOnce, []() {
        using il::runtime::signatures::make_signature;
        using il::runtime::signatures::SigParam;

        auto capturePriorHandler = [](std::string_view name,
                                      void *currentFn,
                                      UnifiedRuntimeHandler &outHandler) {
            if (const il::vm::ExternDesc *existing = il::vm::RuntimeBridge::findExtern(name)) {
                if (existing->fn != currentFn)
                    outHandler = reinterpret_cast<UnifiedRuntimeHandler>(existing->fn);
            }
        };

        capturePriorHandler("Viper.Threads.Thread.Start",
                            reinterpret_cast<void *>(&unified_thread_start_handler),
                            gPriorThreadStartHandler);
        capturePriorHandler("Viper.Threads.Thread.StartSafe",
                            reinterpret_cast<void *>(&unified_thread_start_safe_handler),
                            gPriorThreadStartSafeHandler);
        capturePriorHandler("Viper.Threads.Async.Run",
                            reinterpret_cast<void *>(&unified_async_run_handler),
                            gPriorAsyncRunHandler);
        capturePriorHandler("Viper.Network.HttpServer.BindHandler",
                            reinterpret_cast<void *>(&unified_http_server_bind_handler),
                            gPriorHttpBindHandler);
    });

    using il::runtime::signatures::make_signature;
    using il::runtime::signatures::SigParam;

    {
        il::vm::ExternDesc ext;
        ext.name = "Viper.Threads.Thread.Start";
        ext.signature = make_signature(ext.name, {SigParam::Ptr, SigParam::Ptr}, {SigParam::Ptr});
        ext.fn = reinterpret_cast<void *>(&unified_thread_start_handler);
        il::vm::RuntimeBridge::registerExtern(ext);
    }
    {
        il::vm::ExternDesc ext;
        ext.name = "Viper.Threads.Thread.StartSafe";
        ext.signature = make_signature(ext.name, {SigParam::Ptr, SigParam::Ptr}, {SigParam::Ptr});
        ext.fn = reinterpret_cast<void *>(&unified_thread_start_safe_handler);
        il::vm::RuntimeBridge::registerExtern(ext);
    }
    {
        il::vm::ExternDesc ext;
        ext.name = "Viper.Threads.Async.Run";
        ext.signature = make_signature(ext.name, {SigParam::Ptr, SigParam::Ptr}, {SigParam::Ptr});
        ext.fn = reinterpret_cast<void *>(&unified_async_run_handler);
        il::vm::RuntimeBridge::registerExtern(ext);
    }
    {
        il::vm::ExternDesc ext;
        ext.name = "Viper.Network.HttpServer.BindHandler";
        ext.signature = make_signature(ext.name, {SigParam::Ptr, SigParam::Str, SigParam::Ptr});
        ext.fn = reinterpret_cast<void *>(&unified_http_server_bind_handler);
        il::vm::RuntimeBridge::registerExtern(ext);
    }
}

/// Static initializer to register the unified thread/async handlers.
/// This overrides the standard VM handlers when BytecodeVM is linked.
struct UnifiedThreadHandlerRegistrar {
    UnifiedThreadHandlerRegistrar() {
        registerUnifiedVmRuntimeHandlers();
    }
};

// Register the unified handlers when the library is loaded
[[maybe_unused]] const UnifiedThreadHandlerRegistrar kUnifiedThreadHandlerRegistrar{};

} // anonymous namespace

} // namespace bytecode
} // namespace viper
