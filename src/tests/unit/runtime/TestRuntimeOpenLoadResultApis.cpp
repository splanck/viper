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

extern "C" {
#include "rt_object.h"
#include "rt_pty.h"
#include "rt_result.h"
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
