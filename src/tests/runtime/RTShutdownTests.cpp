//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tests/runtime/RTShutdownTests.cpp
// Purpose: Verify that the runtime shutdown path correctly runs finalizers
//          on GC-tracked objects and cleans up the legacy context.
// Key invariants:
//   - rt_gc_run_all_finalizers invokes all registered finalizers exactly once
//   - Finalizer pointers are cleared after invocation (no double-finalize)
//   - rt_legacy_context_shutdown cleans up file state
// Ownership/Lifetime:
//   - Test objects are heap-allocated via rt_obj_new_i64; lifetimes managed
//     by the test.
// Links: src/runtime/core/rt_gc.c, src/runtime/core/rt_context.c
//
//===----------------------------------------------------------------------===//

#include "rt.hpp"
#include "runtime/audio/rt_audio.h"
#include "runtime/core/rt_context.h"
#include "runtime/core/rt_gc.h"
#include "runtime/core/rt_heap.h"
#include "runtime/oop/rt_object.h"
#include "rt_string.h"

#include <cassert>
#include <cstdio>
#include <cstring>

// ── vm_trap override ────────────────────────────────────────────────────────
// Prevent process exit on trap during tests.
static int g_trap_count = 0;

extern "C" void vm_trap(const char *msg) {
    (void)msg;
    g_trap_count++;
}

// ── Finalizer tracking ──────────────────────────────────────────────────────

static int g_fin_a_count = 0;
static int g_fin_b_count = 0;
static int g_fin_c_count = 0;

static void finalizer_a(void *obj) {
    (void)obj;
    g_fin_a_count++;
}

static void finalizer_b(void *obj) {
    (void)obj;
    g_fin_b_count++;
}

static void finalizer_c(void *obj) {
    (void)obj;
    g_fin_c_count++;
}

// No-op GC traverse (objects have no child references)
static void noop_traverse(void *obj, rt_gc_visitor_t visitor, void *ctx) {
    (void)obj;
    (void)visitor;
    (void)ctx;
}

static rt_string make_str(const char *s) {
    return rt_string_from_bytes(s, strlen(s));
}

static int write_test_wav_frames(const char *path, uint32_t sample_rate, uint32_t frame_count) {
    FILE *f = fopen(path, "wb");
    if (!f)
        return 0;

    fwrite("RIFF", 1, 4, f);
    uint32_t data_sz = frame_count * 2;
    uint32_t riff_sz = 36 + data_sz;
    fwrite(&riff_sz, 4, 1, f);
    fwrite("WAVE", 1, 4, f);

    fwrite("fmt ", 1, 4, f);
    uint32_t fmt_sz = 16;
    fwrite(&fmt_sz, 4, 1, f);
    uint16_t audio_fmt = 1;
    fwrite(&audio_fmt, 2, 1, f);
    uint16_t channels = 1;
    fwrite(&channels, 2, 1, f);
    fwrite(&sample_rate, 4, 1, f);
    uint32_t byte_rate = sample_rate * 2;
    fwrite(&byte_rate, 4, 1, f);
    uint16_t block_align = 2;
    fwrite(&block_align, 2, 1, f);
    uint16_t bits = 16;
    fwrite(&bits, 2, 1, f);

    fwrite("data", 1, 4, f);
    fwrite(&data_sz, 4, 1, f);
    uint16_t sample = 0;
    for (uint32_t i = 0; i < frame_count; i++)
        fwrite(&sample, 2, 1, f);

    fclose(f);
    return 1;
}

// ── Test: rt_gc_run_all_finalizers invokes all finalizers ────────────────────

static void test_gc_finalizer_sweep() {
    printf("  test_gc_finalizer_sweep ... ");

    g_fin_a_count = 0;
    g_fin_b_count = 0;
    g_fin_c_count = 0;

    // Create three GC-tracked objects with finalizers
    void *obj_a = rt_obj_new_i64(0, 64);
    void *obj_b = rt_obj_new_i64(0, 64);
    void *obj_c = rt_obj_new_i64(0, 64);

    rt_obj_set_finalizer(obj_a, finalizer_a);
    rt_obj_set_finalizer(obj_b, finalizer_b);
    rt_obj_set_finalizer(obj_c, finalizer_c);

    rt_gc_track(obj_a, noop_traverse);
    rt_gc_track(obj_b, noop_traverse);
    rt_gc_track(obj_c, noop_traverse);

    // All three should be tracked
    assert(rt_gc_is_tracked(obj_a));
    assert(rt_gc_is_tracked(obj_b));
    assert(rt_gc_is_tracked(obj_c));

    // Run the shutdown finalizer sweep
    rt_gc_run_all_finalizers();

    // All finalizers should have been called exactly once
    assert(g_fin_a_count == 1);
    assert(g_fin_b_count == 1);
    assert(g_fin_c_count == 1);

    // Objects should still be tracked (sweep doesn't untrack)
    assert(rt_gc_is_tracked(obj_a));

    printf("OK\n");

    // Cleanup: untrack and free
    rt_gc_untrack(obj_a);
    rt_gc_untrack(obj_b);
    rt_gc_untrack(obj_c);
    rt_heap_release(obj_a);
    rt_heap_release(obj_b);
    rt_heap_release(obj_c);
}

// ── Test: double-finalization prevention ────────────────────────────────────

static void test_gc_no_double_finalize() {
    printf("  test_gc_no_double_finalize ... ");

    g_fin_a_count = 0;

    void *obj = rt_obj_new_i64(0, 64);
    rt_obj_set_finalizer(obj, finalizer_a);
    rt_gc_track(obj, noop_traverse);

    // First sweep: finalizer should run
    rt_gc_run_all_finalizers();
    assert(g_fin_a_count == 1);

    // Second sweep: finalizer pointer was cleared, should NOT run again
    rt_gc_run_all_finalizers();
    assert(g_fin_a_count == 1);

    printf("OK\n");

    rt_gc_untrack(obj);
    rt_heap_release(obj);
}

// ── Test: sweep on empty GC table ───────────────────────────────────────────

static void test_gc_sweep_empty() {
    printf("  test_gc_sweep_empty ... ");

    // Should be a safe no-op
    int64_t count_before = rt_gc_tracked_count();
    rt_gc_run_all_finalizers();
    int64_t count_after = rt_gc_tracked_count();
    assert(count_before == count_after);

    printf("OK\n");
}

// ── Test: objects without finalizers are skipped ─────────────────────────────

static void test_gc_sweep_no_finalizer() {
    printf("  test_gc_sweep_no_finalizer ... ");

    g_fin_a_count = 0;

    // Object A has a finalizer
    void *obj_a = rt_obj_new_i64(0, 64);
    rt_obj_set_finalizer(obj_a, finalizer_a);
    rt_gc_track(obj_a, noop_traverse);

    // Object B has no finalizer
    void *obj_b = rt_obj_new_i64(0, 64);
    rt_gc_track(obj_b, noop_traverse);

    rt_gc_run_all_finalizers();

    // Only A's finalizer should have run
    assert(g_fin_a_count == 1);

    printf("OK\n");

    rt_gc_untrack(obj_a);
    rt_gc_untrack(obj_b);
    rt_heap_release(obj_a);
    rt_heap_release(obj_b);
}

// ── Test: legacy context shutdown ───────────────────────────────────────────

static void test_legacy_context_shutdown() {
    printf("  test_legacy_context_shutdown ... ");

    // Force legacy context initialization
    RtContext *legacy = rt_legacy_context();
    assert(legacy != nullptr);

    // Call shutdown — should not crash
    rt_legacy_context_shutdown();

    printf("OK\n");
}

static void test_audio_shutdown_detaches_loaded_handles() {
    printf("  test_audio_shutdown_detaches_loaded_handles ... ");

    const char *path = "/tmp/viper_shutdown_handles.wav";
    if (!write_test_wav_frames(path, 44100, 128)) {
        printf("SKIP (temp wav write failed)\n");
        return;
    }

    if (!rt_audio_init()) {
        remove(path);
        printf("SKIP (audio unavailable)\n");
        return;
    }

    void *sound = rt_sound_load(make_str(path));
    void *music = rt_music_load(make_str(path));
    if (!sound || !music) {
        if (sound)
            rt_sound_destroy(sound);
        if (music)
            rt_music_destroy(music);
        rt_audio_shutdown();
        remove(path);
        printf("SKIP (audio load unavailable)\n");
        return;
    }

    rt_music_play(music, 0);
    rt_audio_shutdown();

    assert(rt_sound_play(sound) == -1);
    rt_music_play(music, 0);
    rt_music_pause(music);
    rt_music_resume(music);
    rt_music_seek(music, 0);
    assert(rt_music_is_playing(music) == 0);
    assert(rt_music_get_duration(music) >= 0);

    rt_sound_destroy(sound);
    rt_music_destroy(music);

    int64_t reinit = rt_audio_init();
    assert(reinit == 0 || reinit == 1);
    rt_audio_shutdown();

    remove(path);
    printf("OK\n");
}

// ── Main ────────────────────────────────────────────────────────────────────

int main() {
    printf("RTShutdownTests:\n");

    test_gc_finalizer_sweep();
    test_gc_no_double_finalize();
    test_gc_sweep_empty();
    test_gc_sweep_no_finalizer();
    test_legacy_context_shutdown();
    test_audio_shutdown_detaches_loaded_handles();

    printf("All shutdown tests passed.\n");
    return 0;
}
