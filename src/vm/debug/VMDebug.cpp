//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Provides the debugging utilities responsible for breakpoint handling,
// stepping, and block parameter propagation.  These helpers keep the
// interpreter loop focused on opcode dispatch while consolidating debug
// bookkeeping in a single translation unit.
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Implements the IL VM's debugger integration helpers.
/// @details The routines here coordinate between the interpreter core, the
///          debug controller, and optional scripting front-ends.  They transfer
///          staged block parameters, evaluate breakpoints, honour step budgets,
///          and surface rich diagnostics describing why execution pauses.
///          Centralising the logic keeps the dispatch loop uncluttered and
///          ensures all debug pathways apply consistent invariants when
///          manipulating VM state.

#include "il/core/BasicBlock.hpp"
#include "il/core/Function.hpp"
#include "il/core/Instr.hpp"
#include "support/source_manager.hpp"
#include "viper/vm/debug/Debug.hpp"
#include "vm/OpHandlerUtils.hpp"
#include "vm/VM.hpp"
#include "vm/VMConstants.hpp"

#include "rt_string.h"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

using namespace il::core;

namespace il::vm {

void requestDebugPause(VM &vm) {
    vm.requestDebugPause();
}

void VM::applyDebugAction(DebugAction action, size_t currentDepth) {
    stepBudget = 0;
    debugStepMode_ = DebugStepMode::None;
    debugStepTargetDepth_ = currentDepth;
    debugStepArmed_ = false;

    switch (action.kind) {
        case DebugActionKind::Continue:
            break;
        case DebugActionKind::Step:
            stepBudget = action.count == 0 ? 1 : action.count;
            break;
        case DebugActionKind::StepOver:
            debugStepMode_ = DebugStepMode::StepOver;
            debugStepTargetDepth_ = currentDepth;
            break;
        case DebugActionKind::StepOut:
            debugStepMode_ = DebugStepMode::StepOut;
            debugStepTargetDepth_ = currentDepth == 0 ? 0 : currentDepth - 1;
            break;
    }
}

namespace {

/// @brief Format a double compactly for variable display.
std::string formatDouble(double d) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%g", d);
    return std::string(buf);
}

/// @brief Render an rt_string as a quoted, truncated display value. Only called
///        when the register is known to hold a valid owned string.
std::string quoteRtString(rt_string s) {
    std::string out = "\"";
    if (s) {
        const char *data = rt_string_cstr(s);
        size_t len = static_cast<size_t>(rt_str_len(s));
        constexpr size_t kMax = 200;
        for (size_t i = 0; i < len && i < kMax; ++i) {
            unsigned char c = static_cast<unsigned char>(data[i]);
            if (c == '\n')
                out += "\\n";
            else if (c == '\t')
                out += "\\t";
            else if (c < 0x20)
                out.push_back(' ');
            else
                out.push_back(static_cast<char>(c));
        }
        if (len > kMax)
            out += "...";
    }
    out.push_back('"');
    return out;
}

/// @brief Read a scalar of @p kind from a frame-stack alloca pointer into @p local.
/// @details Bounds-checks @p ptr against the frame's fixed-size operand stack so a
///          stale or non-alloca pointer cannot fault. Only fixed-size scalars are
///          loaded; aggregate/string pointees are left to the caller.
/// @return True when a value was read and formatted.
bool loadScalarFromAlloca(const Frame &fr, void *ptr, Type::Kind kind, DebugLocalInfo &local) {
    if (!ptr)
        return false;
    size_t size = 0;
    switch (kind) {
        case Type::Kind::I64:
        case Type::Kind::F64:
        case Type::Kind::Ptr:
        case Type::Kind::Str:
            size = 8;
            break;
        case Type::Kind::I32:
            size = 4;
            break;
        case Type::Kind::I16:
            size = 2;
            break;
        case Type::Kind::I1:
            size = 1;
            break;
        default:
            return false; // strings/aggregates: not loaded here
    }
    const uint8_t *base = fr.stack.data();
    const uint8_t *p = static_cast<const uint8_t *>(ptr);
    if (!base || p < base || p + size > base + fr.stack.size())
        return false; // not inside this frame's alloca region

    if (kind == Type::Kind::F64) {
        double d = 0.0;
        std::memcpy(&d, p, 8);
        local.type = "f64";
        local.value = formatDouble(d);
    } else if (kind == Type::Kind::I1) {
        uint8_t b = 0;
        std::memcpy(&b, p, 1);
        local.type = "bool";
        local.value = b ? "true" : "false";
    } else if (kind == Type::Kind::I32) {
        int32_t v = 0;
        std::memcpy(&v, p, 4);
        local.type = "i64";
        local.value = std::to_string(static_cast<int64_t>(v));
    } else if (kind == Type::Kind::I16) {
        int16_t v = 0;
        std::memcpy(&v, p, 2);
        local.type = "i64";
        local.value = std::to_string(static_cast<int64_t>(v));
    } else if (kind == Type::Kind::Str) {
        rt_string s = nullptr;
        std::memcpy(&s, p, sizeof(rt_string));
        local.type = "str";
        // rt_string_is_handle validates against the runtime's string registry and
        // is safe on garbage, so a not-yet-stored alloca (stale pooled bytes) cannot
        // deref an invalid handle.
        if (rt_string_is_handle(s))
            local.value = quoteRtString(s);
        else
            local.value = "\"\"";
    } else { // I64 / Ptr
        int64_t v = 0;
        std::memcpy(&v, p, 8);
        local.type = kind == Type::Kind::Ptr ? "ptr" : "i64";
        local.value = kind == Type::Kind::Ptr ? (v ? "<ptr>" : "null") : std::to_string(v);
    }
    return true;
}

/// @brief Format a scalar register value held directly in an SSA value.
void formatRegScalar(const Frame &fr, size_t id, Type::Kind kind, DebugLocalInfo &local) {
    const Slot &slot = fr.regs[id];
    switch (kind) {
        case Type::Kind::F64:
            local.type = "f64";
            local.value = formatDouble(slot.f64);
            break;
        case Type::Kind::Str:
            local.type = "str";
            if (id < fr.regIsStr.size() && fr.regIsStr[id])
                local.value = quoteRtString(slot.str);
            else
                local.value = "\"\"";
            break;
        case Type::Kind::I1:
            local.type = "bool";
            local.value = slot.i64 ? "true" : "false";
            break;
        default: // integral
            local.type = "i64";
            local.value = std::to_string(slot.i64);
            break;
    }
}

/// @brief Collect source-named locals of @p fr (skipping SSA temporaries),
///        loading mutable variables through their frame-stack allocas and showing
///        immutable SSA values directly. Dedups by source name, preferring the
///        alloca-backed (current) value. All memory reads are bounds-checked.
void collectFrameLocals(const Frame &fr, std::vector<DebugLocalInfo> &out) {
    const Function *fn = fr.func;
    if (!fn)
        return;
    const size_t count = fn->valueNames.size();

    // Per-id static type, and the pointee type of any alloca written by a store.
    std::vector<Type::Kind> kinds(count, Type::Kind::Void);
    std::vector<Type::Kind> allocaPointee(count, Type::Kind::Void);
    std::vector<char> isAlloca(count, 0);
    auto setKind = [&](unsigned id, Type::Kind k) {
        if (id < count)
            kinds[id] = k;
    };
    for (const auto &bb : fn->blocks) {
        for (const auto &p : bb.params)
            setKind(p.id, p.type.kind);
        for (const auto &in : bb.instructions) {
            if (in.result)
                setKind(*in.result, in.type.kind);
            if (in.op == Opcode::Store && in.operands.size() >= 2 &&
                in.operands[0].kind == Value::Kind::Temp) {
                const unsigned pid = in.operands[0].id;
                if (pid < count) {
                    allocaPointee[pid] = in.type.kind;
                    isAlloca[pid] = 1;
                }
            }
        }
    }

    // Insertion-ordered dedup by display name; alloca-backed values are authoritative.
    std::vector<std::pair<std::string, DebugLocalInfo>> ordered;
    auto upsert = [&](std::string name, DebugLocalInfo info, bool authoritative) {
        for (auto &e : ordered) {
            if (e.first == name) {
                if (authoritative)
                    e.second = std::move(info);
                return;
            }
        }
        ordered.emplace_back(std::move(name), std::move(info));
    };

    for (size_t id = 0; id < count; ++id) {
        const std::string &name = fn->valueNames[id];
        if (name.empty() || name[0] == '%') // skip unnamed SSA temporaries ("%tN")
            continue;
        if (id >= fr.regs.size())
            continue;

        DebugLocalInfo local;
        const auto dollar = name.find('$'); // strip "$id" shadow-disambiguation suffix
        local.name = dollar == std::string::npos ? name : name.substr(0, dollar);

        bool authoritative = false;
        if (isAlloca[id]) {
            if (loadScalarFromAlloca(fr, fr.regs[id].ptr, allocaPointee[id], local))
                authoritative = true;
            else
                continue; // string/aggregate alloca or out-of-bounds: skip for now
        } else if (kinds[id] == Type::Kind::Ptr) {
            continue; // raw non-alloca pointer: not user-meaningful
        } else {
            formatRegScalar(fr, id, kinds[id], local);
        }
        upsert(local.name, std::move(local), authoritative);
    }

    out.reserve(ordered.size());
    for (auto &e : ordered)
        out.push_back(std::move(e.second));
}

} // namespace

DebugStopInfo VM::buildStopInfo(std::string_view reason,
                                const il::support::SourceLoc &loc) const {
    DebugStopInfo info;
    info.reason = std::string(reason);
    info.line = loc.line;
    info.column = loc.column;
    const il::support::SourceManager *sm = debug.getSourceManager();
    if (sm && loc.hasFile())
        info.path = std::string(sm->getPath(loc.file_id));

    for (const auto &f : buildBacktrace()) {
        DebugFrameInfo df;
        df.function = f.function;
        df.path = f.file;
        df.line = f.line > 0 ? static_cast<uint32_t>(f.line) : 0;
        info.frames.push_back(std::move(df));
    }

    if (!execStack.empty()) {
        const ExecState *top = execStack.back();
        if (top)
            collectFrameLocals(top->fr, info.locals);
    }
    return info;
}

std::optional<Slot> VM::pauseOrAdvanceDebugScript(ExecState &st, std::string_view reason) {
    if (frontend_) {
        il::support::SourceLoc loc{};
        if (st.bb && st.ip < st.bb->instructions.size())
            loc = st.bb->instructions[st.ip].loc;
        applyDebugAction(frontend_->onStop(buildStopInfo(reason, loc)), execStack.size());
        st.skipBreakOnce = true;
        return std::nullopt;
    }
    std::cerr << "[BREAK] fn=@" << st.fr.func->name << " blk=" << st.bb->label
              << " reason=" << reason << "\n";
    if (!script || script->empty()) {
        Slot s{};
        s.i64 = kDebugBreakpointSentinel;
        return s;
    }

    applyDebugAction(script->nextAction(), execStack.size());
    st.skipBreakOnce = true;
    return std::nullopt;
}

/// @brief Apply pending block parameter transfers for the given block.
/// @details Any arguments staged by a predecessor terminator are copied into the
///          frame's register file and announced to the debug controller.  The
///          routine grows the register vector when necessary, materialises a
///          pseudo-instruction so existing store helpers can marshal the value,
///          and releases transient string handles once consumed.  Parameters are
///          cleared after transfer so repeated calls are harmless when no
///          updates remain.
/// @param fr Current frame whose registers receive parameter values.
/// @param bb Basic block that has just become active.
void VM::transferBlockParams(Frame &fr, const BasicBlock &bb) {
    for (const auto &p : bb.params) {
        if (p.id >= fr.params.size() || p.id >= fr.paramsSet.size()) {
            RuntimeBridge::trap(TrapKind::InvalidOperation,
                                "block parameter ID out of range",
                                {},
                                fr.func ? fr.func->name : std::string{},
                                bb.label);
            continue;
        }
        if (!fr.paramsSet[p.id])
            continue;
        if (fr.regs.size() <= p.id)
            fr.regs.resize(p.id + 1);
        if (fr.regIsStr.size() <= p.id)
            fr.regIsStr.resize(p.id + 1, 0);

        Instr pseudo;
        pseudo.result = p.id;
        pseudo.type = p.type;
        detail::ops::storeResult(fr, pseudo, fr.params[p.id]);
        debug.onStore(
            p.name, p.type.kind, fr.regs[p.id].i64, fr.regs[p.id].f64, fr.func->name, bb.label, 0);
        if (p.type.kind == Type::Kind::Str)
            rt_str_release_maybe(fr.params[p.id].str);
        fr.paramsSet[p.id] = 0;
    }
}

/// @brief Manage a potential debug break before or after executing an instruction.
/// @details The helper first considers block-level breakpoints, honouring the
///          single-step skip flag by deferring only the next break opportunity.
///          When a break triggers and no script is present, a synthetic slot is
///          returned to suspend the interpreter; otherwise the current debug
///          script dictates how many instructions to step before resuming.
///          Source line breakpoints are processed when @p in is non-null so the
///          debugger can halt on specific instructions even when control stays
///          within the same block.
/// @param fr            Current frame.
/// @param bb            Current basic block.
/// @param ip            Instruction index within @p bb.
/// @param skipBreakOnce Internal flag used to skip a single break when stepping.
/// @param in            Optional instruction for source line breakpoints.
/// @return @c std::nullopt to continue or a @c Slot signalling a pause.
std::optional<Slot> VM::handleDebugBreak(
    Frame &fr, const BasicBlock &bb, size_t ip, bool &skipBreakOnce, const Instr *in) {
    if (!in) {
        if (debug.shouldBreak(bb)) {
            if (frontend_) {
                il::support::SourceLoc loc{};
                if (ip < bb.instructions.size())
                    loc = bb.instructions[ip].loc;
                applyDebugAction(frontend_->onStop(buildStopInfo("breakpoint", loc)),
                                 execStack.size());
                skipBreakOnce = true;
                return std::nullopt;
            }
            std::cerr << "[BREAK] fn=@" << fr.func->name << " blk=" << bb.label
                      << " reason=label\n";
            if (!script || script->empty()) {
                Slot s{};
                s.i64 = kDebugBreakpointSentinel;
                return s;
            }
            applyDebugAction(script->nextAction(), execStack.size());
            skipBreakOnce = true;
        }
        return std::nullopt;
    }
    if (debug.hasSrcLineBPs() && debug.shouldBreakOn(*in)) {
        if (frontend_) {
            applyDebugAction(frontend_->onStop(buildStopInfo("breakpoint", in->loc)),
                             execStack.size());
            skipBreakOnce = true;
            return std::nullopt;
        }
        const auto *sm = debug.getSourceManager();
        std::string path;
        if (sm && in->loc.hasFile())
            path = std::filesystem::path(sm->getPath(in->loc.file_id)).filename().string();
        std::cerr << "[BREAK] src=" << path;
        if (in->loc.hasLine()) {
            std::cerr << ':' << in->loc.line;
            if (in->loc.hasColumn())
                std::cerr << ':' << in->loc.column;
        }
        std::cerr << " fn=@" << fr.func->name << " blk=" << bb.label << " ip=#" << ip << "\n";
        if (!script || script->empty()) {
            Slot s{};
            s.i64 = kDebugBreakpointSentinel;
            return s;
        }
        applyDebugAction(script->nextAction(), execStack.size());
        skipBreakOnce = true;
    }
    return std::nullopt;
}

/// @brief Handle debugging-related bookkeeping before or after an instruction executes.
/// @details Prior to execution the helper enforces the global step limit,
///          performs parameter transfers when entering a block, and consults
///          @ref handleDebugBreak to honour label and source breakpoints.  After
///          execution it decrements the remaining step budget, halting when it
///          reaches zero and optionally re-arming the debugger script.  Whenever
///          a pause is requested a dedicated slot value is returned so the
///          interpreter loop can unwind gracefully.
/// @param st      Current execution state.
/// @param in      Instruction being processed, if any.
/// @param postExec Set to true when invoked after executing @p in.
/// @return Optional slot causing execution to pause; @c std::nullopt otherwise.
std::optional<Slot> VM::processDebugControl(ExecState &st, const Instr *in, bool postExec) {
    if (!postExec) {
        // Debug mode runs on the dispatch slow path, where the driver's own poll
        // (DISPATCH_AFTER) does not run; drive it here so a poll callback can drain
        // adapter commands and request an interactive Pause.
        if (pollCallback_ && pollEveryN_ && (++debugPollTick_ % pollEveryN_) == 0)
            (void)pollCallback_(*this);

        if (debugPauseRequested_ && frontend_) {
            debugPauseRequested_ = false;
            // Interactive Pause: surface a stop at the current instruction. The
            // frontend handles it like any other stop (emit + wait for a command).
            (void)pauseOrAdvanceDebugScript(st, "pause");
        }
        if (maxSteps && instrCount >= maxSteps) {
            std::cerr << "VM: step limit exceeded (" << maxSteps << "); aborting.\n";
            Slot s{};
            s.i64 = kDebugPauseSentinel;
            return s;
        }
        if (st.ip == 0 && st.bb) {
            transferBlockParams(st.fr, *st.bb);
            // Refresh the pre-resolved operand cache for the newly active block.
            st.blockCache = getOrBuildBlockCache(st.fr.func, st.bb);
        }
        if (st.ip == 0 && stepBudget == 0 && !st.skipBreakOnce) {
            if (auto br = handleDebugBreak(st.fr, *st.bb, st.ip, st.skipBreakOnce, nullptr))
                return br;
        } else if (st.skipBreakOnce) {
            st.skipBreakOnce = false;
        }
        if (in)
            if (auto br = handleDebugBreak(st.fr, *st.bb, st.ip, st.skipBreakOnce, in))
                return br;
        return std::nullopt;
    }
    if (stepBudget > 0) {
        --stepBudget;
        if (stepBudget == 0) {
            if (auto pause = pauseOrAdvanceDebugScript(st, "step"))
                return pause;
            return std::nullopt;
        }
    }
    if (debugStepMode_ != DebugStepMode::None) {
        debugStepArmed_ = true;
        const size_t depth = execStack.size();
        const bool shouldPause =
            (debugStepMode_ == DebugStepMode::StepOver && depth <= debugStepTargetDepth_) ||
            (debugStepMode_ == DebugStepMode::StepOut && depth <= debugStepTargetDepth_);
        if (debugStepArmed_ && shouldPause) {
            const std::string_view reason =
                debugStepMode_ == DebugStepMode::StepOver ? "step-over" : "step-out";
            debugStepMode_ = DebugStepMode::None;
            debugStepArmed_ = false;
            if (auto pause = pauseOrAdvanceDebugScript(st, reason))
                return pause;
        }
    }
    return std::nullopt;
}

} // namespace il::vm
