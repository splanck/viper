//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Implements the family of opcode handlers responsible for VM control flow,
// function calls, and trap management.  Grouping the logic in one translation
// unit keeps the intricate interactions between branch argument propagation,
// resume tokens, and runtime trap bridging well documented.
//
//===----------------------------------------------------------------------===//

#include "vm/OpHandlers.hpp"

#include "il/core/BasicBlock.hpp"
#include "il/core/Function.hpp"
#include "il/core/Instr.hpp"
#include "il/core/Value.hpp"
#include "vm/control_flow.hpp"
#include "vm/OpHandlerUtils.hpp"
#include "vm/RuntimeBridge.hpp"
#include "vm/Marshal.hpp"
#include "vm/Trap.hpp"
#include "vm/err_bridge.hpp"
#include <string>
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <iterator>
#include <numeric>
#include <type_traits>
#include <utility>
#include <vector>
#include <sstream>
#include <unordered_set>
#include <variant>

using namespace il::core;

namespace
{
static viper::vm::SwitchMode g_switchMode = viper::vm::SwitchMode::Auto;
} // namespace

namespace viper::vm
{
SwitchMode getSwitchMode()
{
    return g_switchMode;
}

void setSwitchMode(SwitchMode mode)
{
    g_switchMode = mode;
}
} // namespace viper::vm

namespace il::vm::detail
{
namespace
{
/// @brief Validate that @p slot references the frame's active resume token.
///
/// Resume instructions accept an opaque pointer operand that must refer to the
/// frame-local @ref Frame::ResumeState.  The helper ensures the pointer matches
/// the active resume state and that the token is still marked valid.
///
/// @param fr Frame owning the authoritative resume state.
/// @param slot Slot operand supplied to a resume instruction.
/// @return Pointer to the resume state when valid; otherwise @c nullptr.
Frame::ResumeState *expectResumeToken(Frame &fr, const Slot &slot)
{
    auto *token = reinterpret_cast<Frame::ResumeState *>(slot.ptr);
    if (!token || token != &fr.resumeState || !token->valid)
        return nullptr;
    return token;
}

/// @brief Report an invalid resume operation via @ref RuntimeBridge::trap.
///
/// Resume opcodes surface a variety of user errors (missing tokens, stale
/// handlers, unknown labels).  This helper formats a message and routes it to
/// the runtime trap mechanism with contextual function/block information.
///
/// @param fr Current frame providing function context.
/// @param in Instruction that triggered the invalid resume attempt.
/// @param bb Current basic block, if available.
/// @param detail Human-readable diagnostic describing the failure.
void trapInvalidResume(Frame &fr,
                       const Instr &in,
                       const BasicBlock *bb,
                       std::string detail)
{
    const std::string functionName = fr.func ? fr.func->name : std::string{};
    const std::string blockLabel = bb ? bb->label : std::string{};
    RuntimeBridge::trap(TrapKind::InvalidOperation, detail, in.loc, functionName, blockLabel);
}

/// @brief Resolve an error token operand to a @ref VmError structure.
///
/// The operand may either directly carry a pointer to a @ref VmError, refer to
/// the thread-local trap token produced by @ref vm_acquire_trap_token, or leave
/// the error unspecified.  In the latter case the frame's @ref Frame::activeError
/// acts as the fallback.
///
/// @param fr Frame supplying the default active error.
/// @param slot Operand to interpret as an error token pointer.
/// @return Pointer to a valid error descriptor for use by err.get handlers.
const VmError *resolveErrorToken(Frame &fr, const Slot &slot)
{
    const auto *error = reinterpret_cast<const VmError *>(slot.ptr);
    if (error)
        return error;

    if (const VmError *token = vm_current_trap_token())
        return token;

    return &fr.activeError;
}
} // namespace

namespace
{
using viper::vm::DenseJumpTable;
using viper::vm::HashedCases;
using viper::vm::SortedCases;
using viper::vm::SwitchCache;
using viper::vm::SwitchCacheEntry;
using viper::vm::SwitchMode;

bool isVmDebugLoggingEnabled()
{
    static const bool enabled = [] {
        if (const char *flag = std::getenv("VIPER_DEBUG_VM"))
            return flag[0] != '\0';
        return false;
    }();
    return enabled;
}

const char *switchCacheKindName(SwitchCacheEntry::Kind kind)
{
    switch (kind)
    {
        case SwitchCacheEntry::Dense:
            return "Dense";
        case SwitchCacheEntry::Sorted:
            return "Sorted";
        case SwitchCacheEntry::Hashed:
            return "Hashed";
        case SwitchCacheEntry::Linear:
            return "Linear";
    }
    return "Unknown";
}

struct SwitchMeta
{
    const void *key = nullptr;
    std::vector<int32_t> values;
    std::vector<int32_t> succIdx;
    int32_t defaultIdx = -1;
};

static SwitchMeta collectSwitchMeta(const Instr &in)
{
    assert(in.op == Opcode::SwitchI32 && "expected switch.i32 instruction");

    SwitchMeta meta{};
    meta.key = static_cast<const void *>(&in);
    meta.defaultIdx = !in.labels.empty() ? 0 : -1;

    const size_t caseCount = switchCaseCount(in);
    meta.values.reserve(caseCount);
    meta.succIdx.reserve(caseCount);

    std::unordered_set<int32_t> seenValues;
    seenValues.reserve(caseCount);

    for (size_t idx = 0; idx < caseCount; ++idx)
    {
        const Value &value = switchCaseValue(in, idx);
        assert(value.kind == Value::Kind::ConstInt && "switch case requires integer literal");
        const int32_t caseValue = static_cast<int32_t>(value.i64);
        const auto [_, inserted] = seenValues.insert(caseValue);
        if (!inserted)
            continue;
        meta.values.push_back(caseValue);
        meta.succIdx.push_back(static_cast<int32_t>(idx + 1));
    }

    assert(meta.values.size() == meta.succIdx.size());
    return meta;
}

static int32_t lookupDense(const DenseJumpTable &T, int32_t sel, int32_t defIdx)
{
    const int64_t off = static_cast<int64_t>(sel) - static_cast<int64_t>(T.base);
    if (off < 0 || off >= static_cast<int64_t>(T.targets.size()))
        return defIdx;
    const int32_t t = T.targets[static_cast<size_t>(off)];
    return (t < 0) ? defIdx : t;
}

static int32_t lookupSorted(const SortedCases &S, int32_t sel, int32_t defIdx)
{
    auto it = std::lower_bound(S.keys.begin(), S.keys.end(), sel);
    if (it == S.keys.end() || *it != sel)
        return defIdx;
    const size_t idx = static_cast<size_t>(it - S.keys.begin());
    return S.targetIdx[idx];
}

static int32_t lookupHashed(const HashedCases &H, int32_t sel, int32_t defIdx)
{
    auto it = H.map.find(sel);
    return (it == H.map.end()) ? defIdx : it->second;
}

/// @brief Select the most appropriate switch cache backend for @p M.
///
/// Dense tables perform best when the case value distribution is tightly
/// packed, hashed dispatch excels when the key set is sparse, and sorted
/// searches are the general-purpose fallback.  The heuristic balances range
/// coverage against the number of explicit cases to maintain predictable
/// performance across workloads.
///
/// @param M Metadata describing the switch instruction being cached.
/// @return Preferred backend kind based on case density and range size.
static SwitchCacheEntry::Kind chooseBackend(const SwitchMeta &M)
{
    if (M.values.empty())
        return SwitchCacheEntry::Sorted;

    const auto [minIt, maxIt] = std::minmax_element(M.values.begin(), M.values.end());
    const int64_t minv = *minIt;
    const int64_t maxv = *maxIt;
    const int64_t range = maxv - minv + 1;
    const double density = static_cast<double>(M.values.size()) / static_cast<double>(range);

    if (range <= 4096 && density >= 0.60)
        return SwitchCacheEntry::Dense;
    if (M.values.size() >= 64 && density < 0.15)
        return SwitchCacheEntry::Hashed;
    return SwitchCacheEntry::Sorted;
}

static DenseJumpTable buildDense(const SwitchMeta &M)
{
    int32_t minv = *std::min_element(M.values.begin(), M.values.end());
    int32_t maxv = *std::max_element(M.values.begin(), M.values.end());
    DenseJumpTable T;
    T.base = minv;
    T.targets.assign(maxv - minv + 1, -1);
    for (size_t i = 0; i < M.values.size(); ++i)
    {
        T.targets[M.values[i] - minv] = M.succIdx[i];
    }
    return T;
}

static HashedCases buildHashed(const SwitchMeta &M)
{
    HashedCases H;
    H.map.reserve(M.values.size() * 2);
    for (size_t i = 0; i < M.values.size(); ++i)
    {
        H.map.emplace(M.values[i], M.succIdx[i]);
    }
    return H;
}

static SortedCases buildSorted(const SwitchMeta &M)
{
    std::vector<size_t> idx(M.values.size());
    std::iota(idx.begin(), idx.end(), 0);
    std::sort(idx.begin(), idx.end(), [&](size_t a, size_t b) {
        return M.values[a] < M.values[b];
    });
    SortedCases S;
    S.keys.reserve(idx.size());
    S.targetIdx.reserve(idx.size());
    for (size_t i : idx)
    {
        S.keys.push_back(M.values[i]);
        S.targetIdx.push_back(M.succIdx[i]);
    }
    return S;
}

static SwitchCacheEntry &getOrBuildSwitchCache(SwitchCache &cache, const Instr &in)
{
    SwitchMeta meta = collectSwitchMeta(in);
    auto it = cache.entries.find(meta.key);
    if (it != cache.entries.end())
        return it->second;

    SwitchCacheEntry entry{};
    entry.defaultIdx = meta.defaultIdx;
    const SwitchMode mode = viper::vm::getSwitchMode();
    const char *selectedKindName = nullptr;
    if (mode != SwitchMode::Auto)
    {
        switch (mode)
        {
            case SwitchMode::Dense:
                entry.kind = SwitchCacheEntry::Dense;
                entry.backend = buildDense(meta);
                selectedKindName = switchCacheKindName(entry.kind);
                break;
            case SwitchMode::Sorted:
                entry.kind = SwitchCacheEntry::Sorted;
                entry.backend = buildSorted(meta);
                selectedKindName = switchCacheKindName(entry.kind);
                break;
            case SwitchMode::Hashed:
                entry.kind = SwitchCacheEntry::Hashed;
                entry.backend = buildHashed(meta);
                selectedKindName = switchCacheKindName(entry.kind);
                break;
            case SwitchMode::Linear:
                entry.kind = SwitchCacheEntry::Linear;
                entry.backend = std::monostate{};
                selectedKindName = switchCacheKindName(entry.kind);
                break;
            case SwitchMode::Auto:
                break;
        }
    }
    else
    {
        SwitchCacheEntry::Kind kind = chooseBackend(meta);
        entry.kind = kind;
        selectedKindName = switchCacheKindName(kind);
        switch (kind)
        {
            case SwitchCacheEntry::Dense:
                entry.backend = buildDense(meta);
                break;
            case SwitchCacheEntry::Sorted:
                entry.backend = buildSorted(meta);
                break;
            case SwitchCacheEntry::Hashed:
                entry.backend = buildHashed(meta);
                break;
            case SwitchCacheEntry::Linear:
                entry.backend = std::monostate{};
                break;
        }
    }

    if (isVmDebugLoggingEnabled() && selectedKindName)
    {
        std::fprintf(stderr,
                     "[DEBUG][VM] switch backend: %s (cases=%zu)\n",
                     selectedKindName,
                     meta.values.size());
    }

    auto [pos, _] = cache.entries.emplace(meta.key, std::move(entry));
    return pos->second;
}

} // namespace

/// @brief Transfer control to a branch target and seed its parameter slots.
/// @param vm Active VM used to evaluate branch argument values.
/// @param fr Current frame receiving parameter updates for the successor block.
/// @param in Branch or terminator instruction describing successor labels and arguments.
/// @param idx Index of the branch label/argument tuple that should be taken.
/// @param blocks Mapping from block labels to IR blocks; lookup must succeed (verified).
/// @param bb Output reference updated to the resolved successor block pointer.
/// @param ip Output instruction index reset to start executing at the new block.
/// @return Execution result flagged as a jump without producing a value.
/// @note Invariant: the branch target must exist in @p blocks; malformed IL would have
///       been rejected earlier by verification. When present, branch arguments are
///       evaluated and copied into the target block parameters before control moves.
VM::ExecResult OpHandlers::branchToTarget(VM &vm,
                                          Frame &fr,
                                          const Instr &in,
                                          size_t idx,
                                          const VM::BlockMap &blocks,
                                          const BasicBlock *&bb,
                                          size_t &ip)
{
    const auto &label = in.labels[idx];
    auto it = blocks.find(label);
    assert(it != blocks.end() && "invalid branch target");
    const BasicBlock *target = it->second;
    const BasicBlock *sourceBlock = bb;
    const std::string sourceLabel = sourceBlock ? sourceBlock->label : std::string{};
    const std::string functionName = fr.func ? fr.func->name : std::string{};

    const size_t expected = target->params.size();
    const size_t provided = idx < in.brArgs.size() ? in.brArgs[idx].size() : 0;
    if (provided != expected)
    {
        std::ostringstream os;
        os << "branch argument count mismatch targeting '" << target->label << '\'';
        if (!sourceLabel.empty())
            os << " from '" << sourceLabel << '\'';
        os << ": expected " << expected << ", got " << provided;
        RuntimeBridge::trap(TrapKind::InvalidOperation, os.str(), in.loc, functionName, sourceLabel);
        return {};
    }

    if (provided > 0)
    {
        const auto &args = in.brArgs[idx];
        for (size_t i = 0; i < provided; ++i)
        {
            const auto id = target->params[i].id;
            assert(id < fr.params.size());
            fr.params[id] = vm.eval(fr, args[i]);
        }
    }

    bb = target;
    ip = 0;
    VM::ExecResult result{};
    result.jumped = true;
    return result;
}

/// @brief Handle a `ret` terminator by yielding control to the caller.
/// @param vm Active VM used to evaluate an optional return operand.
/// @param fr Current frame whose result is propagated through @ref VM::ExecResult.
/// @param in Instruction describing the return operand (if present).
/// @param blocks Unused lookup table required by the handler signature.
/// @param bb Unused basic block reference for this terminator.
/// @param ip Unused instruction pointer reference for this terminator.
/// @return Execution result flagged with @c returned and carrying an optional value.
/// @note Invariant: when a return operand exists it is evaluated exactly once and the
///       resulting slot is stored in the execution result; control never touches
///       subsequent instructions within the block.
VM::ExecResult OpHandlers::handleRet(VM &vm,
                                     Frame &fr,
                                     const Instr &in,
                                     const VM::BlockMap &blocks,
                                     const BasicBlock *&bb,
                                     size_t &ip)
{
    (void)blocks;
    (void)bb;
    (void)ip;
    VM::ExecResult result{};
    if (!in.operands.empty())
        result.value = vm.eval(fr, in.operands[0]);
    result.returned = true;
    return result;
}

/// @brief Handle direct and indirect function calls from within the VM.
/// @param vm Active VM responsible for resolving internal callee definitions.
/// @param fr Current frame providing argument values and storing call results.
/// @param in Instruction describing the callee symbol and operand list.
/// @param blocks Unused block map parameter required by the generic handler signature.
/// @param bb Current basic block, used only for diagnostic context if bridging.
/// @param ip Unused instruction pointer reference for this opcode.
/// @return Execution result representing normal fallthrough.
/// @note Invariant: all operand slots are evaluated prior to dispatch. If the callee
///       exists in @p vm.fnMap, the VM executes it natively; otherwise
///       @ref RuntimeBridge routes the call to the runtime. Any produced value is
///       stored via @ref ops::storeResult, ensuring the destination slot is updated.
VM::ExecResult OpHandlers::handleCall(VM &vm,
                                      Frame &fr,
                                      const Instr &in,
                                      const VM::BlockMap &blocks,
                                      const BasicBlock *&bb,
                                      size_t &ip)
{
    (void)blocks;
    (void)bb;
    (void)ip;
    std::vector<Slot> args;
    args.reserve(in.operands.size());
    for (const auto &op : in.operands)
        args.push_back(vm.eval(fr, op));

    Slot out{};
    auto it = vm.fnMap.find(in.callee);
    if (it != vm.fnMap.end())
        out = vm.execFunction(*it->second, args);
    else
        out = RuntimeBridge::call(vm.runtimeContext, in.callee, args, in.loc, fr.func->name, bb->label);
    ops::storeResult(fr, in, out);
    return {};
}

/// @brief Handle `err.get_*` opcodes by materialising fields from the active error.
/// @param vm Active VM instance used to evaluate the error operand.
/// @param fr Current frame carrying the handler's active error record.
/// @param in Instruction identifying which error field to fetch.
/// @param blocks Unused block map required by the dispatch signature.
/// @param bb Unused current block reference required by the dispatch signature.
/// @param ip Unused instruction index reference required by the dispatch signature.
/// @return Execution result indicating normal continuation.
VM::ExecResult OpHandlers::handleErrGet(VM &vm,
                                        Frame &fr,
                                        const Instr &in,
                                        const VM::BlockMap &blocks,
                                        const BasicBlock *&bb,
                                        size_t &ip)
{
    (void)blocks;
    (void)bb;
    (void)ip;

    Slot operandSlot{};
    if (!in.operands.empty())
        operandSlot = vm.eval(fr, in.operands[0]);

    const VmError *error = resolveErrorToken(fr, operandSlot);

    Slot out{};
    switch (in.op)
    {
        case Opcode::ErrGetKind:
            out.i64 = static_cast<int64_t>(static_cast<int32_t>(error->kind));
            break;
        case Opcode::ErrGetCode:
            out.i64 = static_cast<int64_t>(error->code);
            break;
        case Opcode::ErrGetIp:
            out.i64 = static_cast<int64_t>(error->ip);
            break;
        case Opcode::ErrGetLine:
            out.i64 = static_cast<int64_t>(static_cast<int32_t>(error->line));
            break;
        default:
            out.i64 = 0;
            break;
    }

    ops::storeResult(fr, in, out);
    return {};
}

/// @brief No-op handler executed when entering an exception handler block.
/// @param vm Active VM (unused) included for signature uniformity.
/// @param fr Current frame (unused) retained for future bookkeeping needs.
/// @param in Instruction metadata (unused) describing the eh.entry site.
/// @param blocks Unused block map reference required by the handler interface.
/// @param bb Current block pointer (unused) provided for signature parity.
/// @param ip Instruction pointer (unused) supplied for signature parity.
/// @return Execution result indicating normal fallthrough.
VM::ExecResult OpHandlers::handleEhEntry(VM &vm,
                                         Frame &fr,
                                         const Instr &in,
                                         const VM::BlockMap &blocks,
                                         const BasicBlock *&bb,
                                         size_t &ip)
{
    (void)vm;
    (void)fr;
    (void)in;
    (void)blocks;
    (void)bb;
    (void)ip;
    return {};
}

/// @brief Push a handler entry onto the frame's exception handler stack.
/// @param vm Active VM (unused) included for signature uniformity.
/// @param fr Current frame receiving the handler registration.
/// @param in Instruction carrying the handler label to install.
/// @param blocks Mapping from labels to blocks used to resolve the handler target.
/// @param bb Current block pointer (unused) retained for signature compatibility.
/// @param ip Instruction pointer snapshot recorded for resume.same semantics.
/// @return Execution result indicating normal fallthrough.
VM::ExecResult OpHandlers::handleEhPush(VM &vm,
                                        Frame &fr,
                                        const Instr &in,
                                        const VM::BlockMap &blocks,
                                        const BasicBlock *&bb,
                                        size_t &ip)
{
    (void)vm;
    (void)bb;
    (void)ip;
    assert(!in.labels.empty() && "eh.push requires handler label");
    auto it = blocks.find(in.labels[0]);
    assert(it != blocks.end() && "eh.push target must exist");
    Frame::HandlerRecord record{};
    record.handler = it->second;
    record.ipSnapshot = ip;
    fr.ehStack.push_back(record);
    return {};
}

/// @brief Pop the most recently pushed exception handler record.
///
/// The handler mirrors the semantics of BASIC's `ON ERROR` stack by discarding
/// the last pushed record if one exists.  All parameters besides @p fr are
/// unused but retained for signature compatibility with other handlers.
VM::ExecResult OpHandlers::handleEhPop(VM &vm,
                                       Frame &fr,
                                       const Instr &in,
                                       const VM::BlockMap &blocks,
                                       const BasicBlock *&bb,
                                       size_t &ip)
{
    (void)vm;
    (void)in;
    (void)blocks;
    (void)bb;
    (void)ip;
    if (!fr.ehStack.empty())
        fr.ehStack.pop_back();
    return {};
}

/// @brief Resume execution at the trapping instruction itself.
///
/// Validates the resume token operand, ensures the recorded block remains
/// available, and then jumps back to the faulting instruction so it can be
/// retried.  The resume token is invalidated after use to mirror the runtime's
/// single-use semantics.
VM::ExecResult OpHandlers::handleResumeSame(VM &vm,
                                            Frame &fr,
                                            const Instr &in,
                                            const VM::BlockMap &blocks,
                                            const BasicBlock *&bb,
                                            size_t &ip)
{
    (void)blocks;
    (void)bb;
    if (in.operands.empty())
    {
        trapInvalidResume(fr, in, bb, "resume.same: missing resume token operand");
        return {};
    }

    Slot tokSlot = vm.eval(fr, in.operands[0]);
    Frame::ResumeState *token = expectResumeToken(fr, tokSlot);
    if (!token)
    {
        trapInvalidResume(fr, in, bb, "resume.same: requires an active resume token");
        return {};
    }
    if (!token->block)
    {
        trapInvalidResume(fr, in, bb, "resume.same: resume target is no longer available");
        return {};
    }
    fr.resumeState.valid = false;
    bb = token->block;
    ip = token->faultIp;
    VM::ExecResult result{};
    result.jumped = true;
    return result;
}

/// @brief Resume execution at the instruction following the trapping site.
///
/// Validates the resume token operand and then jumps to the recorded
/// @ref Frame::ResumeState::nextIp while invalidating the resume token.  Errors
/// are reported through @ref trapInvalidResume.
VM::ExecResult OpHandlers::handleResumeNext(VM &vm,
                                            Frame &fr,
                                            const Instr &in,
                                            const VM::BlockMap &blocks,
                                            const BasicBlock *&bb,
                                            size_t &ip)
{
    (void)blocks;
    (void)bb;
    if (in.operands.empty())
    {
        trapInvalidResume(fr, in, bb, "resume.next: missing resume token operand");
        return {};
    }

    Slot tokSlot = vm.eval(fr, in.operands[0]);
    Frame::ResumeState *token = expectResumeToken(fr, tokSlot);
    if (!token)
    {
        trapInvalidResume(fr, in, bb, "resume.next: requires an active resume token");
        return {};
    }
    if (!token->block)
    {
        trapInvalidResume(fr, in, bb, "resume.next: resume target is no longer available");
        return {};
    }
    fr.resumeState.valid = false;
    bb = token->block;
    ip = token->nextIp;
    VM::ExecResult result{};
    result.jumped = true;
    return result;
}

/// @brief Resume execution at a specific label selected by the instruction.
///
/// Checks both the resume token and the requested label name before delegating
/// to @ref branchToTarget.  Invalid tokens or unknown labels trigger a trap via
/// @ref trapInvalidResume.
VM::ExecResult OpHandlers::handleResumeLabel(VM &vm,
                                            Frame &fr,
                                            const Instr &in,
                                            const VM::BlockMap &blocks,
                                            const BasicBlock *&bb,
                                            size_t &ip)
{
    if (in.operands.empty())
    {
        trapInvalidResume(fr, in, bb, "resume.label: missing resume token operand");
        return {};
    }

    Slot tokSlot = vm.eval(fr, in.operands[0]);
    Frame::ResumeState *token = expectResumeToken(fr, tokSlot);
    if (!token)
    {
        trapInvalidResume(fr, in, bb, "resume.label: requires an active resume token");
        return {};
    }

    if (in.labels.empty())
    {
        trapInvalidResume(fr, in, bb, "resume.label: missing destination label");
        return {};
    }

    const auto &label = in.labels[0];
    if (blocks.find(label) == blocks.end())
    {
        std::ostringstream os;
        os << "resume.label: unknown destination label '" << label << "'";
        trapInvalidResume(fr, in, bb, os.str());
        return {};
    }
    fr.resumeState.valid = false;
    return branchToTarget(vm, fr, in, 0, blocks, bb, ip);
}

/// @brief Materialise the @ref TrapKind associated with a trap token.
///
/// Accepts an optional pointer operand referencing a @ref VmError.  When the
/// operand is absent the handler falls back to thread-local trap tokens or the
/// frame's active error.  The resulting trap kind is stored in the destination
/// register as a signed 64-bit integer.
VM::ExecResult OpHandlers::handleTrapKind(VM &vm,
                                         Frame &fr,
                                         const Instr &in,
                                         const VM::BlockMap &blocks,
                                         const BasicBlock *&bb,
                                         size_t &ip)
{
    (void)blocks;
    (void)bb;
    (void)ip;

    const VmError *error = nullptr;
    if (!in.operands.empty())
    {
        Slot errorSlot = vm.eval(fr, in.operands[0]);
        error = reinterpret_cast<const VmError *>(errorSlot.ptr);
    }

    if (!error)
        error = vm_current_trap_token();
    if (!error)
        error = &fr.activeError;

    Slot out{};
    const auto kindValue = error ? static_cast<int32_t>(error->kind) : static_cast<int32_t>(TrapKind::RuntimeError);
    out.i64 = static_cast<int64_t>(kindValue);
    ops::storeResult(fr, in, out);
    return {};
}

/// @brief Convert an `err` numeric code (and optional message) into a trap token.
///
/// Allocates or reuses the thread-local trap token, maps the numeric code to a
/// @ref TrapKind, stores the optional message for later retrieval, and returns
/// the token pointer to the caller.
VM::ExecResult OpHandlers::handleTrapErr(VM &vm,
                                        Frame &fr,
                                        const Instr &in,
                                        const VM::BlockMap &blocks,
                                        const BasicBlock *&bb,
                                        size_t &ip)
{
    (void)blocks;
    (void)bb;
    (void)ip;
    (void)fr;

    Slot codeSlot = vm.eval(fr, in.operands[0]);
    const int32_t code = static_cast<int32_t>(codeSlot.i64);

    std::string message;
    if (in.operands.size() > 1)
    {
        Slot textSlot = vm.eval(fr, in.operands[1]);
        if (textSlot.str != nullptr)
        {
            auto view = fromViperString(textSlot.str);
            message.assign(view.begin(), view.end());
        }
    }

    VmError *token = vm_acquire_trap_token();
    token->kind = map_err_to_trap(code);
    token->code = code;
    token->ip = 0;
    token->line = -1;
    vm_store_trap_token_message(message);

    Slot out{};
    out.ptr = token;
    ops::storeResult(fr, in, out);
    return {};
}

/// @brief Implement the legacy `trap` opcodes that raise runtime traps directly.
///
/// Depending on the opcode variant the handler either raises a domain error,
/// maps an @c err code to a trap classification, or falls back to the generic
/// runtime error trap.  The resulting trap terminates execution; the returned
/// execution result is marked as having produced a return to satisfy callers.
VM::ExecResult OpHandlers::handleTrap(VM &vm,
                                      Frame &fr,
                                      const Instr &in,
                                      const VM::BlockMap &blocks,
                                      const BasicBlock *&bb,
                                      size_t &ip)
{
    (void)blocks;
    (void)bb;
    (void)ip;

    switch (in.op)
    {
        case Opcode::Trap:
            vm_raise(TrapKind::DomainError);
            break;
        case Opcode::TrapFromErr:
        {
            Slot codeSlot = vm.eval(fr, in.operands[0]);
            const auto trapKind = map_err_to_trap(static_cast<int32_t>(codeSlot.i64));
            vm_raise(trapKind, static_cast<int32_t>(codeSlot.i64));
            break;
        }
        default:
            vm_raise(TrapKind::RuntimeError);
            break;
    }
    VM::ExecResult result{};
    result.returned = true;
    return result;
}

} // namespace il::vm::detail

