//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/runtime/RTTrapContractTests.cpp
// Purpose: Validate native runtime trap metadata and public API failure paths.
//
//===----------------------------------------------------------------------===//

#include "rt.hpp"
#include "rt_bytes.h"
#include "rt_cancellation.h"
#include "rt_dir.h"
#include "rt_network.h"
#include "rt_option.h"
#include "rt_result.h"
#include "rt_sb_bridge.h"
#include "rt_string_builder.h"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <setjmp.h>

namespace {

using TrapFn = void (*)();

struct StringBuilderObject {
    void *vptr;
    rt_string_builder builder;
};

static void expect_trap(TrapFn fn,
                        int64_t expected_kind,
                        int64_t expected_code,
                        const char *snippet) {
    jmp_buf env;
    rt_trap_set_recovery(&env);
    if (setjmp(env) == 0) {
        fn();
        rt_trap_clear_recovery();
        assert(false && "expected trap");
    }

    const char *message = rt_trap_get_error();
    assert(rt_trap_get_kind() == expected_kind);
    assert(rt_trap_get_code() == expected_code);
    assert(message != nullptr);
    if (snippet)
        assert(std::strstr(message, snippet) != nullptr);
    rt_trap_clear_recovery();
}

static void trap_network_error() {
    rt_trap_net("Network: connection failed", Err_NetworkError);
}

static void trap_divide_by_zero() {
    rt_trap_div0();
}

static void trap_cancellation_interrupt() {
    void *token = rt_cancellation_new();
    rt_cancellation_cancel(token);
    rt_cancellation_throw_if_cancelled(token);
}

static void trap_option_expect_null_message() {
    rt_option_expect(rt_option_none(), nullptr);
}

static void trap_result_expect_err_null_message() {
    rt_result_expect_err(rt_result_ok_i64(42), nullptr);
}

static void trap_stringbuilder_null_receiver() {
    (void)rt_text_sb_get_length(nullptr);
}

static void trap_stringbuilder_append_line_overflow() {
    StringBuilderObject obj{};
    rt_sb_init(&obj.builder);
    obj.builder.len = SIZE_MAX - 1;
    obj.builder.cap = SIZE_MAX;
    rt_text_sb_append_line(&obj, rt_const_cstr("x"));
}

static void trap_bytes_bounds() {
    void *bytes = rt_bytes_new(1);
    (void)rt_bytes_get(bytes, 1);
}

static void trap_dir_not_found() {
    char missing_path[96];
    std::snprintf(missing_path,
                  sizeof(missing_path),
                  "__viper_missing_dir_%p__",
                  static_cast<void *>(missing_path));
    rt_string path = rt_string_from_bytes(missing_path, std::strlen(missing_path));
    (void)rt_dir_entries_seq(path);
}

static void trap_url_null_receiver() {
    (void)rt_url_host(nullptr);
}

static void trap_url_invalid_port() {
    (void)rt_url_parse(rt_const_cstr("http://example.com:70000"));
}

} // namespace

int main() {
    expect_trap(trap_network_error,
                RT_TRAP_KIND_NETWORK_ERROR,
                Err_NetworkError,
                "Network: connection failed");
    expect_trap(trap_divide_by_zero, RT_TRAP_KIND_DIVIDE_BY_ZERO, 0, "division by zero");
    expect_trap(
        trap_cancellation_interrupt, RT_TRAP_KIND_INTERRUPT, 0, "cancellation was requested");
    expect_trap(trap_option_expect_null_message,
                RT_TRAP_KIND_INVALID_OPERATION,
                Err_InvalidOperation,
                "Option expect");
    expect_trap(trap_result_expect_err_null_message,
                RT_TRAP_KIND_INVALID_OPERATION,
                Err_InvalidOperation,
                "Result expect_err");
    expect_trap(trap_stringbuilder_null_receiver,
                RT_TRAP_KIND_INVALID_OPERATION,
                Err_InvalidOperation,
                "null receiver");
    expect_trap(trap_stringbuilder_append_line_overflow,
                RT_TRAP_KIND_OVERFLOW,
                Err_Overflow,
                "AppendLine overflow");
    expect_trap(trap_bytes_bounds, RT_TRAP_KIND_BOUNDS, Err_Bounds, "index out of bounds");
    expect_trap(
        trap_dir_not_found, RT_TRAP_KIND_FILE_NOT_FOUND, Err_FileNotFound, "directory not found");
    expect_trap(trap_url_null_receiver,
                RT_TRAP_KIND_INVALID_OPERATION,
                Err_InvalidOperation,
                "null receiver");
    expect_trap(trap_url_invalid_port, RT_TRAP_KIND_NETWORK_ERROR, Err_InvalidUrl, "parse URL");
    return 0;
}
