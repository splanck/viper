// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.

#include "bytecode/BytecodeVM.hpp"
#include "il/core/Module.hpp"
#include "il/runtime/RuntimeSignatures.hpp"
#include "il/runtime/signatures/Registry.hpp"
#include "rt_async.h"
#include "rt_future.h"
#include "rt_http_server.h"
#include "rt_https_server.h"
#include "rt_object.h"
#include "rt_threads.h"
#include "support/small_vector.hpp"
#include "viper/runtime/rt.h"
#include "vm/OpHandlerAccess.hpp"
#include "vm/RuntimeBridge.hpp"
#include "vm/VM.hpp"
#include "vm/VMContext.hpp"
#include "vm/err_bridge.hpp"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <exception>
#include <limits>
#include <mutex>
#include <new>
#include <span>
#include <sstream>
#include <string_view>
#include <vector>

namespace viper {
namespace bytecode {

namespace {

/// @brief True if parameter @p index of runtime signature @p sig is a string,
///        so the caller knows that argument participates in string ownership.
bool runtimeParamIsString(const il::runtime::RuntimeSignature *sig, size_t index) {
    return sig && index < sig->paramTypes.size() &&
           sig->paramTypes[index].kind == il::core::Type::Kind::Str;
}

void registerUnifiedVmRuntimeHandlers();

/// @brief No-op runtime trap observer; installed so the runtime's trap-signal
///        hook has a valid target while the bytecode VM handles traps itself.
void bytecodeRuntimeTrapPassthrough(const il::vm::RuntimeTrapSignal &, void *) {}

/// @brief Default numeric error code for a trap kind when none is supplied
///        (DivideByZero→0, Overflow→4, Bounds→7, NullPointer→91, else 0).
int32_t defaultBytecodeTrapErrorCode(TrapKind trapKind) {
    switch (trapKind) {
        case TrapKind::DivideByZero:
            return 0;
        case TrapKind::Overflow:
            return 4;
        case TrapKind::Bounds:
            return 7;
        case TrapKind::NullPointer:
            return 91;
        default:
            return 0;
    }
}

/// @brief Stable human-readable name for a trap kind (for diagnostics/traces);
///        returns "Unknown" for any value outside the enum.
const char *bytecodeTrapKindName(TrapKind trapKind) {
    switch (trapKind) {
        case TrapKind::DivideByZero:
            return "DivideByZero";
        case TrapKind::Overflow:
            return "Overflow";
        case TrapKind::InvalidCast:
            return "InvalidCast";
        case TrapKind::DomainError:
            return "DomainError";
        case TrapKind::Bounds:
            return "Bounds";
        case TrapKind::FileNotFound:
            return "FileNotFound";
        case TrapKind::EndOfFile:
            return "EndOfFile";
        case TrapKind::IOError:
            return "IOError";
        case TrapKind::InvalidOperation:
            return "InvalidOperation";
        case TrapKind::RuntimeError:
            return "RuntimeError";
        case TrapKind::Interrupt:
            return "Interrupt";
        case TrapKind::NetworkError:
            return "NetworkError";
        case TrapKind::NullPointer:
            return "NullPointer";
        case TrapKind::StackOverflow:
            return "StackOverflow";
        case TrapKind::InvalidOpcode:
            return "InvalidOpcode";
        case TrapKind::None:
            return "None";
    }
    return "Unknown";
}

using UnifiedRuntimeHandler = void (*)(void **, void *);
std::once_flag gUnifiedRuntimeHandlersOnce;
UnifiedRuntimeHandler gPriorThreadStartHandler = nullptr;
UnifiedRuntimeHandler gPriorThreadStartOwnedHandler = nullptr;
UnifiedRuntimeHandler gPriorThreadStartSafeHandler = nullptr;
UnifiedRuntimeHandler gPriorThreadStartSafeOwnedHandler = nullptr;
UnifiedRuntimeHandler gPriorAsyncRunHandler = nullptr;
UnifiedRuntimeHandler gPriorHttpBindHandler = nullptr;
UnifiedRuntimeHandler gPriorHttpsBindHandler = nullptr;

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

/// @brief The BytecodeVM executing on the current thread, or NULL — lets
///        runtime handlers detect a bytecode caller and adapt accordingly.
BytecodeVM *activeBytecodeVMInstance() {
    return tlsActiveBytecodeVM;
}

/// @brief The BytecodeModule active on the current thread (used when spawning
///        worker threads that must run bytecode entry points).
const BytecodeModule *activeBytecodeModule() {
    return tlsActiveBytecodeModule;
}

/// @brief RAII: make @p vm the thread-active VM for the guard's lifetime,
///        restoring the previous one on scope exit (supports re-entrant
///        bytecode execution, e.g. a runtime callback into bytecode).
ActiveBytecodeVMGuard::ActiveBytecodeVMGuard(BytecodeVM *vm)
    : previous_(tlsActiveBytecodeVM), current_(vm) {
    tlsActiveBytecodeVM = vm;
}

/// @brief Restore the previously-active thread VM.
ActiveBytecodeVMGuard::~ActiveBytecodeVMGuard() {
    tlsActiveBytecodeVM = previous_;
}

/// @brief Construct an idle VM with pre-allocated execution buffers.
/// @details The value stack and per-slot string-ownership bitmap are sized for
///          the worst case up front, and the alloca arena reserves its full
///          16 MiB so it never reallocates mid-run — alloca pointers live in
///          registers/operand slots and must stay valid for the function's
///          lifetime. Threaded dispatch is the default.
BytecodeVM::BytecodeVM()
    : module_(nullptr), state_(VMState::Ready), trapKind_(TrapKind::None), currentErrorCode_(0),
      sp_(nullptr), fp_(nullptr), instrCount_(0), maxInstrCount_(0), runtimeBridgeEnabled_(false),
      useThreadedDispatch_(true), // Default to faster threaded dispatch
      trustedDispatch_(false), allocaTop_(0), singleStep_(false) {
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

/// @brief Tear down the VM: unwind execution state, release owned globals and
///        the trap record, and unref every cached rt_string literal.
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

/// @brief Reset the string cache slots for the loaded module.
///
/// The runtime expects managed rt_string handles, not raw C strings. Handles
/// are materialized lazily by @ref getStringLiteral so worker VMs only create
/// literals they actually execute.
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

    stringCache_.assign(module_->stringPool.size(), nullptr);
}

/// @brief Lazily materialize and cache the rt_string for string-pool entry
///        @p idx; returns NULL if there is no module or @p idx is out of range.
/// @details Handles are created on first use so a worker VM only allocates the
///          literals it actually executes.
rt_string BytecodeVM::getStringLiteral(uint16_t idx) {
    if (!module_ || idx >= module_->stringPool.size())
        return nullptr;
    if (idx >= stringCache_.size())
        stringCache_.resize(module_->stringPool.size(), nullptr);

    rt_string &cached = stringCache_[idx];
    if (!cached)
        cached = rt_const_cstr(module_->stringPool[idx].c_str());
    return cached;
}

/// @brief True if runtime function @p name takes ownership of string args via
///        a *clone* (caller keeps its reference). Must mirror rtgen's
///        needsConsumingStringHandler() — e.g. rt_str_concat.
bool BytecodeVM::runtimeCallConsumesClonedStringArgs(std::string_view name) {
    // Keep this list aligned with rtgen's needsConsumingStringHandler().
    return name == "rt_str_concat" || name == "Viper.String.Concat" ||
           name == "Viper.String.ConcatSelf";
}

/// @brief True if runtime function @p name consumes the *caller's owned*
///        string reference (e.g. rt_str_release_maybe) — the slot's ownership
///        flag must be cleared after the call.
bool BytecodeVM::runtimeCallConsumesOwnedStringArgs(std::string_view name) {
    return name == "rt_str_release_maybe";
}

/// @brief For a clone-consuming runtime call, return a copy of @p args with an
///        extra retain on each string parameter, so the callee can consume its
///        reference while the VM's originals stay valid. Empty if N/A.
std::vector<BCSlot> BytecodeVM::cloneRuntimeStringArgs(const NativeFuncRef &ref,
                                                       const BCSlot *args,
                                                       size_t argCount) const {
    if (!args || argCount == 0)
        return {};
    if (!ref.consumesClonedStringArgs)
        return {};

    const auto *sig = ref.runtimeSignature;
    if (!sig)
        return {};

    std::vector<BCSlot> cloned(args, args + argCount);
    for (size_t i = 0; i < argCount; ++i) {
        if (runtimeParamIsString(sig, i))
            rt_str_retain_maybe(static_cast<rt_string>(cloned[i].ptr));
    }
    return cloned;
}

/// @brief Drop the extra retains added by @ref cloneRuntimeStringArgs once the
///        runtime call has returned (balances the clone's reference counts).
void BytecodeVM::releaseRuntimeStringArgs(const NativeFuncRef &ref,
                                          std::vector<BCSlot> &args) const {
    if (args.empty())
        return;
    if (!ref.consumesClonedStringArgs)
        return;

    const auto *sig = ref.runtimeSignature;
    if (!sig)
        return;
    for (size_t i = 0; i < args.size(); ++i) {
        if (runtimeParamIsString(sig, i))
            rt_str_release_maybe(static_cast<rt_string>(args[i].ptr));
    }
}

/// @brief Call a native runtime function through the VM RuntimeBridge.
/// @details Clones string args when the callee consumes them, reinterprets
///          BCSlots as VM Slots, and dispatches by descriptor or by name. A
///          RuntimeTrapSignal is converted to a VM trap (via dispatchTrap, or
///          a hard trap if unhandled) and the call reports failure.
/// @return true on normal return (@p result set); false if the call trapped.
bool BytecodeVM::invokeRuntimeBridgeNative(const NativeFuncRef &ref,
                                           BCSlot *args,
                                           uint8_t argCount,
                                           BCSlot &result) {
    std::vector<BCSlot> preservedArgs =
        cloneRuntimeStringArgs(ref, args, static_cast<size_t>(argCount));
    BCSlot *callArgs = preservedArgs.empty() ? args : preservedArgs.data();
    il::vm::Slot *vmArgs = reinterpret_cast<il::vm::Slot *>(callArgs);
    std::span<const il::vm::Slot> argSpan{vmArgs, static_cast<size_t>(argCount)};

    il::vm::RuntimeCallContext ctx;
    try {
        il::vm::ScopedRuntimeTrapInterceptor trapInterceptor(&bytecodeRuntimeTrapPassthrough, this);
        il::vm::Slot vmResult =
            ref.runtimeDescriptor
                ? il::vm::RuntimeBridge::call(ctx,
                                              *ref.runtimeDescriptor,
                                              argSpan,
                                              il::support::SourceLoc{},
                                              fp_ && fp_->func ? fp_->func->name : std::string{},
                                              "")
                : il::vm::RuntimeBridge::call(ctx,
                                              ref.name,
                                              argSpan,
                                              il::support::SourceLoc{},
                                              fp_ && fp_->func ? fp_->func->name : std::string{},
                                              "");
        result.i64 = vmResult.i64;
    } catch (const il::vm::RuntimeTrapSignal &signal) {
        releaseRuntimeStringArgs(ref, preservedArgs);
        if (!dispatchTrap(static_cast<TrapKind>(signal.kind), signal.code, signal.message.c_str()))
            trap(static_cast<TrapKind>(signal.kind), signal.message.c_str());
        return false;
    }

    releaseRuntimeStringArgs(ref, preservedArgs);
    return true;
}

/// @brief After an owned-consuming runtime call, clear the VM-side ownership
///        flag on each string arg so the VM won't double-release a handle the
///        callee already took ownership of.
void BytecodeVM::dismissConsumedStringArgs(const NativeFuncRef &ref,
                                           BCSlot *args,
                                           uint8_t argCount) {
    if (!args || argCount == 0)
        return;
    if (!ref.consumesOwnedStringArgs)
        return;

    const auto *sig = ref.runtimeSignature;
    if (!sig)
        return;

    for (uint8_t i = 0; i < argCount; ++i) {
        if (runtimeParamIsString(sig, i))
            setSlotOwnsString(args + i, false);
    }
}

/// @brief Register a C++ handler for native function @p name, overriding any
///        prior handler of that name (used for builtins not in the bridge).
void BytecodeVM::registerNativeHandler(const std::string &name, NativeHandler handler) {
    nativeHandlers_[name] = std::move(handler);
}

/// @brief Snapshot the tunable execution settings (bridge enabled, dispatch
///        mode, trusted flag, native handler table) for transfer to a worker VM.
BytecodeVM::ExecutionEnvironment BytecodeVM::captureExecutionEnvironment() const {
    ExecutionEnvironment env;
    env.runtimeBridgeEnabled = runtimeBridgeEnabled_;
    env.useThreadedDispatch = useThreadedDispatch_;
    env.trustedDispatch = trustedDispatch_;
    env.maxInstructions = maxInstrCount_;
    env.nativeHandlers = nativeHandlers_;
    return env;
}

/// @brief Apply a previously captured execution environment onto this VM.
void BytecodeVM::applyExecutionEnvironment(const ExecutionEnvironment &env) {
    runtimeBridgeEnabled_ = env.runtimeBridgeEnabled;
    useThreadedDispatch_ = env.useThreadedDispatch;
    trustedDispatch_ = env.trustedDispatch;
    maxInstrCount_ = env.maxInstructions;
    nativeHandlers_ = env.nativeHandlers;
}

/// @brief Convenience: copy @p other's execution environment onto this VM
///        (used when a spawned worker VM should mirror its parent's config).
void BytecodeVM::copyExecutionEnvironmentFrom(const BytecodeVM &other) {
    applyExecutionEnvironment(other.captureExecutionEnvironment());
}

/// @brief Bind @p module for execution: install unified runtime handlers,
///        reset state, and allocate + initialize global storage (string
///        globals get an owned rt_string from their init bytes; scalar
///        globals are memcpy'd from their baked init image) plus the string
///        literal cache. Safe to call again to re-load a different module.
void BytecodeVM::load(const BytecodeModule *module) {
    registerUnifiedVmRuntimeHandlers();
    resetExecutionState();
    releaseOwnedGlobals();
    clearTrapRecord();

    module_ = module;
    state_ = VMState::Ready;
    trapKind_ = TrapKind::None;
    currentErrorCode_ = 0;
    pendingTrapErrorCode_ = false;
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
            if (gi.type.kind == il::core::Type::Kind::Str) {
                globals_[i].ptr = rt_string_from_bytes(gi.initString.data(), gi.initString.size());
                globalsStringOwned_[i] = globals_[i].ptr ? 1 : 0;
            } else if (!gi.initData.empty()) {
                size_t copySize = std::min<size_t>(gi.initData.size(), sizeof(BCSlot));
                std::memcpy(&globals_[i], gi.initData.data(), copySize);
            }
        }
    }

    // Initialize string cache with proper rt_string objects
    initStringCache();
}

/// @brief Index of @p slot within the value stack (the key into the parallel
///        string-ownership bitmap). Asserts the pointer is in range.
size_t BytecodeVM::slotIndex(const BCSlot *slot) const {
    assert(slot >= valueStack_.data());
    assert(slot < valueStack_.data() + valueStack_.size());
    return static_cast<size_t>(slot - valueStack_.data());
}

/// @brief True if @p slot currently holds an owned string reference (the VM
///        is responsible for releasing it).
bool BytecodeVM::slotOwnsString(const BCSlot *slot) const {
    return valueStackStringOwned_[slotIndex(slot)] != 0;
}

/// @brief Set/clear @p slot's "owns string reference" flag.
void BytecodeVM::setSlotOwnsString(const BCSlot *slot, bool owns) {
    valueStackStringOwned_[slotIndex(slot)] = owns ? 1 : 0;
}

/// @brief True if local slot @p idx of @p frame is typed as a string (so its
///        contents are reference-counted).
bool BytecodeVM::localIsString(const BCFrame &frame, uint32_t idx) const {
    return idx < frame.func->localIsString.size() && frame.func->localIsString[idx] != 0;
}

/// @brief Defensive check that @p ptr is a live rt_string handle.
/// @return true if NULL or a valid handle; otherwise traps (RuntimeError,
///         naming @p site) and returns false. Guards against type-confused
///         slots corrupting the runtime string heap.
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

/// @brief If @p slot owns a string, release that reference and clear both the
///        pointer and the ownership flag (idempotent; tolerates a bad handle
///        by just dropping it).
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

/// @brief Push local slot @p idx onto the operand stack; if the local is a
///        string, retain the reference and mark the new slot as owning it.
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

/// @brief Pop the operand stack into local slot @p idx. For string locals,
///        releases the old value, then transfers the stack slot's reference
///        (retaining only if the source did not already own it) so the net
///        reference count stays balanced.
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

/// @brief Release any owned string references held in a call's argument slots.
void BytecodeVM::releaseCallArgs(BCSlot *args, uint8_t argCount) {
    for (uint8_t i = 0; i < argCount; ++i)
        releaseOwnedString(args + i);
}

/// @brief Release owned string references in every local of @p frame and clear
///        the ownership flags — called when a frame is torn down.
void BytecodeVM::releaseFrameLocals(const BCFrame &frame) {
    for (uint32_t i = 0; i < frame.func->numLocals; ++i) {
        if (localIsString(frame, i))
            releaseOwnedString(frame.locals + i);
        else
            setSlotOwnsString(frame.locals + i, false);
    }
}

/// @brief Release every owned string still sitting on the value stack — used
///        during teardown/unwind so a trap can't leak operand-stack strings.
void BytecodeVM::releaseOwnedValueStack() {
    for (size_t i = 0; i < valueStack_.size(); ++i) {
        if (valueStackStringOwned_[i] == 0)
            continue;
        releaseOwnedString(valueStack_.data() + i);
    }
}

/// @brief Release every owned string global (module teardown / re-load).
void BytecodeVM::releaseOwnedGlobals() {
    for (size_t i = 0; i < globals_.size(); ++i) {
        releaseOwnedGlobalString(i);
    }
}

/// @brief Reverse-map a raw pointer to a global slot to its global index, or
///        SIZE_MAX if @p ptr is not the address of a global (lets GEP/store
///        opcodes detect writes that target a string global).
size_t BytecodeVM::globalIndexForAddress(const void *ptr) const {
    if (!ptr)
        return SIZE_MAX;
    for (size_t i = 0; i < globals_.size(); ++i) {
        if (ptr == static_cast<const void *>(&globals_[i]))
            return i;
    }
    return SIZE_MAX;
}

/// @brief Release the string reference owned by global @p idx (no-op if the
///        global is not an owned string), clearing pointer and ownership flag.
void BytecodeVM::releaseOwnedGlobalString(size_t idx) {
    if (idx >= globals_.size() || idx >= globalsStringOwned_.size() ||
        globalsStringOwned_[idx] == 0)
        return;
    if (globals_[idx].ptr && validateStringHandle(globals_[idx].ptr, "BytecodeVM::globals")) {
        rt_str_release_maybe(static_cast<rt_string>(globals_[idx].ptr));
    }
    globals_[idx].ptr = nullptr;
    globalsStringOwned_[idx] = 0;
}

/// @brief Before a raw (untyped) store through a pointer that aliases a string
///        global, release the global's old reference so it isn't leaked.
void BytecodeVM::clearGlobalStringOwnershipForRawStore(void *ptr) {
    const size_t idx = globalIndexForAddress(ptr);
    if (idx == SIZE_MAX)
        return;
    releaseOwnedGlobalString(idx);
}

/// @brief Mark the string global aliased by @p ptr as owned/not-owned after a
///        typed store transferred a reference into it.
void BytecodeVM::setGlobalStringOwnershipForAddress(void *ptr, bool owns) {
    const size_t idx = globalIndexForAddress(ptr);
    if (idx == SIZE_MAX || idx >= globalsStringOwned_.size())
        return;
    globalsStringOwned_[idx] = owns ? 1 : 0;
}

/// @brief Release any owned strings captured in the pending trap record and
///        reset it — called once the trap has been delivered or discarded.
void BytecodeVM::clearTrapRecord() {
    for (size_t i = 0; i < trapRecord_.valueSlots.size() && i < trapRecord_.valueOwned.size();
         ++i) {
        if (trapRecord_.valueOwned[i] == 0)
            continue;
        if (trapRecord_.valueSlots[i].ptr &&
            validateStringHandle(trapRecord_.valueSlots[i].ptr, "BytecodeVM::clearTrapRecord")) {
            rt_str_release_maybe(static_cast<rt_string>(trapRecord_.valueSlots[i].ptr));
        }
    }
    trapRecord_ = TrapRecord{};
}

/// @brief Return the VM to a clean pre-execution state: release stack/trap
///        strings, clear the call and EH stacks, rewind sp/fp/alloca, and
///        zero the string-ownership bitmap. (Globals are kept — see
///        @ref releaseOwnedGlobals.)
void BytecodeVM::resetExecutionState() {
    releaseOwnedValueStack();
    clearTrapRecord();
    callStack_.clear();
    ehStack_.clear();
    sp_ = valueStack_.data();
    fp_ = nullptr;
    allocaTop_ = 0;
    pendingTrapErrorCode_ = false;
    std::fill(valueStackStringOwned_.begin(), valueStackStringOwned_.end(), 0);
}

/// @brief Current operand-stack depth of @p frame given stack pointer @p sp
///        (slots pushed since the frame's stack base).
uint32_t BytecodeVM::operandDepth(const BCFrame &frame, const BCSlot *sp) const {
    return static_cast<uint32_t>(sp - frame.stackBase);
}

/// @brief Verifier guard: trap (InvalidOpcode) unless @p pc is within @p func's
///        code. Part of the untrusted-bytecode safety net.
bool BytecodeVM::ensurePcInRange(const BytecodeFunction &func, uint32_t pc, const char *site) {
    if (pc < func.code.size())
        return true;
    trap(TrapKind::InvalidOpcode, (std::string(site) + ": program counter out of range").c_str());
    return false;
}

/// @brief Verifier guard: trap unless @p words instruction words starting at
///        @p pc lie within @p func's code (overflow-safe in 64-bit).
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

/// @brief Verifier guard: trap unless branch @p target is a valid code offset
///        in @p func.
bool BytecodeVM::ensureBranchTarget(const BytecodeFunction &func,
                                    uint32_t target,
                                    const char *site) {
    if (target < func.code.size())
        return true;
    trap(TrapKind::InvalidOpcode, (std::string(site) + ": branch target out of range").c_str());
    return false;
}

/// @brief Verifier guard: decode @p instr's stack effect and trap if the
///        operand stack would underflow (too few inputs) or overflow the
///        frame's reserved depth. Run before executing each instruction in
///        untrusted mode so malformed bytecode cannot corrupt the stack.
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
        case BCOpcode::ARR_I32_GET_FAST:
        case BCOpcode::ARR_I64_GET_FAST:
        case BCOpcode::ARR_F64_GET_FAST:
            required = 2;
            delta = -1;
            break;
        case BCOpcode::IDX_CHK:
            required = 3;
            delta = -2;
            break;
        case BCOpcode::ARR_I32_SET_FAST:
        case BCOpcode::ARR_I64_SET_FAST:
        case BCOpcode::ARR_F64_SET_FAST:
            required = 3;
            delta = -3;
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
        case BCOpcode::LOAD_GLOBAL_ADDR:
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
              frame.func->name + " @pc=" + std::to_string(frame.pc) +
              " depth=" + std::to_string(depth) + " required=" + std::to_string(required) +
              " max=" + std::to_string(frame.func->maxStack))
                 .c_str());
        return false;
    }

    if (delta > 0 && depth + static_cast<uint32_t>(delta) > frame.func->maxStack) {
        trap(TrapKind::StackOverflow,
             (std::string(site) + ": operand stack overflow at " + opcodeName(op) + " in " +
              frame.func->name + " @pc=" + std::to_string(frame.pc) +
              " depth=" + std::to_string(depth) + " delta=" + std::to_string(delta) +
              " max=" + std::to_string(frame.func->maxStack))
                 .c_str());
        return false;
    }

    return true;
}

/// @brief Verifier guard: trap unless the operand-stack depth at a call site
///        exactly equals the callee's declared parameter count.
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

/// @brief Verifier guard: validate the callee's frame metadata (locals ≥
///        params) and that pushing its frame won't exceed stack limits,
///        before a new BCFrame is created.
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

/// @brief Execute the named function with @p args; traps if no module is
///        loaded or the name is unknown. Convenience over the pointer overload.
/// @return The function's return slot (default-constructed on trap).
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

/// @brief Execute @p func as a fresh top-level invocation.
/// @details Validates module/function/arity, installs the thread-local
///          active-VM and active-module context (so Thread.Start and other
///          runtime handlers can re-enter bytecode), resets execution state,
///          pushes @p args as the entry frame's initial locals, runs to
///          completion, and returns the result slot (default on trap).
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
    pendingTrapErrorCode_ = false;
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

    // Return result. Void functions do not leave a meaningful result slot.
    if (state_ == VMState::Halted && !func->hasReturn) {
        return BCSlot{};
    }
    if (state_ == VMState::Halted && sp_ > valueStack_.data()) {
        return *(sp_ - 1);
    }
    return BCSlot{};
}

/// @brief Portable interpreter loop: the `switch`-based fallback executed when
///        threaded dispatch is unavailable or disabled.
/// @details Fetches/decodes/executes one instruction per iteration until the
///          VM leaves the Running state (Halted on return from the entry
///          frame, or Trapped). @ref runThreaded is the faster path with
///          identical semantics.
void BytecodeVM::run() {
    state_ = VMState::Running;

    while (state_ == VMState::Running) {
        if (!fp_ || !fp_->func)
            return;

        if (!trustedDispatch_) {
            if (!ensurePcInRange(*fp_->func, fp_->pc, "BytecodeVM::run(fetch)"))
                return;
            if (!ensureStackForInstruction(*fp_, sp_, fp_->func->code[fp_->pc], "BytecodeVM::run"))
                return;
        }

        // Fetch instruction
        uint32_t instr = fp_->func->code[fp_->pc++];
        BCOpcode op = decodeOpcode(instr);

        ++instrCount_;
        if (maxInstrCount_ != 0 && instrCount_ > maxInstrCount_) {
            trap(TrapKind::Interrupt, "VM: step limit exceeded");
            continue;
        }

        // Trap unknown (non-enumerator) opcode bytes here so the dispatch switch
        // below can omit a `default:` and let -Wswitch enforce that every defined
        // opcode has an explicit handler (compile-time completeness).
        if (!isKnownOpcode(static_cast<uint8_t>(op))) {
            trap(TrapKind::InvalidOpcode, "Unknown opcode");
            continue;
        }

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
                if (!ensureLocalIndex(idx, "BytecodeVM::LOAD_LOCAL"))
                    break;
                pushLocal(idx);
                break;
            }

            case BCOpcode::STORE_LOCAL: {
                uint8_t idx = decodeArg8_0(instr);
                if (!ensureLocalIndex(idx, "BytecodeVM::STORE_LOCAL"))
                    break;
                storeLocal(idx);
                break;
            }

            case BCOpcode::LOAD_LOCAL_W: {
                uint16_t idx = decodeArg16(instr);
                if (!ensureLocalIndex(idx, "BytecodeVM::LOAD_LOCAL_W"))
                    break;
                pushLocal(idx);
                break;
            }

            case BCOpcode::STORE_LOCAL_W: {
                uint16_t idx = decodeArg16(instr);
                if (!ensureLocalIndex(idx, "BytecodeVM::STORE_LOCAL_W"))
                    break;
                storeLocal(idx);
                break;
            }

            case BCOpcode::INC_LOCAL: {
                uint8_t idx = decodeArg8_0(instr);
                if (!ensureLocalIndex(idx, "BytecodeVM::INC_LOCAL"))
                    break;
                fp_->locals[idx].i64 = wrappingAdd(fp_->locals[idx].i64, 1);
                break;
            }

            case BCOpcode::DEC_LOCAL: {
                uint8_t idx = decodeArg8_0(instr);
                if (!ensureLocalIndex(idx, "BytecodeVM::DEC_LOCAL"))
                    break;
                fp_->locals[idx].i64 = wrappingSub(fp_->locals[idx].i64, 1);
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
                sp_[-2].i64 = wrappingAdd(sp_[-2].i64, sp_[-1].i64);
                sp_--;
                break;

            case BCOpcode::SUB_I64:
                sp_[-2].i64 = wrappingSub(sp_[-2].i64, sp_[-1].i64);
                sp_--;
                break;

            case BCOpcode::MUL_I64:
                sp_[-2].i64 = wrappingMul(sp_[-2].i64, sp_[-1].i64);
                sp_--;
                break;

            case BCOpcode::SDIV_I64: {
                int64_t result = 0;
                TrapKind fault = TrapKind::None;
                if (!safeSignedDiv(sp_[-2].i64, sp_[-1].i64, result, fault)) {
                    if (!dispatchTrap(fault)) {
                        trap(fault,
                             fault == TrapKind::DivideByZero
                                 ? "division by zero"
                                 : "Overflow: integer division overflow");
                    }
                    break;
                }
                sp_[-2].i64 = result;
                sp_--;
            } break;

            case BCOpcode::UDIV_I64: {
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
            } break;

            case BCOpcode::SREM_I64: {
                int64_t result = 0;
                TrapKind fault = TrapKind::None;
                if (!safeSignedRem(sp_[-2].i64, sp_[-1].i64, result, fault)) {
                    if (!dispatchTrap(fault)) {
                        trap(fault,
                             fault == TrapKind::DivideByZero
                                 ? "division by zero"
                                 : "Overflow: integer remainder overflow");
                    }
                    break;
                }
                sp_[-2].i64 = result;
                sp_--;
            } break;

            case BCOpcode::UREM_I64: {
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
            } break;

            case BCOpcode::NEG_I64: {
                int64_t result = 0;
                TrapKind fault = TrapKind::None;
                if (!safeNegate(sp_[-1].i64, result, fault)) {
                    if (!dispatchTrap(fault)) {
                        trap(fault, "Overflow: integer negation overflow");
                    }
                    break;
                }
                sp_[-1].i64 = result;
            } break;

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

            case BCOpcode::SDIV_I64_CHK: {
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
            } break;

            case BCOpcode::UDIV_I64_CHK: {
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
            } break;

            case BCOpcode::SREM_I64_CHK: {
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
            } break;

            case BCOpcode::UREM_I64_CHK: {
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
            } break;

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
                sp_[-2].i64 = wrappingShl(sp_[-2].i64, sp_[-1].i64);
                sp_--;
                break;

            case BCOpcode::LSHR_I64:
                sp_[-2].i64 =
                    static_cast<int64_t>(static_cast<uint64_t>(sp_[-2].i64) >> (sp_[-1].i64 & 63));
                sp_--;
                break;

            case BCOpcode::ASHR_I64:
                sp_[-2].i64 = arithmeticShr(sp_[-2].i64, sp_[-1].i64);
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

            case BCOpcode::F64_TO_I64: {
                int64_t converted = 0;
                TrapKind fault = TrapKind::None;
                if (!truncF64ToI64(sp_[-1].f64, converted, fault)) {
                    trapOrDispatch(fault,
                                   fault == TrapKind::InvalidCast
                                       ? "InvalidCast: invalid float to int conversion"
                                       : "Overflow: float to int conversion overflow");
                    break;
                }
                sp_[-1].i64 = converted;
            } break;

            case BCOpcode::F64_TO_I64_CHK: {
                // Float to signed int64 with overflow check and round-to-even
                int64_t converted = 0;
                TrapKind fault = TrapKind::None;
                if (!roundF64ToI64(sp_[-1].f64, converted, fault)) {
                    trapOrDispatch(fault,
                                   fault == TrapKind::InvalidCast
                                       ? "InvalidCast: invalid float to int conversion"
                                       : "Overflow: float to int conversion overflow");
                    break;
                }
                sp_[-1].i64 = converted;
                break;
            }

            case BCOpcode::F64_TO_U64_CHK: {
                // Float to unsigned int64 with overflow check and round-to-even
                int64_t converted = 0;
                TrapKind fault = TrapKind::None;
                if (!roundF64ToU64Bits(sp_[-1].f64, converted, fault)) {
                    trapOrDispatch(fault,
                                   fault == TrapKind::InvalidCast
                                       ? "InvalidCast: invalid float to uint conversion"
                                       : "Overflow: float to uint conversion overflow");
                    break;
                }
                sp_[-1].i64 = converted;
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

                dismissConsumedStringArgs(ref, args, argCount);
                releaseCallArgs(args, argCount);

                // Pop arguments
                sp_ -= argCount;

                // Push result if function returns a value
                if (ref.hasReturn) {
                    *sp_++ = result;
                    if (ref.returnsString && result.ptr) {
                        setSlotOwnsString(sp_ - 1, true);
                    } else {
                        setSlotOwnsString(sp_ - 1, false);
                    }
                }
                break;
            }

            case BCOpcode::ARR_I32_GET_FAST: {
                BCSlot *idxSlot = --sp_;
                const size_t idx = static_cast<size_t>(idxSlot->i64);
                setSlotOwnsString(idxSlot, false);
                BCSlot *arrSlot = sp_ - 1;
                auto *arr = static_cast<int32_t *>(arrSlot->ptr);
                arrSlot->i64 = static_cast<int64_t>(arr[idx]);
                setSlotOwnsString(arrSlot, false);
                break;
            }

            case BCOpcode::ARR_I32_SET_FAST: {
                BCSlot *valueSlot = --sp_;
                const int32_t value = static_cast<int32_t>(valueSlot->i64);
                setSlotOwnsString(valueSlot, false);
                BCSlot *idxSlot = --sp_;
                const size_t idx = static_cast<size_t>(idxSlot->i64);
                setSlotOwnsString(idxSlot, false);
                BCSlot *arrSlot = --sp_;
                auto *arr = static_cast<int32_t *>(arrSlot->ptr);
                setSlotOwnsString(arrSlot, false);
                arr[idx] = value;
                break;
            }

            case BCOpcode::ARR_I64_GET_FAST: {
                BCSlot *idxSlot = --sp_;
                const size_t idx = static_cast<size_t>(idxSlot->i64);
                setSlotOwnsString(idxSlot, false);
                BCSlot *arrSlot = sp_ - 1;
                auto *arr = static_cast<int64_t *>(arrSlot->ptr);
                arrSlot->i64 = arr[idx];
                setSlotOwnsString(arrSlot, false);
                break;
            }

            case BCOpcode::ARR_I64_SET_FAST: {
                BCSlot *valueSlot = --sp_;
                const int64_t value = valueSlot->i64;
                setSlotOwnsString(valueSlot, false);
                BCSlot *idxSlot = --sp_;
                const size_t idx = static_cast<size_t>(idxSlot->i64);
                setSlotOwnsString(idxSlot, false);
                BCSlot *arrSlot = --sp_;
                auto *arr = static_cast<int64_t *>(arrSlot->ptr);
                setSlotOwnsString(arrSlot, false);
                arr[idx] = value;
                break;
            }

            case BCOpcode::ARR_F64_GET_FAST: {
                BCSlot *idxSlot = --sp_;
                const size_t idx = static_cast<size_t>(idxSlot->i64);
                setSlotOwnsString(idxSlot, false);
                BCSlot *arrSlot = sp_ - 1;
                auto *arr = static_cast<double *>(arrSlot->ptr);
                arrSlot->f64 = arr[idx];
                setSlotOwnsString(arrSlot, false);
                break;
            }

            case BCOpcode::ARR_F64_SET_FAST: {
                BCSlot *valueSlot = --sp_;
                const double value = valueSlot->f64;
                setSlotOwnsString(valueSlot, false);
                BCSlot *idxSlot = --sp_;
                const size_t idx = static_cast<size_t>(idxSlot->i64);
                setSlotOwnsString(idxSlot, false);
                BCSlot *arrSlot = --sp_;
                auto *arr = static_cast<double *>(arrSlot->ptr);
                setSlotOwnsString(arrSlot, false);
                arr[idx] = value;
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
                const int64_t size = (--sp_)->i64;
                void *ptr = nullptr;
                if (!allocateAlloca(size, ptr, "BytecodeVM::ALLOCA"))
                    break;
                sp_->ptr = ptr;
                setSlotOwnsString(sp_, false);
                sp_++;
                break;
            }

            case BCOpcode::GEP: {
                int64_t offset = (--sp_)->i64;
                uint8_t *ptr = static_cast<uint8_t *>(sp_[-1].ptr);
                if (!ptr) {
                    if (offset != 0)
                        trapOrDispatch(TrapKind::NullPointer, "GEP on null pointer");
                    sp_[-1].ptr = nullptr;
                    break;
                }
                sp_[-1].ptr = ptr + offset;
                break;
            }

            case BCOpcode::LOAD_I64_MEM: {
                void *ptr = sp_[-1].ptr;
                if (!ensureMemoryAddress(ptr, "BytecodeVM::LOAD_I64_MEM"))
                    break;
                int64_t val;
                std::memcpy(&val, ptr, sizeof(val));
                sp_[-1].i64 = val;
                break;
            }

            case BCOpcode::STORE_I64_MEM: {
                int64_t val = (--sp_)->i64;
                void *ptr = (--sp_)->ptr;
                if (!ensureMemoryAddress(ptr, "BytecodeVM::STORE_I64_MEM"))
                    break;
                clearGlobalStringOwnershipForRawStore(ptr);
                std::memcpy(ptr, &val, sizeof(val));
                break;
            }

            case BCOpcode::LOAD_I8_MEM: {
                void *ptr = sp_[-1].ptr;
                if (!ensureMemoryAddress(ptr, "BytecodeVM::LOAD_I8_MEM"))
                    break;
                int8_t val;
                std::memcpy(&val, ptr, sizeof(val));
                sp_[-1].i64 = val; // Sign extend
                break;
            }

            case BCOpcode::LOAD_I16_MEM: {
                void *ptr = sp_[-1].ptr;
                if (!ensureMemoryAddress(ptr, "BytecodeVM::LOAD_I16_MEM"))
                    break;
                int16_t val;
                std::memcpy(&val, ptr, sizeof(val));
                sp_[-1].i64 = val; // Sign extend
                break;
            }

            case BCOpcode::LOAD_I32_MEM: {
                void *ptr = sp_[-1].ptr;
                if (!ensureMemoryAddress(ptr, "BytecodeVM::LOAD_I32_MEM"))
                    break;
                int32_t val;
                std::memcpy(&val, ptr, sizeof(val));
                sp_[-1].i64 = val; // Sign extend
                break;
            }

            case BCOpcode::LOAD_F64_MEM: {
                void *ptr = sp_[-1].ptr;
                if (!ensureMemoryAddress(ptr, "BytecodeVM::LOAD_F64_MEM"))
                    break;
                double val;
                std::memcpy(&val, ptr, sizeof(val));
                sp_[-1].f64 = val;
                break;
            }

            case BCOpcode::LOAD_PTR_MEM: {
                void *val;
                if (!ensureMemoryAddress(sp_[-1].ptr, "BytecodeVM::LOAD_PTR_MEM"))
                    break;
                std::memcpy(&val, sp_[-1].ptr, sizeof(val));
                sp_[-1].ptr = val;
                setSlotOwnsString(sp_ - 1, false);
                break;
            }

            case BCOpcode::LOAD_STR_MEM: {
                rt_string val = nullptr;
                if (!ensureMemoryAddress(sp_[-1].ptr, "BytecodeVM::LOAD_STR_MEM"))
                    break;
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
                if (!ensureMemoryAddress(ptr, "BytecodeVM::STORE_I8_MEM"))
                    break;
                clearGlobalStringOwnershipForRawStore(ptr);
                std::memcpy(ptr, &val, sizeof(val));
                break;
            }

            case BCOpcode::STORE_I16_MEM: {
                int16_t val = static_cast<int16_t>((--sp_)->i64);
                void *ptr = (--sp_)->ptr;
                if (!ensureMemoryAddress(ptr, "BytecodeVM::STORE_I16_MEM"))
                    break;
                clearGlobalStringOwnershipForRawStore(ptr);
                std::memcpy(ptr, &val, sizeof(val));
                break;
            }

            case BCOpcode::STORE_I32_MEM: {
                int32_t val = static_cast<int32_t>((--sp_)->i64);
                void *ptr = (--sp_)->ptr;
                if (!ensureMemoryAddress(ptr, "BytecodeVM::STORE_I32_MEM"))
                    break;
                clearGlobalStringOwnershipForRawStore(ptr);
                std::memcpy(ptr, &val, sizeof(val));
                break;
            }

            case BCOpcode::STORE_F64_MEM: {
                double val = (--sp_)->f64;
                void *ptr = (--sp_)->ptr;
                if (!ensureMemoryAddress(ptr, "BytecodeVM::STORE_F64_MEM"))
                    break;
                clearGlobalStringOwnershipForRawStore(ptr);
                std::memcpy(ptr, &val, sizeof(val));
                break;
            }

            case BCOpcode::STORE_PTR_MEM: {
                void *val = (--sp_)->ptr;
                void *ptr = (--sp_)->ptr;
                if (!ensureMemoryAddress(ptr, "BytecodeVM::STORE_PTR_MEM"))
                    break;
                clearGlobalStringOwnershipForRawStore(ptr);
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
                if (!ensureMemoryAddress(ptr, "BytecodeVM::STORE_STR_MEM"))
                    break;
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
                setGlobalStringOwnershipForAddress(ptr, incoming != nullptr);
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

            case BCOpcode::LOAD_GLOBAL_ADDR: {
                uint16_t idx = decodeArg16(instr);
                if (idx >= globals_.size()) {
                    trap(TrapKind::InvalidOpcode, "LOAD_GLOBAL_ADDR index out of range");
                    break;
                }
                sp_->ptr = &globals_[idx];
                setSlotOwnsString(sp_, false);
                sp_++;
                break;
            }

            //==================================================================
            // String Operations
            //==================================================================
            case BCOpcode::LOAD_STR: {
                uint16_t idx = decodeArg16(instr);
                sp_->ptr = getStringLiteral(idx);
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
                // Pop legacy error code from stack, map it to a structured
                // trap kind, and preserve the original code for diagnostics.
                int64_t code = (--sp_)->i64;
                const il::vm::TrapKind vmTrapKind =
                    il::vm::map_err_to_trap(static_cast<int32_t>(code));
                TrapKind trapKind = static_cast<TrapKind>(static_cast<int32_t>(vmTrapKind));
                if (!dispatchTrap(trapKind, static_cast<int32_t>(code))) {
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
            // Opcodes with no run()-switch handler — trap. (Same set the
            // threaded VM routes to L_DEFAULT via BC_OPCODE_TRAP.) There is no
            // `default:`: with the unknown-byte guard above, the switch covers
            // every BCOpcode enumerator, so -Wswitch -Werror forces any newly
            // added opcode to be handled here too.
            //==================================================================
            case BCOpcode::TAIL_CALL:
            case BCOpcode::MAKE_ERROR:
            case BCOpcode::LINE:
            case BCOpcode::BREAKPOINT:
            case BCOpcode::WATCH_VAR:
            case BCOpcode::OPCODE_COUNT:
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
    if (!(pendingTrapErrorCode_ && trapKind_ == kind))
        currentErrorCode_ = defaultBytecodeTrapErrorCode(kind);
    pendingTrapErrorCode_ = false;
    trapKind_ = kind;
    trapMessage_ = formatTrapMessage(kind, currentErrorCode_, message);
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

/// @brief Signed 64-bit division with trap detection.
/// @return true and sets @p result; false and sets @p fault to DivideByZero
///         (b==0) or Overflow (INT64_MIN / -1). Backs SDIV_I64[_CHK].
bool BytecodeVM::safeSignedDiv(int64_t a, int64_t b, int64_t &result, TrapKind &fault) const {
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

/// @brief Unsigned 64-bit division (operands reinterpreted as uint64).
/// @return false with @p fault = DivideByZero when b==0, else true.
bool BytecodeVM::safeUnsignedDiv(int64_t a, int64_t b, int64_t &result, TrapKind &fault) const {
    if (b == 0) {
        fault = TrapKind::DivideByZero;
        return false;
    }
    result = static_cast<int64_t>(static_cast<uint64_t>(a) / static_cast<uint64_t>(b));
    return true;
}

/// @brief Signed 64-bit remainder; traps DivideByZero on b==0 and defines the
///        INT64_MIN % -1 corner as 0 (where the hardware op would fault).
bool BytecodeVM::safeSignedRem(int64_t a, int64_t b, int64_t &result, TrapKind &fault) const {
    if (b == 0) {
        fault = TrapKind::DivideByZero;
        return false;
    }
    if (a == std::numeric_limits<int64_t>::min() && b == -1) {
        result = 0;
        return true;
    }
    result = a % b;
    return true;
}

/// @brief Unsigned 64-bit remainder; traps DivideByZero on b==0.
bool BytecodeVM::safeUnsignedRem(int64_t a, int64_t b, int64_t &result, TrapKind &fault) const {
    if (b == 0) {
        fault = TrapKind::DivideByZero;
        return false;
    }
    result = static_cast<int64_t>(static_cast<uint64_t>(a) % static_cast<uint64_t>(b));
    return true;
}

/// @brief Checked signed negation; traps Overflow on -INT64_MIN.
bool BytecodeVM::safeNegate(int64_t value, int64_t &result, TrapKind &fault) const {
    if (value == std::numeric_limits<int64_t>::min()) {
        fault = TrapKind::Overflow;
        return false;
    }
    result = -value;
    return true;
}

/// @brief Two's-complement wrapping add (no UB): computes in uint64 and
///        bit-copies back. Backs the non-checked ADD_I64.
int64_t BytecodeVM::wrappingAdd(int64_t a, int64_t b) noexcept {
    const uint64_t result = static_cast<uint64_t>(a) + static_cast<uint64_t>(b);
    int64_t out = 0;
    std::memcpy(&out, &result, sizeof(out));
    return out;
}

/// @brief Two's-complement wrapping subtract (no UB).
int64_t BytecodeVM::wrappingSub(int64_t a, int64_t b) noexcept {
    const uint64_t result = static_cast<uint64_t>(a) - static_cast<uint64_t>(b);
    int64_t out = 0;
    std::memcpy(&out, &result, sizeof(out));
    return out;
}

/// @brief Two's-complement wrapping multiply (no UB).
int64_t BytecodeVM::wrappingMul(int64_t a, int64_t b) noexcept {
    const uint64_t result = static_cast<uint64_t>(a) * static_cast<uint64_t>(b);
    int64_t out = 0;
    std::memcpy(&out, &result, sizeof(out));
    return out;
}

/// @brief Logical left shift with the shift amount masked to 0–63 (defined
///        for any shift; no UB from over-shift).
int64_t BytecodeVM::wrappingShl(int64_t value, int64_t shift) noexcept {
    const uint64_t result = static_cast<uint64_t>(value) << (static_cast<uint64_t>(shift) & 63u);
    int64_t out = 0;
    std::memcpy(&out, &result, sizeof(out));
    return out;
}

/// @brief Arithmetic (sign-extending) right shift, amount masked to 0–63 and
///        sign replication done manually so behavior is portable/defined.
int64_t BytecodeVM::arithmeticShr(int64_t value, int64_t shift) noexcept {
    const uint32_t amount = static_cast<uint32_t>(static_cast<uint64_t>(shift) & 63u);
    if (amount == 0)
        return value;
    uint64_t bits = static_cast<uint64_t>(value);
    if (value < 0)
        bits = (bits >> amount) | (~uint64_t{0} << (64u - amount));
    else
        bits >>= amount;
    int64_t out = 0;
    std::memcpy(&out, &bits, sizeof(out));
    return out;
}

/// @brief Convert a double to i64 by truncation toward zero (F64_TO_I64_CHK).
/// @return false with @p fault = InvalidCast (NaN/Inf) or Overflow (outside
///         [-2^63, 2^63)); otherwise true.
bool BytecodeVM::truncF64ToI64(double value, int64_t &result, TrapKind &fault) noexcept {
    fault = TrapKind::None;
    if (!std::isfinite(value)) {
        fault = TrapKind::InvalidCast;
        return false;
    }
    const double lower = -std::ldexp(1.0, 63);
    const double upper = std::ldexp(1.0, 63);
    if (value < lower || value >= upper) {
        fault = TrapKind::Overflow;
        return false;
    }
    result = static_cast<int64_t>(value);
    return true;
}

/// @brief Convert a double to i64 with round-to-nearest (std::rint), same
///        InvalidCast/Overflow trapping as @ref truncF64ToI64.
bool BytecodeVM::roundF64ToI64(double value, int64_t &result, TrapKind &fault) noexcept {
    fault = TrapKind::None;
    if (!std::isfinite(value)) {
        fault = TrapKind::InvalidCast;
        return false;
    }
    const double rounded = std::rint(value);
    const double lower = -std::ldexp(1.0, 63);
    const double upper = std::ldexp(1.0, 63);
    if (rounded < lower || rounded >= upper) {
        fault = TrapKind::Overflow;
        return false;
    }
    result = static_cast<int64_t>(rounded);
    return true;
}

/// @brief Convert a double to u64 (round-to-nearest), returning the bit
///        pattern in @p result. Traps InvalidCast on NaN/Inf or a negative
///        value, Overflow at/above 2^64 (F64_TO_U64_CHK).
bool BytecodeVM::roundF64ToU64Bits(double value, int64_t &result, TrapKind &fault) noexcept {
    fault = TrapKind::None;
    if (!std::isfinite(value)) {
        fault = TrapKind::InvalidCast;
        return false;
    }
    const double rounded = std::rint(value);
    const double upper = std::ldexp(1.0, 64);
    if (rounded < 0.0) {
        fault = TrapKind::InvalidCast;
        return false;
    }
    if (rounded >= upper) {
        fault = TrapKind::Overflow;
        return false;
    }
    const uint64_t unsignedResult = static_cast<uint64_t>(rounded);
    std::memcpy(&result, &unsignedResult, sizeof(result));
    return true;
}

/// @brief Raise a trap, preferring an in-bytecode handler: try dispatchTrap
///        (transfer to an EH handler block) and, if none catches it, fall
///        back to a hard @ref trap that unwinds the VM.
/// @return true if a handler took over (execution continues), else false.
bool BytecodeVM::trapOrDispatch(TrapKind kind, const char *message, int32_t errorCode) {
    if (dispatchTrap(kind, errorCode, message))
        return true;
    trap(kind, message);
    return false;
}

/// @brief Bounds-check a local-slot index against the current frame; on
///        failure raises InvalidOpcode via @ref trapOrDispatch.
bool BytecodeVM::ensureLocalIndex(uint32_t idx, const char *site) {
    if (fp_ && fp_->func && idx < fp_->func->numLocals)
        return true;
    trapOrDispatch(TrapKind::InvalidOpcode,
                   (std::string(site) + ": local index out of range").c_str());
    return false;
}

/// @brief Reject a null or low (< first 4 KiB page) pointer before a
///        load/store; raises NullPointer via @ref trapOrDispatch on failure.
bool BytecodeVM::ensureMemoryAddress(const void *ptr, const char *site) {
    if (ptr && reinterpret_cast<uintptr_t>(ptr) >= 4096)
        return true;
    trapOrDispatch(TrapKind::NullPointer,
                   (std::string(site) + ": null or invalid memory address").c_str());
    return false;
}

/// @brief Bump-allocate @p requestedSize bytes (8-byte aligned) from the
///        function-lifetime alloca arena, returning the block in @p ptr.
/// @details Traps DomainError on a negative size and StackOverflow on size
///          overflow or arena exhaustion (16 MiB cap). The arena was reserved
///          at construction so it never reallocates — see the ctor note —
///          keeping previously handed-out alloca pointers valid.
bool BytecodeVM::allocateAlloca(int64_t requestedSize, void *&ptr, const char *site) {
    ptr = nullptr;
    if (requestedSize < 0) {
        trapOrDispatch(TrapKind::DomainError,
                       (std::string(site) + ": negative alloca size").c_str());
        return false;
    }

    constexpr size_t kMaxAllocaBytes = 16u * 1024u * 1024u;
    const uint64_t rawSize = static_cast<uint64_t>(requestedSize);
    if (rawSize > static_cast<uint64_t>(std::numeric_limits<size_t>::max() - 7u)) {
        trapOrDispatch(TrapKind::StackOverflow,
                       (std::string(site) + ": alloca size overflow").c_str());
        return false;
    }

    const size_t alignedSize = (static_cast<size_t>(rawSize) + 7u) & ~size_t{7u};
    if (alignedSize > kMaxAllocaBytes || allocaTop_ > kMaxAllocaBytes - alignedSize) {
        trapOrDispatch(TrapKind::StackOverflow, "alloca stack overflow");
        return false;
    }

    const size_t needed = allocaTop_ + alignedSize;
    if (needed > allocaBuffer_.size()) {
        size_t newSize = allocaBuffer_.empty() ? 64u * 1024u : allocaBuffer_.size();
        while (newSize < needed && newSize < kMaxAllocaBytes)
            newSize = std::min(newSize * 2u, kMaxAllocaBytes);
        if (newSize < needed) {
            trapOrDispatch(TrapKind::StackOverflow, "alloca stack overflow");
            return false;
        }
        allocaBuffer_.resize(newSize);
    }

    ptr = allocaBuffer_.data() + allocaTop_;
    std::memset(ptr, 0, alignedSize);
    allocaTop_ = needed;
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

/// @brief PC of the faulting instruction (pc-1, since the fetch already
///        advanced past it); 0 if there is no frame.
uint32_t BytecodeVM::currentFaultPc() const {
    return (fp_ && fp_->pc > 0) ? (fp_->pc - 1) : 0;
}

/// @brief Block label associated with @p pc via the function's label table
///        (empty if unavailable) — used in trap diagnostics.
std::string BytecodeVM::currentBlockLabelForPc(const BytecodeFunction *func, uint32_t pc) const {
    if (!func || pc >= func->blockLabelTable.size())
        return {};
    return func->blockLabelTable[pc];
}

/// @brief Resolve the source file path for @p pc from the function's
///        source-file table (empty if unknown) — used in trap diagnostics.
std::string BytecodeVM::currentSourcePathForPc(const BytecodeFunction *func, uint32_t pc) const {
    if (!module_ || !func)
        return {};

    uint32_t sourceFileEntry = 0;
    if (pc < func->sourceFileTable.size())
        sourceFileEntry = func->sourceFileTable[pc];

    if (sourceFileEntry != 0) {
        const uint32_t sourceIndex = sourceFileEntry - 1;
        if (sourceIndex < module_->sourceFiles.size())
            return module_->sourceFiles[sourceIndex].path;
    }

    if (func->sourceFileIdx < module_->sourceFiles.size())
        return module_->sourceFiles[func->sourceFileIdx].path;

    return {};
}

/// @brief Build the human-readable trap report string: trap kind name,
///        optional error code and message, and the faulting function /
///        block / source location resolved from the current frame.
std::string BytecodeVM::formatTrapMessage(TrapKind kind,
                                          int32_t errorCode,
                                          const char *message) const {
    std::ostringstream out;
    out << "Trap";

    if (fp_ && fp_->func) {
        const BytecodeFunction *func = fp_->func;
        const uint32_t pc = currentFaultPc();
        out << " @" << func->name;

        const std::string blockLabel = currentBlockLabelForPc(func, pc);
        if (!blockLabel.empty())
            out << ':' << blockLabel;

        out << '#' << pc;

        const std::string sourcePath = currentSourcePathForPc(func, pc);
        const uint32_t sourceLine = getSourceLine(func, pc);
        if (!sourcePath.empty() || sourceLine != 0) {
            out << " (";
            if (!sourcePath.empty())
                out << sourcePath;
            if (sourceLine != 0) {
                if (!sourcePath.empty())
                    out << ':';
                out << sourceLine;
            }
            out << ')';
        }
    }

    out << ": " << bytecodeTrapKindName(kind) << " (code=" << errorCode << ')';
    if (message && *message)
        out << ": " << message;
    return out.str();
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
    currentErrorCode_ = errorCode >= 0 ? errorCode : defaultBytecodeTrapErrorCode(kind);
    pendingTrapErrorCode_ = true;
    if (message)
        trapMessage_ = formatTrapMessage(kind, currentErrorCode_, message);

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
        const uint32_t line =
            (fp_ && fp_->func) ? getSourceLine(fp_->func, trapRecord_.faultPc) : 0;
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
            currentErrorCode_ = errorCode >= 0 ? errorCode : defaultBytecodeTrapErrorCode(kind);
            pendingTrapErrorCode_ = false;

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

/// @brief Implement the RESUME_* opcodes: validate the resume token against
///        the saved trap record, restore the captured value stack / call /
///        EH state, and continue at the faulting instruction (@p useNextPc
///        false) or the one after it (RESUME_NEXT).
/// @return false if the token does not match a valid trap record (the caller
///         then treats it as a hard error).
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
    std::copy(
        trapRecord_.allocaBytes.begin(), trapRecord_.allocaBytes.end(), allocaBuffer_.begin());
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
    bool ownsArg;
    BytecodeVM::ExecutionEnvironment environment;
};

/// Payload for bytecode-backed Async.Run.
struct BytecodeAsyncPayload {
    const BytecodeModule *module;
    const BytecodeFunction *entry;
    void *arg;
    bool ownsArg;
    BytecodeVM::ExecutionEnvironment environment;
    void *promise;
};

/// @brief Drop a worker thread/async payload's owned argument object (no-op
///        if not owned or null).
static void releaseWorkerArg(void *arg, bool ownsArg) {
    if (!ownsArg || !arg)
        return;
    if (rt_obj_release_check0(arg))
        rt_obj_free(arg);
}

/// @brief Settle an Async.Run promise with a successful @p result.
/// @details If the worker returned its own owned argument object, ownership is
///          transferred to the promise (no double free); otherwise the owned
///          argument is released and the result is set as owned.
static void completeAsyncPromiseWithResult(void *promise,
                                           void *result,
                                           void **ownedArg,
                                           bool *ownsArg) {
    if (ownedArg && ownsArg && *ownsArg) {
        if (result == *ownedArg) {
            *ownedArg = nullptr;
            *ownsArg = false;
            rt_promise_set_transferred(promise, result);
            return;
        }
        releaseWorkerArg(*ownedArg, true);
        *ownedArg = nullptr;
        *ownsArg = false;
    }
    rt_promise_set_owned(promise, result);
}

/// @brief Settle an Async.Run promise with an @p error, first releasing any
///        owned worker argument so a failed task cannot leak it.
static void completeAsyncPromiseWithError(void *promise,
                                          rt_string error,
                                          void **ownedArg,
                                          bool *ownsArg) {
    if (ownedArg && ownsArg && *ownsArg) {
        releaseWorkerArg(*ownedArg, true);
        *ownedArg = nullptr;
        *ownsArg = false;
    }
    rt_promise_set_error(promise, error);
}

/// @brief Run a spawned bytecode thread's entry function on a fresh worker VM.
/// @details Builds a worker BytecodeVM, mirrors the parent's execution
///          environment, installs the active-VM/module thread context, and
///          invokes @c payload->entry with its argument. On failure writes a
///          message into @p errorBuf.
/// @return true on clean completion, false on error (message in @p errorBuf).
static bool runBytecodeThreadPayload(BytecodeThreadPayload *payload,
                                     char *errorBuf,
                                     size_t errorBufSize) {
    if (errorBuf && errorBufSize > 0)
        errorBuf[0] = '\0';
    if (!payload || !payload->module || !payload->entry) {
        if (errorBuf && errorBufSize > 0)
            std::snprintf(errorBuf, errorBufSize, "%s", "Thread.StartSafe: invalid bytecode entry");
        if (payload)
            releaseWorkerArg(payload->arg, payload->ownsArg);
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
        releaseWorkerArg(payload->arg, payload->ownsArg);
        delete payload;
        return false;
    }

    releaseWorkerArg(payload->arg, payload->ownsArg);
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
    bool ownsArg = false;
};

struct VmAsyncRunPayload {
    const il::core::Module *module = nullptr;
    std::shared_ptr<il::vm::VM::ProgramState> program;
    il::vm::ExternRegistry *externRegistry = nullptr;
    const il::core::Function *entry = nullptr;
    void *arg = nullptr;
    bool ownsArg = false;
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
        if (payload) {
            releaseWorkerArg(payload->arg, payload->ownsArg);
            il::vm::releaseExternRegistry(payload->externRegistry);
        }
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
    } catch (const il::vm::RuntimeTrapSignal &signal) {
        char error[512];
        const char *message =
            signal.message.empty() ? "Thread.Start: trapped VM worker" : signal.message.c_str();
        std::snprintf(error, sizeof(error), "%s", message);
        releaseWorkerArg(payload->arg, payload->ownsArg);
        il::vm::releaseExternRegistry(payload->externRegistry);
        payload->externRegistry = nullptr;
        delete payload;
        rt_abort(error);
        return;
    } catch (const std::exception &ex) {
        char error[512];
        std::snprintf(error, sizeof(error), "Thread.Start: unhandled exception: %s", ex.what());
        releaseWorkerArg(payload->arg, payload->ownsArg);
        il::vm::releaseExternRegistry(payload->externRegistry);
        payload->externRegistry = nullptr;
        delete payload;
        rt_abort(error);
        return;
    } catch (...) {
        releaseWorkerArg(payload->arg, payload->ownsArg);
        il::vm::releaseExternRegistry(payload->externRegistry);
        payload->externRegistry = nullptr;
        delete payload;
        rt_abort("Thread.Start: unhandled non-standard exception");
        return;
    }
    releaseWorkerArg(payload->arg, payload->ownsArg);
    il::vm::releaseExternRegistry(payload->externRegistry);
    delete payload;
}

/// Standard VM safe thread entry trampoline.
extern "C" void vm_thread_safe_entry_trampoline_bc(void *raw) {
    VmThreadStartPayload *payload = static_cast<VmThreadStartPayload *>(raw);
    if (!payload || !payload->module || !payload->entry) {
        if (payload) {
            releaseWorkerArg(payload->arg, payload->ownsArg);
            il::vm::releaseExternRegistry(payload->externRegistry);
        }
        delete payload;
        rt_trap("Thread.StartSafe: invalid entry");
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
    } catch (const il::vm::RuntimeTrapSignal &signal) {
        char error[512];
        const char *message =
            signal.message.empty() ? "Thread.StartSafe: trapped VM worker" : signal.message.c_str();
        std::snprintf(error, sizeof(error), "%s", message);
        releaseWorkerArg(payload->arg, payload->ownsArg);
        il::vm::releaseExternRegistry(payload->externRegistry);
        payload->externRegistry = nullptr;
        delete payload;
        rt_trap(error);
        return;
    } catch (const std::exception &ex) {
        char error[512];
        std::snprintf(error, sizeof(error), "Thread.StartSafe: unhandled exception: %s", ex.what());
        releaseWorkerArg(payload->arg, payload->ownsArg);
        il::vm::releaseExternRegistry(payload->externRegistry);
        payload->externRegistry = nullptr;
        delete payload;
        rt_trap(error);
        return;
    } catch (...) {
        releaseWorkerArg(payload->arg, payload->ownsArg);
        il::vm::releaseExternRegistry(payload->externRegistry);
        payload->externRegistry = nullptr;
        delete payload;
        rt_trap("Thread.StartSafe: unhandled non-standard exception");
        return;
    }
    releaseWorkerArg(payload->arg, payload->ownsArg);
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

/// @brief Trap unless IL function @p fn matches the Async.Run entry shape
///        (ptr return, single ptr parameter).
static void validateAsyncEntrySignature(const il::core::Function &fn) {
    using Kind = il::core::Type::Kind;
    if (fn.retType.kind != Kind::Ptr)
        rt_trap("Async.Run: invalid entry signature");
    if (fn.params.size() == 1 && fn.params[0].type.kind == Kind::Ptr)
        return;
    rt_trap("Async.Run: invalid entry signature");
}

/// @brief Trap unless IL function @p fn matches the HttpServer.BindHandler
///        entry shape (void return, two ptr parameters).
static void validateHttpHandlerSignature(const il::core::Function &fn) {
    using Kind = il::core::Type::Kind;
    if (fn.retType.kind != Kind::Void || fn.params.size() != 2 ||
        fn.params[0].type.kind != Kind::Ptr || fn.params[1].type.kind != Kind::Ptr) {
        rt_trap("HttpServer.BindHandler: invalid entry signature");
    }
}

/// @brief Trap unless bytecode function @p fn is a valid Thread.Start entry
///        (no return value; zero or one parameter).
static void validateBytecodeThreadEntrySignature(const BytecodeFunction &fn) {
    if (fn.hasReturn)
        rt_trap("Thread.Start: invalid bytecode entry signature");
    if (fn.numParams == 0 || fn.numParams == 1)
        return;
    rt_trap("Thread.Start: invalid bytecode entry signature");
}

/// @brief Trap unless bytecode function @p fn is a valid Async.Run entry
///        (returns a value; exactly one parameter).
static void validateBytecodeAsyncEntrySignature(const BytecodeFunction &fn) {
    if (!fn.hasReturn || fn.numParams != 1)
        rt_trap("Async.Run: invalid bytecode entry signature");
}

/// @brief Trap unless bytecode function @p fn is a valid HttpServer
///        BindHandler entry (no return value; exactly two parameters).
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

        auto *payload = new (std::nothrow) VmThreadStartPayload{
            &module, std::move(program), stdVm->externRegistry(), entryFn, arg, false};
        if (!payload)
            rt_trap("Thread.Start: payload allocation failed");
        il::vm::retainExternRegistry(payload->externRegistry);
        void *thread =
            rt_thread_start(reinterpret_cast<void *>(&vm_thread_entry_trampoline_bc), payload);
        if (!thread) {
            releaseWorkerArg(payload->arg, payload->ownsArg);
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

        auto *payload = new (std::nothrow) BytecodeThreadPayload{
            bcModule, entryFn, arg, false, bcVm->captureExecutionEnvironment()};
        if (!payload)
            rt_trap("Thread.Start: payload allocation failed");
        void *thread =
            rt_thread_start(reinterpret_cast<void *>(&bytecode_thread_entry_trampoline), payload);
        if (!thread) {
            releaseWorkerArg(payload->arg, payload->ownsArg);
            delete payload;
            rt_trap("Thread.Start: failed to create thread");
        }
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

/// Handler for Viper.Threads.Thread.StartOwned - handles both standard VM and BytecodeVM.
static void unified_thread_start_owned_handler(void **args, void *result) {
    void *entry = nullptr;
    void *arg = nullptr;
    if (args && args[0])
        entry = *reinterpret_cast<void **>(args[0]);
    if (args && args[1])
        arg = *reinterpret_cast<void **>(args[1]);

    if (!entry)
        rt_trap("Thread.StartOwned: null entry");

    // Check for standard VM first
    il::vm::VM *stdVm = il::vm::activeVMInstance();
    if (stdVm) {
        std::shared_ptr<il::vm::VM::ProgramState> program = stdVm->programState();
        if (!program)
            rt_trap("Thread.StartOwned: invalid runtime state");

        const il::core::Module &module = stdVm->module();
        const il::core::Function *entryFn = resolveILEntry(module, entry);
        if (!entryFn)
            rt_trap("Thread.StartOwned: invalid entry");
        validateEntrySignature(*entryFn);

        auto *payload = new (std::nothrow) VmThreadStartPayload{
            &module, std::move(program), stdVm->externRegistry(), entryFn, arg, arg != nullptr};
        if (!payload)
            rt_trap("Thread.StartOwned: payload allocation failed");
        if (payload->ownsArg)
            rt_obj_retain_maybe(arg);
        il::vm::retainExternRegistry(payload->externRegistry);
        void *thread =
            rt_thread_start(reinterpret_cast<void *>(&vm_thread_entry_trampoline_bc), payload);
        if (!thread) {
            releaseWorkerArg(payload->arg, payload->ownsArg);
            il::vm::releaseExternRegistry(payload->externRegistry);
            delete payload;
            rt_trap("Thread.StartOwned: failed to create thread");
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
            rt_trap("Thread.StartOwned: invalid bytecode entry");
        validateBytecodeThreadEntrySignature(*entryFn);

        auto *payload = new (std::nothrow) BytecodeThreadPayload{
            bcModule, entryFn, arg, arg != nullptr, bcVm->captureExecutionEnvironment()};
        if (!payload)
            rt_trap("Thread.StartOwned: payload allocation failed");
        if (payload->ownsArg)
            rt_obj_retain_maybe(arg);
        void *thread =
            rt_thread_start(reinterpret_cast<void *>(&bytecode_thread_entry_trampoline), payload);
        if (!thread) {
            releaseWorkerArg(payload->arg, payload->ownsArg);
            delete payload;
            rt_trap("Thread.StartOwned: failed to create thread");
        }
        if (result)
            *reinterpret_cast<void **>(result) = thread;
        return;
    }

    // No VM active - direct call (native code path)
    if (gPriorThreadStartOwnedHandler) {
        gPriorThreadStartOwnedHandler(args, result);
        return;
    }
    void *thread = rt_thread_start_owned(entry, arg);
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

        auto *payload = new (std::nothrow) VmThreadStartPayload{
            &module, std::move(program), stdVm->externRegistry(), entryFn, arg, false};
        if (!payload)
            rt_trap("Thread.StartSafe: payload allocation failed");
        il::vm::retainExternRegistry(payload->externRegistry);
        void *thread = rt_thread_start_safe(
            reinterpret_cast<void *>(&vm_thread_safe_entry_trampoline_bc), payload);
        if (!thread) {
            releaseWorkerArg(payload->arg, payload->ownsArg);
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

        auto *payload = new (std::nothrow) BytecodeThreadPayload{
            bcModule, entryFn, arg, false, bcVm->captureExecutionEnvironment()};
        if (!payload)
            rt_trap("Thread.StartSafe: payload allocation failed");
        void *thread = rt_thread_start_safe(
            reinterpret_cast<void *>(&bytecode_thread_safe_entry_trampoline), payload);
        if (!thread) {
            releaseWorkerArg(payload->arg, payload->ownsArg);
            delete payload;
            rt_trap("Thread.StartSafe: failed to create thread");
        }
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

/// Handler for Viper.Threads.Thread.StartSafeOwned - handles both standard VM and BytecodeVM.
static void unified_thread_start_safe_owned_handler(void **args, void *result) {
    void *entry = nullptr;
    void *arg = nullptr;
    if (args && args[0])
        entry = *reinterpret_cast<void **>(args[0]);
    if (args && args[1])
        arg = *reinterpret_cast<void **>(args[1]);

    if (!entry)
        rt_trap("Thread.StartSafeOwned: null entry");

    // Check for standard VM first
    il::vm::VM *stdVm = il::vm::activeVMInstance();
    if (stdVm) {
        std::shared_ptr<il::vm::VM::ProgramState> program = stdVm->programState();
        if (!program)
            rt_trap("Thread.StartSafeOwned: invalid runtime state");

        const il::core::Module &module = stdVm->module();
        const il::core::Function *entryFn = resolveILEntry(module, entry);
        if (!entryFn)
            rt_trap("Thread.StartSafeOwned: invalid entry");
        validateEntrySignature(*entryFn);

        auto *payload = new (std::nothrow) VmThreadStartPayload{
            &module, std::move(program), stdVm->externRegistry(), entryFn, arg, arg != nullptr};
        if (!payload)
            rt_trap("Thread.StartSafeOwned: payload allocation failed");
        if (payload->ownsArg)
            rt_obj_retain_maybe(arg);
        il::vm::retainExternRegistry(payload->externRegistry);
        void *thread = rt_thread_start_safe(
            reinterpret_cast<void *>(&vm_thread_safe_entry_trampoline_bc), payload);
        if (!thread) {
            releaseWorkerArg(payload->arg, payload->ownsArg);
            il::vm::releaseExternRegistry(payload->externRegistry);
            delete payload;
            rt_trap("Thread.StartSafeOwned: failed to create thread");
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
            rt_trap("Thread.StartSafeOwned: invalid bytecode entry");
        validateBytecodeThreadEntrySignature(*entryFn);

        auto *payload = new (std::nothrow) BytecodeThreadPayload{
            bcModule, entryFn, arg, arg != nullptr, bcVm->captureExecutionEnvironment()};
        if (!payload)
            rt_trap("Thread.StartSafeOwned: payload allocation failed");
        if (payload->ownsArg)
            rt_obj_retain_maybe(arg);
        void *thread = rt_thread_start_safe(
            reinterpret_cast<void *>(&bytecode_thread_safe_entry_trampoline), payload);
        if (!thread) {
            releaseWorkerArg(payload->arg, payload->ownsArg);
            delete payload;
            rt_trap("Thread.StartSafeOwned: failed to create thread");
        }
        if (result)
            *reinterpret_cast<void **>(result) = thread;
        return;
    }

    // No VM active - direct call (native code path)
    if (gPriorThreadStartSafeOwnedHandler) {
        gPriorThreadStartSafeOwnedHandler(args, result);
        return;
    }
    void *thread = rt_thread_start_safe_owned(entry, arg);
    if (result)
        *reinterpret_cast<void **>(result) = thread;
}

/// @brief C ABI trampoline: run a standard-VM Async.Run entry from a bytecode
///        context. Decodes the payload, executes the entry, settles the
///        promise with the result or error, and frees the payload + owned arg.
extern "C" void vm_async_run_entry_trampoline_bc(void *raw) {
    VmAsyncRunPayload *payload = static_cast<VmAsyncRunPayload *>(raw);
    if (!payload || !payload->module || !payload->entry || !payload->promise) {
        if (payload) {
            releaseWorkerArg(payload->arg, payload->ownsArg);
            il::vm::releaseExternRegistry(payload->externRegistry);
        }
        delete payload;
        rt_abort("Async.Run: invalid entry");
        return;
    }

    il::vm::Slot result{};
    bool completed = false;
    rt_string error = nullptr;

    try {
        {
            il::vm::VM vm(*payload->module, payload->program);
            vm.setExternRegistry(payload->externRegistry);
            il::support::SmallVector<il::vm::Slot, 2> args;
            il::vm::Slot s{};
            s.ptr = payload->arg;
            args.push_back(s);
            result = il::vm::detail::VMAccess::callFunction(vm, *payload->entry, args);
        }
        completed = true;
    } catch (const il::vm::RuntimeTrapSignal &signal) {
        const char *message =
            signal.message.empty() ? "Async.Run: trapped VM worker" : signal.message.c_str();
        error = rt_string_from_bytes(message, std::strlen(message));
    } catch (const std::exception &ex) {
        std::string message = std::string("Async.Run: unhandled exception: ") + ex.what();
        error = rt_string_from_bytes(message.data(), message.size());
    } catch (...) {
        error = rt_const_cstr("Async.Run: unhandled non-standard exception");
    }

    if (completed) {
        completeAsyncPromiseWithResult(
            payload->promise, result.ptr, &payload->arg, &payload->ownsArg);
    } else {
        completeAsyncPromiseWithError(payload->promise, error, &payload->arg, &payload->ownsArg);
        rt_str_release_maybe(error);
    }

    if (rt_obj_release_check0(payload->promise))
        rt_obj_free(payload->promise);
    il::vm::releaseExternRegistry(payload->externRegistry);
    delete payload;
}

/// @brief C ABI trampoline: run a bytecode Async.Run entry on a worker VM,
///        settling its promise with the returned result or an error and
///        releasing the payload.
extern "C" void bytecode_async_entry_trampoline(void *raw) {
    BytecodeAsyncPayload *payload = static_cast<BytecodeAsyncPayload *>(raw);
    if (!payload || !payload->module || !payload->entry || !payload->promise) {
        if (payload)
            releaseWorkerArg(payload->arg, payload->ownsArg);
        delete payload;
        rt_abort("Async.Run: invalid bytecode entry");
        return;
    }

    BCSlot result{};
    VMState workerState = VMState::Ready;
    std::string trapMessage;

    {
        BytecodeVM vm;
        vm.load(payload->module);
        vm.applyExecutionEnvironment(payload->environment);

        std::vector<BCSlot> args;
        BCSlot argSlot{};
        argSlot.ptr = payload->arg;
        args.push_back(argSlot);

        result = vm.exec(payload->entry, args);
        workerState = vm.state();
        if (workerState == VMState::Trapped)
            trapMessage = vm.trapMessage();
    }

    if (workerState == VMState::Trapped) {
        rt_string error = trapMessage.empty()
                              ? rt_const_cstr("Async.Run: trapped")
                              : rt_string_from_bytes(trapMessage.data(), trapMessage.size());
        completeAsyncPromiseWithError(payload->promise, error, &payload->arg, &payload->ownsArg);
        rt_str_release_maybe(error);
    } else {
        completeAsyncPromiseWithResult(
            payload->promise, result.ptr, &payload->arg, &payload->ownsArg);
    }

    if (rt_obj_release_check0(payload->promise))
        rt_obj_free(payload->promise);
    delete payload;
}

/// @brief C ABI trampoline: dispatch one HTTP request to a standard-VM
///        bind-handler from a bytecode context (@p req/@p res are the
///        request/response objects).
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

/// @brief C ABI trampoline: dispatch one HTTP request to a bytecode
///        bind-handler, invoking its entry on the active VM with the
///        request/response pair.
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

/// @brief C ABI destructor for a standard-VM HTTP handler payload (called by
///        the runtime when the bound handler is torn down).
extern "C" void destroy_vm_http_handler_payload_bc(void *raw) {
    auto *payload = static_cast<VmHttpHandlerPayload *>(raw);
    if (!payload)
        return;
    il::vm::releaseExternRegistry(payload->externRegistry);
    delete payload;
}

/// @brief C ABI destructor for a bytecode HTTP handler payload.
extern "C" void destroy_bytecode_http_handler_payload(void *raw) {
    delete static_cast<BytecodeHttpHandlerPayload *>(raw);
}

/// @brief Runtime handler for HttpServer.BindHandler that works for both the
///        standard VM and the bytecode VM: detects which engine the entry
///        belongs to, validates its signature, and registers the matching
///        dispatch + payload-destructor trampolines.
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

/// @brief HTTPS counterpart of @ref unified_http_server_bind_handler (same
///        dual-engine dispatch over a TLS server).
static void unified_https_server_bind_handler(void **args, void *result) {
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
        rt_trap("HttpsServer.BindHandler: null entry");

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
        rt_https_server_bind_handler_dispatch(
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
        rt_https_server_bind_handler_dispatch(
            server,
            tag,
            reinterpret_cast<void *>(&bytecode_http_handler_dispatch),
            payload,
            reinterpret_cast<void *>(&destroy_bytecode_http_handler_payload));
        return;
    }

    if (gPriorHttpsBindHandler) {
        gPriorHttpsBindHandler(args, result);
        return;
    }
    rt_https_server_bind_handler(server, tag, entry);
}

/// @brief Runtime handler for Async.Run usable from both engines: validates
///        the entry signature, builds the appropriate async payload, and
///        schedules it on a worker, returning the promise.
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
        auto *payload = new (std::nothrow) VmAsyncRunPayload{&module,
                                                             std::move(program),
                                                             stdVm->externRegistry(),
                                                             entryFn,
                                                             arg,
                                                             arg != nullptr,
                                                             promise};
        if (!payload) {
            rt_promise_set_error(promise, rt_const_cstr("Async.Run: payload allocation failed"));
            if (rt_obj_release_check0(promise))
                rt_obj_free(promise);
            if (result)
                *reinterpret_cast<void **>(result) = future;
            return;
        }
        if (payload->ownsArg)
            rt_obj_retain_maybe(arg);
        il::vm::retainExternRegistry(payload->externRegistry);
        void *thread =
            rt_thread_start(reinterpret_cast<void *>(&vm_async_run_entry_trampoline_bc), payload);
        if (!thread) {
            releaseWorkerArg(payload->arg, payload->ownsArg);
            il::vm::releaseExternRegistry(payload->externRegistry);
            delete payload;
            rt_promise_set_error(promise, rt_const_cstr("Async.Run: failed to create thread"));
            if (rt_obj_release_check0(promise))
                rt_obj_free(promise);
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
        auto *payload = new (std::nothrow) BytecodeAsyncPayload{
            bcModule, entryFn, arg, arg != nullptr, bcVm->captureExecutionEnvironment(), promise};
        if (!payload) {
            rt_promise_set_error(promise, rt_const_cstr("Async.Run: payload allocation failed"));
            if (rt_obj_release_check0(promise))
                rt_obj_free(promise);
            if (result)
                *reinterpret_cast<void **>(result) = future;
            return;
        }
        if (payload->ownsArg)
            rt_obj_retain_maybe(arg);
        void *thread =
            rt_thread_start(reinterpret_cast<void *>(&bytecode_async_entry_trampoline), payload);
        if (!thread) {
            releaseWorkerArg(payload->arg, payload->ownsArg);
            delete payload;
            rt_promise_set_error(promise, rt_const_cstr("Async.Run: failed to create thread"));
            if (rt_obj_release_check0(promise))
                rt_obj_free(promise);
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

/// @brief Install the dual-engine runtime handlers (Thread.Start, Async.Run,
///        Http/Https BindHandler) exactly once per process, chaining to any
///        previously registered handlers. Idempotent via std::call_once;
///        invoked from load()/exec() so bytecode programs get correct
///        threading/async/HTTP behavior.
void registerUnifiedVmRuntimeHandlers() {
    std::call_once(gUnifiedRuntimeHandlersOnce, []() {
        using il::runtime::signatures::make_signature;
        using il::runtime::signatures::SigParam;

        auto capturePriorHandler =
            [](std::string_view name, void *currentFn, UnifiedRuntimeHandler &outHandler) {
                if (const il::vm::ExternDesc *existing = il::vm::RuntimeBridge::findExtern(name)) {
                    if (existing->fn != currentFn)
                        outHandler = reinterpret_cast<UnifiedRuntimeHandler>(existing->fn);
                }
            };

        capturePriorHandler("Viper.Threads.Thread.Start",
                            reinterpret_cast<void *>(&unified_thread_start_handler),
                            gPriorThreadStartHandler);
        capturePriorHandler("Viper.Threads.Thread.StartOwned",
                            reinterpret_cast<void *>(&unified_thread_start_owned_handler),
                            gPriorThreadStartOwnedHandler);
        capturePriorHandler("Viper.Threads.Thread.StartSafe",
                            reinterpret_cast<void *>(&unified_thread_start_safe_handler),
                            gPriorThreadStartSafeHandler);
        capturePriorHandler("Viper.Threads.Thread.StartSafeOwned",
                            reinterpret_cast<void *>(&unified_thread_start_safe_owned_handler),
                            gPriorThreadStartSafeOwnedHandler);
        capturePriorHandler("Viper.Threads.Async.Run",
                            reinterpret_cast<void *>(&unified_async_run_handler),
                            gPriorAsyncRunHandler);
        capturePriorHandler("Viper.Network.HttpServer.BindHandler",
                            reinterpret_cast<void *>(&unified_http_server_bind_handler),
                            gPriorHttpBindHandler);
        capturePriorHandler("Viper.Network.HttpsServer.BindHandler",
                            reinterpret_cast<void *>(&unified_https_server_bind_handler),
                            gPriorHttpsBindHandler);
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
        ext.name = "Viper.Threads.Thread.StartOwned";
        ext.signature = make_signature(ext.name, {SigParam::Ptr, SigParam::Ptr}, {SigParam::Ptr});
        ext.fn = reinterpret_cast<void *>(&unified_thread_start_owned_handler);
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
        ext.name = "Viper.Threads.Thread.StartSafeOwned";
        ext.signature = make_signature(ext.name, {SigParam::Ptr, SigParam::Ptr}, {SigParam::Ptr});
        ext.fn = reinterpret_cast<void *>(&unified_thread_start_safe_owned_handler);
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
    {
        il::vm::ExternDesc ext;
        ext.name = "Viper.Network.HttpsServer.BindHandler";
        ext.signature = make_signature(ext.name, {SigParam::Ptr, SigParam::Str, SigParam::Ptr});
        ext.fn = reinterpret_cast<void *>(&unified_https_server_bind_handler);
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
