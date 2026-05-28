//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
// File: src/tests/runtime/RTDebugProtocolTests.cpp
// Purpose: Tests for the headless debug protocol session model.
//
//===----------------------------------------------------------------------===//

#include "rt_debug_protocol.h"

#include "rt_internal.h"
#include "rt_map.h"
#include "rt_seq.h"
#include "rt_string.h"

#include <cassert>
#include <string>

extern "C" void vm_trap(const char *msg) {
    rt_abort(msg);
}

static std::string get_str(void *map, const char *key) {
    rt_string value = rt_map_get_str(map, rt_const_cstr(key));
    std::string out(rt_string_cstr(value), (size_t)rt_str_len(value));
    rt_string_unref(value);
    return out;
}

int main() {
    void *session = rt_debug_protocol_session_new();
    rt_debug_protocol_set_breakpoint(session, rt_const_cstr("main.zia"), 2);
    void *event = rt_debug_protocol_launch(
        session,
        rt_const_cstr("main.zia"),
        rt_const_cstr("var before = 1;\nvar answer = 42;\nvar after = answer;\n"));
    assert(get_str(event, "type") == "stopped");
    assert(get_str(event, "reason") == "breakpoint");
    assert(rt_map_get_int(event, rt_const_cstr("line")) == 2);

    event = rt_debug_protocol_step_over(session);
    assert(get_str(event, "reason") == "step");
    void *locals = rt_debug_protocol_locals(session);
    assert(rt_seq_len(locals) == 2);
    void *frames = rt_debug_protocol_stack_frames(session);
    assert(rt_seq_len(frames) == 1);
    assert(rt_map_get_int(rt_seq_get(frames, 0), rt_const_cstr("line")) == 3);

    event = rt_debug_protocol_continue(session);
    assert(get_str(event, "type") == "terminated");
    assert(get_str(event, "reason") == "exit");
    assert(rt_debug_protocol_is_running(session) == 0);
    assert(rt_seq_len(rt_debug_protocol_events(session)) >= 3);

    event = rt_debug_protocol_launch(
        session, rt_const_cstr("crash.zia"), rt_const_cstr("var ok = 1;\ncrash\n"));
    assert(get_str(event, "type") == "terminated");
    assert(get_str(event, "reason") == "crash");
    return 0;
}
