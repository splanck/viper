//===----------------------------------------------------------------------===//
// Part of the Zanna project, under the GNU GPL v3.
// RTScreenFXTests.cpp - Unit tests for rt_screenfx
//===----------------------------------------------------------------------===//

#include "rt_screenfx.h"
#include <cassert>
#include <cstdint>
#include <cstdio>

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) static void test_##name()
#define RUN_TEST(name)                                                                             \
    do {                                                                                           \
        printf("  %s...", #name);                                                                  \
        test_##name();                                                                             \
        printf(" OK\n");                                                                           \
        tests_passed++;                                                                            \
    } while (0)

#define ASSERT(cond)                                                                               \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            printf(" FAILED at line %d: %s\n", __LINE__, #cond);                                   \
            tests_failed++;                                                                        \
            return;                                                                                \
        }                                                                                          \
    } while (0)

TEST(create_destroy) {
    rt_screenfx fx = rt_screenfx_new();
    ASSERT(fx != NULL);
    ASSERT(rt_screenfx_is_active(fx) == 0);
    ASSERT(rt_screenfx_get_shake_x(fx) == 0);
    ASSERT(rt_screenfx_get_shake_y(fx) == 0);
    ASSERT(rt_screenfx_get_overlay_alpha(fx) == 0);
    rt_screenfx_destroy(fx);
}

TEST(shake) {
    rt_screenfx fx = rt_screenfx_new();
    /// @brief Rt_screenfx_shake.
    rt_screenfx_shake(fx, 10000, 100, 0); // 10 pixels, 100ms, no decay

    ASSERT(rt_screenfx_is_active(fx) == 1);
    ASSERT(rt_screenfx_is_type_active(fx, RT_SCREENFX_SHAKE) == 1);

    // Update and check shake values change
    rt_screenfx_update(fx, 16);
    // Shake should produce non-zero offsets (though random)
    // After several updates, we expect some offset
    for (int i = 0; i < 5; i++) {
        rt_screenfx_update(fx, 16);
    }

    rt_screenfx_destroy(fx);
}

TEST(shake_decay) {
    rt_screenfx fx = rt_screenfx_new();
    /// @brief Rt_screenfx_shake.
    rt_screenfx_shake(fx, 10000, 200, 500); // 50% decay

    // Run until completion
    for (int i = 0; i < 20; i++) {
        rt_screenfx_update(fx, 16);
    }

    // After duration, should be inactive
    rt_screenfx_update(fx, 200);
    ASSERT(rt_screenfx_is_type_active(fx, RT_SCREENFX_SHAKE) == 0);

    rt_screenfx_destroy(fx);
}

TEST(flash) {
    rt_screenfx fx = rt_screenfx_new();
    /// @brief Rt_screenfx_flash.
    rt_screenfx_flash(fx, 0xFF0000FF, 100); // Red with alpha 255

    ASSERT(rt_screenfx_is_active(fx) == 1);
    ASSERT(rt_screenfx_is_type_active(fx, RT_SCREENFX_FLASH) == 1);

    rt_screenfx_update(fx, 10);
    ASSERT(rt_screenfx_get_overlay_alpha(fx) > 0);

    // Reaching the duration enters the terminal frame — still active for one
    // draw — and the following update reclaims the slot (VDOC-265).
    rt_screenfx_update(fx, 100);
    ASSERT(rt_screenfx_is_type_active(fx, RT_SCREENFX_FLASH) == 1);
    rt_screenfx_update(fx, 1);
    ASSERT(rt_screenfx_is_type_active(fx, RT_SCREENFX_FLASH) == 0);

    rt_screenfx_destroy(fx);
}

TEST(negative_update_is_noop) {
    rt_screenfx fx = rt_screenfx_new();
    rt_screenfx_flash(fx, 0xFF0000FF, 100);
    rt_screenfx_update(fx, 10);
    int64_t alpha = rt_screenfx_get_overlay_alpha(fx);
    rt_screenfx_update(fx, -50);
    ASSERT(rt_screenfx_is_type_active(fx, RT_SCREENFX_FLASH) == 1);
    ASSERT(rt_screenfx_get_overlay_alpha(fx) == alpha);
    rt_screenfx_destroy(fx);
}

TEST(zero_update_is_noop) {
    rt_screenfx fx = rt_screenfx_new();
    rt_screenfx_flash(fx, 0xFF0000FF, 100);
    rt_screenfx_update(fx, 10);
    int64_t alpha = rt_screenfx_get_overlay_alpha(fx);
    int64_t color = rt_screenfx_get_overlay_color(fx);

    rt_screenfx_update(fx, 0);
    ASSERT(rt_screenfx_is_type_active(fx, RT_SCREENFX_FLASH) == 1);
    ASSERT(rt_screenfx_get_overlay_alpha(fx) == alpha);
    ASSERT(rt_screenfx_get_overlay_color(fx) == color);

    rt_screenfx_destroy(fx);
}

TEST(fade_in) {
    rt_screenfx fx = rt_screenfx_new();
    /// @brief Rt_screenfx_fade_in.
    rt_screenfx_fade_in(fx, 0x000000FF, 100); // Black, alpha 255

    rt_screenfx_update(fx, 10);
    int64_t alpha1 = rt_screenfx_get_overlay_alpha(fx);

    rt_screenfx_update(fx, 40);
    int64_t alpha2 = rt_screenfx_get_overlay_alpha(fx);

    // Fade-in: alpha should decrease over time
    ASSERT(alpha2 < alpha1);

    rt_screenfx_destroy(fx);
}

TEST(fade_out) {
    rt_screenfx fx = rt_screenfx_new();
    /// @brief Rt_screenfx_fade_out.
    rt_screenfx_fade_out(fx, 0x000000FF, 100); // Black, alpha 255

    rt_screenfx_update(fx, 10);
    int64_t alpha1 = rt_screenfx_get_overlay_alpha(fx);

    rt_screenfx_update(fx, 40);
    int64_t alpha2 = rt_screenfx_get_overlay_alpha(fx);

    // Fade-out: alpha should increase over time
    ASSERT(alpha2 > alpha1);

    rt_screenfx_destroy(fx);
}

TEST(cancel_all) {
    rt_screenfx fx = rt_screenfx_new();
    rt_screenfx_shake(fx, 5000, 500, 0);
    rt_screenfx_flash(fx, 0xFF0000FF, 500);

    ASSERT(rt_screenfx_is_active(fx) == 1);

    rt_screenfx_cancel_all(fx);
    ASSERT(rt_screenfx_is_active(fx) == 0);
    ASSERT(rt_screenfx_get_shake_x(fx) == 0);
    ASSERT(rt_screenfx_get_shake_y(fx) == 0);
    ASSERT(rt_screenfx_get_overlay_alpha(fx) == 0);

    rt_screenfx_destroy(fx);
}

TEST(cancel_type) {
    rt_screenfx fx = rt_screenfx_new();
    rt_screenfx_shake(fx, 5000, 500, 0);
    rt_screenfx_flash(fx, 0xFF0000FF, 500);

    rt_screenfx_cancel_type(fx, RT_SCREENFX_SHAKE);

    ASSERT(rt_screenfx_is_type_active(fx, RT_SCREENFX_SHAKE) == 0);
    ASSERT(rt_screenfx_is_type_active(fx, RT_SCREENFX_FLASH) == 1);

    rt_screenfx_destroy(fx);
}

TEST(shake_quadratic_decay) {
    rt_screenfx fx = rt_screenfx_new();
    // Large intensity, long duration, quadratic decay (decay >= 1500 triggers x^2 model).
    // At 99% progress (elapsed=9900 of 10000ms):
    //   remaining = 10, decay_factor = 10*10/1000 = 0  →  current_intensity = 0.
    // Both shake offsets must be exactly zero regardless of random seed.
    rt_screenfx_shake(fx, 100000, 10000, 2000);
    rt_screenfx_update(fx, 9900);
    ASSERT(rt_screenfx_get_shake_x(fx) == 0);
    ASSERT(rt_screenfx_get_shake_y(fx) == 0);
    rt_screenfx_destroy(fx);
}

TEST(huge_shake_parameters_do_not_overflow) {
    rt_screenfx fx = rt_screenfx_new();
    rt_screenfx_shake(fx, INT64_MAX, 100, 1000);
    rt_screenfx_update(fx, 50);
    ASSERT(rt_screenfx_is_type_active(fx, RT_SCREENFX_SHAKE) == 1);
    (void)rt_screenfx_get_shake_x(fx);
    (void)rt_screenfx_get_shake_y(fx);
    rt_screenfx_update(fx, 50); // reaches duration → terminal frame (still active)
    ASSERT(rt_screenfx_is_type_active(fx, RT_SCREENFX_SHAKE) == 1);
    rt_screenfx_update(fx, 1); // reclaim on the following tick (VDOC-265)
    ASSERT(rt_screenfx_is_type_active(fx, RT_SCREENFX_SHAKE) == 0);
    rt_screenfx_destroy(fx);
}

// VDOC-265: an effect that reaches its duration is held one extra update at its
// terminal state so the fully-covered final frame is drawable. A FadeOut that
// targets an opaque overlay must therefore expose alpha 255 before the slot is
// reclaimed — previously the slot vanished at elapsed>=duration and the fully
// covered frame was never observable.
TEST(fadeout_terminal_frame_is_observable) {
    rt_screenfx fx = rt_screenfx_new();
    rt_screenfx_fade_out(fx, 0x000000FF, 100); // fade TO opaque black (alpha 255)

    rt_screenfx_update(fx, 90);
    ASSERT(rt_screenfx_get_overlay_alpha(fx) < 255); // not yet fully covered

    rt_screenfx_update(fx, 10);             // reaches duration → terminal frame
    ASSERT(rt_screenfx_is_active(fx) == 1); // finished advancing, not yet removed
    ASSERT(rt_screenfx_get_overlay_alpha(fx) == 255); // final covered frame visible
    ASSERT(rt_screenfx_get_transition_progress(fx) == 0); // FadeOut is not a transition

    rt_screenfx_update(fx, 1); // reclaim
    ASSERT(rt_screenfx_is_finished(fx) == 1);
    ASSERT(rt_screenfx_get_overlay_alpha(fx) == 0);
    rt_screenfx_destroy(fx);
}

// VDOC-265: a transition must expose progress 1000 on its terminal frame before
// the slot is reclaimed, so scene code that waits for full coverage can act.
TEST(transition_terminal_progress_is_1000) {
    rt_screenfx fx = rt_screenfx_new();
    rt_screenfx_wipe(fx, 0, 0x000000, 100);

    rt_screenfx_update(fx, 100); // reaches duration → terminal frame
    ASSERT(rt_screenfx_is_active(fx) == 1);
    ASSERT(rt_screenfx_get_transition_progress(fx) == 1000);

    rt_screenfx_update(fx, 1); // reclaim
    ASSERT(rt_screenfx_is_finished(fx) == 1);
    ASSERT(rt_screenfx_get_transition_progress(fx) == 0);
    rt_screenfx_destroy(fx);
}

// VDOC-266: CancelType must not leave cached overlay output drawable after the
// slot that produced it is gone. Before the fix the composited overlay alpha
// survived cancellation and Draw would keep rendering a "canceled" flash.
TEST(cancel_type_clears_cached_overlay) {
    rt_screenfx fx = rt_screenfx_new();
    rt_screenfx_flash(fx, 0xFFFFFFFF, 1000); // white, alpha 255
    rt_screenfx_update(fx, 100);
    ASSERT(rt_screenfx_get_overlay_alpha(fx) > 0);

    rt_screenfx_cancel_type(fx, RT_SCREENFX_FLASH);
    ASSERT(rt_screenfx_is_active(fx) == 0);
    ASSERT(rt_screenfx_get_overlay_alpha(fx) == 0); // cache invalidated immediately
    ASSERT(rt_screenfx_get_overlay_color(fx) == 0);
    rt_screenfx_destroy(fx);
}

// VDOC-266: CancelType on a shake must clear the stale composited camera offset,
// not leave one frame of displacement drawable after the shake is gone.
TEST(cancel_type_clears_stale_shake_offset) {
    rt_screenfx fx = rt_screenfx_new();
    rt_screenfx_shake(fx, 100000, 1000, 0); // strong, constant amplitude
    rt_screenfx_update(fx, 100);

    rt_screenfx_cancel_type(fx, RT_SCREENFX_SHAKE);
    ASSERT(rt_screenfx_is_active(fx) == 0);
    ASSERT(rt_screenfx_get_shake_x(fx) == 0);
    ASSERT(rt_screenfx_get_shake_y(fx) == 0);
    rt_screenfx_destroy(fx);
}

TEST(multiple_effects) {
    rt_screenfx fx = rt_screenfx_new();

    // Can have shake and flash active simultaneously
    rt_screenfx_shake(fx, 5000, 200, 0);
    rt_screenfx_flash(fx, 0xFF0000FF, 200);

    ASSERT(rt_screenfx_is_type_active(fx, RT_SCREENFX_SHAKE) == 1);
    ASSERT(rt_screenfx_is_type_active(fx, RT_SCREENFX_FLASH) == 1);

    rt_screenfx_update(fx, 16);

    rt_screenfx_destroy(fx);
}

TEST(grows_past_default_effect_slots) {
    rt_screenfx fx = rt_screenfx_new();

    for (int i = 0; i < RT_SCREENFX_MAX_EFFECTS + 3; i++)
        rt_screenfx_flash(fx, 0xFF0000FF, 1000 + i);

    ASSERT(rt_screenfx_is_active(fx) == 1);
    ASSERT(rt_screenfx_is_type_active(fx, RT_SCREENFX_FLASH) == 1);
    rt_screenfx_update(fx, 10);
    ASSERT(rt_screenfx_get_overlay_alpha(fx) > 0);

    rt_screenfx_destroy(fx);
}

// VDOC-267: ScreenFX overlay/transition colors use their own byte orders, distinct
// from the canonical Zanna.Graphics.Color (0xAARRGGBB). The Rgba/Rgb constructors
// pack the correct encodings so callers never hand-pack or pass an incompatible
// Color value. A value built with Rgba must round-trip through Flash's alpha channel.
TEST(color_constructors_pack_screenfx_byte_orders) {
    // Rgba packs 0xRRGGBBAA (alpha in the low byte).
    ASSERT(rt_screenfx_rgba(0x12, 0x34, 0x56, 0x78) == 0x12345678);
    ASSERT(rt_screenfx_rgba(255, 0, 0, 255) == 0xFF0000FF); // opaque red overlay
    // Rgb packs 0x00RRGGBB (no alpha).
    ASSERT(rt_screenfx_rgb(0x12, 0x34, 0x56) == 0x123456);
    ASSERT(rt_screenfx_rgb(255, 0, 0) == 0xFF0000);
    // Channels clamp to [0, 255].
    ASSERT(rt_screenfx_rgba(-5, 300, 0, 999) == 0x00FF00FF);

    // A Flash built from Rgba shows a non-zero overlay; the low byte drove alpha.
    rt_screenfx fx = rt_screenfx_new();
    rt_screenfx_flash(fx, rt_screenfx_rgba(255, 0, 0, 255), 100);
    rt_screenfx_update(fx, 10);
    ASSERT(rt_screenfx_get_overlay_alpha(fx) > 0);
    rt_screenfx_destroy(fx);
}

// VDOC-268: the effect-type and wipe-direction values were private to the C
// header, so callers (including the shipped API audits) copied raw integers and
// used the wrong ID — 0 is NONE, not shake. The registered constants must mirror
// the private enum exactly so IsTypeActive/CancelType select the intended effect.
TEST(named_constants_match_enum) {
    ASSERT(rt_screenfx_type_shake() == RT_SCREENFX_SHAKE);
    ASSERT(rt_screenfx_type_flash() == RT_SCREENFX_FLASH);
    ASSERT(rt_screenfx_type_fade_in() == RT_SCREENFX_FADE_IN);
    ASSERT(rt_screenfx_type_fade_out() == RT_SCREENFX_FADE_OUT);
    ASSERT(rt_screenfx_type_wipe() == RT_SCREENFX_WIPE);
    ASSERT(rt_screenfx_type_circle_in() == RT_SCREENFX_CIRCLE_IN);
    ASSERT(rt_screenfx_type_circle_out() == RT_SCREENFX_CIRCLE_OUT);
    ASSERT(rt_screenfx_type_dissolve() == RT_SCREENFX_DISSOLVE);
    ASSERT(rt_screenfx_type_pixelate() == RT_SCREENFX_PIXELATE);
    ASSERT(rt_screenfx_type_shake() != RT_SCREENFX_NONE); // the audit's bug: 0 != shake
    ASSERT(rt_screenfx_dir_left() == RT_DIR_LEFT);
    ASSERT(rt_screenfx_dir_right() == RT_DIR_RIGHT);
    ASSERT(rt_screenfx_dir_up() == RT_DIR_UP);
    ASSERT(rt_screenfx_dir_down() == RT_DIR_DOWN);

    // The constant genuinely selects the shake for CancelType.
    rt_screenfx fx = rt_screenfx_new();
    rt_screenfx_shake(fx, 5000, 500, 0);
    rt_screenfx_update(fx, 16);
    rt_screenfx_cancel_type(fx, rt_screenfx_type_shake());
    ASSERT(rt_screenfx_is_type_active(fx, rt_screenfx_type_shake()) == 0);
    rt_screenfx_destroy(fx);
}

/// @brief Main.
int main() {
    printf("RTScreenFXTests:\n");
    RUN_TEST(create_destroy);
    RUN_TEST(shake);
    RUN_TEST(shake_decay);
    RUN_TEST(flash);
    RUN_TEST(negative_update_is_noop);
    RUN_TEST(zero_update_is_noop);
    RUN_TEST(fade_in);
    RUN_TEST(fade_out);
    RUN_TEST(cancel_all);
    RUN_TEST(cancel_type);
    RUN_TEST(shake_quadratic_decay);
    RUN_TEST(huge_shake_parameters_do_not_overflow);
    RUN_TEST(fadeout_terminal_frame_is_observable);
    RUN_TEST(transition_terminal_progress_is_1000);
    RUN_TEST(cancel_type_clears_cached_overlay);
    RUN_TEST(cancel_type_clears_stale_shake_offset);
    RUN_TEST(color_constructors_pack_screenfx_byte_orders);
    RUN_TEST(named_constants_match_enum);
    RUN_TEST(multiple_effects);
    RUN_TEST(grows_past_default_effect_slots);

    printf("\n%d tests passed, %d tests failed\n", tests_passed, tests_failed);
    return tests_failed > 0 ? 1 : 0;
}
