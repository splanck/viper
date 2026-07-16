//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tests/unit/runtime/TestRuntimeOpenLoadResultApis.cpp
// Purpose: Tests Result-returning connect/open/load APIs added by the runtime
//          public API overhaul.
//
//===----------------------------------------------------------------------===//

#include "tests/TestHarness.hpp"

#include <atomic>
#include <thread>
#include <vector>

extern "C" {
#include "rt_object.h"
#include "rt_pty.h"
#include "rt_result.h"
#include "rt_seq.h"
#include "rt_scene_editor.h"
#include "rt_string.h"
#include "rt_tls.h"
}

/// @brief Release a runtime object test handle when its reference count reaches zero.
/// @param obj Runtime object handle, or NULL.
static void release_obj(void *obj) {
    if (obj && rt_obj_release_check0(obj))
        rt_obj_free(obj);
}

/// @brief Assert that a Result is Err and carries a non-empty string message.
/// @param result Opaque Viper.Result object returned by a runtime API under test.
static void expect_err_with_message(void *result) {
    ASSERT_TRUE(result != nullptr);
    EXPECT_EQ(rt_result_is_err(result), 1);
    rt_string message = rt_result_unwrap_err_str(result);
    ASSERT_TRUE(message != nullptr);
    EXPECT_TRUE(rt_str_len(message) > 0);
}

TEST(RuntimeOpenLoadResultApis, TlsConnectResultWrapsInvalidArguments) {
    void *bad_port = rt_viper_tls_connect_result(rt_const_cstr("localhost"), 0);
    expect_err_with_message(bad_port);
    release_obj(bad_port);

    void *bad_timeout =
        rt_viper_tls_connect_for_result(rt_const_cstr("localhost"), 443, 2147483648LL);
    expect_err_with_message(bad_timeout);
    release_obj(bad_timeout);
}

TEST(RuntimeOpenLoadResultApis, PtyOpenResultWrapsValidationTraps) {
    void *result = rt_pty_open_result(rt_const_cstr(""), nullptr, nullptr, nullptr, 80, 24);
    expect_err_with_message(result);
    release_obj(result);
}

// VDOC-213: a PTY whose program fails to exec must NOT report a valid session.
// The exec-status pipe lets the parent observe the child's exec failure and
// return Err instead of an apparently-open session that only fails later.
TEST(RuntimeOpenLoadResultApis, PtyOpenSurfacesExecFailure) {
    if (!rt_pty_is_supported())
        return; // no PTY backend in this environment
    void *result = rt_pty_open_result(
        rt_const_cstr("/nonexistent/viper/pty/program/xyz"), nullptr, nullptr, nullptr, 80, 24);
    expect_err_with_message(result);
    release_obj(result);

    // A bare program name with an explicit environment resolves via PATH and
    // opens successfully (VDOC-213 PATH consistency).
    void *env = rt_seq_new();
    rt_seq_push(env, rt_const_cstr("PATH=/usr/bin:/bin"));
    void *ok = rt_pty_open_result(rt_const_cstr("sh"), nullptr, nullptr, env, 80, 24);
    EXPECT_EQ(rt_result_is_ok(ok), 1);

    // VDOC-214: Resize returns TRUE only when the backend actually applied the
    // new size. On a live session the TIOCSWINSZ succeeds, so Resize reports 1.
    void *session = rt_result_unwrap(ok);
    ASSERT_TRUE(session != nullptr);
    EXPECT_EQ(rt_pty_resize(session, 120, 40), 1);
    release_obj(ok);
}

// VDOC-215: the PTY LastError buffer is thread-local, so many threads triggering
// PTY errors and reading LastError concurrently never tear or clobber each
// other's diagnostic. Each thread reads back a non-empty error from its own
// failing open.
TEST(RuntimeOpenLoadResultApis, PtyLastErrorIsThreadLocal) {
    std::atomic<bool> ok{true};
    std::vector<std::thread> workers;
    for (int t = 0; t < 8; ++t) {
        workers.emplace_back([&]() {
            for (int i = 0; i < 500; ++i) {
                // An empty program is always a validation error, independent of
                // PTY backend availability.
                void *res = rt_pty_open_result(rt_const_cstr(""), nullptr, nullptr, nullptr, 80, 24);
                release_obj(res);
                rt_string err = rt_pty_last_error();
                // This thread just failed, so its own LastError is non-empty and
                // a readable string (no torn write from another thread).
                if (!err || rt_str_len(err) <= 0)
                    ok.store(false);
                if (err)
                    rt_string_unref(err);
            }
        });
    }
    for (auto &w : workers)
        w.join();
    EXPECT_EQ(ok.load() ? 1 : 0, 1);
}

TEST(RuntimeOpenLoadResultApis, SceneDocumentLoadJsonResultWrapsDiagnostics) {
    void *scene = rt_game_scene_new(4, 3, 16, 16);
    ASSERT_TRUE(scene != nullptr);
    rt_string json = rt_game_scene_to_json(scene);
    ASSERT_TRUE(json != nullptr);

    void *ok = rt_game_scene_load_json_result(json);
    ASSERT_TRUE(ok != nullptr);
    EXPECT_EQ(rt_result_is_ok(ok), 1);
    void *loaded = rt_result_unwrap(ok);
    ASSERT_TRUE(loaded != nullptr);
    EXPECT_EQ(rt_game_scene_get_width(loaded), 4);
    EXPECT_EQ(rt_game_scene_get_height(loaded), 3);

    release_obj(ok);
    rt_string_unref(json);
    release_obj(scene);

    void *err = rt_game_scene_load_json_result(rt_const_cstr("{"));
    expect_err_with_message(err);
    release_obj(err);
}

TEST(RuntimeOpenLoadResultApis, SceneDocumentLoadResultWrapsMissingFile) {
    void *err =
        rt_game_scene_load_file_result(rt_const_cstr("definitely_missing_scene_12345.scene"));
    expect_err_with_message(err);
    release_obj(err);
}

int main() {
    return viper_test::run_all_tests();
}
