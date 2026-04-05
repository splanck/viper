//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tests/runtime/RTAudioUnavailableTests.cpp
// Purpose: Verify that audio stub builds fail loudly for load/play operations
//          instead of silently returning fake success values.
//
//===----------------------------------------------------------------------===//

#include "rt.hpp"
#include "rt_audio.h"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <setjmp.h>

#ifndef VIPER_ENABLE_AUDIO
namespace {

using TrapFn = void (*)();

static void expect_invalid_operation(TrapFn fn, const char *snippet) {
    jmp_buf env;
    rt_trap_set_recovery(&env);
    if (setjmp(env) == 0) {
        fn();
        rt_trap_clear_recovery();
        assert(false && "expected trap");
    }

    const char *message = rt_trap_get_error();
    assert(rt_trap_get_kind() == RT_TRAP_KIND_INVALID_OPERATION);
    assert(rt_trap_get_code() == Err_InvalidOperation);
    assert(message != nullptr);
    assert(std::strstr(message, snippet) != nullptr);
    rt_trap_clear_recovery();
}

static void trap_sound_load() {
    (void)rt_sound_load(rt_const_cstr("missing.wav"));
}

static void trap_sound_play() {
    (void)rt_sound_play(reinterpret_cast<void *>(1));
}

static void trap_music_load() {
    (void)rt_music_load(rt_const_cstr("missing.ogg"));
}

static void trap_music_play() {
    rt_music_play(reinterpret_cast<void *>(1), 0);
}

} // namespace
#endif

int main() {
#ifdef VIPER_ENABLE_AUDIO
    assert(rt_audio_is_available() == 1);
    std::printf("SKIP: audio enabled in this build\n");
    return 0;
#else
    assert(rt_audio_is_available() == 0);
    expect_invalid_operation(trap_sound_load, "not compiled in");
    expect_invalid_operation(trap_sound_play, "not compiled in");
    expect_invalid_operation(trap_music_load, "not compiled in");
    expect_invalid_operation(trap_music_play, "not compiled in");
    std::printf("All audio-unavailable tests passed.\n");
    return 0;
#endif
}
