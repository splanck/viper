//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
// File: src/runtime/core/rt_debug_protocol.h
// Purpose: Headless debug protocol session model for IDE debugger integration.
//
//===----------------------------------------------------------------------===//

#pragma once

#include "rt_string.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RT_DEBUG_PROTOCOL_SESSION_CLASS_ID INT64_C(-0x444250524f544f)

void *rt_debug_protocol_session_new(void);
void rt_debug_protocol_set_breakpoint(void *session, rt_string path, int64_t line);
void rt_debug_protocol_clear_breakpoints(void *session, rt_string path);
void *rt_debug_protocol_launch(void *session, rt_string path, rt_string source);
void *rt_debug_protocol_continue(void *session);
void *rt_debug_protocol_step_over(void *session);
void *rt_debug_protocol_pause(void *session);
void *rt_debug_protocol_terminate(void *session);
void *rt_debug_protocol_stack_frames(void *session);
void *rt_debug_protocol_locals(void *session);
void *rt_debug_protocol_events(void *session);
int8_t rt_debug_protocol_is_running(void *session);

#ifdef __cplusplus
}
#endif
