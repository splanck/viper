//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
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

#include "common/PlatformCapabilities.hpp"
#include "rt.hpp"
#include "rt_string.h"
#include "runtime/audio/rt_audio.h"
#include "runtime/core/rt_context.h"
#include "runtime/core/rt_gc.h"
#include "runtime/core/rt_heap.h"
#include "runtime/core/rt_random.h"
#include "runtime/oop/rt_object.h"
#include "runtime/system/rt_shutdown.h"

#include <atomic>
#include <cassert>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <thread>

// ── vm_trap override ────────────────────────────────────────────────────────
// Prevent process exit on trap during tests.
static std::atomic<int> g_trap_count{0};

extern "C" void vm_trap(const char *msg) {
    (void)msg;
    g_trap_count.fetch_add(1, std::memory_order_relaxed);
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

/// @brief Verify shutdown sweeping does not steal a zero-ref deferred payload.
/// @details `rt_obj_release_check0` transfers destruction responsibility to
///          its caller while leaving the allocation registered until
///          `rt_obj_free`. A concurrent shutdown snapshot must skip that
///          payload so only the final-release owner invokes its finalizer and
///          reclaims its memory.
static void test_gc_sweep_skips_deferred_zero_ref() {
    printf("  test_gc_sweep_skips_deferred_zero_ref ... ");

    g_fin_a_count = 0;
    void *obj = rt_obj_new_i64(0, 64);
    rt_obj_set_finalizer(obj, finalizer_a);
    rt_gc_track(obj, noop_traverse);

    assert(rt_obj_release_check0(obj) == 1);
    assert(rt_heap_is_payload(obj));
    rt_gc_run_all_finalizers();
    assert(g_fin_a_count == 0);
    assert(rt_heap_is_payload(obj));

    rt_obj_free(obj);
    assert(g_fin_a_count == 1);
    assert(!rt_heap_is_payload(obj));

    printf("OK\n");
}

// ── Test: context binding guards ────────────────────────────────────────────

/// @brief Verify cleanup unbinds a sole owner but rejects a shared context.
/// @details The first half exercises the convenience path used by embedders
///          that clean up their currently bound context. The second simulates
///          another live binding and verifies a returning trap hook cannot let
///          cleanup destroy state that the binding still references.
static void test_context_cleanup_binding_guards() {
    printf("  test_context_cleanup_binding_guards ... ");

    RtContext sole{};
    rt_context_init(&sole);
    rt_set_current_context(&sole);
    int traps_before = g_trap_count.load(std::memory_order_relaxed);
    rt_context_cleanup(&sole);
    assert(rt_get_current_context() == nullptr);
    assert(sole.bind_count == 0);
    assert(g_trap_count.load(std::memory_order_relaxed) == traps_before);

    RtContext shared{};
    rt_context_init(&shared);
    rt_set_current_context(&shared);
    shared.bind_count = 2;
    rt_context_cleanup(&shared);
    assert(g_trap_count.load(std::memory_order_relaxed) == traps_before + 1);
    assert(rt_get_current_context() == &shared);
    assert(shared.bind_count == 2);

    shared.bind_count = 1;
    rt_set_current_context(nullptr);
    rt_context_cleanup(&shared);

    printf("OK\n");
}

/// @brief Verify failed binding counter transitions preserve the prior TLS state.
/// @details Exercises both overflow on the destination and underflow on the
///          source with a trap hook that returns. In each case the destination
///          reservation is absent or rolled back and callers continue using
///          the original context rather than a partially published binding.
static void test_context_binding_counter_guards() {
    printf("  test_context_binding_counter_guards ... ");

    RtContext original{};
    RtContext destination{};
    rt_context_init(&original);
    rt_context_init(&destination);
    rt_set_current_context(&original);

    int traps_before = g_trap_count.load(std::memory_order_relaxed);
    destination.bind_count = SIZE_MAX;
    rt_set_current_context(&destination);
    assert(g_trap_count.load(std::memory_order_relaxed) == traps_before + 1);
    assert(rt_get_current_context() == &original);
    assert(original.bind_count == 1);
    assert(destination.bind_count == SIZE_MAX);

    destination.bind_count = 0;
    original.bind_count = 0;
    rt_set_current_context(&destination);
    assert(g_trap_count.load(std::memory_order_relaxed) == traps_before + 2);
    assert(rt_get_current_context() == &original);
    assert(original.bind_count == 0);
    assert(destination.bind_count == 0);

    original.bind_count = 1;
    rt_set_current_context(nullptr);
    rt_context_cleanup(&original);
    rt_context_cleanup(&destination);

    printf("OK\n");
}

/// @brief Race context cleanup against a first binding without destroying live locks.
/// @details Each iteration permits either transaction to win. If binding wins,
///          cleanup traps while the worker safely uses and unbinds the context;
///          if cleanup wins, binding traps before publishing TLS. The surviving
///          ready state is cleaned by the parent, and a final post-cleanup bind
///          proves uninitialized storage is rejected rather than resurrected.
static void test_context_cleanup_binding_race() {
    printf("  test_context_cleanup_binding_race ... ");

    constexpr int kIterations = 500;
    for (int iteration = 0; iteration < kIterations; ++iteration) {
        RtContext context{};
        rt_context_init(&context);
        assert(context.lifecycle_state == RT_CONTEXT_LIFECYCLE_READY);

        std::atomic<bool> go{false};
        std::thread binder([&]() {
            while (!go.load(std::memory_order_acquire))
                std::this_thread::yield();
            rt_set_current_context(&context);
            if (rt_get_current_context() == &context) {
                for (int step = 0; step < 8; ++step)
                    (void)rt_rnd();
                rt_set_current_context(nullptr);
            }
        });
        std::thread cleaner([&]() {
            while (!go.load(std::memory_order_acquire))
                std::this_thread::yield();
            rt_context_cleanup(&context);
        });

        go.store(true, std::memory_order_release);
        binder.join();
        cleaner.join();

        assert(context.bind_count == 0);
        if (context.lifecycle_state == RT_CONTEXT_LIFECYCLE_READY)
            rt_context_cleanup(&context);
        assert(context.lifecycle_state == RT_CONTEXT_LIFECYCLE_UNINITIALIZED);
    }

    RtContext cleaned{};
    rt_context_init(&cleaned);
    rt_context_cleanup(&cleaned);
    const int traps_before = g_trap_count.load(std::memory_order_relaxed);
    rt_set_current_context(&cleaned);
    assert(rt_get_current_context() == nullptr);
    assert(g_trap_count.load(std::memory_order_relaxed) == traps_before + 1);

    printf("OK\n");
}

// ── Test: legacy context shutdown ───────────────────────────────────────────

/// @brief Verify legacy shutdown resets the lazy context for later reuse.
/// @details A shutdown must release owned subsystem state and publish the
///          uninitialized state rather than leaving a destroyed registry
///          behind a permanent READY flag. The next accessor must therefore
///          see deterministic freshly initialized values.
static void test_legacy_context_shutdown() {
    printf("  test_legacy_context_shutdown ... ");

    // Force legacy context initialization
    RtContext *legacy = rt_legacy_context();
    assert(legacy != nullptr);
    legacy->rng_state = 7;

    // Shutdown and immediately reuse the fallback context.
    rt_legacy_context_shutdown();
    RtContext *fresh = rt_legacy_context();
    assert(fresh == legacy);
    assert(fresh->rng_state == 0xDEADBEEFCAFEBABEULL);
    assert(fresh->bind_count == 0);

    printf("OK\n");
}

static void test_audio_shutdown_detaches_loaded_handles() {
    printf("  test_audio_shutdown_detaches_loaded_handles ... ");

    const char *path = "/tmp/zanna_shutdown_handles.wav";
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

static void test_graceful_shutdown_poll_api() {
    printf("  test_graceful_shutdown_poll_api ... ");

    rt_shutdown_clear();
    assert(rt_shutdown_const_none() == RT_SHUTDOWN_REASON_NONE);
    assert(rt_shutdown_const_interrupt() == RT_SHUTDOWN_REASON_INTERRUPT);
    assert(rt_shutdown_const_terminate() == RT_SHUTDOWN_REASON_TERMINATE);
    assert(rt_shutdown_pending() == 0);
    assert(rt_shutdown_poll() == RT_SHUTDOWN_REASON_NONE);

    rt_shutdown_request(RT_SHUTDOWN_REASON_INTERRUPT);
    assert(rt_shutdown_pending() == 1);
    assert(rt_shutdown_poll() == RT_SHUTDOWN_REASON_INTERRUPT);
    assert(rt_shutdown_pending() == 0);
    assert(rt_shutdown_poll() == RT_SHUTDOWN_REASON_NONE);

    rt_shutdown_request(RT_SHUTDOWN_REASON_INTERRUPT | RT_SHUTDOWN_REASON_TERMINATE);
    assert(rt_shutdown_pending() == 1);
    assert(rt_shutdown_poll() == (RT_SHUTDOWN_REASON_INTERRUPT | RT_SHUTDOWN_REASON_TERMINATE));
    assert(rt_shutdown_pending() == 0);

    rt_shutdown_request(RT_SHUTDOWN_REASON_TERMINATE);
    rt_shutdown_clear();
    assert(rt_shutdown_pending() == 0);

    printf("OK\n");
}

#if !ZANNA_HOST_WINDOWS
// VDOC-210: after rt_shutdown_install_signal_handlers, a real SIGINT/SIGTERM is
// published through rt_shutdown_request so Poll observes it — the OS integration
// that was previously VM-only is now available to native programs too.
static void test_installed_signal_handlers_publish() {
    printf("  test_installed_signal_handlers_publish ... ");
    rt_shutdown_clear();
    rt_shutdown_install_signal_handlers();

    raise(SIGINT);
    assert(rt_shutdown_poll() == RT_SHUTDOWN_REASON_INTERRUPT);

    rt_shutdown_clear();
    raise(SIGTERM);
    assert(rt_shutdown_poll() == RT_SHUTDOWN_REASON_TERMINATE);

    rt_shutdown_clear();
    printf("OK\n");
}
#endif

// ── Main ────────────────────────────────────────────────────────────────────

int main() {
    printf("RTShutdownTests:\n");

    test_gc_finalizer_sweep();
    test_gc_no_double_finalize();
    test_gc_sweep_empty();
    test_gc_sweep_no_finalizer();
    test_gc_sweep_skips_deferred_zero_ref();
    test_context_cleanup_binding_guards();
    test_context_binding_counter_guards();
    test_context_cleanup_binding_race();
    test_legacy_context_shutdown();
    test_audio_shutdown_detaches_loaded_handles();
    test_graceful_shutdown_poll_api();
#if !ZANNA_HOST_WINDOWS
    test_installed_signal_handlers_publish();
#endif

    printf("All shutdown tests passed.\n");
    return 0;
}
