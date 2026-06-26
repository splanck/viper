//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tools/viper/DebugAdapter.cpp
// Purpose: Interactive VM-backed debug adapter for ViperIDE — breakpoints, stop,
//          and step control over a newline-JSON protocol.
// Key invariants:
//   - Events are emitted as "@@VDBG@@ <compact-json>\n" on stderr; the debuggee's
//     own stdout/stderr remain its own.
//   - Commands arrive as newline-delimited JSON on stdin.
// Links: src/tools/viper/DebugAdapter.hpp, include/viper/vm/debug/DebugFrontend.hpp
//
//===----------------------------------------------------------------------===//
#include "tools/viper/DebugAdapter.hpp"

#include "il/core/Module.hpp"
#include "support/source_manager.hpp"
#include "tools/lsp-common/Json.hpp"
#include "tools/viper/DebugExpr.hpp"
#include "viper/vm/VM.hpp"
#include "viper/vm/debug/DebugFrontend.hpp"

#include <condition_variable>
#include <cstdint>
#include <cstdlib>
#include <deque>
#include <iostream>
#include <limits>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>

namespace il::tools::debug {
namespace {

using viper::server::JsonValue;

/// @brief Sentinel marking a control-protocol line on stderr, keeping it distinct
///        from any raw stderr the debuggee writes.
constexpr const char *kSentinel = "@@VDBG@@ ";

/// @brief Trailing path component, for tolerant file matching (the adapter reports
///        canonical paths while the IDE sends its own spelling).
std::string baseNameOf(const std::string &p) {
    const size_t s = p.find_last_of("/\\");
    return s == std::string::npos ? p : p.substr(s + 1);
}

/// @brief Per-breakpoint condition / logpoint message. Plain breakpoints have no
///        entry; conditional/logpoints are keyed by basename:line.
struct BpMeta {
    std::string condition;
    std::string logMessage;
};
using BpMetaMap = std::unordered_map<std::string, BpMeta>;

std::string metaKey(const std::string &path, uint32_t line) {
    return baseNameOf(path) + ":" + std::to_string(line);
}

/// @brief Apply a setBreakpoints command's optional `meta` to @p map, replacing any
///        prior condition/logpoint for the lines it lists (so a cleared condition
///        drops out). Lines without a meta entry hold no entry (plain breakpoint).
void applyBpMeta(BpMetaMap &map, const JsonValue &cmd) {
    const std::string &path = cmd["path"].asString();
    if (const JsonValue *lines = cmd.get("lines")) {
        for (const auto &ln : lines->asArray())
            map.erase(metaKey(path, static_cast<uint32_t>(ln.asInt())));
    }
    if (const JsonValue *meta = cmd.get("meta")) {
        for (const auto &e : meta->asArray()) {
            const auto line = static_cast<uint32_t>(e["line"].asInt());
            BpMeta bm;
            if (const JsonValue *c = e.get("condition"))
                bm.condition = c->asString();
            if (const JsonValue *l = e.get("logMessage"))
                bm.logMessage = l->asString();
            if (!bm.condition.empty() || !bm.logMessage.empty())
                map[metaKey(path, line)] = std::move(bm);
        }
    }
}

/// @brief Build a logpoint output event (adapter -> IDE).
JsonValue logEvent(uint32_t line, const std::string &message) {
    return JsonValue::object({
        {"type", JsonValue("log")},
        {"line", JsonValue(static_cast<int64_t>(line))},
        {"message", JsonValue(message)},
    });
}

/// @brief Newline-delimited JSON control channel: events out on stderr (sentinel
///        prefixed), commands in on stdin. A background reader thread parses stdin
///        into a queue so commands can be consumed blockingly (while stopped) or
///        non-blockingly (a poll callback checking for Pause while running).
class DebugChannel {
  public:
    DebugChannel() : reader_([this] { readerLoop(); }) {}
    ~DebugChannel() { reader_.detach(); } // the process exits via _Exit; no join

    void emit(const JsonValue &event) {
        std::lock_guard<std::mutex> lock(outMutex_);
        std::cerr << kSentinel << event.toCompactString() << "\n";
        std::cerr.flush();
    }

    /// @brief Block for the next command (used while the debuggee is stopped).
    std::optional<JsonValue> readCommand() {
        std::unique_lock<std::mutex> lock(mutex_);
        cv_.wait(lock, [this] { return !queue_.empty() || eof_; });
        if (queue_.empty())
            return std::nullopt;
        JsonValue cmd = std::move(queue_.front());
        queue_.pop_front();
        return cmd;
    }

    /// @brief Pop the next queued command without blocking, or nullopt if none
    ///        (used by the poll callback while the debuggee is running).
    std::optional<JsonValue> tryReadCommand() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (queue_.empty())
            return std::nullopt;
        JsonValue cmd = std::move(queue_.front());
        queue_.pop_front();
        return cmd;
    }

  private:
    void readerLoop() {
        std::string line;
        while (std::getline(std::cin, line)) {
            if (line.empty())
                continue;
            try {
                JsonValue cmd = JsonValue::parse(line);
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    queue_.push_back(std::move(cmd));
                }
                cv_.notify_one();
            } catch (const std::exception &) {
                // Ignore an unparseable line.
            }
        }
        {
            std::lock_guard<std::mutex> lock(mutex_);
            eof_ = true;
        }
        cv_.notify_one();
    }

    std::thread reader_;
    std::mutex mutex_;
    std::mutex outMutex_;
    std::condition_variable cv_;
    std::deque<JsonValue> queue_;
    bool eof_ = false;
};

JsonValue stoppedEvent(const il::vm::DebugStopInfo &info) {
    JsonValue::ArrayType frames;
    frames.reserve(info.frames.size());
    for (const auto &f : info.frames) {
        frames.push_back(JsonValue::object({
            {"function", JsonValue(f.function)},
            {"path", JsonValue(f.path)},
            {"line", JsonValue(static_cast<int64_t>(f.line))},
        }));
    }
    JsonValue::ArrayType locals;
    locals.reserve(info.locals.size());
    for (const auto &l : info.locals) {
        locals.push_back(JsonValue::object({
            {"name", JsonValue(l.name)},
            {"value", JsonValue(l.value)},
            {"type", JsonValue(l.type)},
        }));
    }
    return JsonValue::object({
        {"type", JsonValue("stopped")},
        {"reason", JsonValue(info.reason)},
        {"path", JsonValue(info.path)},
        {"line", JsonValue(static_cast<int64_t>(info.line))},
        {"column", JsonValue(static_cast<int64_t>(info.column))},
        {"frames", JsonValue::array(std::move(frames))},
        {"locals", JsonValue::array(std::move(locals))},
    });
}

JsonValue terminatedEvent(const char *reason, int exitCode) {
    return JsonValue::object({
        {"type", JsonValue("terminated")},
        {"reason", JsonValue(reason)},
        {"exitCode", JsonValue(static_cast<int64_t>(exitCode))},
    });
}

/// @brief Resolve a watch/evaluate expression against the current stop. v1 is a
///        name lookup over the top frame's locals (side-effect free); an
///        unresolved name returns ok=false so the IDE can show "not in scope".
JsonValue evaluatedEvent(const il::vm::DebugStopInfo &info, const std::string &expr) {
    for (const auto &l : info.locals) {
        if (l.name == expr) {
            return JsonValue::object({
                {"type", JsonValue("evaluated")},
                {"expr", JsonValue(expr)},
                {"value", JsonValue(l.value)},
                {"valueType", JsonValue(l.type)},
                {"ok", JsonValue(true)},
            });
        }
    }
    return JsonValue::object({
        {"type", JsonValue("evaluated")},
        {"expr", JsonValue(expr)},
        {"value", JsonValue("")},
        {"valueType", JsonValue("")},
        {"ok", JsonValue(false)},
    });
}

/// @brief DebugFrontend that serializes each stop and blocks for the IDE's next
///        command. The VM thread is paused for the duration of onStop.
/// @details Implements source-LINE stepping on top of the VM's instruction-level
///          step machinery: while a step keeps landing on the same source line it
///          auto-issues another step without notifying the IDE, surfacing a stop
///          only when the line changes (or a breakpoint intervenes).
class AdapterFrontend : public il::vm::DebugFrontend {
  public:
    AdapterFrontend(DebugChannel &chan, BpMetaMap meta)
        : chan_(chan), bpMeta_(std::move(meta)) {}

    il::vm::DebugAction onStop(const il::vm::DebugStopInfo &info) override {
        // Run-to-cursor: keep stepping over until the target line is reached. A
        // real breakpoint or exception cancels it; the step bound guards against a
        // target that is never hit (then it behaves like Continue-to-end).
        if (runToActive_) {
            const bool hitTarget =
                info.line == runToLine_ && baseNameOf(info.path) == baseNameOf(runToPath_);
            const bool interrupted = info.reason == "breakpoint" || info.reason == "exception";
            if (!hitTarget && !interrupted && info.line != 0 && runToSteps_ < kMaxRunToSteps) {
                ++runToSteps_;
                return {il::vm::DebugActionKind::StepOver, 0};
            }
            runToActive_ = false;
            runToSteps_ = 0;
            mode_ = StepMode::None;
            autoSteps_ = 0;
            // fall through to surface the stop at the current location
        }

        // Continue an in-progress line step while still on the origin line. Step-out
        // always surfaces its first pause (it lands in the caller). A breakpoint or
        // an unhandled trap always surfaces, even mid-step.
        if (mode_ != StepMode::None && info.reason != "breakpoint" &&
            info.reason != "exception") {
            const bool sameLine = info.line == originLine_ && info.path == originPath_;
            if (mode_ != StepMode::Out && sameLine && info.line != 0 &&
                autoSteps_ < kMaxAutoSteps) {
                ++autoSteps_;
                return stepAction(mode_);
            }
        }
        mode_ = StepMode::None;
        autoSteps_ = 0;

        // Conditional breakpoints / logpoints (breakpoint stops only — stepping onto
        // such a line always stops). A logpoint emits a message and resumes; a
        // conditional stop is suppressed when the condition is falsey.
        if (info.reason == "breakpoint") {
            auto it = bpMeta_.find(metaKey(info.path, info.line));
            if (it != bpMeta_.end()) {
                auto resolve = [&info](const std::string &name, std::string &val,
                                       std::string &ty) -> bool {
                    for (const auto &l : info.locals) {
                        if (l.name == name) {
                            val = l.value;
                            ty = l.type;
                            return true;
                        }
                    }
                    return false;
                };
                if (!it->second.logMessage.empty()) {
                    chan_.emit(logEvent(
                        info.line, viper::dbgexpr::interpolate(it->second.logMessage, resolve)));
                    return {il::vm::DebugActionKind::Continue, 0};
                }
                if (!it->second.condition.empty() &&
                    !viper::dbgexpr::conditionHolds(it->second.condition, resolve)) {
                    return {il::vm::DebugActionKind::Continue, 0};
                }
            }
        }

        chan_.emit(stoppedEvent(info));
        for (;;) {
            auto cmd = chan_.readCommand();
            if (!cmd)
                std::_Exit(0); // IDE/pipe gone: stop debugging immediately.
            const std::string &type = (*cmd)["type"].asString();
            if (type == "continue")
                return {il::vm::DebugActionKind::Continue, 0};
            if (type == "stepOver")
                return beginStep(StepMode::Over, info);
            if (type == "stepIn")
                return beginStep(StepMode::In, info);
            if (type == "stepOut")
                return beginStep(StepMode::Out, info);
            if (type == "runToLine") {
                runToActive_ = true;
                runToPath_ = (*cmd)["path"].asString();
                runToLine_ = static_cast<uint32_t>((*cmd)["line"].asInt());
                runToSteps_ = 0;
                return {il::vm::DebugActionKind::StepOver, 0};
            }
            if (type == "setBreakpoints") {
                // Mid-session edit of conditions/logpoints. The breakpoint line set
                // itself is fixed at launch; adding/removing lines while running is
                // future dynamic-breakpoint work.
                applyBpMeta(bpMeta_, *cmd);
                continue;
            }
            if (type == "evaluate") {
                // Watch/hover query: report the value and stay stopped.
                chan_.emit(evaluatedEvent(info, (*cmd)["expr"].asString()));
                continue;
            }
            if (type == "terminate") {
                chan_.emit(terminatedEvent("terminated", 0));
                std::_Exit(0);
            }
            // "pause" while already stopped, or any unknown command: keep waiting.
        }
    }

  private:
    enum class StepMode { None, Over, In, Out };

    il::vm::DebugAction beginStep(StepMode mode, const il::vm::DebugStopInfo &info) {
        mode_ = mode;
        originLine_ = info.line;
        originPath_ = info.path;
        autoSteps_ = 0;
        return stepAction(mode);
    }

    static il::vm::DebugAction stepAction(StepMode mode) {
        switch (mode) {
            case StepMode::In:
                return {il::vm::DebugActionKind::Step, 1};
            case StepMode::Over:
                return {il::vm::DebugActionKind::StepOver, 0};
            case StepMode::Out:
                return {il::vm::DebugActionKind::StepOut, 0};
            default:
                return {il::vm::DebugActionKind::Continue, 0};
        }
    }

    /// @brief Safety bound on per-line auto-steps so a degenerate line cannot spin.
    static constexpr int kMaxAutoSteps = 200000;
    /// @brief Safety bound on run-to-cursor steps (it may cross many lines).
    static constexpr int kMaxRunToSteps = 5000000;

    DebugChannel &chan_;
    StepMode mode_ = StepMode::None;
    uint32_t originLine_ = 0;
    std::string originPath_;
    int autoSteps_ = 0;

    // Run-to-cursor: step over until reaching (runToPath_, runToLine_), or until an
    // existing breakpoint/exception intervenes, or the step bound is exhausted.
    bool runToActive_ = false;
    uint32_t runToLine_ = 0;
    std::string runToPath_;
    int runToSteps_ = 0;

    // Conditional-breakpoint / logpoint metadata, keyed by basename:line.
    BpMetaMap bpMeta_;
};

} // namespace

int runDebugAdapter(il::core::Module &module,
                    const std::vector<std::string> &programArgs,
                    uint64_t maxSteps,
                    il::support::SourceManager &sm) {
    DebugChannel chan;
    chan.emit(JsonValue::object({
        {"type", JsonValue("initialized")},
        {"reason", JsonValue("launch")},
    }));

    il::vm::RunConfig runCfg;
    runCfg.trace.sm = &sm;
    runCfg.maxSteps = maxSteps;
    runCfg.programArgs = programArgs;
    runCfg.debug.setSourceManager(&sm);

    // Handshake: accept breakpoints, then wait for an explicit launch so the IDE
    // can install breakpoints before the program runs.
    BpMetaMap initialMeta;
    bool launched = false;
    while (!launched) {
        auto cmd = chan.readCommand();
        if (!cmd)
            return 0; // IDE gone before launch.
        const std::string &type = (*cmd)["type"].asString();
        if (type == "setBreakpoints") {
            const std::string &path = (*cmd)["path"].asString();
            for (const auto &ln : (*cmd)["lines"].asArray()) {
                const int64_t line = ln.asInt();
                if (line > 0)
                    runCfg.debug.addBreakSrcLine(path, static_cast<uint32_t>(line));
            }
            applyBpMeta(initialMeta, *cmd);
        } else if (type == "launch") {
            launched = true;
        } else if (type == "terminate") {
            chan.emit(terminatedEvent("terminated", 0));
            return 0;
        }
    }

    AdapterFrontend frontend(chan, std::move(initialMeta));
    runCfg.frontend = &frontend;

    // While the program runs freely (between stops), poll for an interactive Pause
    // or a terminate. requestDebugPause makes the frontend stop at the next
    // instruction without ending the run, so execution can resume.
    runCfg.interruptEveryN = 20000;
    runCfg.pollCallback = [&chan](il::vm::VM &vm) -> bool {
        while (auto cmd = chan.tryReadCommand()) {
            const std::string &type = (*cmd)["type"].asString();
            if (type == "pause") {
                il::vm::requestDebugPause(vm);
                // Stop draining so the pause stop materializes before any queued
                // follow-up command (e.g. terminate) is handled at the stop.
                break;
            }
            if (type == "terminate") {
                chan.emit(terminatedEvent("terminated", 0));
                std::_Exit(0);
            }
            // Other commands have no meaning while running; ignore them.
        }
        return true; // keep running
    };

    il::vm::Runner runner(module, std::move(runCfg));
    const int64_t runResult = runner.run();

    int rc = 0;
    const auto intMin = static_cast<int64_t>(std::numeric_limits<int>::min());
    const auto intMax = static_cast<int64_t>(std::numeric_limits<int>::max());
    if (runResult < intMin || runResult > intMax)
        rc = 1;
    else
        rc = static_cast<int>(runResult);

    const auto trapMessage = runner.lastTrapMessage();
    const char *reason = "exit";
    if (trapMessage && !trapMessage->empty()) {
        reason = "crash";
        std::cerr << *trapMessage;
        if (trapMessage->back() != '\n')
            std::cerr << '\n';
        if (rc == 0)
            rc = 1;
    }

    chan.emit(terminatedEvent(reason, rc));
    return rc;
}

} // namespace il::tools::debug
