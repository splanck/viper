//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
// File: src/runtime/core/rt_debug_protocol.cpp
// Purpose: Headless debug protocol session model for IDE debugger integration.
//
//===----------------------------------------------------------------------===//

#include "rt_debug_protocol.h"

#include "rt_map.h"
#include "rt_object.h"
#include "rt_seq.h"
#include "rt_string.h"
#include "rt_trap.h"

#include <algorithm>
#include <cctype>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace {

std::string toStd(rt_string s) {
    if (!s)
        return {};
    const char *data = rt_string_cstr(s);
    int64_t len = rt_str_len(s);
    if (!data || len <= 0)
        return {};
    return std::string(data, static_cast<size_t>(len));
}

rt_string makeString(const std::string &value) {
    return rt_string_from_bytes(value.data(), value.size());
}

void releaseObject(void *obj) {
    if (obj && rt_obj_release_check0(obj))
        rt_obj_free(obj);
}

void mapSetStr(void *map, const char *key, const std::string &value) {
    rt_string k = rt_const_cstr(key);
    rt_string s = makeString(value);
    rt_map_set_str(map, k, s);
    rt_string_unref(s);
    rt_string_unref(k);
}

void mapSetInt(void *map, const char *key, int64_t value) {
    rt_string k = rt_const_cstr(key);
    rt_map_set_int(map, k, value);
    rt_string_unref(k);
}

std::string trim(std::string s) {
    size_t first = 0;
    while (first < s.size() && std::isspace(static_cast<unsigned char>(s[first])))
        first++;
    size_t last = s.size();
    while (last > first && std::isspace(static_cast<unsigned char>(s[last - 1])))
        last--;
    return s.substr(first, last - first);
}

struct DebugState {
    std::string path;
    std::vector<std::string> lines;
    std::map<std::string, std::set<int64_t>> breakpoints;
    std::map<std::string, std::string> locals;
    std::vector<void *> events;
    int64_t pc{1};
    bool running{false};
    bool terminated{false};
};

struct DebugHandle {
    DebugState *state{nullptr};
};

DebugHandle *requireSession(void *session) {
    if (!session || rt_obj_class_id(session) != RT_DEBUG_PROTOCOL_SESSION_CLASS_ID)
        rt_trap("Debug.Protocol: invalid session");
    auto *h = static_cast<DebugHandle *>(session);
    if (!h->state)
        rt_trap("Debug.Protocol: destroyed session");
    return h;
}

void finalizer(void *obj) {
    auto *h = static_cast<DebugHandle *>(obj);
    if (h->state) {
        for (void *event : h->state->events)
            releaseObject(event);
        delete h->state;
        h->state = nullptr;
    }
}

std::vector<std::string> splitLines(const std::string &source) {
    std::vector<std::string> lines;
    std::stringstream ss(source);
    std::string line;
    while (std::getline(ss, line)) {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        lines.push_back(line);
    }
    if (lines.empty())
        lines.push_back("");
    return lines;
}

void *makeEvent(const std::string &type,
                const std::string &reason,
                const std::string &path,
                int64_t line) {
    void *event = rt_map_new();
    mapSetStr(event, "type", type);
    mapSetStr(event, "reason", reason);
    mapSetStr(event, "path", path);
    mapSetInt(event, "line", line);
    return event;
}

void recordEvent(DebugState &s, void *event) {
    rt_obj_retain_maybe(event);
    s.events.push_back(event);
}

bool hasBreakpoint(DebugState &s, int64_t line) {
    auto it = s.breakpoints.find(s.path);
    return it != s.breakpoints.end() && it->second.count(line) != 0;
}

void executeLine(DebugState &s) {
    if (s.pc < 1 || s.pc > static_cast<int64_t>(s.lines.size()))
        return;
    std::string line = trim(s.lines[static_cast<size_t>(s.pc - 1)]);
    if (line.rfind("var ", 0) == 0 || line.rfind("let ", 0) == 0) {
        line = trim(line.substr(4));
        size_t eq = line.find('=');
        if (eq != std::string::npos) {
            std::string name = trim(line.substr(0, eq));
            std::string value = trim(line.substr(eq + 1));
            if (!value.empty() && value.back() == ';')
                value.pop_back();
            s.locals[name] = trim(value);
        }
    }
}

void *runUntilStop(DebugState &s, bool singleStep) {
    if (s.terminated)
        return makeEvent("terminated", "already_terminated", s.path, s.pc);
    s.running = true;
    if (s.pc < 1)
        s.pc = 1;
    while (s.pc <= static_cast<int64_t>(s.lines.size())) {
        if (!singleStep && hasBreakpoint(s, s.pc)) {
            s.running = false;
            void *event = makeEvent("stopped", "breakpoint", s.path, s.pc);
            recordEvent(s, event);
            return event;
        }
        if (trim(s.lines[static_cast<size_t>(s.pc - 1)]).find("crash") != std::string::npos) {
            s.running = false;
            s.terminated = true;
            void *event = makeEvent("terminated", "crash", s.path, s.pc);
            recordEvent(s, event);
            return event;
        }
        executeLine(s);
        s.pc++;
        if (singleStep) {
            s.running = false;
            void *event = makeEvent("stopped", "step", s.path, s.pc);
            recordEvent(s, event);
            return event;
        }
    }
    s.running = false;
    s.terminated = true;
    void *event = makeEvent("terminated", "exit", s.path, s.pc);
    recordEvent(s, event);
    return event;
}

} // namespace

extern "C" {

void *rt_debug_protocol_session_new(void) {
    auto *h = static_cast<DebugHandle *>(
        rt_obj_new_i64(RT_DEBUG_PROTOCOL_SESSION_CLASS_ID, sizeof(DebugHandle)));
    h->state = new DebugState();
    rt_obj_set_finalizer(h, finalizer);
    return h;
}

void rt_debug_protocol_set_breakpoint(void *session, rt_string path, int64_t line) {
    auto *h = requireSession(session);
    if (line > 0)
        h->state->breakpoints[toStd(path)].insert(line);
}

void rt_debug_protocol_clear_breakpoints(void *session, rt_string path) {
    requireSession(session)->state->breakpoints.erase(toStd(path));
}

void *rt_debug_protocol_launch(void *session, rt_string path, rt_string source) {
    auto *h = requireSession(session);
    DebugState &s = *h->state;
    for (void *event : s.events)
        releaseObject(event);
    s.events.clear();
    s.path = toStd(path);
    s.lines = splitLines(toStd(source));
    s.locals.clear();
    s.pc = 1;
    s.terminated = false;
    void *launched = makeEvent("initialized", "launch", s.path, 1);
    recordEvent(s, launched);
    releaseObject(launched);
    return runUntilStop(s, false);
}

void *rt_debug_protocol_continue(void *session) {
    auto *h = requireSession(session);
    if (hasBreakpoint(*h->state, h->state->pc))
        h->state->pc++;
    return runUntilStop(*h->state, false);
}

void *rt_debug_protocol_step_over(void *session) {
    return runUntilStop(*requireSession(session)->state, true);
}

void *rt_debug_protocol_pause(void *session) {
    auto *h = requireSession(session);
    h->state->running = false;
    void *event = makeEvent("stopped", "pause", h->state->path, h->state->pc);
    recordEvent(*h->state, event);
    return event;
}

void *rt_debug_protocol_terminate(void *session) {
    auto *h = requireSession(session);
    h->state->running = false;
    h->state->terminated = true;
    void *event = makeEvent("terminated", "terminated", h->state->path, h->state->pc);
    recordEvent(*h->state, event);
    return event;
}

void *rt_debug_protocol_stack_frames(void *session) {
    auto *h = requireSession(session);
    void *seq = rt_seq_new_owned();
    void *frame = rt_map_new();
    mapSetInt(frame, "id", 0);
    mapSetStr(frame, "name", "start");
    mapSetStr(frame, "path", h->state->path);
    mapSetInt(frame, "line", h->state->pc);
    rt_seq_push(seq, frame);
    releaseObject(frame);
    return seq;
}

void *rt_debug_protocol_locals(void *session) {
    auto *h = requireSession(session);
    void *seq = rt_seq_new_owned();
    for (const auto &[name, value] : h->state->locals) {
        void *local = rt_map_new();
        mapSetStr(local, "name", name);
        mapSetStr(local, "value", value);
        mapSetStr(local, "type", "inferred");
        rt_seq_push(seq, local);
        releaseObject(local);
    }
    return seq;
}

void *rt_debug_protocol_events(void *session) {
    auto *h = requireSession(session);
    void *seq = rt_seq_new_owned();
    for (void *event : h->state->events)
        rt_seq_push(seq, event);
    return seq;
}

int8_t rt_debug_protocol_is_running(void *session) {
    return requireSession(session)->state->running ? 1 : 0;
}

} // extern "C"
