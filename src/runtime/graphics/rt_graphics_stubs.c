//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/rt_graphics_stubs.c
// Purpose: Stub implementations for graphics-disabled builds. Stateful Canvas
//   operations fail loudly with a deterministic InvalidOperation trap, while
//   backend-free helpers such as color math and text metrics remain usable.
//
// Key invariants:
//   - This file is compiled only when VIPER_ENABLE_GRAPHICS is NOT defined.
//   - Canvas operations trap with Err_InvalidOperation instead of silently
//     succeeding.
//   - Pure Color helpers and text measurement stay functional without a
//     graphics backend.
//
// Ownership/Lifetime:
//   - No resources are allocated; all functions are stateless stubs.
//
// Links: src/runtime/graphics/rt_graphics.h (public API),
//        src/runtime/graphics/rt_graphics.c (real implementations)
//
//===----------------------------------------------------------------------===//

#include "rt_audio3d.h"
#include "rt_canvas3d.h"
#include "rt_collider3d.h"
#include "rt_decal3d.h"
#include "rt_error.h"
#include "rt_fbx_loader.h"
#include "rt_gltf.h"
#include "rt_graphics.h"
#include "rt_graphics_internal.h"
#include "rt_instbatch3d.h"
#include "rt_joints3d.h"
#include "rt_model3d.h"
#include "rt_morphtarget3d.h"
#include "rt_navmesh3d.h"
#include "rt_particles3d.h"
#include "rt_path3d.h"
#include "rt_physics3d.h"
#include "rt_postfx3d.h"
#include "rt_raycast3d.h"
#include "rt_scene3d.h"
#include "rt_skeleton3d.h"
#include "rt_sprite3d.h"
#include "rt_terrain3d.h"
#include "rt_texatlas3d.h"
#include "rt_transform3d.h"
#include "rt_water3d.h"

#include <stdint.h>

/// @brief Raise the canonical "graphics support not compiled in" trap.
///
/// Shared sink used by every stub in this translation unit so the same kind,
/// error code, and source location (line=0, the runtime stub layer) are
/// reported regardless of which Canvas / Sprite / 3D entry point the user
/// reached. Builds without `VIPER_ENABLE_GRAPHICS` substitute this file for
/// the real `rt_graphics.c` implementation, so reaching any of these stubs
/// signals that the program tried to use a graphics API that was deliberately
/// excluded at configure time.
///
/// @param msg Diagnostic string describing the offending entry point. The
///            convention used by the `RT_GRAPHICS_TRAP_*` macros below is
///            `"<Class>.<Method>: graphics support not compiled in"`.
///
/// @note Never returns under normal control flow — `rt_trap_raise_kind`
///       unwinds via the active trap handler. The macros wrap this call so
///       call sites can return a typed fallback value (`NULL`, `0`, `1`) to
///       satisfy the C type checker even though the return is unreachable.
static void rt_graphics_unavailable_(const char *msg) {
    rt_trap_raise_kind(RT_TRAP_KIND_INVALID_OPERATION, Err_InvalidOperation, 0, msg);
}

#define RT_GRAPHICS_TRAP_VOID(msg)                                                                 \
    do {                                                                                           \
        rt_graphics_unavailable_(msg);                                                             \
    } while (0)

#define RT_GRAPHICS_TRAP_RET(msg, fallback)                                                        \
    do {                                                                                           \
        rt_graphics_unavailable_(msg);                                                             \
        return (fallback);                                                                         \
    } while (0)

/// @brief Report whether the Canvas runtime is usable in this build.
///
/// Mirror of the real implementation's availability flag. In the stubs build
/// this always returns 0 so frontends and Zia/BASIC programs can branch on
/// graphics availability with the canonical
/// `if (Canvas.IsAvailable()) { ... }` pattern instead of crashing on the
/// first Canvas operation. Cheap enough to be called on every frame.
///
/// @return 0 — graphics support was excluded at configure time. The
///         non-stub build returns 1.
int8_t rt_canvas_is_available(void) {
    return 0;
}

/// @brief Stub for `Canvas.New` — would normally allocate and return a
///        Canvas window handle.
///
/// This stub is reached when a program calls `Canvas.New(title, w, h)` in a
/// build without `VIPER_ENABLE_GRAPHICS`. There is no underlying window
/// system, so we cannot construct a Canvas at all; the only correct response
/// is to raise an `InvalidOperation` trap so the failure surfaces at the call
/// site instead of returning a NULL handle that would crash later inside
/// `Flip`/`Box`/etc.
///
/// @param title  Window title (ignored).
/// @param width  Requested canvas width in pixels (ignored).
/// @param height Requested canvas height in pixels (ignored).
///
/// @return Never returns normally; control transfers to the active trap
///         handler. The `NULL` literal in the macro exists only to satisfy
///         the C return-type rule.
void *rt_canvas_new(rt_string title, int64_t width, int64_t height) {
    (void)title;
    (void)width;
    (void)height;
    RT_GRAPHICS_TRAP_RET("Canvas.New: graphics support not compiled in", NULL);
}

/// @brief Stub for `Canvas.Destroy` — would normally release the underlying
///        OS window, framebuffer, and event queue.
///
/// In the stubs build there is no Canvas to destroy, so this is a true no-op
/// (rather than a trap) — destroying a never-acquired resource is safe and
/// makes finalizer calls in defensive code paths harmless. Reaching this
/// stub almost always means the caller already trapped at `Canvas.New`.
///
/// @param canvas Canvas handle (always NULL in the stubs build; ignored).
void rt_canvas_destroy(void *canvas) {
    (void)canvas;
}

/// @brief Stub for `Canvas.Width` — would normally return the canvas width
///        in logical pixels.
///
/// Without a real Canvas there is no meaningful width to report. Traps with
/// `InvalidOperation` so the failure surfaces at the property access rather
/// than silently feeding `0` into downstream layout / draw math.
///
/// @param canvas Canvas handle (ignored).
///
/// @return Never returns normally; the `0` exists only to satisfy the
///         C type system after the trap.
int64_t rt_canvas_width(void *canvas) {
    (void)canvas;
    RT_GRAPHICS_TRAP_RET("Canvas.Width: graphics support not compiled in", 0);
}

/// @brief Stub for `Canvas.Height` — would normally return the canvas height
///        in logical pixels.
///
/// Same rationale as `rt_canvas_width`: traps so the absent backend cannot
/// be mistaken for a zero-sized window.
///
/// @param canvas Canvas handle (ignored).
///
/// @return Never returns normally.
int64_t rt_canvas_height(void *canvas) {
    (void)canvas;
    RT_GRAPHICS_TRAP_RET("Canvas.Height: graphics support not compiled in", 0);
}

/// @brief Stub for `Canvas.ShouldClose` — would normally report whether the
///        OS window has received a close request.
///
/// Returning `1` (close requested) would be tempting but unreachable, since
/// the trap fires first. The `1` literal exists for type-system reasons; the
/// trap is the actual contract.
///
/// @param canvas Canvas handle (ignored).
///
/// @return Never returns normally.
int64_t rt_canvas_should_close(void *canvas) {
    (void)canvas;
    RT_GRAPHICS_TRAP_RET("Canvas.ShouldClose: graphics support not compiled in", 1);
}

/// @brief Stub for `Canvas.Flip` — would normally present the back buffer
///        to the screen (double-buffer swap).
///
/// `Flip` is the most common per-frame call site, so the diagnostic message
/// uses the fully qualified name to make profiling output unambiguous when a
/// game loop hits this stub on every frame.
///
/// @param canvas Canvas handle (ignored).
void rt_canvas_flip(void *canvas) {
    (void)canvas;
    RT_GRAPHICS_TRAP_VOID("Canvas.Flip: graphics support not compiled in");
}

/// @brief Stub for `Canvas.Clear` — would normally fill the back buffer
///        with a solid color.
///
/// @param canvas Canvas handle (ignored).
/// @param color  Packed 0xAARRGGBB color (ignored).
void rt_canvas_clear(void *canvas, int64_t color) {
    (void)canvas;
    (void)color;
    RT_GRAPHICS_TRAP_VOID("Canvas.Clear: graphics support not compiled in");
}

/// @brief Stub for `Canvas.Line` — would normally draw a 1px line segment.
///
/// Endpoints are inclusive on both ends, in canvas-pixel coordinates with
/// origin at the top-left and +Y pointing down (matches the screen-space
/// convention used everywhere else in the runtime).
///
/// @param canvas Canvas handle (ignored).
/// @param x1     Start point x (ignored).
/// @param y1     Start point y (ignored).
/// @param x2     End point x (ignored).
/// @param y2     End point y (ignored).
/// @param color  Packed 0xAARRGGBB color (ignored).
void rt_canvas_line(void *canvas, int64_t x1, int64_t y1, int64_t x2, int64_t y2, int64_t color) {
    (void)canvas;
    (void)x1;
    (void)y1;
    (void)x2;
    (void)y2;
    (void)color;
    RT_GRAPHICS_TRAP_VOID("Canvas.Line: graphics support not compiled in");
}

/// @brief Stub for `Canvas.Box` — would normally draw a filled axis-aligned
///        rectangle.
///
/// @param canvas Canvas handle (ignored).
/// @param x      Left edge in canvas pixels (ignored).
/// @param y      Top edge in canvas pixels (ignored).
/// @param w      Width in pixels; must be positive (ignored).
/// @param h      Height in pixels; must be positive (ignored).
/// @param color  Packed 0xAARRGGBB color (ignored).
void rt_canvas_box(void *canvas, int64_t x, int64_t y, int64_t w, int64_t h, int64_t color) {
    (void)canvas;
    (void)x;
    (void)y;
    (void)w;
    (void)h;
    (void)color;
    RT_GRAPHICS_TRAP_VOID("Canvas.Box: graphics support not compiled in");
}

/// @brief Stub for `Canvas.Frame` — would normally draw an unfilled axis-
///        aligned rectangle (outline only, 1px wide).
///
/// @param canvas Canvas handle (ignored).
/// @param x      Left edge in canvas pixels (ignored).
/// @param y      Top edge in canvas pixels (ignored).
/// @param w      Width in pixels; must be positive (ignored).
/// @param h      Height in pixels; must be positive (ignored).
/// @param color  Packed 0xAARRGGBB color (ignored).
void rt_canvas_frame(void *canvas, int64_t x, int64_t y, int64_t w, int64_t h, int64_t color) {
    (void)canvas;
    (void)x;
    (void)y;
    (void)w;
    (void)h;
    (void)color;
    RT_GRAPHICS_TRAP_VOID("Canvas.Frame: graphics support not compiled in");
}

/// @brief Stub for `Canvas.Disc` — would normally draw a filled circle.
///
/// Center is given in canvas pixels; radius is the distance from the center
/// to the rim (so the bounding box is `2*radius+1` pixels wide and tall).
///
/// @param canvas Canvas handle (ignored).
/// @param cx     Center x (ignored).
/// @param cy     Center y (ignored).
/// @param radius Radius in pixels; must be non-negative (ignored).
/// @param color  Packed 0xAARRGGBB color (ignored).
void rt_canvas_disc(void *canvas, int64_t cx, int64_t cy, int64_t radius, int64_t color) {
    (void)canvas;
    (void)cx;
    (void)cy;
    (void)radius;
    (void)color;
    RT_GRAPHICS_TRAP_VOID("Canvas.Disc: graphics support not compiled in");
}

/// @brief Stub for `Canvas.Ring` — would normally draw an unfilled circle
///        (outline only, 1px wide).
///
/// @param canvas Canvas handle (ignored).
/// @param cx     Center x (ignored).
/// @param cy     Center y (ignored).
/// @param radius Radius in pixels; must be non-negative (ignored).
/// @param color  Packed 0xAARRGGBB color (ignored).
void rt_canvas_ring(void *canvas, int64_t cx, int64_t cy, int64_t radius, int64_t color) {
    (void)canvas;
    (void)cx;
    (void)cy;
    (void)radius;
    (void)color;
    RT_GRAPHICS_TRAP_VOID("Canvas.Ring: graphics support not compiled in");
}

/// @brief Stub for `Canvas.Plot` — would normally light a single pixel.
///
/// `Plot` is the lowest-level draw primitive; everything else (Line, Box,
/// Disc) ultimately decomposes into Plot calls in the software renderer.
///
/// @param canvas Canvas handle (ignored).
/// @param x      Pixel x in canvas coordinates (ignored).
/// @param y      Pixel y in canvas coordinates (ignored).
/// @param color  Packed 0xAARRGGBB color (ignored).
void rt_canvas_plot(void *canvas, int64_t x, int64_t y, int64_t color) {
    (void)canvas;
    (void)x;
    (void)y;
    (void)color;
    RT_GRAPHICS_TRAP_VOID("Canvas.Plot: graphics support not compiled in");
}

/// @brief Stub for `Canvas.Poll` — would normally pump the OS event queue
///        and update keyboard/mouse/gamepad input snapshots for this frame.
///
/// In the real implementation this is the per-frame entry point that drives
/// `WasPressed` / `WasReleased` edge detection. Without a window there is
/// nothing to poll, so this traps so the missing event source is loud.
///
/// @param canvas Canvas handle (ignored).
///
/// @return Never returns normally.
int64_t rt_canvas_poll(void *canvas) {
    (void)canvas;
    RT_GRAPHICS_TRAP_RET("Canvas.Poll: graphics support not compiled in", 0);
}

/// @brief Stub for `Canvas.KeyHeld` — would normally report whether the
///        given key is currently held down.
///
/// @param canvas Canvas handle (ignored).
/// @param key    Platform-native key code (ignored).
///
/// @return Never returns normally.
int64_t rt_canvas_key_held(void *canvas, int64_t key) {
    (void)canvas;
    (void)key;
    RT_GRAPHICS_TRAP_RET("Canvas.KeyHeld: graphics support not compiled in", 0);
}

/// @brief Stub for `Canvas.Text` — would normally render a string in the
///        built-in 8x8 bitmap font.
///
/// `(x, y)` is the top-left of the first glyph; subsequent glyphs are laid
/// out left-to-right at 8px stride. No kerning, no bidi, no shaping.
///
/// @param canvas Canvas handle (ignored).
/// @param x      Top-left x of the first glyph in canvas pixels (ignored).
/// @param y      Top-left y of the first glyph in canvas pixels (ignored).
/// @param text   Glyph source string (ignored).
/// @param color  Packed 0xAARRGGBB foreground color (ignored).
void rt_canvas_text(void *canvas, int64_t x, int64_t y, rt_string text, int64_t color) {
    (void)canvas;
    (void)x;
    (void)y;
    (void)text;
    (void)color;
    RT_GRAPHICS_TRAP_VOID("Canvas.Text: graphics support not compiled in");
}

/// @brief Stub for `Canvas.TextBg` — would normally render text with both a
///        foreground glyph color and an opaque background fill behind each
///        glyph cell.
///
/// @param canvas Canvas handle (ignored).
/// @param x      Top-left x of the first glyph (ignored).
/// @param y      Top-left y of the first glyph (ignored).
/// @param text   Glyph source string (ignored).
/// @param fg     Packed 0xAARRGGBB foreground color (ignored).
/// @param bg     Packed 0xAARRGGBB background fill color (ignored).
void rt_canvas_text_bg(void *canvas, int64_t x, int64_t y, rt_string text, int64_t fg, int64_t bg) {
    (void)canvas;
    (void)x;
    (void)y;
    (void)text;
    (void)fg;
    (void)bg;
    RT_GRAPHICS_TRAP_VOID("Canvas.TextBg: graphics support not compiled in");
}

/// @brief Compute the width in pixels of `text` rendered in the built-in
///        8x8 bitmap font.
///
/// Unlike most stubs in this file, text *measurement* is intentionally still
/// functional in graphics-disabled builds — see the file header. The font is
/// fixed-width 8 pixels per glyph, so the answer is always
/// `length(text) * 8` (or `0` for a NULL string).
///
/// @param text Source string. NULL is treated as empty.
///
/// @return Width in canvas pixels. `0` if `text` is NULL.
int64_t rt_canvas_text_width(rt_string text) {
    if (!text)
        return 0;
    return rt_str_len(text) * 8;
}

/// @brief Return the row height of the built-in 8x8 bitmap font.
///
/// Constant 8 — the font is fixed-cell. Like `rt_canvas_text_width`, this is
/// a real implementation rather than a trap stub: text metrics work in any
/// build because they don't depend on the windowing backend.
///
/// @return Always `8`.
int64_t rt_canvas_text_height(void) {
    return 8;
}

/// @brief Stub for `Canvas.TextScaled` — would normally render text in the
///        built-in font with each pixel duplicated `scale` times along both
///        axes (nearest-neighbor blow-up, no smoothing).
///
/// @param canvas Canvas handle (ignored).
/// @param x      Top-left x of the first glyph (ignored).
/// @param y      Top-left y of the first glyph (ignored).
/// @param text   Glyph source string (ignored).
/// @param scale  Integer pixel multiplier; must be >= 1 (ignored).
/// @param color  Packed 0xAARRGGBB foreground color (ignored).
void rt_canvas_text_scaled(
    void *canvas, int64_t x, int64_t y, rt_string text, int64_t scale, int64_t color) {
    (void)canvas;
    (void)x;
    (void)y;
    (void)text;
    (void)scale;
    (void)color;
    RT_GRAPHICS_TRAP_VOID("Canvas.TextScaled: graphics support not compiled in");
}

/// @brief Stub for `Canvas.TextScaledBg` — scaled-text variant with an
///        opaque background fill behind each glyph cell.
///
/// @param canvas Canvas handle (ignored).
/// @param x      Top-left x of the first glyph (ignored).
/// @param y      Top-left y of the first glyph (ignored).
/// @param text   Glyph source string (ignored).
/// @param scale  Integer pixel multiplier; must be >= 1 (ignored).
/// @param fg     Packed 0xAARRGGBB foreground color (ignored).
/// @param bg     Packed 0xAARRGGBB background fill color (ignored).
void rt_canvas_text_scaled_bg(
    void *canvas, int64_t x, int64_t y, rt_string text, int64_t scale, int64_t fg, int64_t bg) {
    (void)canvas;
    (void)x;
    (void)y;
    (void)text;
    (void)scale;
    (void)fg;
    (void)bg;
    RT_GRAPHICS_TRAP_VOID("Canvas.TextScaledBg: graphics support not compiled in");
}

/// @brief Compute the rendered width of `text` at the given integer scale
///        in the built-in 8x8 bitmap font.
///
/// Real implementation (not a trap stub) — text metrics are backend-free.
/// Returns `0` for NULL text or non-positive `scale` so callers can use this
/// for layout math without first validating arguments.
///
/// @param text  Source string. NULL is treated as empty.
/// @param scale Integer pixel multiplier; values < 1 produce `0`.
///
/// @return Width in canvas pixels: `length(text) * 8 * scale`, or `0`.
int64_t rt_canvas_text_scaled_width(rt_string text, int64_t scale) {
    if (!text || scale < 1)
        return 0;
    return rt_str_len(text) * 8 * scale;
}

/// @brief Stub for `Canvas.TextCentered` — would normally horizontally
///        center `text` on the canvas at vertical position `y`.
///
/// @param canvas Canvas handle (ignored).
/// @param y      Top-left y for the rendered text (ignored).
/// @param text   Glyph source string (ignored).
/// @param color  Packed 0xAARRGGBB foreground color (ignored).
void rt_canvas_text_centered(void *canvas, int64_t y, rt_string text, int64_t color) {
    (void)canvas;
    (void)y;
    (void)text;
    (void)color;
    RT_GRAPHICS_TRAP_VOID("Canvas.TextCentered: graphics support not compiled in");
}

/// @brief Stub for `Canvas.TextRight` — would normally right-align `text`
///        with the right edge `margin` pixels in from the canvas's right
///        side.
///
/// @param canvas Canvas handle (ignored).
/// @param margin Distance from the right edge of the canvas in pixels (ignored).
/// @param y      Top-left y for the rendered text (ignored).
/// @param text   Glyph source string (ignored).
/// @param color  Packed 0xAARRGGBB foreground color (ignored).
void rt_canvas_text_right(void *canvas, int64_t margin, int64_t y, rt_string text, int64_t color) {
    (void)canvas;
    (void)margin;
    (void)y;
    (void)text;
    (void)color;
    RT_GRAPHICS_TRAP_VOID("Canvas.TextRight: graphics support not compiled in");
}

/// @brief Stub for `Canvas.TextCenteredScaled` — would horizontally center
///        `text` rendered at `scale` magnification at vertical position `y`.
///
/// @param canvas Canvas handle (ignored).
/// @param y      Top-left y for the rendered text (ignored).
/// @param text   Glyph source string (ignored).
/// @param color  Packed 0xAARRGGBB foreground color (ignored).
/// @param scale  Integer pixel multiplier; must be >= 1 (ignored).
void rt_canvas_text_centered_scaled(
    void *canvas, int64_t y, rt_string text, int64_t color, int64_t scale) {
    (void)canvas;
    (void)y;
    (void)text;
    (void)scale;
    (void)color;
    RT_GRAPHICS_TRAP_VOID("Canvas.TextCenteredScaled: graphics support not compiled in");
}

/// @brief Stub for `Canvas.BoxAlpha` — would normally draw a filled
///        rectangle blended with the destination using `alpha` as the
///        per-channel coverage (0 = transparent, 255 = opaque).
///
/// The `color` argument's own alpha byte is ignored; only the explicit
/// `alpha` parameter controls blending so callers can reuse opaque palette
/// constants without rebuilding them.
///
/// @param canvas Canvas handle (ignored).
/// @param x      Left edge in canvas pixels (ignored).
/// @param y      Top edge in canvas pixels (ignored).
/// @param w      Width in pixels; must be positive (ignored).
/// @param h      Height in pixels; must be positive (ignored).
/// @param color  Packed 0xAARRGGBB color; alpha byte ignored (ignored).
/// @param alpha  Coverage 0..255 (ignored).
void rt_canvas_box_alpha(
    void *canvas, int64_t x, int64_t y, int64_t w, int64_t h, int64_t color, int64_t alpha) {
    (void)canvas;
    (void)x;
    (void)y;
    (void)w;
    (void)h;
    (void)color;
    (void)alpha;
    RT_GRAPHICS_TRAP_VOID("Canvas.BoxAlpha: graphics support not compiled in");
}

/// @brief Stub for `Canvas.DiscAlpha` — alpha-blended filled circle. See
///        `rt_canvas_box_alpha` for blending semantics.
///
/// @param canvas Canvas handle (ignored).
/// @param cx     Center x (ignored).
/// @param cy     Center y (ignored).
/// @param radius Radius in pixels; must be non-negative (ignored).
/// @param color  Packed 0xAARRGGBB color; alpha byte ignored (ignored).
/// @param alpha  Coverage 0..255 (ignored).
void rt_canvas_disc_alpha(
    void *canvas, int64_t cx, int64_t cy, int64_t radius, int64_t color, int64_t alpha) {
    (void)canvas;
    (void)cx;
    (void)cy;
    (void)radius;
    (void)color;
    (void)alpha;
    RT_GRAPHICS_TRAP_VOID("Canvas.DiscAlpha: graphics support not compiled in");
}

/// @brief Stub for `Canvas.EllipseAlpha` — alpha-blended filled axis-aligned
///        ellipse with independent x and y radii.
///
/// @param canvas Canvas handle (ignored).
/// @param cx     Center x (ignored).
/// @param cy     Center y (ignored).
/// @param rx     X-axis radius in pixels (ignored).
/// @param ry     Y-axis radius in pixels (ignored).
/// @param color  Packed 0xAARRGGBB color; alpha byte ignored (ignored).
/// @param alpha  Coverage 0..255 (ignored).
void rt_canvas_ellipse_alpha(
    void *canvas, int64_t cx, int64_t cy, int64_t rx, int64_t ry, int64_t color, int64_t alpha) {
    (void)canvas;
    (void)cx;
    (void)cy;
    (void)rx;
    (void)ry;
    (void)color;
    (void)alpha;
    RT_GRAPHICS_TRAP_VOID("Canvas.EllipseAlpha: graphics support not compiled in");
}

/// @brief Stub for `Canvas.Blit` — would normally copy a Pixels surface
///        onto the canvas at `(x, y)` using straight overwrite (no blending).
///
/// `pixels` is opaque to this function — the real implementation reads the
/// width/height/stride out of the underlying `rt_pixels` object.
///
/// @param canvas Canvas handle (ignored).
/// @param x      Top-left destination x (ignored).
/// @param y      Top-left destination y (ignored).
/// @param pixels Source `rt_pixels` handle (ignored).
void rt_canvas_blit(void *canvas, int64_t x, int64_t y, void *pixels) {
    (void)canvas;
    (void)x;
    (void)y;
    (void)pixels;
    RT_GRAPHICS_TRAP_VOID("Canvas.Blit: graphics support not compiled in");
}

/// @brief Stub for `Canvas.BlitRegion` — would normally copy a sub-rect of
///        a Pixels surface onto the canvas with straight overwrite.
///
/// `(sx, sy, w, h)` selects a region within the source; `(dx, dy)` is the
/// destination origin. Used by sprite sheets and tile atlases to draw a
/// single frame without slicing the source asset.
///
/// @param canvas Canvas handle (ignored).
/// @param dx     Destination top-left x (ignored).
/// @param dy     Destination top-left y (ignored).
/// @param pixels Source `rt_pixels` handle (ignored).
/// @param sx     Source rect top-left x (ignored).
/// @param sy     Source rect top-left y (ignored).
/// @param w      Source rect width in pixels (ignored).
/// @param h      Source rect height in pixels (ignored).
void rt_canvas_blit_region(void *canvas,
                           int64_t dx,
                           int64_t dy,
                           void *pixels,
                           int64_t sx,
                           int64_t sy,
                           int64_t w,
                           int64_t h) {
    (void)canvas;
    (void)dx;
    (void)dy;
    (void)pixels;
    (void)sx;
    (void)sy;
    (void)w;
    (void)h;
    RT_GRAPHICS_TRAP_VOID("Canvas.BlitRegion: graphics support not compiled in");
}

/// @brief Stub for `Canvas.BlitAlpha` — would normally copy a Pixels surface
///        onto the canvas honoring per-pixel alpha (source-over blending).
///
/// Distinct from `rt_canvas_blit` which always overwrites; this variant is
/// what sprites with transparent pixels use.
///
/// @param canvas Canvas handle (ignored).
/// @param x      Top-left destination x (ignored).
/// @param y      Top-left destination y (ignored).
/// @param pixels Source `rt_pixels` handle with per-pixel alpha (ignored).
void rt_canvas_blit_alpha(void *canvas, int64_t x, int64_t y, void *pixels) {
    (void)canvas;
    (void)x;
    (void)y;
    (void)pixels;
    RT_GRAPHICS_TRAP_VOID("Canvas.BlitAlpha: graphics support not compiled in");
}

/// @brief Stub for `Canvas.ThickLine` — would normally draw a line segment
///        with `thickness` pixels of width.
///
/// `thickness == 1` would degenerate to `rt_canvas_line`. The real
/// implementation expands the centerline by `thickness/2` perpendicular to
/// the segment direction.
///
/// @param canvas    Canvas handle (ignored).
/// @param x1        Start point x (ignored).
/// @param y1        Start point y (ignored).
/// @param x2        End point x (ignored).
/// @param y2        End point y (ignored).
/// @param thickness Line width in pixels; must be >= 1 (ignored).
/// @param color     Packed 0xAARRGGBB color (ignored).
void rt_canvas_thick_line(void *canvas,
                          int64_t x1,
                          int64_t y1,
                          int64_t x2,
                          int64_t y2,
                          int64_t thickness,
                          int64_t color) {
    (void)canvas;
    (void)x1;
    (void)y1;
    (void)x2;
    (void)y2;
    (void)thickness;
    (void)color;
    RT_GRAPHICS_TRAP_VOID("Canvas.ThickLine: graphics support not compiled in");
}

/// @brief Stub for `Canvas.RoundBox` — would normally draw a filled
///        rounded-rectangle with corner `radius`.
///
/// Used by GUI widgets and HUD chrome. `radius` is clamped to half of the
/// shorter side in the real implementation; passing `0` degenerates to
/// `rt_canvas_box`.
///
/// @param canvas Canvas handle (ignored).
/// @param x      Left edge in canvas pixels (ignored).
/// @param y      Top edge in canvas pixels (ignored).
/// @param w      Width in pixels; must be positive (ignored).
/// @param h      Height in pixels; must be positive (ignored).
/// @param radius Corner radius in pixels (ignored).
/// @param color  Packed 0xAARRGGBB color (ignored).
void rt_canvas_round_box(
    void *canvas, int64_t x, int64_t y, int64_t w, int64_t h, int64_t radius, int64_t color) {
    (void)canvas;
    (void)x;
    (void)y;
    (void)w;
    (void)h;
    (void)radius;
    (void)color;
    RT_GRAPHICS_TRAP_VOID("Canvas.RoundBox: graphics support not compiled in");
}

/// @brief Stub for `Canvas.RoundFrame` — would normally draw the outline
///        (1px wide) of a rounded rectangle with corner `radius`.
///
/// @param canvas Canvas handle (ignored).
/// @param x      Left edge in canvas pixels (ignored).
/// @param y      Top edge in canvas pixels (ignored).
/// @param w      Width in pixels; must be positive (ignored).
/// @param h      Height in pixels; must be positive (ignored).
/// @param radius Corner radius in pixels (ignored).
/// @param color  Packed 0xAARRGGBB color (ignored).
void rt_canvas_round_frame(
    void *canvas, int64_t x, int64_t y, int64_t w, int64_t h, int64_t radius, int64_t color) {
    (void)canvas;
    (void)x;
    (void)y;
    (void)w;
    (void)h;
    (void)radius;
    (void)color;
    RT_GRAPHICS_TRAP_VOID("Canvas.RoundFrame: graphics support not compiled in");
}

/// @brief Stub for `Canvas.FloodFill` — would normally flood-fill the
///        4-connected region of pixels matching the color at `(x, y)` with
///        `color`.
///
/// Operates in screen-space using the framebuffer's current contents. The
/// real implementation uses an iterative scanline fill to avoid recursion
/// blowups on large regions.
///
/// @param canvas Canvas handle (ignored).
/// @param x      Seed pixel x (ignored).
/// @param y      Seed pixel y (ignored).
/// @param color  Replacement color, packed 0xAARRGGBB (ignored).
void rt_canvas_flood_fill(void *canvas, int64_t x, int64_t y, int64_t color) {
    (void)canvas;
    (void)x;
    (void)y;
    (void)color;
    RT_GRAPHICS_TRAP_VOID("Canvas.FloodFill: graphics support not compiled in");
}

/// @brief Stub for `Canvas.Triangle` — would normally draw a filled
///        triangle with the three given vertices.
///
/// Vertex winding does not matter; the rasterizer is direction-agnostic.
///
/// @param canvas Canvas handle (ignored).
/// @param x1     Vertex 1 x (ignored).
/// @param y1     Vertex 1 y (ignored).
/// @param x2     Vertex 2 x (ignored).
/// @param y2     Vertex 2 y (ignored).
/// @param x3     Vertex 3 x (ignored).
/// @param y3     Vertex 3 y (ignored).
/// @param color  Packed 0xAARRGGBB color (ignored).
void rt_canvas_triangle(void *canvas,
                        int64_t x1,
                        int64_t y1,
                        int64_t x2,
                        int64_t y2,
                        int64_t x3,
                        int64_t y3,
                        int64_t color) {
    (void)canvas;
    (void)x1;
    (void)y1;
    (void)x2;
    (void)y2;
    (void)x3;
    (void)y3;
    (void)color;
    RT_GRAPHICS_TRAP_VOID("Canvas.Triangle: graphics support not compiled in");
}

/// @brief Stub for `Canvas.TriangleFrame` — would normally draw the outline
///        (3 line segments) of a triangle.
///
/// @param canvas Canvas handle (ignored).
/// @param x1     Vertex 1 x (ignored).
/// @param y1     Vertex 1 y (ignored).
/// @param x2     Vertex 2 x (ignored).
/// @param y2     Vertex 2 y (ignored).
/// @param x3     Vertex 3 x (ignored).
/// @param y3     Vertex 3 y (ignored).
/// @param color  Packed 0xAARRGGBB color (ignored).
void rt_canvas_triangle_frame(void *canvas,
                              int64_t x1,
                              int64_t y1,
                              int64_t x2,
                              int64_t y2,
                              int64_t x3,
                              int64_t y3,
                              int64_t color) {
    (void)canvas;
    (void)x1;
    (void)y1;
    (void)x2;
    (void)y2;
    (void)x3;
    (void)y3;
    (void)color;
    RT_GRAPHICS_TRAP_VOID("Canvas.TriangleFrame: graphics support not compiled in");
}

/// @brief Stub for `Canvas.Ellipse` — would normally draw a filled
///        axis-aligned ellipse with independent x and y radii.
///
/// @param canvas Canvas handle (ignored).
/// @param cx     Center x (ignored).
/// @param cy     Center y (ignored).
/// @param rx     X-axis radius in pixels (ignored).
/// @param ry     Y-axis radius in pixels (ignored).
/// @param color  Packed 0xAARRGGBB color (ignored).
void rt_canvas_ellipse(
    void *canvas, int64_t cx, int64_t cy, int64_t rx, int64_t ry, int64_t color) {
    (void)canvas;
    (void)cx;
    (void)cy;
    (void)rx;
    (void)ry;
    (void)color;
    RT_GRAPHICS_TRAP_VOID("Canvas.Ellipse: graphics support not compiled in");
}

/// @brief Stub for `Canvas.EllipseFrame` — would normally draw the outline
///        of an axis-aligned ellipse.
///
/// @param canvas Canvas handle (ignored).
/// @param cx     Center x (ignored).
/// @param cy     Center y (ignored).
/// @param rx     X-axis radius in pixels (ignored).
/// @param ry     Y-axis radius in pixels (ignored).
/// @param color  Packed 0xAARRGGBB color (ignored).
void rt_canvas_ellipse_frame(
    void *canvas, int64_t cx, int64_t cy, int64_t rx, int64_t ry, int64_t color) {
    (void)canvas;
    (void)cx;
    (void)cy;
    (void)rx;
    (void)ry;
    (void)color;
    RT_GRAPHICS_TRAP_VOID("Canvas.EllipseFrame: graphics support not compiled in");
}

/// @brief Stub for `Canvas.Arc` — would normally draw a filled circular
///        arc between `start_angle` and `end_angle`.
///
/// Angles are in degrees, measured clockwise from 12 o'clock (matching the
/// rest of the runtime's angle convention). The "filled" form is a pie
/// slice — connecting the arc endpoints back to the center.
///
/// @param canvas      Canvas handle (ignored).
/// @param cx          Center x (ignored).
/// @param cy          Center y (ignored).
/// @param radius      Radius in pixels (ignored).
/// @param start_angle Sweep start in degrees (ignored).
/// @param end_angle   Sweep end in degrees (ignored).
/// @param color       Packed 0xAARRGGBB color (ignored).
void rt_canvas_arc(void *canvas,
                   int64_t cx,
                   int64_t cy,
                   int64_t radius,
                   int64_t start_angle,
                   int64_t end_angle,
                   int64_t color) {
    (void)canvas;
    (void)cx;
    (void)cy;
    (void)radius;
    (void)start_angle;
    (void)end_angle;
    (void)color;
    RT_GRAPHICS_TRAP_VOID("Canvas.Arc: graphics support not compiled in");
}

/// @brief Stub for `Canvas.ArcFrame` — outline-only variant of `Arc`. Draws
///        just the curved rim, not the radial spokes connecting the
///        endpoints to the center.
///
/// @param canvas      Canvas handle (ignored).
/// @param cx          Center x (ignored).
/// @param cy          Center y (ignored).
/// @param radius      Radius in pixels (ignored).
/// @param start_angle Sweep start in degrees (ignored).
/// @param end_angle   Sweep end in degrees (ignored).
/// @param color       Packed 0xAARRGGBB color (ignored).
void rt_canvas_arc_frame(void *canvas,
                         int64_t cx,
                         int64_t cy,
                         int64_t radius,
                         int64_t start_angle,
                         int64_t end_angle,
                         int64_t color) {
    (void)canvas;
    (void)cx;
    (void)cy;
    (void)radius;
    (void)start_angle;
    (void)end_angle;
    (void)color;
    RT_GRAPHICS_TRAP_VOID("Canvas.ArcFrame: graphics support not compiled in");
}

/// @brief Stub for `Canvas.Bezier` — would normally draw a quadratic
///        Bézier curve from `(x1, y1)` to `(x2, y2)` with `(cx, cy)` as the
///        single control point.
///
/// @param canvas Canvas handle (ignored).
/// @param x1     Start point x (ignored).
/// @param y1     Start point y (ignored).
/// @param cx     Control point x (ignored).
/// @param cy     Control point y (ignored).
/// @param x2     End point x (ignored).
/// @param y2     End point y (ignored).
/// @param color  Packed 0xAARRGGBB color (ignored).
void rt_canvas_bezier(void *canvas,
                      int64_t x1,
                      int64_t y1,
                      int64_t cx,
                      int64_t cy,
                      int64_t x2,
                      int64_t y2,
                      int64_t color) {
    (void)canvas;
    (void)x1;
    (void)y1;
    (void)cx;
    (void)cy;
    (void)x2;
    (void)y2;
    (void)color;
    RT_GRAPHICS_TRAP_VOID("Canvas.Bezier: graphics support not compiled in");
}

/// @brief Stub for `Canvas.Polyline` — would normally draw a sequence of
///        line segments connecting consecutive vertices in `points`.
///
/// `points` is an `rt_int64list`-style buffer of interleaved `(x, y)` pairs;
/// `count` is the number of vertices (so the buffer holds `2*count` ints).
/// The polyline is open — the last vertex is *not* connected back to the
/// first. Use `rt_canvas_polygon_frame` for a closed outline.
///
/// @param canvas Canvas handle (ignored).
/// @param points Vertex buffer of interleaved (x, y) ints (ignored).
/// @param count  Vertex count (ignored).
/// @param color  Packed 0xAARRGGBB color (ignored).
void rt_canvas_polyline(void *canvas, void *points, int64_t count, int64_t color) {
    (void)canvas;
    (void)points;
    (void)count;
    (void)color;
    RT_GRAPHICS_TRAP_VOID("Canvas.Polyline: graphics support not compiled in");
}

/// @brief Stub for `Canvas.Polygon` — would normally draw a filled polygon
///        with the given vertices.
///
/// Vertex buffer layout matches `rt_canvas_polyline`. The polygon is implicitly
/// closed (an edge is drawn from the last vertex back to the first).
/// Self-intersecting polygons follow the even-odd fill rule.
///
/// @param canvas Canvas handle (ignored).
/// @param points Vertex buffer of interleaved (x, y) ints (ignored).
/// @param count  Vertex count (ignored).
/// @param color  Packed 0xAARRGGBB color (ignored).
void rt_canvas_polygon(void *canvas, void *points, int64_t count, int64_t color) {
    (void)canvas;
    (void)points;
    (void)count;
    (void)color;
    RT_GRAPHICS_TRAP_VOID("Canvas.Polygon: graphics support not compiled in");
}

/// @brief Stub for `Canvas.PolygonFrame` — outline-only variant of
///        `Polygon`. Draws closed-path edges only, no fill.
///
/// @param canvas Canvas handle (ignored).
/// @param points Vertex buffer of interleaved (x, y) ints (ignored).
/// @param count  Vertex count (ignored).
/// @param color  Packed 0xAARRGGBB color (ignored).
void rt_canvas_polygon_frame(void *canvas, void *points, int64_t count, int64_t color) {
    (void)canvas;
    (void)points;
    (void)count;
    (void)color;
    RT_GRAPHICS_TRAP_VOID("Canvas.PolygonFrame: graphics support not compiled in");
}

/// @brief Stub for `Canvas.GetPixel` — would normally read the
///        framebuffer's current color at `(x, y)`.
///
/// Out-of-bounds reads in the real implementation return `0`; here the call
/// traps before it can be tested.
///
/// @param canvas Canvas handle (ignored).
/// @param x      Pixel x in canvas coordinates (ignored).
/// @param y      Pixel y in canvas coordinates (ignored).
///
/// @return Never returns normally.
int64_t rt_canvas_get_pixel(void *canvas, int64_t x, int64_t y) {
    (void)canvas;
    (void)x;
    (void)y;
    RT_GRAPHICS_TRAP_RET("Canvas.GetPixel: graphics support not compiled in", 0);
}

/// @brief Stub for `Canvas.CopyRect` — would normally allocate a fresh
///        Pixels surface and copy the canvas's `(x, y, w, h)` rect into it.
///
/// In the real implementation the returned object is GC-managed and owns
/// its own storage (no aliasing back to the canvas).
///
/// @param canvas Canvas handle (ignored).
/// @param x      Source rect top-left x (ignored).
/// @param y      Source rect top-left y (ignored).
/// @param w      Source rect width (ignored).
/// @param h      Source rect height (ignored).
///
/// @return Never returns normally.
void *rt_canvas_copy_rect(void *canvas, int64_t x, int64_t y, int64_t w, int64_t h) {
    (void)canvas;
    (void)x;
    (void)y;
    (void)w;
    (void)h;
    RT_GRAPHICS_TRAP_RET("Canvas.CopyRect: graphics support not compiled in", NULL);
}

/// @brief Stub for `Canvas.SaveBmp` — would normally serialize the
///        canvas's current framebuffer to a 24-bit BMP file at `path`.
///
/// @param canvas Canvas handle (ignored).
/// @param path   Output file path (ignored).
///
/// @return Never returns normally; would otherwise return non-zero on
///         success and `0` on I/O failure.
int64_t rt_canvas_save_bmp(void *canvas, rt_string path) {
    (void)canvas;
    (void)path;
    RT_GRAPHICS_TRAP_RET("Canvas.SaveBmp: graphics support not compiled in", 0);
}

/// @brief Stub for `Canvas.SavePng` — would normally serialize the
///        canvas's current framebuffer to a PNG file at `path`.
///
/// The real implementation uses Viper's from-scratch PNG encoder (no
/// libpng) and supports all 5 PNG color types.
///
/// @param canvas Canvas handle (ignored).
/// @param path   Output file path (ignored).
///
/// @return Never returns normally; would otherwise return non-zero on
///         success and `0` on I/O failure.
int64_t rt_canvas_save_png(void *canvas, rt_string path) {
    (void)canvas;
    (void)path;
    RT_GRAPHICS_TRAP_RET("Canvas.SavePng: graphics support not compiled in", 0);
}

// Color constants — packed 0x00RRGGBB
/// @brief Return the predefined red color constant.
int64_t rt_color_red(void) {
    return 0xFF0000;
}

/// @brief Return the predefined green color constant.
int64_t rt_color_green(void) {
    return 0x00FF00;
}

/// @brief Return the predefined blue color constant.
int64_t rt_color_blue(void) {
    return 0x0000FF;
}

/// @brief Return the predefined white color constant.
int64_t rt_color_white(void) {
    return 0xFFFFFF;
}

/// @brief Return the predefined black color constant.
int64_t rt_color_black(void) {
    return 0x000000;
}

/// @brief Return the predefined yellow color constant.
int64_t rt_color_yellow(void) {
    return 0xFFFF00;
}

/// @brief Return the predefined cyan color constant.
int64_t rt_color_cyan(void) {
    return 0x00FFFF;
}

/// @brief Return the predefined magenta color constant.
int64_t rt_color_magenta(void) {
    return 0xFF00FF;
}

/// @brief Return the predefined gray color constant.
int64_t rt_color_gray(void) {
    return 0x808080;
}

/// @brief Return the predefined orange color constant.
int64_t rt_color_orange(void) {
    return 0xFFA500;
}

/// @brief Construct a color from red, green, blue components (0-255).
int64_t rt_color_rgb(int64_t r, int64_t g, int64_t b) {
    uint8_t r8 = (r < 0) ? 0 : (r > 255) ? 255 : (uint8_t)r;
    uint8_t g8 = (g < 0) ? 0 : (g > 255) ? 255 : (uint8_t)g;
    uint8_t b8 = (b < 0) ? 0 : (b > 255) ? 255 : (uint8_t)b;
    return (int64_t)(((uint32_t)r8 << 16) | ((uint32_t)g8 << 8) | (uint32_t)b8);
}

/// @brief Construct a color from red, green, blue, alpha components (0-255).
int64_t rt_color_rgba(int64_t r, int64_t g, int64_t b, int64_t a) {
    uint8_t r8 = (r < 0) ? 0 : (r > 255) ? 255 : (uint8_t)r;
    uint8_t g8 = (g < 0) ? 0 : (g > 255) ? 255 : (uint8_t)g;
    uint8_t b8 = (b < 0) ? 0 : (b > 255) ? 255 : (uint8_t)b;
    uint8_t a8 = (a < 0) ? 0 : (a > 255) ? 255 : (uint8_t)a;
    return (int64_t)(((uint32_t)a8 << 24) | ((uint32_t)r8 << 16) | ((uint32_t)g8 << 8) |
                     (uint32_t)b8);
}

/// @brief Hsl the from.
int64_t rt_color_from_hsl(int64_t h, int64_t s, int64_t l) {
    h = ((h % 360) + 360) % 360;
    if (s < 0)
        s = 0;
    if (s > 100)
        s = 100;
    if (l < 0)
        l = 0;
    if (l > 100)
        l = 100;

    int64_t r, g, b;
    rtg_hsl_to_rgb(h, s, l, &r, &g, &b);
    return ((r & 0xFF) << 16) | ((g & 0xFF) << 8) | (b & 0xFF);
}

/// @brief Get the h value.
/// @param color
/// @return Result value.
int64_t rt_color_get_h(int64_t color) {
    int64_t r = (color >> 16) & 0xFF;
    int64_t g = (color >> 8) & 0xFF;
    int64_t b = color & 0xFF;
    int64_t h, s, l;
    rtg_rgb_to_hsl(r, g, b, &h, &s, &l);
    return h;
}

/// @brief Get the s value.
/// @param color
/// @return Result value.
int64_t rt_color_get_s(int64_t color) {
    int64_t r = (color >> 16) & 0xFF;
    int64_t g = (color >> 8) & 0xFF;
    int64_t b = color & 0xFF;
    int64_t h, s, l;
    rtg_rgb_to_hsl(r, g, b, &h, &s, &l);
    return s;
}

/// @brief Extract the lightness (L) component of a packed RGB color in the
///        HSL color space.
///
/// Real implementation — Color helpers operate purely on the integer
/// representation, so they remain functional in graphics-disabled builds.
/// Routes through the shared `rtg_rgb_to_hsl` helper.
///
/// @param color Packed 0xAARRGGBB color; alpha is ignored.
///
/// @return Lightness in 0..100 (percentage).
int64_t rt_color_get_l(int64_t color) {
    int64_t r = (color >> 16) & 0xFF;
    int64_t g = (color >> 8) & 0xFF;
    int64_t b = color & 0xFF;
    int64_t h, s, l;
    rtg_rgb_to_hsl(r, g, b, &h, &s, &l);
    return l;
}

/// @brief Linearly interpolate between two packed RGB colors.
///
/// Real implementation. Each channel is interpolated independently with
/// `t` clamped to 0..100. Alpha bytes are ignored — the result has alpha 0.
///
/// @param c1 Start color, packed 0xAARRGGBB.
/// @param c2 End color, packed 0xAARRGGBB.
/// @param t  Interpolation parameter as a percentage (0 = c1, 100 = c2);
///           clamped before use.
///
/// @return Interpolated color, packed 0x00RRGGBB.
int64_t rt_color_lerp(int64_t c1, int64_t c2, int64_t t) {
    if (t < 0)
        t = 0;
    if (t > 100)
        t = 100;

    int64_t r1 = (c1 >> 16) & 0xFF;
    int64_t g1 = (c1 >> 8) & 0xFF;
    int64_t b1 = c1 & 0xFF;
    int64_t r2 = (c2 >> 16) & 0xFF;
    int64_t g2 = (c2 >> 8) & 0xFF;
    int64_t b2 = c2 & 0xFF;

    int64_t r = r1 + (r2 - r1) * t / 100;
    int64_t g = g1 + (g2 - g1) * t / 100;
    int64_t b = b1 + (b2 - b1) * t / 100;
    return ((r & 0xFF) << 16) | ((g & 0xFF) << 8) | (b & 0xFF);
}

/// @brief Extract the red byte from a packed 0xAARRGGBB color.
///
/// @param color Packed color.
///
/// @return Red component in 0..255.
int64_t rt_color_get_r(int64_t color) {
    return (color >> 16) & 0xFF;
}

/// @brief Extract the green byte from a packed 0xAARRGGBB color.
///
/// @param color Packed color.
///
/// @return Green component in 0..255.
int64_t rt_color_get_g(int64_t color) {
    return (color >> 8) & 0xFF;
}

/// @brief Extract the blue byte from a packed 0xAARRGGBB color.
///
/// @param color Packed color.
///
/// @return Blue component in 0..255.
int64_t rt_color_get_b(int64_t color) {
    return color & 0xFF;
}

/// @brief Extract the alpha byte from a packed 0xAARRGGBB color.
///
/// @param color Packed color.
///
/// @return Alpha component in 0..255 (0 = fully transparent, 255 = opaque).
int64_t rt_color_get_a(int64_t color) {
    return (color >> 24) & 0xFF;
}

/// @brief Brighten an RGB color by interpolating each channel toward 255.
///
/// Real implementation. Each channel is shifted `amount`% of the way toward
/// white. `amount = 0` returns the original color; `amount = 100` returns
/// pure white. Operates per-channel — does not preserve hue under saturation.
///
/// @param color  Packed 0xAARRGGBB color; alpha is dropped from the result.
/// @param amount Brightening percentage 0..100; clamped before use.
///
/// @return Brightened color, packed 0x00RRGGBB.
int64_t rt_color_brighten(int64_t color, int64_t amount) {
    if (amount < 0)
        amount = 0;
    if (amount > 100)
        amount = 100;

    int64_t r = (color >> 16) & 0xFF;
    int64_t g = (color >> 8) & 0xFF;
    int64_t b = color & 0xFF;

    r = r + (255 - r) * amount / 100;
    g = g + (255 - g) * amount / 100;
    b = b + (255 - b) * amount / 100;
    return ((r & 0xFF) << 16) | ((g & 0xFF) << 8) | (b & 0xFF);
}

/// @brief Darken an RGB color by interpolating each channel toward 0.
///
/// Real implementation, mirror of `rt_color_brighten`. `amount = 0` returns
/// the original color; `amount = 100` returns pure black.
///
/// @param color  Packed 0xAARRGGBB color; alpha is dropped from the result.
/// @param amount Darkening percentage 0..100; clamped before use.
///
/// @return Darkened color, packed 0x00RRGGBB.
int64_t rt_color_darken(int64_t color, int64_t amount) {
    if (amount < 0)
        amount = 0;
    if (amount > 100)
        amount = 100;

    int64_t r = (color >> 16) & 0xFF;
    int64_t g = (color >> 8) & 0xFF;
    int64_t b = color & 0xFF;

    r = r - r * amount / 100;
    g = g - g * amount / 100;
    b = b - b * amount / 100;
    return ((r & 0xFF) << 16) | ((g & 0xFF) << 8) | (b & 0xFF);
}

/// @brief Parse a CSS-style hex color string into a packed integer.
///
/// Real implementation. Accepts an optional leading `#` and three forms:
///   - 6-digit `RRGGBB` → returns `0x00RRGGBB`
///   - 8-digit `RRGGBBAA` → returns `0xAARRGGBB` (alpha re-positioned)
///   - 3-digit `RGB` shorthand → expanded to `0x00RRGGBB` by duplicating each
///     nibble (e.g. `#F0A` → `0x00FF00AA`)
/// Any other length is parsed as raw hex without further reinterpretation.
///
/// @param hex Source string. NULL or non-hex content returns 0.
///
/// @return Packed color value (see length-specific layout above), or 0.
int64_t rt_color_from_hex(rt_string hex) {
    const char *s = rt_string_cstr(hex);
    if (!s)
        return 0;
    if (*s == '#')
        s++;
    size_t len = strlen(s);
    unsigned long val = strtoul(s, NULL, 16);
    if (len == 6)
        return (int64_t)val;
    if (len == 8) {
        int64_t r = (val >> 24) & 0xFF;
        int64_t g = (val >> 16) & 0xFF;
        int64_t b = (val >> 8) & 0xFF;
        int64_t a = val & 0xFF;
        return (a << 24) | (r << 16) | (g << 8) | b;
    }
    if (len == 3) {
        int64_t r = (val >> 8) & 0xF;
        int64_t g = (val >> 4) & 0xF;
        int64_t b = val & 0xF;
        return ((r | (r << 4)) << 16) | ((g | (g << 4)) << 8) | (b | (b << 4));
    }
    return (int64_t)val;
}

/// @brief Hex the to.
rt_string rt_color_to_hex(int64_t color) {
    char buf[10];
    int64_t a = (color >> 24) & 0xFF;
    int64_t r = (color >> 16) & 0xFF;
    int64_t g = (color >> 8) & 0xFF;
    int64_t b = color & 0xFF;
    int len;
    if (a != 0)
        len = snprintf(buf, sizeof(buf), "#%02X%02X%02X%02X", (int)r, (int)g, (int)b, (int)a);
    else
        len = snprintf(buf, sizeof(buf), "#%02X%02X%02X", (int)r, (int)g, (int)b);
    return rt_string_from_bytes(buf, (size_t)len);
}

/// @brief Increase the saturation of an RGB color via HSL round-trip.
///
/// Real implementation. Converts to HSL, adds `amount` to the saturation
/// channel (clamping at 100), then converts back to RGB. Hue and lightness
/// are preserved exactly. `amount = 0` returns the input unchanged;
/// `amount = 100` produces fully saturated output.
///
/// @param color  Packed 0xAARRGGBB color; alpha is dropped from the result.
/// @param amount Saturation delta as a percentage (0..100); clamped.
///
/// @return Saturated color, packed 0x00RRGGBB.
int64_t rt_color_saturate(int64_t color, int64_t amount) {
    if (amount < 0)
        amount = 0;
    if (amount > 100)
        amount = 100;
    int64_t r = (color >> 16) & 0xFF;
    int64_t g = (color >> 8) & 0xFF;
    int64_t b = color & 0xFF;
    int64_t h, s, l;
    rtg_rgb_to_hsl(r, g, b, &h, &s, &l);
    s += amount;
    if (s > 100)
        s = 100;
    rtg_hsl_to_rgb(h, s, l, &r, &g, &b);
    return ((r & 0xFF) << 16) | ((g & 0xFF) << 8) | (b & 0xFF);
}

/// @brief Decrease the saturation of an RGB color via HSL round-trip.
///
/// Real implementation, mirror of `rt_color_saturate`. `amount = 100`
/// reduces the color to a pure gray (saturation 0).
///
/// @param color  Packed 0xAARRGGBB color; alpha is dropped from the result.
/// @param amount Saturation delta as a percentage (0..100); clamped.
///
/// @return Desaturated color, packed 0x00RRGGBB.
int64_t rt_color_desaturate(int64_t color, int64_t amount) {
    if (amount < 0)
        amount = 0;
    if (amount > 100)
        amount = 100;
    int64_t r = (color >> 16) & 0xFF;
    int64_t g = (color >> 8) & 0xFF;
    int64_t b = color & 0xFF;
    int64_t h, s, l;
    rtg_rgb_to_hsl(r, g, b, &h, &s, &l);
    s -= amount;
    if (s < 0)
        s = 0;
    rtg_hsl_to_rgb(h, s, l, &r, &g, &b);
    return ((r & 0xFF) << 16) | ((g & 0xFF) << 8) | (b & 0xFF);
}

/// @brief Compute the hue-complement of an RGB color (180° hue rotation).
///
/// Real implementation. Used by UI palettes to derive a high-contrast
/// accent from a primary color. Saturation and lightness are preserved.
///
/// @param color Packed 0xAARRGGBB color; alpha is dropped from the result.
///
/// @return Complementary color, packed 0x00RRGGBB.
int64_t rt_color_complement(int64_t color) {
    int64_t r = (color >> 16) & 0xFF;
    int64_t g = (color >> 8) & 0xFF;
    int64_t b = color & 0xFF;
    int64_t h, s, l;
    rtg_rgb_to_hsl(r, g, b, &h, &s, &l);
    h = (h + 180) % 360;
    rtg_hsl_to_rgb(h, s, l, &r, &g, &b);
    return ((r & 0xFF) << 16) | ((g & 0xFF) << 8) | (b & 0xFF);
}

/// @brief Convert an RGB color to grayscale using ITU-R BT.601 luma weights.
///
/// Real implementation. Applies the standard NTSC/JPEG luma formula
/// `Y = 0.299*R + 0.587*G + 0.114*B`, broadcasts the resulting brightness
/// to all three channels, and returns the packed gray.
///
/// @param color Packed 0xAARRGGBB color; alpha is dropped from the result.
///
/// @return Gray color, packed 0x00YYYYYY where YY = computed luma.
int64_t rt_color_grayscale(int64_t color) {
    int64_t r = (color >> 16) & 0xFF;
    int64_t g = (color >> 8) & 0xFF;
    int64_t b = color & 0xFF;
    int64_t gray = (r * 299 + g * 587 + b * 114) / 1000;
    return ((gray & 0xFF) << 16) | ((gray & 0xFF) << 8) | (gray & 0xFF);
}

/// @brief Bitwise-invert each RGB channel of a packed color.
///
/// Real implementation. Each channel is replaced by `255 - channel`. Alpha
/// is dropped. Note: this is per-channel inversion, not hue rotation —
/// for the latter use `rt_color_complement`.
///
/// @param color Packed 0xAARRGGBB color; alpha is dropped from the result.
///
/// @return Inverted color, packed 0x00RRGGBB.
int64_t rt_color_invert(int64_t color) {
    int64_t r = 255 - ((color >> 16) & 0xFF);
    int64_t g = 255 - ((color >> 8) & 0xFF);
    int64_t b = 255 - (color & 0xFF);
    return ((r & 0xFF) << 16) | ((g & 0xFF) << 8) | (b & 0xFF);
}

/// @brief Stub for `Canvas.SetClipRect` — would normally constrain
///        subsequent draw calls to the rectangle `(x, y, w, h)`.
///
/// The clip rect is stateful and persists until the next call to
/// `SetClipRect` or `ClearClipRect`. Pixels outside the clip are dropped
/// before they hit the framebuffer.
///
/// @param canvas Canvas handle (ignored).
/// @param x      Left edge of clip rect in canvas pixels (ignored).
/// @param y      Top edge of clip rect (ignored).
/// @param w      Width of clip rect; must be positive (ignored).
/// @param h      Height of clip rect; must be positive (ignored).
void rt_canvas_set_clip_rect(void *canvas, int64_t x, int64_t y, int64_t w, int64_t h) {
    (void)canvas;
    (void)x;
    (void)y;
    (void)w;
    (void)h;
    RT_GRAPHICS_TRAP_VOID("Canvas.SetClipRect: graphics support not compiled in");
}

/// @brief Stub for `Canvas.ClearClipRect` — would normally restore the
///        clip rect to the full canvas (no clipping).
///
/// @param canvas Canvas handle (ignored).
void rt_canvas_clear_clip_rect(void *canvas) {
    (void)canvas;
    RT_GRAPHICS_TRAP_VOID("Canvas.ClearClipRect: graphics support not compiled in");
}

/// @brief Stub for `Canvas.SetTitle` — would normally update the OS
///        window's title bar.
///
/// @param canvas Canvas handle (ignored).
/// @param title  New title string; lifetime managed by the caller (ignored).
void rt_canvas_set_title(void *canvas, rt_string title) {
    (void)canvas;
    (void)title;
    RT_GRAPHICS_TRAP_VOID("Canvas.SetTitle: graphics support not compiled in");
}

/// @brief Stub for `Canvas.GetTitle` — would normally return the title
///        string passed to `Canvas.New` (or last `SetTitle` call).
///
/// @param canvas Canvas handle (ignored).
///
/// @return Never returns normally; the empty-string fallback is unreachable.
rt_string rt_canvas_get_title(void *canvas) {
    (void)canvas;
    RT_GRAPHICS_TRAP_RET("Canvas.GetTitle: graphics support not compiled in", rt_str_empty());
}

/// @brief Stub for `Canvas.Resize` — would normally request the OS to
///        resize the underlying window to `(width, height)` logical pixels.
///
/// On macOS the window's frame origin is preserved; the new size may be
/// clipped by the screen bounds.
///
/// @param canvas Canvas handle (ignored).
/// @param width  Requested logical width in pixels (ignored).
/// @param height Requested logical height in pixels (ignored).
void rt_canvas_resize(void *canvas, int64_t width, int64_t height) {
    (void)canvas;
    (void)width;
    (void)height;
    RT_GRAPHICS_TRAP_VOID("Canvas.Resize: graphics support not compiled in");
}

/// @brief Stub for `Canvas.Close` — would normally request the window be
///        closed and signal `should_close` to the game loop.
///
/// Distinct from `Canvas.Destroy`: `Close` is a graceful request that the
/// game loop can observe and respond to (saving state, etc.); `Destroy`
/// tears the window down immediately.
///
/// @param canvas Canvas handle (ignored).
void rt_canvas_close(void *canvas) {
    (void)canvas;
    RT_GRAPHICS_TRAP_VOID("Canvas.Close: graphics support not compiled in");
}

/// @brief Stub for `Canvas.Screenshot` — would normally allocate a fresh
///        Pixels surface containing the current framebuffer contents.
///
/// The returned object is GC-managed and owns its own storage in the real
/// implementation.
///
/// @param canvas Canvas handle (ignored).
///
/// @return Never returns normally.
void *rt_canvas_screenshot(void *canvas) {
    (void)canvas;
    RT_GRAPHICS_TRAP_RET("Canvas.Screenshot: graphics support not compiled in", NULL);
}

/// @brief Stub for `Canvas.Fullscreen` — would normally switch the window
///        to exclusive or borderless fullscreen mode.
///
/// Mode selection (exclusive vs borderless) is a backend choice; the
/// runtime API does not expose it.
///
/// @param canvas Canvas handle (ignored).
void rt_canvas_fullscreen(void *canvas) {
    (void)canvas;
    RT_GRAPHICS_TRAP_VOID("Canvas.Fullscreen: graphics support not compiled in");
}

/// @brief Stub for `Canvas.Windowed` — would normally restore a fullscreen
///        window to its previous windowed dimensions and frame origin.
///
/// @param canvas Canvas handle (ignored).
void rt_canvas_windowed(void *canvas) {
    (void)canvas;
    RT_GRAPHICS_TRAP_VOID("Canvas.Windowed: graphics support not compiled in");
}

/// @brief Stub for `Canvas.GradientH` — would normally draw a horizontal
///        linear gradient from `c1` (left edge) to `c2` (right edge) inside
///        the rectangle `(x, y, w, h)`.
///
/// Interpolation is per-channel linear in 0xRRGGBB space (no gamma
/// correction); see `rt_color_lerp` for the same primitive used for single
/// colors.
///
/// @param canvas Canvas handle (ignored).
/// @param x      Left edge of the gradient rect (ignored).
/// @param y      Top edge of the gradient rect (ignored).
/// @param w      Width in pixels; must be positive (ignored).
/// @param h      Height in pixels; must be positive (ignored).
/// @param c1     Color at the left edge, packed 0xAARRGGBB (ignored).
/// @param c2     Color at the right edge, packed 0xAARRGGBB (ignored).
void rt_canvas_gradient_h(
    void *canvas, int64_t x, int64_t y, int64_t w, int64_t h, int64_t c1, int64_t c2) {
    (void)canvas;
    (void)x;
    (void)y;
    (void)w;
    (void)h;
    (void)c1;
    (void)c2;
    RT_GRAPHICS_TRAP_VOID("Canvas.GradientH: graphics support not compiled in");
}

/// @brief Stub for `Canvas.GradientV` — vertical gradient variant of
///        `GradientH`. `c1` is at the top edge, `c2` at the bottom.
///
/// @param canvas Canvas handle (ignored).
/// @param x      Left edge of the gradient rect (ignored).
/// @param y      Top edge of the gradient rect (ignored).
/// @param w      Width in pixels; must be positive (ignored).
/// @param h      Height in pixels; must be positive (ignored).
/// @param c1     Color at the top edge, packed 0xAARRGGBB (ignored).
/// @param c2     Color at the bottom edge, packed 0xAARRGGBB (ignored).
void rt_canvas_gradient_v(
    void *canvas, int64_t x, int64_t y, int64_t w, int64_t h, int64_t c1, int64_t c2) {
    (void)canvas;
    (void)x;
    (void)y;
    (void)w;
    (void)h;
    (void)c1;
    (void)c2;
    RT_GRAPHICS_TRAP_VOID("Canvas.GradientV: graphics support not compiled in");
}

/// @brief Stub for `Canvas.GetScale` — would normally return the HiDPI
///        backing scale factor (e.g., 2.0 on Retina displays).
///
/// In the real implementation this is the ratio of physical to logical
/// pixels; canvas drawing math always operates in logical pixels and the
/// runtime applies the scale at present time.
///
/// @param canvas Canvas handle (ignored).
///
/// @return Never returns normally; the `1.0` fallback is unreachable.
double rt_canvas_get_scale(void *canvas) {
    (void)canvas;
    RT_GRAPHICS_TRAP_RET("Canvas.GetScale: graphics support not compiled in", 1.0);
}

/// @brief Stub for `Canvas.GetPosition` — would normally write the
///        window's current screen-relative top-left corner to `*x` and
///        `*y`.
///
/// @param canvas Canvas handle (ignored).
/// @param x      Out-parameter for the window x position (ignored).
/// @param y      Out-parameter for the window y position (ignored).
void rt_canvas_get_position(void *canvas, int64_t *x, int64_t *y) {
    (void)canvas;
    (void)x;
    (void)y;
    RT_GRAPHICS_TRAP_VOID("Canvas.GetPosition: graphics support not compiled in");
}

/// @brief Stub for `Canvas.SetPosition` — would normally request the OS
///        to move the window so its top-left corner is at `(x, y)` in
///        screen coordinates.
///
/// @param canvas Canvas handle (ignored).
/// @param x      New window x position in screen pixels (ignored).
/// @param y      New window y position in screen pixels (ignored).
void rt_canvas_set_position(void *canvas, int64_t x, int64_t y) {
    (void)canvas;
    (void)x;
    (void)y;
    RT_GRAPHICS_TRAP_VOID("Canvas.SetPosition: graphics support not compiled in");
}

/// @brief Stub for `Canvas.GetFps` — would normally return the target
///        frames-per-second cap requested at `Canvas.New` (or `-1` if
///        uncapped / vsync-driven).
///
/// @param canvas Canvas handle (ignored).
///
/// @return Never returns normally; the `-1` fallback is unreachable.
int64_t rt_canvas_get_fps(void *canvas) {
    (void)canvas;
    RT_GRAPHICS_TRAP_RET("Canvas.GetFps: graphics support not compiled in", -1);
}

/// @brief Stub for `Canvas.SetFps` — would normally adjust the
///        frames-per-second cap. `0` disables the cap.
///
/// @param canvas Canvas handle (ignored).
/// @param fps    Target FPS; `0` for uncapped (ignored).
void rt_canvas_set_fps(void *canvas, int64_t fps) {
    (void)canvas;
    (void)fps;
    RT_GRAPHICS_TRAP_VOID("Canvas.SetFps: graphics support not compiled in");
}

/// @brief Get the delta time value.
/// @param canvas
/// @return Result value.
int64_t rt_canvas_get_delta_time(void *canvas) {
    (void)canvas;
    RT_GRAPHICS_TRAP_RET("Canvas.DeltaTime: graphics support not compiled in", 0);
}

/// @brief Set the dt max value.
/// @param canvas
/// @param max_ms
void rt_canvas_set_dt_max(void *canvas, int64_t max_ms) {
    (void)canvas;
    (void)max_ms;
    RT_GRAPHICS_TRAP_VOID("Canvas.SetDTMax: graphics support not compiled in");
}

/// @brief Begin frame.
/// @param canvas
/// @return Result value.
int64_t rt_canvas_begin_frame(void *canvas) {
    (void)canvas;
    RT_GRAPHICS_TRAP_RET("Canvas.BeginFrame: graphics support not compiled in", 0);
}

/// @brief Check if maximized.
/// @param canvas
/// @return Result value.
int8_t rt_canvas_is_maximized(void *canvas) {
    (void)canvas;
    RT_GRAPHICS_TRAP_RET("Canvas.IsMaximized: graphics support not compiled in", 0);
}

/// @brief Maximize operation.
void rt_canvas_maximize(void *canvas) {
    (void)canvas;
    RT_GRAPHICS_TRAP_VOID("Canvas.Maximize: graphics support not compiled in");
}

/// @brief Check if minimized.
/// @param canvas
/// @return Result value.
int8_t rt_canvas_is_minimized(void *canvas) {
    (void)canvas;
    RT_GRAPHICS_TRAP_RET("Canvas.IsMinimized: graphics support not compiled in", 0);
}

/// @brief Minimize operation.
void rt_canvas_minimize(void *canvas) {
    (void)canvas;
    RT_GRAPHICS_TRAP_VOID("Canvas.Minimize: graphics support not compiled in");
}

/// @brief Restore operation.
void rt_canvas_restore(void *canvas) {
    (void)canvas;
    RT_GRAPHICS_TRAP_VOID("Canvas.Restore: graphics support not compiled in");
}

/// @brief Check if focused.
/// @param canvas
/// @return Result value.
int8_t rt_canvas_is_focused(void *canvas) {
    (void)canvas;
    RT_GRAPHICS_TRAP_RET("Canvas.IsFocused: graphics support not compiled in", 0);
}

/// @brief Focus operation.
void rt_canvas_focus(void *canvas) {
    (void)canvas;
    RT_GRAPHICS_TRAP_VOID("Canvas.Focus: graphics support not compiled in");
}

/// @brief Close the prevent.
void rt_canvas_prevent_close(void *canvas, int64_t prevent) {
    (void)canvas;
    (void)prevent;
    RT_GRAPHICS_TRAP_VOID("Canvas.PreventClose: graphics support not compiled in");
}

/// @brief Get the monitor size value.
/// @param canvas
/// @param w
/// @param h
void rt_canvas_get_monitor_size(void *canvas, int64_t *w, int64_t *h) {
    (void)canvas;
    (void)w;
    (void)h;
    RT_GRAPHICS_TRAP_VOID("Canvas.GetMonitorSize: graphics support not compiled in");
}

//=============================================================================
// Graphics 3D stubs — Canvas3D, Mesh3D, Camera3D, Material3D, Light3D
//=============================================================================

/// @brief Stub for `CubeMap3D.New` — would normally allocate a six-
///        face cubemap from the given Pixels surfaces. Used for skyboxes
///        and environment-map reflections; all six faces should share
///        the same dimensions.
///
/// Trapping stub: cubemaps are sampled by skybox draws and PBR
/// reflection passes — a NULL return would crash the renderer.
///
/// @param px Pixels handle for the +X face (right) (ignored).
/// @param nx Pixels handle for the -X face (left) (ignored).
/// @param py Pixels handle for the +Y face (top) (ignored).
/// @param ny Pixels handle for the -Y face (bottom) (ignored).
/// @param pz Pixels handle for the +Z face (front) (ignored).
/// @param nz Pixels handle for the -Z face (back) (ignored).
///
/// @return Never returns normally.
void *rt_cubemap3d_new(void *px, void *nx, void *py, void *ny, void *pz, void *nz) {
    (void)px;
    (void)nx;
    (void)py;
    (void)ny;
    (void)pz;
    (void)nz;
    rt_graphics_unavailable_("CubeMap3D.New: graphics support not compiled in");
    return NULL;
}

/// @brief Set the skybox of the canvas3d.
void rt_canvas3d_set_skybox(void *c, void *cm) {
    (void)c;
    (void)cm;
}

/// @brief Clear the skybox of the canvas3d.
void rt_canvas3d_clear_skybox(void *c) {
    (void)c;
}

/// @brief Set the env map of the material3d.
void rt_material3d_set_env_map(void *o, void *cm) {
    (void)o;
    (void)cm;
}

/// @brief Set the reflectivity of the material3d.
void rt_material3d_set_reflectivity(void *o, double r) {
    (void)o;
    (void)r;
}

/// @brief Stub for `Material3D.Reflectivity` — would normally return the
///        material's reflectivity coefficient (0..1).
///
/// Silent stub: returns `0.0` rather than trapping so that asset-loading
/// code paths that probe material properties before binding to the GPU
/// don't fail headlessly.
///
/// @param o Material3D handle (ignored).
///
/// @return `0.0` — non-reflective default.
double rt_material3d_get_reflectivity(void *o) {
    (void)o;
    return 0.0;
}

/// @brief Stub for `RenderTarget3D.New` — would normally allocate an
///        offscreen `(w x h)` 3D render target (color + depth).
///
/// Trapping stub: render targets cannot be faked headlessly because they
/// are explicit GPU-side allocations the caller will try to bind/sample.
///
/// @param w Target width in pixels (ignored).
/// @param h Target height in pixels (ignored).
///
/// @return Never returns normally.
void *rt_rendertarget3d_new(int64_t w, int64_t h) {
    (void)w;
    (void)h;
    rt_graphics_unavailable_("RenderTarget3D.New: graphics support not compiled in");
    return NULL;
}

/// @brief Stub for `RenderTarget3D.Width`.
///
/// Silent stub returning `0`. Reachable only via a NULL handle that the
/// caller obtained from a different source (real RenderTarget3Ds cannot be
/// created in this build because `rt_rendertarget3d_new` traps).
///
/// @param o RenderTarget3D handle (ignored).
///
/// @return `0`.
int64_t rt_rendertarget3d_get_width(void *o) {
    (void)o;
    return 0;
}

/// @brief Stub for `RenderTarget3D.Height`.
///
/// Silent stub returning `0`. See `rt_rendertarget3d_get_width` for the
/// reachability note.
///
/// @param o RenderTarget3D handle (ignored).
///
/// @return `0`.
int64_t rt_rendertarget3d_get_height(void *o) {
    (void)o;
    return 0;
}

/// @brief Stub for `RenderTarget3D.AsPixels` — would normally return a
///        Pixels view of the render target's color attachment.
///
/// Silent stub returning NULL. Callers should null-check before drawing.
///
/// @param o RenderTarget3D handle (ignored).
///
/// @return `NULL`.
void *rt_rendertarget3d_as_pixels(void *o) {
    (void)o;
    return NULL;
}

/// @brief Stub for `Canvas3D.SetRenderTarget` — would normally redirect
///        subsequent 3D draws into the given offscreen target instead of
///        the on-screen framebuffer.
///
/// Silent no-op stub: state mutators on a non-existent backend are
/// harmless to swallow.
///
/// @param c Canvas3D handle (ignored).
/// @param t RenderTarget3D handle (ignored).
void rt_canvas3d_set_render_target(void *c, void *t) {
    (void)c;
    (void)t;
}

/// @brief Stub for `Canvas3D.ResetRenderTarget` — would normally restore
///        on-screen rendering after a `SetRenderTarget` call.
///
/// Silent no-op stub.
///
/// @param c Canvas3D handle (ignored).
void rt_canvas3d_reset_render_target(void *c) {
    (void)c;
}

/// @brief Stub for `Canvas3D.New` — would normally create a 3D-capable
///        canvas window with an attached depth buffer and a backend
///        (Metal / D3D11 / OpenGL / software auto-selected).
///
/// Trapping stub: there is no plausible no-op behavior — callers expect a
/// usable handle they can issue draw calls against.
///
/// @param title Window title (ignored).
/// @param w     Width in pixels (ignored).
/// @param h     Height in pixels (ignored).
///
/// @return Never returns normally.
void *rt_canvas3d_new(rt_string title, int64_t w, int64_t h) {
    (void)title;
    (void)w;
    (void)h;
    rt_graphics_unavailable_("Canvas3D.New: graphics support not compiled in");
    return NULL;
}

/// @brief Stub for `Canvas3D.Clear` — would normally clear the color and
///        depth buffers to the given RGB and `1.0` respectively.
///
/// Silent no-op stub.
///
/// @param o Canvas3D handle (ignored).
/// @param r Clear color red component, 0..1 (ignored).
/// @param g Clear color green component, 0..1 (ignored).
/// @param b Clear color blue component, 0..1 (ignored).
void rt_canvas3d_clear(void *o, double r, double g, double b) {
    (void)o;
    (void)r;
    (void)g;
    (void)b;
}

/// @brief Stub for `Canvas3D.Begin` — would normally start a 3D render
///        pass with the given Camera3D and prepare per-frame uniforms.
///
/// Silent no-op stub.
///
/// @param o Canvas3D handle (ignored).
/// @param c Camera3D handle (ignored).
void rt_canvas3d_begin(void *o, void *c) {
    (void)o;
    (void)c;
}

/// @brief Stub for `Canvas3D.DrawMesh` — would normally issue a single
///        textured/material-bound mesh draw with a model-space transform.
///
/// Silent no-op stub.
///
/// @param o  Canvas3D handle (ignored).
/// @param m  Mesh3D handle (ignored).
/// @param t  Transform3D handle (ignored).
/// @param mt Material3D handle (ignored).
void rt_canvas3d_draw_mesh(void *o, void *m, void *t, void *mt) {
    (void)o;
    (void)m;
    (void)t;
    (void)mt;
}

/// @brief Stub for `Canvas3D.End` — would normally finalize the current
///        render pass and submit accumulated draw commands to the backend.
///
/// Silent no-op stub.
///
/// @param o Canvas3D handle (ignored).
void rt_canvas3d_end(void *o) {
    (void)o;
}

/// @brief Stub for `Canvas3D.Flip` — would normally present the back
///        buffer to the screen.
///
/// Silent no-op stub.
///
/// @param o Canvas3D handle (ignored).
void rt_canvas3d_flip(void *o) {
    (void)o;
}

/// @brief Stub for `Canvas3D.Poll` — would normally pump the OS event
///        queue and update input state for this frame.
///
/// Silent stub returning `0` (no events pending).
///
/// @param o Canvas3D handle (ignored).
///
/// @return `0`.
int64_t rt_canvas3d_poll(void *o) {
    (void)o;
    return 0;
}

/// @brief Stub for `Canvas3D.ShouldClose` — would normally report whether
///        the OS window has received a close request.
///
/// Silent stub returning `0` (never closes). Note the divergence from the
/// 2D `rt_canvas_should_close` stub which traps; this one is silent so
/// 3D-headless smoke probes can run a few iterations of a game loop.
///
/// @param o Canvas3D handle (ignored).
///
/// @return `0`.
int8_t rt_canvas3d_should_close(void *o) {
    (void)o;
    return 0;
}

/// @brief Stub for `Canvas3D.SetWireframe` — would normally enable or
///        disable wireframe rasterization for subsequent draws.
///
/// Silent no-op stub.
///
/// @param o Canvas3D handle (ignored).
/// @param e Non-zero to enable wireframe (ignored).
void rt_canvas3d_set_wireframe(void *o, int8_t e) {
    (void)o;
    (void)e;
}

/// @brief Stub for `Canvas3D.SetBackfaceCull` — would normally enable or
///        disable backface culling for subsequent draws.
///
/// Silent no-op stub.
///
/// @param o Canvas3D handle (ignored).
/// @param e Non-zero to enable backface culling (ignored).
void rt_canvas3d_set_backface_cull(void *o, int8_t e) {
    (void)o;
    (void)e;
}

/// @brief Get the width of the canvas3d.
int64_t rt_canvas3d_get_width(void *o) {
    (void)o;
    return 0;
}

/// @brief Get the height of the canvas3d.
int64_t rt_canvas3d_get_height(void *o) {
    (void)o;
    return 0;
}

/// @brief Get the fps of the canvas3d.
int64_t rt_canvas3d_get_fps(void *o) {
    (void)o;
    return 0;
}

/// @brief Get the delta time of the canvas3d.
int64_t rt_canvas3d_get_delta_time(void *o) {
    (void)o;
    return 0;
}

/// @brief Stub for `Canvas3D.SetDtMax` — would normally cap the maximum
///        delta-time value reported to the game loop on slow frames so a
///        single hitch can't tunnel through physics in one step.
///
/// Silent no-op stub.
///
/// @param o Canvas3D handle (ignored).
/// @param m Maximum delta-time in milliseconds (ignored).
void rt_canvas3d_set_dt_max(void *o, int64_t m) {
    (void)o;
    (void)m;
}

/// @brief Stub for `Canvas3D.SetLight` — would normally bind a Light3D to
///        slot `i` of the per-frame light array (max 8 lights).
///
/// Silent no-op stub.
///
/// @param o Canvas3D handle (ignored).
/// @param i Light slot index (ignored).
/// @param l Light3D handle, or NULL to clear the slot (ignored).
void rt_canvas3d_set_light(void *o, int64_t i, void *l) {
    (void)o;
    (void)i;
    (void)l;
}

/// @brief Stub for `Canvas3D.SetAmbient` — would normally set the global
///        ambient illumination color used in the lighting equation.
///
/// Silent no-op stub.
///
/// @param o Canvas3D handle (ignored).
/// @param r Ambient red, 0..1 (ignored).
/// @param g Ambient green, 0..1 (ignored).
/// @param b Ambient blue, 0..1 (ignored).
void rt_canvas3d_set_ambient(void *o, double r, double g, double b) {
    (void)o;
    (void)r;
    (void)g;
    (void)b;
}

/// @brief Stub for `Canvas3D.DrawLine3D` — would normally draw a 3D line
///        segment between two world-space points.
///
/// Silent no-op stub. Used for debug visualization (gizmos, raycast hits)
/// in the real backend.
///
/// @param o Canvas3D handle (ignored).
/// @param f Vec3 start point handle (ignored).
/// @param t Vec3 end point handle (ignored).
/// @param c Packed 0xAARRGGBB color (ignored).
void rt_canvas3d_draw_line3d(void *o, void *f, void *t, int64_t c) {
    (void)o;
    (void)f;
    (void)t;
    (void)c;
}

/// @brief Stub for `Canvas3D.DrawPoint3D` — would normally draw a 3D
///        point sprite at the given world-space position.
///
/// Silent no-op stub. Used for debug visualization.
///
/// @param o Canvas3D handle (ignored).
/// @param p Vec3 point handle (ignored).
/// @param c Packed 0xAARRGGBB color (ignored).
/// @param s Point sprite size in screen pixels (ignored).
void rt_canvas3d_draw_point3d(void *o, void *p, int64_t c, int64_t s) {
    (void)o;
    (void)p;
    (void)c;
    (void)s;
}

/// @brief Stub for `Canvas3D.Backend` — would normally return the name of
///        the active 3D backend ("metal", "d3d11", "opengl", or "software").
///
/// Silent stub returning NULL so that backend-detection code can branch
/// without needing to handle a trap.
///
/// @param o Canvas3D handle (ignored).
///
/// @return `NULL`.
rt_string rt_canvas3d_get_backend(void *o) {
    (void)o;
    return NULL;
}

/// @brief Stub for `Canvas3D.Screenshot` — would normally read back the
///        current 3D framebuffer into a fresh Pixels surface.
///
/// Silent stub returning NULL.
///
/// @param o Canvas3D handle (ignored).
///
/// @return `NULL`.
void *rt_canvas3d_screenshot(void *o) {
    (void)o;
    return NULL;
}

/// @brief Stub for `Mesh3D.New` — would normally allocate an empty mesh
///        ready to receive vertices and triangles.
///
/// Trapping stub: there's no useful empty mesh to return — callers will
/// immediately try to populate it with `AddVertex`/`AddTriangle`, all of
/// which would also be no-ops, producing silent rendering bugs.
///
/// @return Never returns normally.
void *rt_mesh3d_new(void) {
    rt_graphics_unavailable_("Mesh3D.New: graphics support not compiled in");
    return NULL;
}

/// @brief Stub for `Mesh3D.Clear` — would normally reset vertex/index
///        counts to zero without freeing backing arrays (mesh reuse).
///
/// Silent no-op stub.
///
/// @param o Mesh3D handle (ignored).
void rt_mesh3d_clear(void *o) {
    (void)o;
}

/// @brief Stub for `Mesh3D.NewBox` — would normally allocate a box mesh
///        with the given full extents along each axis.
///
/// @param sx Full extent along X (ignored).
/// @param sy Full extent along Y (ignored).
/// @param sz Full extent along Z (ignored).
///
/// @return Never returns normally.
void *rt_mesh3d_new_box(double sx, double sy, double sz) {
    (void)sx;
    (void)sy;
    (void)sz;
    rt_graphics_unavailable_("Mesh3D.NewBox: graphics support not compiled in");
    return NULL;
}

/// @brief Stub for `Mesh3D.NewSphere` — would normally allocate a UV
///        sphere with the given radius and subdivision count.
///
/// @param r Sphere radius (ignored).
/// @param s Subdivision count along each axis (ignored).
///
/// @return Never returns normally.
void *rt_mesh3d_new_sphere(double r, int64_t s) {
    (void)r;
    (void)s;
    rt_graphics_unavailable_("Mesh3D.NewSphere: graphics support not compiled in");
    return NULL;
}

/// @brief Stub for `Mesh3D.NewPlane` — would normally allocate an
///        XZ-plane quad with full extents `(sx, sz)` centered at the origin.
///
/// @param sx Full extent along X (ignored).
/// @param sz Full extent along Z (ignored).
///
/// @return Never returns normally.
void *rt_mesh3d_new_plane(double sx, double sz) {
    (void)sx;
    (void)sz;
    rt_graphics_unavailable_("Mesh3D.NewPlane: graphics support not compiled in");
    return NULL;
}

/// @brief Stub for `Mesh3D.NewCylinder` — would normally allocate an
///        upright Y-axis cylinder with the given radius, height, and
///        side-segment count.
///
/// @param r Cylinder radius (ignored).
/// @param h Cylinder height (ignored).
/// @param s Side segment count around the circumference (ignored).
///
/// @return Never returns normally.
void *rt_mesh3d_new_cylinder(double r, double h, int64_t s) {
    (void)r;
    (void)h;
    (void)s;
    rt_graphics_unavailable_("Mesh3D.NewCylinder: graphics support not compiled in");
    return NULL;
}

/// @brief Stub for `Mesh3D.FromOBJ` — would normally load a Wavefront OBJ
///        file at `p` and return a populated mesh.
///
/// @param p Filesystem path to the OBJ file (ignored).
///
/// @return Never returns normally.
void *rt_mesh3d_from_obj(rt_string p) {
    (void)p;
    rt_graphics_unavailable_("Mesh3D.FromOBJ: graphics support not compiled in");
    return NULL;
}

/// @brief Stub for `Mesh3D.FromSTL` — would normally load an STL file
///        (binary or ASCII auto-detected) at `p` and return a populated mesh.
///
/// @param p Filesystem path to the STL file (ignored).
///
/// @return Never returns normally.
void *rt_mesh3d_from_stl(rt_string p) {
    (void)p;
    rt_graphics_unavailable_("Mesh3D.FromSTL: graphics support not compiled in");
    return NULL;
}

/// @brief Stub for `Mesh3D.VertexCount` — would normally return the
///        number of vertices currently stored in the mesh.
///
/// Silent stub returning `0`.
///
/// @param o Mesh3D handle (ignored).
///
/// @return `0`.
int64_t rt_mesh3d_get_vertex_count(void *o) {
    (void)o;
    return 0;
}

/// @brief Stub for `Mesh3D.TriangleCount` — would normally return the
///        number of triangles currently stored in the mesh (== `IndexCount / 3`).
///
/// Silent stub returning `0`.
///
/// @param o Mesh3D handle (ignored).
///
/// @return `0`.
int64_t rt_mesh3d_get_triangle_count(void *o) {
    (void)o;
    return 0;
}

/// @brief Stub for `Mesh3D.AddVertex` — append a vertex with position
///        `(x, y, z)`, normal `(nx, ny, nz)`, and UV `(u, v)` to the mesh.
///        Returns the new vertex index implicitly via insertion order.
///
/// Silent no-op stub. The real implementation grows the underlying vertex
/// buffer; you almost always pair this with `AddTriangle` to reference
/// the new vertex.
///
/// @param o  Mesh3D handle (ignored).
/// @param x  Position x (ignored).
/// @param y  Position y (ignored).
/// @param z  Position z (ignored).
/// @param nx Normal x; should be normalized (ignored).
/// @param ny Normal y (ignored).
/// @param nz Normal z (ignored).
/// @param u  Texture U coordinate, typically 0..1 (ignored).
/// @param v  Texture V coordinate, typically 0..1 (ignored).
void rt_mesh3d_add_vertex(
    void *o, double x, double y, double z, double nx, double ny, double nz, double u, double v) {
    (void)o;
    (void)x;
    (void)y;
    (void)z;
    (void)nx;
    (void)ny;
    (void)nz;
    (void)u;
    (void)v;
}

/// @brief Stub for `Mesh3D.AddTriangle` — append a triangle to the
///        mesh's index buffer using three previously-added vertex indices.
///
/// Silent no-op stub. Winding order matters for backface culling: with
/// the default cull mode, triangles wound counter-clockwise (when viewed
/// from outside) are visible.
///
/// @param o  Mesh3D handle (ignored).
/// @param v0 First vertex index, 0..VertexCount-1 (ignored).
/// @param v1 Second vertex index (ignored).
/// @param v2 Third vertex index (ignored).
void rt_mesh3d_add_triangle(void *o, int64_t v0, int64_t v1, int64_t v2) {
    (void)o;
    (void)v0;
    (void)v1;
    (void)v2;
}

/// @brief Recalc the normals of the mesh3d.
void rt_mesh3d_recalc_normals(void *o) {
    (void)o;
}

/// @brief Stub for `Mesh3D.Clone` — would normally allocate a fresh
///        mesh holding deep copies of the source's vertex and index
///        buffers (no aliasing back to the source). Use when you want
///        to mutate a mesh per-instance without affecting the original.
///
/// Silent stub returning NULL.
///
/// @param o Source Mesh3D handle (ignored).
///
/// @return `NULL`.
void *rt_mesh3d_clone(void *o) {
    (void)o;
    return NULL;
}

/// @brief Stub for `Mesh3D.Transform` — apply a Mat4 transformation to
///        every vertex in the mesh in place. Positions are transformed
///        directly; normals are transformed by the inverse-transpose of
///        the upper 3x3 to remain perpendicular under non-uniform scale.
///
/// Silent no-op stub.
///
/// @param o Mesh3D handle (ignored).
/// @param m Mat4 transformation handle (ignored).
void rt_mesh3d_transform(void *o, void *m) {
    (void)o;
    (void)m;
}

/// @brief Stub for `Camera3D.New` — would normally create a perspective
///        camera with vertical field of view `f` (radians), aspect ratio
///        `a`, near plane `n`, and far plane `fa`. Position is `(0, 0, 0)`,
///        looking along -Z, with +Y up; reposition with `LookAt`.
///
/// Trapping stub: cameras are referenced by `Canvas3D.Begin` and similar
/// draw calls — a NULL return would crash later.
///
/// @param f  Vertical FOV in radians (ignored).
/// @param a  Aspect ratio, width/height (ignored).
/// @param n  Near plane distance, world units (ignored).
/// @param fa Far plane distance (ignored).
///
/// @return Never returns normally.
void *rt_camera3d_new(double f, double a, double n, double fa) {
    (void)f;
    (void)a;
    (void)n;
    (void)fa;
    rt_graphics_unavailable_("Camera3D.New: graphics support not compiled in");
    return NULL;
}

/// @brief Stub for `Camera3D.NewOrtho` — would normally create an
///        orthographic camera with vertical "size" `s` (the half-height
///        of the orthographic view volume in world units), aspect `a`,
///        near `n`, far `fa`. Used for isometric / strategy / 2.5D games.
///
/// @param s  Vertical view-volume half-height in world units (ignored).
/// @param a  Aspect ratio, width/height (ignored).
/// @param n  Near plane distance (ignored).
/// @param fa Far plane distance (ignored).
///
/// @return Never returns normally.
void *rt_camera3d_new_ortho(double s, double a, double n, double fa) {
    (void)s;
    (void)a;
    (void)n;
    (void)fa;
    rt_graphics_unavailable_("Camera3D.NewOrtho: graphics support not compiled in");
    return NULL;
}

/// @brief Stub for `Camera3D.IsOrtho` — true for orthographic cameras
///        (created via `NewOrtho`), false for perspective cameras.
///
/// Silent stub returning `0` (perspective default).
///
/// @param o Camera3D handle (ignored).
///
/// @return `0`.
int8_t rt_camera3d_is_ortho(void *o) {
    (void)o;
    return 0;
}

/// @brief Stub for `Camera3D.LookAt` — orient the camera so it sits at
///        eye position `e` looking at world-space target `t`, with `u`
///        as the up reference (typically `(0, 1, 0)`).
///
/// Silent no-op stub. The real implementation builds the view matrix
/// from `e`, the forward vector `(t - e).normalized()`, and an
/// orthonormal basis derived from `u`.
///
/// @param o Camera3D handle (ignored).
/// @param e Vec3 eye position (ignored).
/// @param t Vec3 look-at target (ignored).
/// @param u Vec3 up reference (ignored).
void rt_camera3d_look_at(void *o, void *e, void *t, void *u) {
    (void)o;
    (void)e;
    (void)t;
    (void)u;
}

/// @brief Stub for `Camera3D.Orbit` — position the camera at distance
///        `d` from world-space target `t`, with yaw `y` and pitch `p`
///        (radians). Always looks at `t`.
///
/// Silent no-op stub. Convenience for orbital / RTS camera rigs.
///
/// @param o Camera3D handle (ignored).
/// @param t Vec3 orbit target (ignored).
/// @param d Distance from target (ignored).
/// @param y Yaw in radians (azimuth around target) (ignored).
/// @param p Pitch in radians (elevation above XZ plane) (ignored).
void rt_camera3d_orbit(void *o, void *t, double d, double y, double p) {
    (void)o;
    (void)t;
    (void)d;
    (void)y;
    (void)p;
}

/// @brief Stub for `Camera3D.FOV` — get the camera's vertical field of
///        view in radians.
///
/// Silent stub returning `0.0`.
///
/// @param o Camera3D handle (ignored).
///
/// @return `0.0`.
double rt_camera3d_get_fov(void *o) {
    (void)o;
    return 0.0;
}

/// @brief Stub for `Camera3D.SetFOV` — adjust the vertical field of
///        view; the projection matrix is recomputed lazily on next use.
///
/// Silent no-op stub.
///
/// @param o Camera3D handle (ignored).
/// @param f Vertical FOV in radians (ignored).
void rt_camera3d_set_fov(void *o, double f) {
    (void)o;
    (void)f;
}

/// @brief Stub for `Camera3D.Position` — get the camera's eye position
///        as a Vec3.
///
/// Silent stub returning NULL.
///
/// @param o Camera3D handle (ignored).
///
/// @return `NULL`.
void *rt_camera3d_get_position(void *o) {
    (void)o;
    return NULL;
}

/// @brief Stub for `Camera3D.SetPosition` — move the camera's eye to
///        the given Vec3 position. Forward direction is preserved.
///
/// Silent no-op stub.
///
/// @param o Camera3D handle (ignored).
/// @param p Vec3 position (ignored).
void rt_camera3d_set_position(void *o, void *p) {
    (void)o;
    (void)p;
}

/// @brief Stub for `Camera3D.Forward` — get the camera's normalized
///        forward direction as a Vec3 (the "look" axis).
///
/// Silent stub returning NULL.
///
/// @param o Camera3D handle (ignored).
///
/// @return `NULL`.
void *rt_camera3d_get_forward(void *o) {
    (void)o;
    return NULL;
}

/// @brief Stub for `Camera3D.Right` — get the camera's normalized right
///        direction as a Vec3 (the "strafe" axis, perpendicular to
///        Forward and Up).
///
/// Silent stub returning NULL.
///
/// @param o Camera3D handle (ignored).
///
/// @return `NULL`.
void *rt_camera3d_get_right(void *o) {
    (void)o;
    return NULL;
}

/// @brief Stub for `Camera3D.ScreenToRay` — would normally build a
///        world-space picking ray from a screen-pixel coordinate.
///        `(sx, sy)` is the screen pixel; `(sw, sh)` is the viewport
///        size. The ray's origin is the camera eye and direction passes
///        through the corresponding clip-space point.
///
/// Silent stub returning NULL.
///
/// @param o  Camera3D handle (ignored).
/// @param sx Screen pixel x (ignored).
/// @param sy Screen pixel y (ignored).
/// @param sw Viewport width in pixels (ignored).
/// @param sh Viewport height in pixels (ignored).
///
/// @return `NULL`.
void *rt_camera3d_screen_to_ray(void *o, int64_t sx, int64_t sy, int64_t sw, int64_t sh) {
    (void)o;
    (void)sx;
    (void)sy;
    (void)sw;
    (void)sh;
    return NULL;
}

/// @brief Stub for `Material3D.New` — would normally create a default
///        Blinn-Phong material (white diffuse, no textures, shininess 32).
///        Use `NewColor`, `NewTextured`, or `NewPBR` for shorthand
///        constructors.
///
/// Trapping stub.
///
/// @return Never returns normally.
void *rt_material3d_new(void) {
    rt_graphics_unavailable_("Material3D.New: graphics support not compiled in");
    return NULL;
}

/// @brief Stub for `Material3D.NewColor` — would normally create a
///        Blinn-Phong material with the given solid base color and no
///        texture maps.
///
/// @param r Base color red, 0..1 (ignored).
/// @param g Base color green, 0..1 (ignored).
/// @param b Base color blue, 0..1 (ignored).
///
/// @return Never returns normally.
void *rt_material3d_new_color(double r, double g, double b) {
    (void)r;
    (void)g;
    (void)b;
    rt_graphics_unavailable_("Material3D.NewColor: graphics support not compiled in");
    return NULL;
}

/// @brief Stub for `Material3D.NewTextured` — would normally create a
///        Blinn-Phong material with the given diffuse texture (slot 0)
///        and white tint.
///
/// @param p Pixels handle for the diffuse texture (ignored).
///
/// @return Never returns normally.
void *rt_material3d_new_textured(void *p) {
    (void)p;
    rt_graphics_unavailable_("Material3D.NewTextured: graphics support not compiled in");
    return NULL;
}

/// @brief Stub for `Material3D.NewPBR` — would normally create a PBR
///        material with metallic-roughness workflow. `(r, g, b)` are
///        traditionally interpreted as `(metallic, roughness, ao)` in the
///        runtime; pair with `SetAlbedoMap` for the base color.
///
/// @param r Metallic, 0..1 (ignored).
/// @param g Roughness, 0..1 (ignored).
/// @param b Ambient occlusion, 0..1 (ignored).
///
/// @return Never returns normally.
void *rt_material3d_new_pbr(double r, double g, double b) {
    (void)r;
    (void)g;
    (void)b;
    rt_graphics_unavailable_("Material3D.NewPBR: graphics support not compiled in");
    return NULL;
}

/// @brief Stub for `Material3D.Clone` — would normally return a deep
///        copy of the material with its own scalar parameters and
///        independent texture references (incremented refcounts on the
///        bound textures).
///
/// Trapping stub.
///
/// @param o Source Material3D handle (ignored).
///
/// @return Never returns normally.
void *rt_material3d_clone(void *o) {
    (void)o;
    rt_graphics_unavailable_("Material3D.Clone: graphics support not compiled in");
    return NULL;
}

/// @brief Stub for `Material3D.MakeInstance` — would normally produce a
///        per-instance override material that shares the base material's
///        textures but allows independent scalar parameters.
///
/// Trapping stub: there is no base material to clone in the headless build.
///
/// @param o Source Material3D handle (ignored).
///
/// @return Never returns normally.
void *rt_material3d_make_instance(void *o) {
    (void)o;
    rt_graphics_unavailable_("Material3D.MakeInstance: graphics support not compiled in");
    return NULL;
}

/// @brief Stub for `Material3D.SetColor` — would normally set the
///        material's diffuse / base color.
///
/// Silent no-op stub. Components are linear-space 0..1 floats.
///
/// @param o Material3D handle (ignored).
/// @param r Red, 0..1 (ignored).
/// @param g Green, 0..1 (ignored).
/// @param b Blue, 0..1 (ignored).
void rt_material3d_set_color(void *o, double r, double g, double b) {
    (void)o;
    (void)r;
    (void)g;
    (void)b;
}

/// @brief Stub for `Material3D.SetTexture` — would normally bind a Pixels
///        surface as the diffuse texture (slot 0).
///
/// Silent no-op stub. Equivalent to `SetAlbedoMap` for non-PBR materials.
///
/// @param o Material3D handle (ignored).
/// @param p Pixels handle for the texture, or NULL to clear (ignored).
void rt_material3d_set_texture(void *o, void *p) {
    (void)o;
    (void)p;
}

/// @brief Stub for `Material3D.SetAlbedoMap` — PBR-workflow alias for
///        `SetTexture`. Binds the base-color texture (slot 0).
///
/// Silent no-op stub.
///
/// @param o Material3D handle (ignored).
/// @param p Pixels handle for the albedo map, or NULL (ignored).
void rt_material3d_set_albedo_map(void *o, void *p) {
    (void)o;
    (void)p;
}

/// @brief Stub for `Material3D.SetShininess` — Blinn-Phong specular
///        exponent. Higher values produce a tighter specular highlight.
///
/// Silent no-op stub. Typical range 1..200.
///
/// @param o Material3D handle (ignored).
/// @param s Specular exponent (ignored).
void rt_material3d_set_shininess(void *o, double s) {
    (void)o;
    (void)s;
}

/// @brief Stub for `Material3D.SetAlpha` — sets the per-material alpha
///        multiplier applied during blending.
///
/// Silent no-op stub. Multiplied with per-vertex/texture alpha.
///
/// @param o Material3D handle (ignored).
/// @param a Alpha multiplier, 0..1 (ignored).
void rt_material3d_set_alpha(void *o, double a) {
    (void)o;
    (void)a;
}

/// @brief Stub for `Material3D.Alpha` — get the per-material alpha.
///
/// Silent stub returning `1.0` (fully opaque) so blend-mode probes don't
/// see a misleading transparency value.
///
/// @param o Material3D handle (ignored).
///
/// @return `1.0`.
double rt_material3d_get_alpha(void *o) {
    (void)o;
    return 1.0;
}

/// @brief Stub for `Material3D.SetMetallic` — PBR metallic coefficient.
///        0 = dielectric (insulator), 1 = pure metal.
///
/// Silent no-op stub.
///
/// @param o Material3D handle (ignored).
/// @param v Metallic value, 0..1 (ignored).
void rt_material3d_set_metallic(void *o, double v) {
    (void)o;
    (void)v;
}

/// @brief Stub for `Material3D.Metallic` — get the PBR metallic value.
///
/// Silent stub returning `0.0` (non-metal).
///
/// @param o Material3D handle (ignored).
///
/// @return `0.0`.
double rt_material3d_get_metallic(void *o) {
    (void)o;
    return 0.0;
}

/// @brief Stub for `Material3D.SetRoughness` — PBR microfacet roughness.
///        0 = mirror-smooth, 1 = fully rough/diffuse.
///
/// Silent no-op stub.
///
/// @param o Material3D handle (ignored).
/// @param v Roughness, 0..1 (ignored).
void rt_material3d_set_roughness(void *o, double v) {
    (void)o;
    (void)v;
}

/// @brief Stub for `Material3D.Roughness` — get the PBR roughness value.
///
/// Silent stub returning `0.5` (the PBR shader default).
///
/// @param o Material3D handle (ignored).
///
/// @return `0.5`.
double rt_material3d_get_roughness(void *o) {
    (void)o;
    return 0.5;
}

/// @brief Stub for `Material3D.SetAO` — PBR ambient-occlusion multiplier.
///        Modulates the ambient/indirect lighting term.
///
/// Silent no-op stub. Pair with `SetAOMap` to vary AO across the surface.
///
/// @param o Material3D handle (ignored).
/// @param v AO value, 0..1 (ignored).
void rt_material3d_set_ao(void *o, double v) {
    (void)o;
    (void)v;
}

/// @brief Stub for `Material3D.AO` — get the PBR ambient-occlusion value.
///
/// Silent stub returning `1.0` (no occlusion).
///
/// @param o Material3D handle (ignored).
///
/// @return `1.0`.
double rt_material3d_get_ao(void *o) {
    (void)o;
    return 1.0;
}

/// @brief Stub for `Material3D.SetEmissiveIntensity` — multiplier on the
///        emissive map / color contribution.
///
/// Silent no-op stub. Values > 1 are valid for HDR / bloom workflows.
///
/// @param o Material3D handle (ignored).
/// @param v Emissive intensity (ignored).
void rt_material3d_set_emissive_intensity(void *o, double v) {
    (void)o;
    (void)v;
}

/// @brief Stub for `Material3D.EmissiveIntensity` — get the emissive
///        multiplier.
///
/// Silent stub returning `1.0`.
///
/// @param o Material3D handle (ignored).
///
/// @return `1.0`.
double rt_material3d_get_emissive_intensity(void *o) {
    (void)o;
    return 1.0;
}

/// @brief Stub for `Material3D.SetUnlit` — when enabled, the material
///        skips lighting entirely (treat the diffuse color/texture as
///        already-shaded final pixel color). Used for HUD elements and
///        flat-shaded sprites in 3D space.
///
/// Silent no-op stub.
///
/// @param o Material3D handle (ignored).
/// @param u Non-zero to enable unlit shading (ignored).
void rt_material3d_set_unlit(void *o, int8_t u) {
    (void)o;
    (void)u;
}

/// @brief Stub for `Material3D.SetShadingModel` — selects the per-material
///        fragment shading path: 0=BlinnPhong (default), 1=Toon, 4=Fresnel,
///        5=Emissive.
///
/// Silent no-op stub.
///
/// @param o Material3D handle (ignored).
/// @param m Shading model index (ignored).
void rt_material3d_set_shading_model(void *o, int64_t m) {
    (void)o;
    (void)m;
}

/// @brief Stub for `Material3D.SetCustomParam` — write to one of the 8
///        per-material float parameter slots used by the active shading
///        model (e.g. Toon band count, Fresnel power/bias).
///
/// Silent no-op stub.
///
/// @param o Material3D handle (ignored).
/// @param i Parameter slot index, 0..7 (ignored).
/// @param v Parameter value (ignored).
void rt_material3d_set_custom_param(void *o, int64_t i, double v) {
    (void)o;
    (void)i;
    (void)v;
}

/// @brief Stub for `Material3D.SetNormalMap` — bind a tangent-space normal
///        map (slot 1).
///
/// Silent no-op stub. Real implementation does TBN perturbation with
/// Gram-Schmidt orthonormalization.
///
/// @param o Material3D handle (ignored).
/// @param p Pixels handle for the normal map, or NULL (ignored).
void rt_material3d_set_normal_map(void *o, void *p) {
    (void)o;
    (void)p;
}

/// @brief Stub for `Material3D.SetMetallicRoughnessMap` — bind a packed
///        PBR map where R = metallic, G = roughness (slots 4/5 in the
///        unified PBR shader).
///
/// Silent no-op stub. Matches the glTF 2.0 metallic-roughness workflow.
///
/// @param o Material3D handle (ignored).
/// @param p Pixels handle for the packed map, or NULL (ignored).
void rt_material3d_set_metallic_roughness_map(void *o, void *p) {
    (void)o;
    (void)p;
}

/// @brief Stub for `Material3D.SetAOMap` — bind an ambient-occlusion
///        map. Modulates the ambient lighting term per-fragment.
///
/// Silent no-op stub.
///
/// @param o Material3D handle (ignored).
/// @param p Pixels handle for the AO map, or NULL (ignored).
void rt_material3d_set_ao_map(void *o, void *p) {
    (void)o;
    (void)p;
}

/// @brief Stub for `Material3D.SetSpecularMap` — bind a per-texel
///        specular intensity map (slot 2).
///
/// Silent no-op stub. Used by the Blinn-Phong shading path; PBR workflows
/// derive specular response from the metallic-roughness pair instead.
///
/// @param o Material3D handle (ignored).
/// @param p Pixels handle for the specular map, or NULL (ignored).
void rt_material3d_set_specular_map(void *o, void *p) {
    (void)o;
    (void)p;
}

/// @brief Stub for `Material3D.SetEmissiveMap` — bind a per-texel
///        emissive color map (slot 3). Sampled and added on top of the lit
///        result.
///
/// Silent no-op stub.
///
/// @param o Material3D handle (ignored).
/// @param p Pixels handle for the emissive map, or NULL (ignored).
void rt_material3d_set_emissive_map(void *o, void *p) {
    (void)o;
    (void)p;
}

/// @brief Set the emissive color of the material3d.
void rt_material3d_set_emissive_color(void *o, double r, double g, double b) {
    (void)o;
    (void)r;
    (void)g;
    (void)b;
}

/// @brief Stub for `Material3D.SetNormalScale` — strength multiplier on
///        the bound normal map's perturbation. `1.0` is full effect; `0.0`
///        flattens the normal map back to the geometric normal; values
///        > 1 over-exaggerate.
///
/// Silent no-op stub.
///
/// @param o Material3D handle (ignored).
/// @param v Normal map intensity multiplier (ignored).
void rt_material3d_set_normal_scale(void *o, double v) {
    (void)o;
    (void)v;
}

/// @brief Stub for `Material3D.NormalScale` — get the normal map
///        intensity multiplier.
///
/// Silent stub returning `1.0` (full effect — the renderer's default).
///
/// @param o Material3D handle (ignored).
///
/// @return `1.0`.
double rt_material3d_get_normal_scale(void *o) {
    (void)o;
    return 1.0;
}

/// @brief Stub for `Material3D.SetAlphaMode` — choose how the material
///        composites: 0=Opaque (alpha ignored), 1=Mask (alpha-test
///        cutout, no blending), 2=Blend (full alpha blending). Mask is
///        cheaper than Blend but produces hard edges.
///
/// Silent no-op stub.
///
/// @param o Material3D handle (ignored).
/// @param m Alpha mode 0..2 (ignored).
void rt_material3d_set_alpha_mode(void *o, int64_t m) {
    (void)o;
    (void)m;
}

/// @brief Stub for `Material3D.AlphaMode` — get the material's
///        transparency mode: 0=Opaque, 1=Mask (alpha-tested), 2=Blend
///        (alpha-blended).
///
/// Silent stub returning Opaque (the renderer-friendly default).
///
/// @param o Material3D handle (ignored).
///
/// @return `RT_MATERIAL3D_ALPHA_MODE_OPAQUE`.
int64_t rt_material3d_get_alpha_mode(void *o) {
    (void)o;
    return RT_MATERIAL3D_ALPHA_MODE_OPAQUE;
}

/// @brief Stub for `Material3D.SetDoubleSided` — when enabled, both faces
///        of each triangle are rendered (no backface culling for this
///        material).
///
/// Silent no-op stub. Used for foliage and thin geometry.
///
/// @param o Material3D handle (ignored).
/// @param e Non-zero to enable double-sided rendering (ignored).
void rt_material3d_set_double_sided(void *o, int8_t e) {
    (void)o;
    (void)e;
}

/// @brief Stub for `Material3D.DoubleSided` — get the double-sided flag.
///
/// Silent stub returning `0` (single-sided / culled).
///
/// @param o Material3D handle (ignored).
///
/// @return `0`.
int8_t rt_material3d_get_double_sided(void *o) {
    (void)o;
    return 0;
}

/// @brief Stub for `Mesh3D.CalcTangents` — would normally compute and
///        store per-vertex tangents from positions, normals, and UVs (used
///        by normal-map shading).
///
/// Silent no-op stub.
///
/// @param o Mesh3D handle (ignored).
void rt_mesh3d_calc_tangents(void *o) {
    (void)o;
}

/// @brief Stub for `Light3D.NewDirectional` — would normally create a
///        directional (sun-like) light with the given direction and RGB
///        color.
///
/// Trapping stub: lights are referenced by Canvas3D draws, so a NULL
/// return would crash later when bound.
///
/// @param d Vec3 direction handle (must be normalized) (ignored).
/// @param r Light color red, 0..1 (ignored).
/// @param g Light color green, 0..1 (ignored).
/// @param b Light color blue, 0..1 (ignored).
///
/// @return Never returns normally.
void *rt_light3d_new_directional(void *d, double r, double g, double b) {
    (void)d;
    (void)r;
    (void)g;
    (void)b;
    rt_graphics_unavailable_("Light3D.NewDirectional: graphics support not compiled in");
    return NULL;
}

/// @brief Stub for `Light3D.NewPoint` — would normally create a point
///        light with the given position, RGB color, and falloff radius.
///
/// @param p Vec3 position handle (ignored).
/// @param r Light color red, 0..1 (ignored).
/// @param g Light color green, 0..1 (ignored).
/// @param b Light color blue, 0..1 (ignored).
/// @param a Attenuation/falloff radius in world units (ignored).
///
/// @return Never returns normally.
void *rt_light3d_new_point(void *p, double r, double g, double b, double a) {
    (void)p;
    (void)r;
    (void)g;
    (void)b;
    (void)a;
    rt_graphics_unavailable_("Light3D.NewPoint: graphics support not compiled in");
    return NULL;
}

/// @brief Stub for `Light3D.NewAmbient` — would normally create a global
///        ambient light contribution with the given RGB color.
///
/// In the real shader the ambient term applies uniformly regardless of
/// surface normal.
///
/// @param r Ambient red, 0..1 (ignored).
/// @param g Ambient green, 0..1 (ignored).
/// @param b Ambient blue, 0..1 (ignored).
///
/// @return Never returns normally.
void *rt_light3d_new_ambient(double r, double g, double b) {
    (void)r;
    (void)g;
    (void)b;
    rt_graphics_unavailable_("Light3D.NewAmbient: graphics support not compiled in");
    return NULL;
}

/// @brief Stub for `Light3D.NewSpot` — would normally create a cone-
///        attenuated spotlight at the given position pointing in the given
///        direction with smoothstep falloff between inner and outer angles.
///
/// @param p Vec3 position handle (ignored).
/// @param d Vec3 direction handle (must be normalized) (ignored).
/// @param r Light color red, 0..1 (ignored).
/// @param g Light color green, 0..1 (ignored).
/// @param b Light color blue, 0..1 (ignored).
/// @param a Attenuation/falloff radius in world units (ignored).
/// @param i Inner cone half-angle in radians (full intensity inside) (ignored).
/// @param o Outer cone half-angle in radians (zero intensity beyond) (ignored).
///
/// @return Never returns normally.
void *rt_light3d_new_spot(
    void *p, void *d, double r, double g, double b, double a, double i, double o) {
    (void)p;
    (void)d;
    (void)r;
    (void)g;
    (void)b;
    (void)a;
    (void)i;
    (void)o;
    rt_graphics_unavailable_("Light3D.NewSpot: graphics support not compiled in");
    return NULL;
}

/// @brief Stub for `Light3D.SetIntensity` — multiplier applied to the
///        light's RGB contribution (HDR-friendly, may exceed 1.0).
///
/// Silent no-op stub.
///
/// @param o Light3D handle (ignored).
/// @param i Intensity multiplier (ignored).
void rt_light3d_set_intensity(void *o, double i) {
    (void)o;
    (void)i;
}

/// @brief Stub for `Light3D.SetColor` — overwrite the light's RGB color.
///
/// Silent no-op stub. Components are linear-space 0..1 floats.
///
/// @param o Light3D handle (ignored).
/// @param r Red, 0..1 (ignored).
/// @param g Green, 0..1 (ignored).
/// @param b Blue, 0..1 (ignored).
void rt_light3d_set_color(void *o, double r, double g, double b) {
    (void)o;
    (void)r;
    (void)g;
    (void)b;
}

/* Scene3D / SceneNode3D stubs */

/// @brief Stub for `Scene3D.New` — would normally create an empty scene
///        graph with a single root node ready to receive child nodes.
///
/// Trapping stub: scene graphs are accessed via the returned root, so a
/// NULL return would crash on the first `GetRoot` / `Add` call.
///
/// @return Never returns normally.
void *rt_scene3d_new(void) {
    rt_graphics_unavailable_("Scene3D.New: graphics support not compiled in");
    return NULL;
}

/// @brief Stub for `Scene3D.Root` — get the scene's root SceneNode3D.
///
/// Silent stub returning NULL because there is no Scene3D to query.
///
/// @param s Scene3D handle (ignored).
///
/// @return `NULL`.
void *rt_scene3d_get_root(void *s) {
    (void)s;
    return NULL;
}

/// @brief Stub for `Scene3D.Add` — would normally attach a SceneNode3D
///        as a top-level child of the scene's root.
///
/// Silent no-op stub.
///
/// @param s Scene3D handle (ignored).
/// @param n SceneNode3D handle (ignored).
void rt_scene3d_add(void *s, void *n) {
    (void)s;
    (void)n;
}

/// @brief Stub for `Scene3D.Remove` — would normally detach a SceneNode3D
///        from its parent, leaving subtree intact for re-attachment.
///
/// Silent no-op stub.
///
/// @param s Scene3D handle (ignored).
/// @param n SceneNode3D handle (ignored).
void rt_scene3d_remove(void *s, void *n) {
    (void)s;
    (void)n;
}

/// @brief Stub for `Scene3D.Find` — would normally search the scene tree
///        for a node by name and return the first match.
///
/// Silent stub returning NULL (not found).
///
/// @param s Scene3D handle (ignored).
/// @param n Node name to search for (ignored).
///
/// @return `NULL`.
void *rt_scene3d_find(void *s, rt_string n) {
    (void)s;
    (void)n;
    return NULL;
}

/// @brief Stub for `Scene3D.Draw` — would normally walk the scene graph
///        and issue draw calls for every visible mesh node, in front-to-back
///        order with frustum culling.
///
/// Silent no-op stub.
///
/// @param s   Scene3D handle (ignored).
/// @param c   Canvas3D handle (ignored).
/// @param cam Camera3D handle (ignored).
void rt_scene3d_draw(void *s, void *c, void *cam) {
    (void)s;
    (void)c;
    (void)cam;
}

/// @brief Stub for `Scene3D.Clear` — would normally remove all top-level
///        children from the scene's root, releasing references.
///
/// Silent no-op stub.
///
/// @param s Scene3D handle (ignored).
void rt_scene3d_clear(void *s) {
    (void)s;
}

/// @brief Stub for `Scene3D.NodeCount` — would normally return the total
///        number of nodes in the scene graph (recursive count from root).
///
/// Silent stub returning `0`.
///
/// @param s Scene3D handle (ignored).
///
/// @return `0`.
int64_t rt_scene3d_get_node_count(void *s) {
    (void)s;
    return 0;
}

/// @brief Stub for `Scene3D.Save` — would normally serialize the scene
///        graph to a `.vscn` file (Viper's native scene format) at
///        `path`. The format records node hierarchy, transforms, and
///        bound mesh/material references; it does not embed mesh data.
///
/// Trapping stub.
///
/// @param s    Scene3D handle (ignored).
/// @param path Output filesystem path (ignored).
///
/// @return Never returns normally.
int64_t rt_scene3d_save(void *s, rt_string path) {
    (void)s;
    (void)path;
    RT_GRAPHICS_TRAP_RET("Scene3D.Save: graphics support not compiled in", 0);
}

/// @brief Stub for `Scene3D.Load` — would normally parse a `.vscn`
///        file (Viper's native scene format) and reconstruct the node
///        hierarchy with shared resources.
///
/// Trapping stub.
///
/// @param path Filesystem path to the .vscn file (ignored).
///
/// @return Never returns normally.
void *rt_scene3d_load(rt_string path) {
    (void)path;
    rt_graphics_unavailable_("Scene3D.Load: graphics support not compiled in");
    return NULL;
}

/// @brief Stub for `Scene3D.SyncBindings` — batch-update transforms for
///        every node bound to a Physics3DBody, and advance every bound
///        AnimController3D by `dt` seconds. Should be called once per
///        frame after physics step but before draw.
///
/// Silent no-op stub.
///
/// @param s  Scene3D handle (ignored).
/// @param dt Delta time in seconds (ignored).
void rt_scene3d_sync_bindings(void *s, double dt) {
    (void)s;
    (void)dt;
}

/// @brief Stub for `SceneNode3D.New` — would normally create a new
///        scene-graph node with identity transform and no children.
///        Attach to a scene via `Scene3D.Add` or `SceneNode3D.AddChild`.
///
/// Trapping stub: nodes are wired into the scene immediately after
/// creation; a NULL return would crash the caller.
///
/// @return Never returns normally.
void *rt_scene_node3d_new(void) {
    rt_graphics_unavailable_("SceneNode3D.New: graphics support not compiled in");
    return NULL;
}

/// @brief Stub for `SceneNode3D.SetPosition` — set the node's local-
///        space position. Combined with the parent's world transform on
///        `Draw` to derive the node's world position.
///
/// Silent no-op stub.
///
/// @param n SceneNode3D handle (ignored).
/// @param x Local x (ignored).
/// @param y Local y (ignored).
/// @param z Local z (ignored).
void rt_scene_node3d_set_position(void *n, double x, double y, double z) {
    (void)n;
    (void)x;
    (void)y;
    (void)z;
}

/// @brief Stub for `SceneNode3D.Position` — get the node's local-space
///        position as a Vec3.
///
/// Silent stub returning NULL.
///
/// @param n SceneNode3D handle (ignored).
///
/// @return `NULL`.
void *rt_scene_node3d_get_position(void *n) {
    (void)n;
    return NULL;
}

/// @brief Stub for `SceneNode3D.SetRotation` — set the node's local
///        rotation from a Quaternion handle.
///
/// Silent no-op stub.
///
/// @param n SceneNode3D handle (ignored).
/// @param q Quaternion handle (ignored).
void rt_scene_node3d_set_rotation(void *n, void *q) {
    (void)n;
    (void)q;
}

/// @brief Stub for `SceneNode3D.Rotation` — get the node's local
///        rotation as a Quaternion handle.
///
/// Silent stub returning NULL.
///
/// @param n SceneNode3D handle (ignored).
///
/// @return `NULL`.
void *rt_scene_node3d_get_rotation(void *n) {
    (void)n;
    return NULL;
}

/// @brief Stub for `SceneNode3D.SetScale` — set the node's per-axis
///        local scale. `(1, 1, 1)` is identity.
///
/// Silent no-op stub.
///
/// @param n SceneNode3D handle (ignored).
/// @param x Scale x (ignored).
/// @param y Scale y (ignored).
/// @param z Scale z (ignored).
void rt_scene_node3d_set_scale(void *n, double x, double y, double z) {
    (void)n;
    (void)x;
    (void)y;
    (void)z;
}

/// @brief Stub for `SceneNode3D.Scale` — get the node's local scale as
///        a Vec3.
///
/// Silent stub returning NULL.
///
/// @param n SceneNode3D handle (ignored).
///
/// @return `NULL`.
void *rt_scene_node3d_get_scale(void *n) {
    (void)n;
    return NULL;
}

/// @brief Stub for `SceneNode3D.WorldMatrix` — get the node's composed
///        world-space TRS transform as a 4x4 matrix (concatenation of
///        every ancestor's local transform). Computed lazily on Draw.
///
/// Silent stub returning NULL.
///
/// @param n SceneNode3D handle (ignored).
///
/// @return `NULL`.
void *rt_scene_node3d_get_world_matrix(void *n) {
    (void)n;
    return NULL;
}

/// @brief Stub for `SceneNode3D.BindBody` — would normally attach a
///        Physics3DBody to this node so the scene-graph transform follows
///        the simulated body each frame (governed by the node's sync mode).
///
/// Silent no-op stub.
///
/// @param n SceneNode3D handle (ignored).
/// @param b Body3D handle, or NULL to detach (ignored).
void rt_scene_node3d_bind_body(void *n, void *b) {
    (void)n;
    (void)b;
}

/// @brief Stub for `SceneNode3D.ClearBodyBinding` — detach any bound
///        Physics3DBody from this node.
///
/// Silent no-op stub. Equivalent to `BindBody(node, NULL)`.
///
/// @param n SceneNode3D handle (ignored).
void rt_scene_node3d_clear_body_binding(void *n) {
    (void)n;
}

/// @brief Stub for `SceneNode3D.Body` — get the bound Physics3DBody, or
///        NULL if none is bound.
///
/// Silent stub returning NULL.
///
/// @param n SceneNode3D handle (ignored).
///
/// @return `NULL`.
void *rt_scene_node3d_get_body(void *n) {
    (void)n;
    return NULL;
}

/// @brief Stub for `SceneNode3D.SetSyncMode` — controls how the node and
///        its bound Physics3DBody exchange transforms each frame.
///        0 = none, 1 = body→node (kinematic visual), 2 = node→body
///        (kinematic physics).
///
/// Silent no-op stub.
///
/// @param n    SceneNode3D handle (ignored).
/// @param mode Sync mode, 0..2 (ignored).
void rt_scene_node3d_set_sync_mode(void *n, int64_t mode) {
    (void)n;
    (void)mode;
}

/// @brief Stub for `SceneNode3D.SyncMode` — get the current body/node
///        sync mode (see `SetSyncMode` for the value space).
///
/// Silent stub returning `0` (no sync).
///
/// @param n SceneNode3D handle (ignored).
///
/// @return `0`.
int64_t rt_scene_node3d_get_sync_mode(void *n) {
    (void)n;
    return 0;
}

/// @brief Stub for `SceneNode3D.BindAnimator` — attach an
///        AnimController3D so `Scene3D.SyncBindings(dt)` advances the
///        controller and applies the resulting pose to this node's skeleton.
///
/// Silent no-op stub.
///
/// @param n SceneNode3D handle (ignored).
/// @param c AnimController3D handle, or NULL to detach (ignored).
void rt_scene_node3d_bind_animator(void *n, void *c) {
    (void)n;
    (void)c;
}

/// @brief Stub for `SceneNode3D.ClearAnimatorBinding` — detach any bound
///        AnimController3D from this node.
///
/// Silent no-op stub. Equivalent to `BindAnimator(node, NULL)`.
///
/// @param n SceneNode3D handle (ignored).
void rt_scene_node3d_clear_animator_binding(void *n) {
    (void)n;
}

/// @brief Stub for `SceneNode3D.Animator` — get the bound
///        AnimController3D, or NULL if none.
///
/// Silent stub returning NULL.
///
/// @param n SceneNode3D handle (ignored).
///
/// @return `NULL`.
void *rt_scene_node3d_get_animator(void *n) {
    (void)n;
    return NULL;
}

/// @brief Stub for `SceneNode3D.AddChild` — would normally append `c` as
///        the last child of `n`, taking ownership of the reference.
///
/// Silent no-op stub.
///
/// @param n Parent SceneNode3D handle (ignored).
/// @param c Child SceneNode3D handle (ignored).
void rt_scene_node3d_add_child(void *n, void *c) {
    (void)n;
    (void)c;
}

/// @brief Stub for `SceneNode3D.RemoveChild` — detach `c` from `n` (if it
///        is a child); the subtree is preserved for re-attachment.
///
/// Silent no-op stub.
///
/// @param n Parent SceneNode3D handle (ignored).
/// @param c Child SceneNode3D handle (ignored).
void rt_scene_node3d_remove_child(void *n, void *c) {
    (void)n;
    (void)c;
}

/// @brief Stub for `SceneNode3D.ChildCount` — number of direct children
///        (not recursive).
///
/// Silent stub returning `0`.
///
/// @param n SceneNode3D handle (ignored).
///
/// @return `0`.
int64_t rt_scene_node3d_child_count(void *n) {
    (void)n;
    return 0;
}

/// @brief Stub for `SceneNode3D.Child(i)` — get the `i`th direct child by
///        insertion order.
///
/// Silent stub returning NULL.
///
/// @param n SceneNode3D handle (ignored).
/// @param i Child index, 0..ChildCount-1 (ignored).
///
/// @return `NULL`.
void *rt_scene_node3d_get_child(void *n, int64_t i) {
    (void)n;
    (void)i;
    return NULL;
}

/// @brief Stub for `SceneNode3D.Parent` — get the parent node, or NULL
///        for the scene root.
///
/// Silent stub returning NULL.
///
/// @param n SceneNode3D handle (ignored).
///
/// @return `NULL`.
void *rt_scene_node3d_get_parent(void *n) {
    (void)n;
    return NULL;
}

/// @brief Stub for `SceneNode3D.Find` — recursive name lookup within the
///        subtree rooted at `n`. Returns the first match or NULL.
///
/// Silent stub returning NULL.
///
/// @param n    SceneNode3D handle (ignored).
/// @param name Name to search for (ignored).
///
/// @return `NULL`.
void *rt_scene_node3d_find(void *n, rt_string name) {
    (void)n;
    (void)name;
    return NULL;
}

/// @brief Stub for `SceneNode3D.SetMesh` — bind a Mesh3D to this node so
///        it will be drawn at the node's world transform during
///        `Scene3D.Draw`.
///
/// Silent no-op stub.
///
/// @param n SceneNode3D handle (ignored).
/// @param m Mesh3D handle, or NULL to make this node a transform-only
///          parent (ignored).
void rt_scene_node3d_set_mesh(void *n, void *m) {
    (void)n;
    (void)m;
}

/// @brief Stub for `SceneNode3D.Mesh` — get the bound Mesh3D, or NULL if
///        none.
///
/// Silent stub returning NULL.
///
/// @param n SceneNode3D handle (ignored).
///
/// @return `NULL`.
void *rt_scene_node3d_get_mesh(void *n) {
    (void)n;
    return NULL;
}

/// @brief Stub for `SceneNode3D.SetMaterial` — bind a Material3D used
///        when drawing this node's mesh. If unset, the renderer falls back
///        to a default white Blinn-Phong material.
///
/// Silent no-op stub.
///
/// @param n SceneNode3D handle (ignored).
/// @param m Material3D handle, or NULL (ignored).
void rt_scene_node3d_set_material(void *n, void *m) {
    (void)n;
    (void)m;
}

/// @brief Stub for `SceneNode3D.Material` — get the bound Material3D, or
///        NULL if none.
///
/// Silent stub returning NULL.
///
/// @param n SceneNode3D handle (ignored).
///
/// @return `NULL`.
void *rt_scene_node3d_get_material(void *n) {
    (void)n;
    return NULL;
}

/// @brief Stub for `SceneNode3D.SetVisible` — when disabled, the node and
///        all descendants are skipped during `Scene3D.Draw`.
///
/// Silent no-op stub.
///
/// @param n SceneNode3D handle (ignored).
/// @param v Non-zero to make visible (ignored).
void rt_scene_node3d_set_visible(void *n, int8_t v) {
    (void)n;
    (void)v;
}

/// @brief Stub for `SceneNode3D.Visible` — get the visibility flag.
///
/// Silent stub returning `0` (hidden) — opposite of the real
/// implementation default, but reachability is moot here.
///
/// @param n SceneNode3D handle (ignored).
///
/// @return `0`.
int8_t rt_scene_node3d_get_visible(void *n) {
    (void)n;
    return 0;
}

/// @brief Stub for `SceneNode3D.SetName` — assign a name to the node so
///        it can be located via `Scene3D.Find` / `SceneNode3D.Find`.
///
/// Silent no-op stub. Names are not required to be unique.
///
/// @param n SceneNode3D handle (ignored).
/// @param s Name string (ignored).
void rt_scene_node3d_set_name(void *n, rt_string s) {
    (void)n;
    (void)s;
}

/// @brief Stub for `SceneNode3D.Name` — get the assigned name, or NULL
///        if unnamed.
///
/// Silent stub returning NULL.
///
/// @param n SceneNode3D handle (ignored).
///
/// @return `NULL`.
rt_string rt_scene_node3d_get_name(void *n) {
    (void)n;
    return NULL;
}

/// @brief Stub for `SceneNode3D.AABBMin` — get the min corner of the
///        node's world-space axis-aligned bounding box.
///
/// Silent stub returning NULL.
///
/// @param n SceneNode3D handle (ignored).
///
/// @return `NULL`.
void *rt_scene_node3d_get_aabb_min(void *n) {
    (void)n;
    return NULL;
}

/// @brief Stub for `SceneNode3D.AABBMax` — get the max corner of the
///        node's world-space axis-aligned bounding box.
///
/// Silent stub returning NULL.
///
/// @param n SceneNode3D handle (ignored).
///
/// @return `NULL`.
void *rt_scene_node3d_get_aabb_max(void *n) {
    (void)n;
    return NULL;
}

/// @brief Stub for `Scene3D.CulledCount` — number of nodes that were
///        skipped during the most recent `Draw` due to frustum culling
///        (debug / profiling).
///
/// Silent stub returning `0`.
///
/// @param s Scene3D handle (ignored).
///
/// @return `0`.
int64_t rt_scene3d_get_culled_count(void *s) {
    (void)s;
    return 0;
}

/* LOD stubs */

/// @brief Stub for `SceneNode3D.AddLOD` — add a level-of-detail entry:
///        when the camera is `>= d` world units away, render `m` instead
///        of the node's primary mesh. Multiple LODs can be stacked at
///        increasing distances.
///
/// Silent no-op stub.
///
/// @param n SceneNode3D handle (ignored).
/// @param d Distance threshold for this LOD level (ignored).
/// @param m Mesh3D handle to render at or beyond `d` (ignored).
void rt_scene_node3d_add_lod(void *n, double d, void *m) {
    (void)n;
    (void)d;
    (void)m;
}

/// @brief Stub for `SceneNode3D.ClearLOD` — remove all LOD entries.
///        After this the node always renders its primary mesh regardless
///        of camera distance.
///
/// Silent no-op stub.
///
/// @param n SceneNode3D handle (ignored).
void rt_scene_node3d_clear_lod(void *n) {
    (void)n;
}

/// @brief Stub for `SceneNode3D.LODCount` — number of LOD entries
///        attached to the node.
///
/// Silent stub returning `0`.
///
/// @param n SceneNode3D handle (ignored).
///
/// @return `0`.
int64_t rt_scene_node3d_get_lod_count(void *n) {
    (void)n;
    return 0;
}

/// @brief Stub for `SceneNode3D.LODDistance(i)` — get the distance
///        threshold of the `i`th LOD entry.
///
/// Silent stub returning `0.0`.
///
/// @param n     SceneNode3D handle (ignored).
/// @param index LOD index, 0..LODCount-1 (ignored).
///
/// @return `0.0`.
double rt_scene_node3d_get_lod_distance(void *n, int64_t index) {
    (void)n;
    (void)index;
    return 0.0;
}

/// @brief Stub for `SceneNode3D.LODMesh(i)` — get the Mesh3D associated
///        with the `i`th LOD entry.
///
/// Silent stub returning NULL.
///
/// @param n     SceneNode3D handle (ignored).
/// @param index LOD index (ignored).
///
/// @return `NULL`.
void *rt_scene_node3d_get_lod_mesh(void *n, int64_t index) {
    (void)n;
    (void)index;
    return NULL;
}

/* Skeleton3D / Animation3D / AnimPlayer3D stubs */

/// @brief Stub for `Skeleton3D.New` — would normally allocate an empty
///        skeleton ready to receive bones via `AddBone`.
///
/// Trapping stub: callers expect a usable handle for bone-add calls.
///
/// @return Never returns normally.
void *rt_skeleton3d_new(void) {
    rt_graphics_unavailable_("Skeleton3D.New: graphics support not compiled in");
    return NULL;
}

/// @brief Stub for `Skeleton3D.AddBone` — append a bone with name `n`,
///        parent index `p` (`-1` for root), and bind-pose transform
///        matrix `m`. Returns the assigned bone index, or `-1` on failure.
///
/// Silent stub returning `-1`.
///
/// @param s Skeleton3D handle (ignored).
/// @param n Bone name (ignored).
/// @param p Parent bone index, or `-1` for root (ignored).
/// @param m Mat4 bind-pose transform handle (ignored).
///
/// @return `-1`.
int64_t rt_skeleton3d_add_bone(void *s, rt_string n, int64_t p, void *m) {
    (void)s;
    (void)n;
    (void)p;
    (void)m;
    return -1;
}

/// @brief Stub for `Skeleton3D.ComputeInverseBind` — precompute and
///        cache the per-bone inverse-bind-pose matrices used by skinning.
///        Must be called once after all bones are added; before this the
///        skeleton cannot be used for rendering.
///
/// Silent no-op stub.
///
/// @param s Skeleton3D handle (ignored).
void rt_skeleton3d_compute_inverse_bind(void *s) {
    (void)s;
}

/// @brief Stub for `Skeleton3D.BoneCount` — number of bones currently in
///        the skeleton.
///
/// Silent stub returning `0`.
///
/// @param s Skeleton3D handle (ignored).
///
/// @return `0`.
int64_t rt_skeleton3d_get_bone_count(void *s) {
    (void)s;
    return 0;
}

/// @brief Stub for `Skeleton3D.FindBone` — get the index of the bone
///        with the given name, or `-1` if not found. O(n) search.
///
/// Silent stub returning `-1`.
///
/// @param s Skeleton3D handle (ignored).
/// @param n Bone name to search for (ignored).
///
/// @return `-1`.
int64_t rt_skeleton3d_find_bone(void *s, rt_string n) {
    (void)s;
    (void)n;
    return -1;
}

/// @brief Stub for `Skeleton3D.BoneName(i)` — get the name of the `i`th
///        bone (as set during `AddBone`).
///
/// Silent stub returning NULL.
///
/// @param s Skeleton3D handle (ignored).
/// @param i Bone index (ignored).
///
/// @return `NULL`.
rt_string rt_skeleton3d_get_bone_name(void *s, int64_t i) {
    (void)s;
    (void)i;
    return NULL;
}

/// @brief Stub for `Skeleton3D.BoneBindPose(i)` — get the `i`th bone's
///        bind-pose transform as a Mat4 handle.
///
/// Silent stub returning NULL.
///
/// @param s Skeleton3D handle (ignored).
/// @param i Bone index (ignored).
///
/// @return `NULL`.
void *rt_skeleton3d_get_bone_bind_pose(void *s, int64_t i) {
    (void)s;
    (void)i;
    return NULL;
}

/// @brief Stub for `Animation3D.New` — would normally allocate an empty
///        animation track with the given name and duration. Keyframes are
///        added afterward via `AddKeyframe`.
///
/// Trapping stub: callers expect a usable handle for the keyframe pipeline.
///
/// @param n Animation name (ignored).
/// @param d Duration in seconds (ignored).
///
/// @return Never returns normally.
void *rt_animation3d_new(rt_string n, double d) {
    (void)n;
    (void)d;
    rt_graphics_unavailable_("Animation3D.New: graphics support not compiled in");
    return NULL;
}

/// @brief Stub for `Animation3D.AddKeyframe` — would normally append a
///        TRS keyframe to the animation track for the given bone.
///
/// Silent no-op stub. Each keyframe captures position, rotation, and scale
/// at a particular time; the player interpolates between adjacent frames.
///
/// @param a Animation3D handle (ignored).
/// @param b Bone index this keyframe applies to (ignored).
/// @param t Time stamp in seconds (ignored).
/// @param p Vec3 position handle (ignored).
/// @param r Quaternion rotation handle (ignored).
/// @param s Vec3 scale handle (ignored).
void rt_animation3d_add_keyframe(void *a, int64_t b, double t, void *p, void *r, void *s) {
    (void)a;
    (void)b;
    (void)t;
    (void)p;
    (void)r;
    (void)s;
}

/// @brief Stub for `Animation3D.SetLooping` — when enabled, the animation
///        wraps around to the start after reaching its duration.
///
/// Silent no-op stub.
///
/// @param a Animation3D handle (ignored).
/// @param l Non-zero to enable looping (ignored).
void rt_animation3d_set_looping(void *a, int8_t l) {
    (void)a;
    (void)l;
}

/// @brief Stub for `Animation3D.Looping` — get the looping flag.
///
/// Silent stub returning `0` (one-shot).
///
/// @param a Animation3D handle (ignored).
///
/// @return `0`.
int8_t rt_animation3d_get_looping(void *a) {
    (void)a;
    return 0;
}

/// @brief Stub for `Animation3D.Duration` — get the animation length in
///        seconds (time of the last keyframe across all bones).
///
/// Silent stub returning `0.0`.
///
/// @param a Animation3D handle (ignored).
///
/// @return `0.0`.
double rt_animation3d_get_duration(void *a) {
    (void)a;
    return 0.0;
}

/// @brief Stub for `Animation3D.Name` — get the animation's name (e.g.
///        "Idle", "Run", set during glTF/FBX import).
///
/// Silent stub returning NULL.
///
/// @param a Animation3D handle (ignored).
///
/// @return `NULL`.
rt_string rt_animation3d_get_name(void *a) {
    (void)a;
    return NULL;
}

/// @brief Stub for `AnimPlayer3D.New` — would normally create a player
///        bound to the given Skeleton3D. The player owns a per-bone pose
///        buffer that the player updates each tick.
///
/// Trapping stub: a NULL player would crash on the first `Play` call.
///
/// @param s Skeleton3D handle (ignored).
///
/// @return Never returns normally.
void *rt_anim_player3d_new(void *s) {
    (void)s;
    rt_graphics_unavailable_("AnimPlayer3D.New: graphics support not compiled in");
    return NULL;
}

/// @brief Stub for `AnimPlayer3D.Play` — start playing the given animation
///        from the beginning, replacing any currently-active animation.
///
/// Silent no-op stub.
///
/// @param p AnimPlayer3D handle (ignored).
/// @param a Animation3D handle (ignored).
void rt_anim_player3d_play(void *p, void *a) {
    (void)p;
    (void)a;
}

/// @brief Stub for `AnimPlayer3D.Crossfade` — start blending into a new
///        animation over `d` seconds. The previous animation is faded out
///        as the new one is faded in.
///
/// Silent no-op stub.
///
/// @param p AnimPlayer3D handle (ignored).
/// @param a Target Animation3D handle (ignored).
/// @param d Crossfade duration in seconds (ignored).
void rt_anim_player3d_crossfade(void *p, void *a, double d) {
    (void)p;
    (void)a;
    (void)d;
}

/// @brief Stub for `AnimPlayer3D.Stop` — halt playback and freeze the
///        skeleton at the current pose.
///
/// Silent no-op stub.
///
/// @param p AnimPlayer3D handle (ignored).
void rt_anim_player3d_stop(void *p) {
    (void)p;
}

/// @brief Stub for `AnimPlayer3D.Update` — advance the animation clock by
///        `d` seconds and recompute the pose. Should be called once per
///        frame.
///
/// Silent no-op stub.
///
/// @param p AnimPlayer3D handle (ignored).
/// @param d Delta time in seconds (ignored).
void rt_anim_player3d_update(void *p, double d) {
    (void)p;
    (void)d;
}

/// @brief Stub for `AnimPlayer3D.SetSpeed` — multiplier on the per-tick
///        delta-time. 1.0 = normal, 0.5 = half-speed, 2.0 = double-speed.
///        Negative values play the animation in reverse.
///
/// Silent no-op stub.
///
/// @param p AnimPlayer3D handle (ignored).
/// @param s Speed multiplier (ignored).
void rt_anim_player3d_set_speed(void *p, double s) {
    (void)p;
    (void)s;
}

/// @brief Stub for `AnimPlayer3D.Speed` — get the current playback speed
///        multiplier.
///
/// Silent stub returning `1.0` (normal speed).
///
/// @param p AnimPlayer3D handle (ignored).
///
/// @return `1.0`.
double rt_anim_player3d_get_speed(void *p) {
    (void)p;
    return 1.0;
}

/// @brief Stub for `AnimPlayer3D.IsPlaying` — true while an animation is
///        active and the clock is advancing.
///
/// Silent stub returning `0` (idle).
///
/// @param p AnimPlayer3D handle (ignored).
///
/// @return `0`.
int8_t rt_anim_player3d_is_playing(void *p) {
    (void)p;
    return 0;
}

/// @brief Stub for `AnimPlayer3D.Time` — current playback time in seconds
///        within the active animation.
///
/// Silent stub returning `0.0`.
///
/// @param p AnimPlayer3D handle (ignored).
///
/// @return `0.0`.
double rt_anim_player3d_get_time(void *p) {
    (void)p;
    return 0.0;
}

/// @brief Stub for `AnimPlayer3D.SetTime` — seek to the given time within
///        the active animation; pose is recomputed from keyframes.
///
/// Silent no-op stub.
///
/// @param p AnimPlayer3D handle (ignored).
/// @param t Time in seconds, 0..duration (ignored).
void rt_anim_player3d_set_time(void *p, double t) {
    (void)p;
    (void)t;
}

/// @brief Stub for `AnimPlayer3D.BoneMatrix` — get the world-space
///        transform matrix for bone `i` at the current pose. Used by the
///        renderer to compute final per-vertex skinning.
///
/// Silent stub returning NULL.
///
/// @param p AnimPlayer3D handle (ignored).
/// @param i Bone index, 0..BoneCount-1 (ignored).
///
/// @return `NULL`.
void *rt_anim_player3d_get_bone_matrix(void *p, int64_t i) {
    (void)p;
    (void)i;
    return NULL;
}

/// @brief Stub for `Mesh3D.SetSkeleton` — bind a Skeleton3D so the mesh
///        can be rendered with `DrawMeshSkinned`. Per-vertex bone weights
///        must also be set via `SetBoneWeights`.
///
/// Silent no-op stub.
///
/// @param m Mesh3D handle (ignored).
/// @param s Skeleton3D handle, or NULL to detach (ignored).
void rt_mesh3d_set_skeleton(void *m, void *s) {
    (void)m;
    (void)s;
}

/// @brief Stub for `Mesh3D.SetBoneWeights` — set the four bone-weight
///        pairs influencing vertex `v`. Bone indices are into the bound
///        Skeleton3D; weights should sum to ~1.0.
///
/// Silent no-op stub. Each vertex can be influenced by up to 4 bones —
/// this matches the GPU-skinning implementation across all backends.
///
/// @param m  Mesh3D handle (ignored).
/// @param v  Vertex index, 0..VertexCount-1 (ignored).
/// @param b0 First bone index (ignored).
/// @param w0 First bone weight (ignored).
/// @param b1 Second bone index (ignored).
/// @param w1 Second bone weight (ignored).
/// @param b2 Third bone index (ignored).
/// @param w2 Third bone weight (ignored).
/// @param b3 Fourth bone index (ignored).
/// @param w3 Fourth bone weight (ignored).
void rt_mesh3d_set_bone_weights(void *m,
                                int64_t v,
                                int64_t b0,
                                double w0,
                                int64_t b1,
                                double w1,
                                int64_t b2,
                                double w2,
                                int64_t b3,
                                double w3) {
    (void)m;
    (void)v;
    (void)b0;
    (void)w0;
    (void)b1;
    (void)w1;
    (void)b2;
    (void)w2;
    (void)b3;
    (void)w3;
}

/// @brief Stub for `Canvas3D.DrawMeshSkinned` — variant of `DrawMesh`
///        that applies skeletal skinning. The vertex shader fetches the
///        bone palette from `p` (AnimPlayer3D) and computes the final
///        per-vertex transform from the four bone-weight pairs.
///
/// Silent no-op stub.
///
/// @param c   Canvas3D handle (ignored).
/// @param m   Mesh3D handle with bone weights set (ignored).
/// @param t   Transform3D handle (ignored).
/// @param mat Material3D handle (ignored).
/// @param p   AnimPlayer3D handle providing the bone palette (ignored).
void rt_canvas3d_draw_mesh_skinned(void *c, void *m, void *t, void *mat, void *p) {
    (void)c;
    (void)m;
    (void)t;
    (void)mat;
    (void)p;
}

/* FBX Loader stubs */

/// @brief Stub for `FBX.Load` — would normally parse an Autodesk FBX
///        file (binary or ASCII auto-detected) and return a populated FBX
///        document handle.
///
/// Trapping stub: a NULL document handle would crash on every subsequent
/// query.
///
/// @param p Filesystem path to the .fbx file (ignored).
///
/// @return Never returns normally.
void *rt_fbx_load(rt_string p) {
    (void)p;
    rt_graphics_unavailable_("FBX.Load: graphics support not compiled in");
    return NULL;
}

/// @brief Stub for `FBX.MeshCount` — number of meshes in the loaded FBX
///        document.
///
/// Silent stub returning `0`.
///
/// @param f FBX document handle (ignored).
///
/// @return `0`.
int64_t rt_fbx_mesh_count(void *f) {
    (void)f;
    return 0;
}

/// @brief Stub for `FBX.Mesh(i)` — get the `i`th mesh as a Mesh3D.
///
/// Silent stub returning NULL.
///
/// @param f FBX document handle (ignored).
/// @param i Mesh index (ignored).
///
/// @return `NULL`.
void *rt_fbx_get_mesh(void *f, int64_t i) {
    (void)f;
    (void)i;
    return NULL;
}

/// @brief Stub for `FBX.Skeleton` — get the document's primary skeleton
///        (FBX typically embeds a single bind-pose skeleton shared by all
///        skinned meshes).
///
/// Silent stub returning NULL.
///
/// @param f FBX document handle (ignored).
///
/// @return `NULL`.
void *rt_fbx_get_skeleton(void *f) {
    (void)f;
    return NULL;
}

/// @brief Stub for `FBX.AnimationCount` — number of animation tracks
///        (clips) in the document.
///
/// Silent stub returning `0`.
///
/// @param f FBX document handle (ignored).
///
/// @return `0`.
int64_t rt_fbx_animation_count(void *f) {
    (void)f;
    return 0;
}

/// @brief Stub for `FBX.Animation(i)` — get the `i`th animation as an
///        Animation3D.
///
/// Silent stub returning NULL.
///
/// @param f FBX document handle (ignored).
/// @param i Animation index (ignored).
///
/// @return `NULL`.
void *rt_fbx_get_animation(void *f, int64_t i) {
    (void)f;
    (void)i;
    return NULL;
}

/// @brief Stub for `FBX.AnimationName(i)` — get the name of the `i`th
///        animation (typically authored in the DCC tool).
///
/// Silent stub returning NULL.
///
/// @param f FBX document handle (ignored).
/// @param i Animation index (ignored).
///
/// @return `NULL`.
rt_string rt_fbx_get_animation_name(void *f, int64_t i) {
    (void)f;
    (void)i;
    return NULL;
}

/// @brief Stub for `FBX.MaterialCount` — number of materials defined in
///        the document.
///
/// Silent stub returning `0`.
///
/// @param f FBX document handle (ignored).
///
/// @return `0`.
int64_t rt_fbx_material_count(void *f) {
    (void)f;
    return 0;
}

/// @brief Stub for `FBX.Material(i)` — get the `i`th material as a
///        Material3D (texture paths and connection traces extracted from
///        the FBX node graph).
///
/// Silent stub returning NULL.
///
/// @param f FBX document handle (ignored).
/// @param i Material index (ignored).
///
/// @return `NULL`.
void *rt_fbx_get_material(void *f, int64_t i) {
    (void)f;
    (void)i;
    return NULL;
}

/// @brief Stub for `FBX.MorphTarget(i)` — get the `i`th morph target
///        as a MorphTarget3D. FBX BlendShape / Shape nodes are extracted
///        with sparse position/normal deltas during import.
///
/// Silent stub returning NULL.
///
/// @param f FBX document handle (ignored).
/// @param i Morph target index (ignored).
///
/// @return `NULL`.
void *rt_fbx_get_morph_target(void *f, int64_t i) {
    (void)f;
    (void)i;
    return NULL;
}

/* GLTF Loader stubs */

/// @brief Stub for `glTF.Load` — would normally parse a `.gltf` (JSON +
///        external buffers) or `.glb` (single binary container) file and
///        return a populated glTF document handle.
///
/// Trapping stub: a NULL document handle would crash on every subsequent
/// query (`MeshCount`, `Material(i)`, etc.).
///
/// @param p Filesystem path to the .gltf or .glb file (ignored).
///
/// @return Never returns normally.
void *rt_gltf_load(rt_string p) {
    (void)p;
    rt_graphics_unavailable_("GLTF.Load: graphics support not compiled in");
    return NULL;
}

/// @brief Stub for `glTF.MeshCount` — number of meshes in the loaded
///        document.
///
/// Silent stub returning `0`.
///
/// @param g glTF document handle (ignored).
///
/// @return `0`.
int64_t rt_gltf_mesh_count(void *g) {
    (void)g;
    return 0;
}

/// @brief Stub for `glTF.Mesh(i)` — get the `i`th mesh as a Mesh3D.
///
/// Silent stub returning NULL.
///
/// @param g glTF document handle (ignored).
/// @param i Mesh index, 0..MeshCount-1 (ignored).
///
/// @return `NULL`.
void *rt_gltf_get_mesh(void *g, int64_t i) {
    (void)g;
    (void)i;
    return NULL;
}

/// @brief Stub for `glTF.MaterialCount` — number of materials in the
///        loaded document.
///
/// Silent stub returning `0`.
///
/// @param g glTF document handle (ignored).
///
/// @return `0`.
int64_t rt_gltf_material_count(void *g) {
    (void)g;
    return 0;
}

/// @brief Stub for `glTF.Material(i)` — get the `i`th material as a
///        Material3D (PBR metallic-roughness).
///
/// Silent stub returning NULL.
///
/// @param g glTF document handle (ignored).
/// @param i Material index, 0..MaterialCount-1 (ignored).
///
/// @return `NULL`.
void *rt_gltf_get_material(void *g, int64_t i) {
    (void)g;
    (void)i;
    return NULL;
}

/// @brief Stub for `glTF.NodeCount` — total number of scene nodes in the
///        document (recursive count from the scene root).
///
/// Silent stub returning `0`.
///
/// @param g glTF document handle (ignored).
///
/// @return `0`.
int64_t rt_gltf_node_count(void *g) {
    (void)g;
    return 0;
}

/// @brief Stub for `glTF.SceneRoot` — get the document's default-scene
///        root SceneNode3D.
///
/// Silent stub returning NULL.
///
/// @param g glTF document handle (ignored).
///
/// @return `NULL`.
void *rt_gltf_get_scene_root(void *g) {
    (void)g;
    return NULL;
}

/* Model3D stubs */

/// @brief Stub for `Model3D.Load` — would normally route by file
///        extension (.vscn / .fbx / .gltf / .glb) and build an internal
///        resource collection (meshes, materials, skeletons, animations).
///
/// Trapping stub. The real `Model3D` is the unified asset container
/// callers go through to share resources across instances.
///
/// @param p Filesystem path to the asset file (ignored).
///
/// @return Never returns normally.
void *rt_model3d_load(rt_string p) {
    (void)p;
    rt_graphics_unavailable_("Model3D.Load: graphics support not compiled in");
    return NULL;
}

/// @brief Stub for `Model3D.MeshCount` — number of meshes in the loaded
///        model's resource collection.
///
/// Silent stub returning `0`.
///
/// @param m Model3D handle (ignored).
///
/// @return `0`.
int64_t rt_model3d_get_mesh_count(void *m) {
    (void)m;
    return 0;
}

/// @brief Stub for `Model3D.MaterialCount` — number of materials.
///
/// Silent stub returning `0`.
///
/// @param m Model3D handle (ignored).
///
/// @return `0`.
int64_t rt_model3d_get_material_count(void *m) {
    (void)m;
    return 0;
}

/// @brief Stub for `Model3D.SkeletonCount` — number of skeletons (one
///        per skinned mesh in the model).
///
/// Silent stub returning `0`.
///
/// @param m Model3D handle (ignored).
///
/// @return `0`.
int64_t rt_model3d_get_skeleton_count(void *m) {
    (void)m;
    return 0;
}

/// @brief Stub for `Model3D.AnimationCount` — number of animations
///        embedded in the model.
///
/// Silent stub returning `0`.
///
/// @param m Model3D handle (ignored).
///
/// @return `0`.
int64_t rt_model3d_get_animation_count(void *m) {
    (void)m;
    return 0;
}

/// @brief Stub for `Model3D.NodeCount` — total node count across the
///        model's scene tree.
///
/// Silent stub returning `0`.
///
/// @param m Model3D handle (ignored).
///
/// @return `0`.
int64_t rt_model3d_get_node_count(void *m) {
    (void)m;
    return 0;
}

/// @brief Stub for `Model3D.Mesh(i)` — get the `i`th mesh from the
///        model's resource collection.
///
/// Silent stub returning NULL.
///
/// @param m Model3D handle (ignored).
/// @param i Mesh index, 0..MeshCount-1 (ignored).
///
/// @return `NULL`.
void *rt_model3d_get_mesh(void *m, int64_t i) {
    (void)m;
    (void)i;
    return NULL;
}

/// @brief Stub for `Model3D.Material(i)` — get the `i`th material.
///
/// Silent stub returning NULL.
///
/// @param m Model3D handle (ignored).
/// @param i Material index, 0..MaterialCount-1 (ignored).
///
/// @return `NULL`.
void *rt_model3d_get_material(void *m, int64_t i) {
    (void)m;
    (void)i;
    return NULL;
}

/// @brief Stub for `Model3D.Skeleton(i)` — get the `i`th skeleton.
///
/// Silent stub returning NULL.
///
/// @param m Model3D handle (ignored).
/// @param i Skeleton index, 0..SkeletonCount-1 (ignored).
///
/// @return `NULL`.
void *rt_model3d_get_skeleton(void *m, int64_t i) {
    (void)m;
    (void)i;
    return NULL;
}

/// @brief Stub for `Model3D.Animation(i)` — get the `i`th animation.
///
/// Silent stub returning NULL.
///
/// @param m Model3D handle (ignored).
/// @param i Animation index, 0..AnimationCount-1 (ignored).
///
/// @return `NULL`.
void *rt_model3d_get_animation(void *m, int64_t i) {
    (void)m;
    (void)i;
    return NULL;
}

/// @brief Stub for `Model3D.FindNode(name)` — recursive name lookup
///        within the model's node hierarchy.
///
/// Silent stub returning NULL.
///
/// @param m    Model3D handle (ignored).
/// @param name Node name to search for (ignored).
///
/// @return `NULL`.
void *rt_model3d_find_node(void *m, rt_string name) {
    (void)m;
    (void)name;
    return NULL;
}

/// @brief Stub for `Model3D.Instantiate` — would normally clone the
///        model's node hierarchy into a fresh SceneNode3D tree, sharing
///        underlying mesh/material/skeleton resources with other instances.
///
/// Silent stub returning NULL. The real implementation enables a single
/// imported model to be drawn many times without duplicating geometry.
///
/// @param m Model3D handle (ignored).
///
/// @return `NULL`.
void *rt_model3d_instantiate(void *m) {
    (void)m;
    return NULL;
}

/// @brief Stub for `Model3D.InstantiateScene` — would normally create a
///        fresh `Scene3D` and attach cloned top-level nodes below the
///        scene root.
///
/// Silent stub returning NULL.
///
/// @param m Model3D handle (ignored).
///
/// @return `NULL`.
void *rt_model3d_instantiate_scene(void *m) {
    (void)m;
    return NULL;
}

/* MorphTarget3D stubs */

/// @brief Stub for `MorphTarget3D.New` — would normally allocate a
///        morph-target container for a mesh with `vc` vertices. The
///        container holds zero shapes; add shapes via `AddShape`.
///
/// Trapping stub: callers expect a usable handle for shape-add and weight
/// queries.
///
/// @param vc Vertex count of the bound mesh (ignored).
///
/// @return Never returns normally.
void *rt_morphtarget3d_new(int64_t vc) {
    (void)vc;
    rt_graphics_unavailable_("MorphTarget3D.New: graphics support not compiled in");
    return NULL;
}

/// @brief Stub for `MorphTarget3D.AddShape` — append a named shape (a
///        per-vertex delta set) to the container. Returns the assigned
///        shape index used by `SetDelta` / `SetWeight`, or `-1` on failure.
///
/// Silent stub returning `-1`.
///
/// @param m MorphTarget3D handle (ignored).
/// @param n Shape name (e.g. "smile", "blink_left") (ignored).
///
/// @return `-1`.
int64_t rt_morphtarget3d_add_shape(void *m, rt_string n) {
    (void)m;
    (void)n;
    return -1;
}

/// @brief Stub for `MorphTarget3D.SetDelta` — set the per-vertex
///        position delta for shape `s`, vertex `v`. Deltas are accumulated
///        weighted by the shape's current weight during morphing.
///
/// Silent no-op stub.
///
/// @param m  MorphTarget3D handle (ignored).
/// @param s  Shape index (ignored).
/// @param v  Vertex index, 0..VertexCount-1 (ignored).
/// @param dx Position delta x (ignored).
/// @param dy Position delta y (ignored).
/// @param dz Position delta z (ignored).
void rt_morphtarget3d_set_delta(void *m, int64_t s, int64_t v, double dx, double dy, double dz) {
    (void)m;
    (void)s;
    (void)v;
    (void)dx;
    (void)dy;
    (void)dz;
}

/// @brief Stub for `MorphTarget3D.SetNormalDelta` — set the per-vertex
///        normal delta for shape `s`, vertex `v`. Required for correct
///        lighting on heavily-morphed surfaces (faces).
///
/// Silent no-op stub.
///
/// @param m  MorphTarget3D handle (ignored).
/// @param s  Shape index (ignored).
/// @param v  Vertex index (ignored).
/// @param dx Normal delta x (ignored).
/// @param dy Normal delta y (ignored).
/// @param dz Normal delta z (ignored).
void rt_morphtarget3d_set_normal_delta(
    void *m, int64_t s, int64_t v, double dx, double dy, double dz) {
    (void)m;
    (void)s;
    (void)v;
    (void)dx;
    (void)dy;
    (void)dz;
}

/// @brief Stub for `MorphTarget3D.SetWeight` — set the blend weight of
///        shape `s`. Weights are typically 0..1; multiple shapes can be
///        active simultaneously and their deltas sum.
///
/// Silent no-op stub.
///
/// @param m MorphTarget3D handle (ignored).
/// @param s Shape index (ignored).
/// @param w Blend weight (ignored).
void rt_morphtarget3d_set_weight(void *m, int64_t s, double w) {
    (void)m;
    (void)s;
    (void)w;
}

/// @brief Stub for `MorphTarget3D.Weight` — get the current blend weight
///        of shape `s`.
///
/// Silent stub returning `0.0`.
///
/// @param m MorphTarget3D handle (ignored).
/// @param s Shape index (ignored).
///
/// @return `0.0`.
double rt_morphtarget3d_get_weight(void *m, int64_t s) {
    (void)m;
    (void)s;
    return 0.0;
}

/// @brief Stub for `MorphTarget3D.SetWeightByName` — set blend weight of
///        the shape with the given name. Convenience wrapper around
///        `SetWeight` for callers that don't track indices.
///
/// Silent no-op stub.
///
/// @param m MorphTarget3D handle (ignored).
/// @param n Shape name (ignored).
/// @param w Blend weight (ignored).
void rt_morphtarget3d_set_weight_by_name(void *m, rt_string n, double w) {
    (void)m;
    (void)n;
    (void)w;
}

/// @brief Stub for `MorphTarget3D.ShapeCount` — number of shapes
///        currently in the container.
///
/// Silent stub returning `0`.
///
/// @param m MorphTarget3D handle (ignored).
///
/// @return `0`.
int64_t rt_morphtarget3d_get_shape_count(void *m) {
    (void)m;
    return 0;
}

/// @brief Stub for `Mesh3D.SetMorphTargets` — bind a MorphTarget3D
///        container to this mesh so subsequent `Canvas3D.DrawMeshMorphed`
///        calls apply weighted shape deltas during vertex transformation.
///
/// Silent no-op stub.
///
/// @param m  Mesh3D handle (ignored).
/// @param mt MorphTarget3D handle, or NULL to clear (ignored).
void rt_mesh3d_set_morph_targets(void *m, void *mt) {
    (void)m;
    (void)mt;
}

/// @brief Stub for `Canvas3D.DrawMeshMorphed` — variant of `DrawMesh`
///        that applies the bound MorphTarget3D's currently-weighted
///        shapes to vertex positions/normals before transformation.
///
/// Silent no-op stub.
///
/// @param c   Canvas3D handle (ignored).
/// @param m   Mesh3D handle (ignored).
/// @param t   Transform3D handle (ignored).
/// @param mat Material3D handle (ignored).
/// @param mt  MorphTarget3D handle (ignored).
void rt_canvas3d_draw_mesh_morphed(void *c, void *m, void *t, void *mat, void *mt) {
    (void)c;
    (void)m;
    (void)t;
    (void)mat;
    (void)mt;
}

/* Particles3D stubs */

/// @brief Stub for `Particles3D.New` — would normally allocate a
///        particle system with `n` slots in the per-particle pool.
///
/// Trapping stub: callers will configure / start the system and depend
/// on a usable handle.
///
/// @param n Maximum live particle count (ignored).
///
/// @return Never returns normally.
void *rt_particles3d_new(int64_t n) {
    (void)n;
    rt_graphics_unavailable_("Particles3D.New: graphics support not compiled in");
    return NULL;
}

/// @brief Stub for `Particles3D.SetPosition` — set the emitter origin
///        in world space. Particles spawn around this position (offset by
///        the emitter shape).
///
/// Silent no-op stub.
///
/// @param o Particles3D handle (ignored).
/// @param x World x (ignored).
/// @param y World y (ignored).
/// @param z World z (ignored).
void rt_particles3d_set_position(void *o, double x, double y, double z) {
    (void)o;
    (void)x;
    (void)y;
    (void)z;
}

/// @brief Stub for `Particles3D.SetDirection` — set the dominant
///        emission direction `(dx, dy, dz)` and initial speed `s`.
///        Particles inherit this velocity at spawn time.
///
/// Silent no-op stub.
///
/// @param o  Particles3D handle (ignored).
/// @param dx Direction x (ignored).
/// @param dy Direction y (ignored).
/// @param dz Direction z (ignored).
/// @param s  Initial speed in world units / second (ignored).
void rt_particles3d_set_direction(void *o, double dx, double dy, double dz, double s) {
    (void)o;
    (void)dx;
    (void)dy;
    (void)dz;
    (void)s;
}

/// @brief Stub for `Particles3D.SetSpeed` — randomized initial speed
///        range. Each particle is spawned with a speed uniformly sampled
///        from `[mn, mx]` along its emission direction.
///
/// Silent no-op stub.
///
/// @param o  Particles3D handle (ignored).
/// @param mn Minimum speed in world units / second (ignored).
/// @param mx Maximum speed in world units / second (ignored).
void rt_particles3d_set_speed(void *o, double mn, double mx) {
    (void)o;
    (void)mn;
    (void)mx;
}

/// @brief Stub for `Particles3D.SetLifetime` — randomized particle
///        lifetime range. Each particle dies when its age exceeds a
///        value uniformly sampled from `[mn, mx]` at spawn time.
///
/// Silent no-op stub.
///
/// @param o  Particles3D handle (ignored).
/// @param mn Minimum lifetime in seconds (ignored).
/// @param mx Maximum lifetime in seconds (ignored).
void rt_particles3d_set_lifetime(void *o, double mn, double mx) {
    (void)o;
    (void)mn;
    (void)mx;
}

/// @brief Stub for `Particles3D.SetSize` — particle size animation
///        envelope: linearly interpolate from start size `s` (at spawn)
///        to end size `e` (at end of lifetime).
///
/// Silent no-op stub.
///
/// @param o Particles3D handle (ignored).
/// @param s Start size in world units (ignored).
/// @param e End size in world units (ignored).
void rt_particles3d_set_size(void *o, double s, double e) {
    (void)o;
    (void)s;
    (void)e;
}

/// @brief Stub for `Particles3D.SetGravity` — per-system gravity vector
///        applied each tick to integrate particle velocity. Useful for
///        smoke/embers/sparks. Independent from the Physics3D world's
///        global gravity.
///
/// Silent no-op stub.
///
/// @param o  Particles3D handle (ignored).
/// @param gx Gravity x in world units / second² (ignored).
/// @param gy Gravity y (ignored).
/// @param gz Gravity z (ignored).
void rt_particles3d_set_gravity(void *o, double gx, double gy, double gz) {
    (void)o;
    (void)gx;
    (void)gy;
    (void)gz;
}

/// @brief Stub for `Particles3D.SetColor` — particle color animation
///        envelope: linearly interpolate from start color `sc` to end
///        color `ec` over each particle's lifetime.
///
/// Silent no-op stub.
///
/// @param o  Particles3D handle (ignored).
/// @param sc Start color, packed 0xAARRGGBB (ignored).
/// @param ec End color, packed 0xAARRGGBB (ignored).
void rt_particles3d_set_color(void *o, int64_t sc, int64_t ec) {
    (void)o;
    (void)sc;
    (void)ec;
}

/// @brief Stub for `Particles3D.SetAlpha` — particle alpha animation
///        envelope: linearly interpolate from start alpha `sa` to end
///        alpha `ea`. Common pattern: `(1.0, 0.0)` for fade-out.
///
/// Silent no-op stub.
///
/// @param o  Particles3D handle (ignored).
/// @param sa Start alpha, 0..1 (ignored).
/// @param ea End alpha, 0..1 (ignored).
void rt_particles3d_set_alpha(void *o, double sa, double ea) {
    (void)o;
    (void)sa;
    (void)ea;
}

/// @brief Stub for `Particles3D.SetRate` — emission rate in particles
///        per second when the system is in continuous mode.
///
/// Silent no-op stub.
///
/// @param o Particles3D handle (ignored).
/// @param r Particles per second (ignored).
void rt_particles3d_set_rate(void *o, double r) {
    (void)o;
    (void)r;
}

/// @brief Stub for `Particles3D.SetAdditive` — when enabled, particles
///        composite using additive blending (good for fire/glow effects)
///        instead of source-over alpha.
///
/// Silent no-op stub.
///
/// @param o Particles3D handle (ignored).
/// @param a Non-zero to enable additive blending (ignored).
void rt_particles3d_set_additive(void *o, int8_t a) {
    (void)o;
    (void)a;
}

/// @brief Stub for `Particles3D.SetTexture` — bind a Pixels surface as
///        the per-particle billboard texture.
///
/// Silent no-op stub.
///
/// @param o Particles3D handle (ignored).
/// @param t Pixels handle, or NULL for untextured (ignored).
void rt_particles3d_set_texture(void *o, void *t) {
    (void)o;
    (void)t;
}

/// @brief Stub for `Particles3D.SetEmitterShape` — selects how new
///        particle positions are sampled: 0=Point, 1=Box, 2=Sphere,
///        3=Cone (axis-aligned).
///
/// Silent no-op stub. Combine with `SetEmitterSize` to control the volume.
///
/// @param o Particles3D handle (ignored).
/// @param s Emitter shape index (ignored).
void rt_particles3d_set_emitter_shape(void *o, int64_t s) {
    (void)o;
    (void)s;
}

/// @brief Stub for `Particles3D.SetEmitterSize` — full extents of the
///        emitter volume along each axis. Interpretation depends on the
///        active emitter shape.
///
/// Silent no-op stub.
///
/// @param o  Particles3D handle (ignored).
/// @param sx Extent along X (ignored).
/// @param sy Extent along Y (ignored).
/// @param sz Extent along Z (ignored).
void rt_particles3d_set_emitter_size(void *o, double sx, double sy, double sz) {
    (void)o;
    (void)sx;
    (void)sy;
    (void)sz;
}

/// @brief Stub for `Particles3D.Start` — begin continuous emission at
///        the configured rate.
///
/// Silent no-op stub.
///
/// @param o Particles3D handle (ignored).
void rt_particles3d_start(void *o) {
    (void)o;
}

/// @brief Stub for `Particles3D.Stop` — halt continuous emission.
///        Existing particles continue to live out their lifetimes.
///
/// Silent no-op stub.
///
/// @param o Particles3D handle (ignored).
void rt_particles3d_stop(void *o) {
    (void)o;
}

/// @brief Stub for `Particles3D.Burst` — emit `n` particles immediately
///        in a single tick (one-shot, independent of continuous emission).
///
/// Silent no-op stub. Used for one-off effects like explosions / hits.
///
/// @param o Particles3D handle (ignored).
/// @param n Particle count to emit (ignored).
void rt_particles3d_burst(void *o, int64_t n) {
    (void)o;
    (void)n;
}

/// @brief Stub for `Particles3D.Clear` — destroy all live particles
///        immediately, ignoring remaining lifetime.
///
/// Silent no-op stub.
///
/// @param o Particles3D handle (ignored).
void rt_particles3d_clear(void *o) {
    (void)o;
}

/// @brief Stub for `Particles3D.Update` — advance every live particle by
///        `dt` seconds: integrate motion, age out expired particles, and
///        spawn new ones from the continuous rate.
///
/// Silent no-op stub.
///
/// @param o  Particles3D handle (ignored).
/// @param dt Delta time in seconds (ignored).
void rt_particles3d_update(void *o, double dt) {
    (void)o;
    (void)dt;
}

/// @brief Stub for `Particles3D.Draw` — render all live particles as
///        camera-facing billboards using the bound texture and the
///        currently-selected blend mode.
///
/// Silent no-op stub.
///
/// @param o   Particles3D handle (ignored).
/// @param c   Canvas3D handle (ignored).
/// @param cam Camera3D handle (ignored).
void rt_particles3d_draw(void *o, void *c, void *cam) {
    (void)o;
    (void)c;
    (void)cam;
}

/// @brief Stub for `Particles3D.Count` — number of currently-live
///        particles.
///
/// Silent stub returning `0`.
///
/// @param o Particles3D handle (ignored).
///
/// @return `0`.
int64_t rt_particles3d_get_count(void *o) {
    (void)o;
    return 0;
}

/// @brief Stub for `Particles3D.IsEmitting` — true while continuous
///        emission is active (i.e. since the last `Start` and before
///        the next `Stop`).
///
/// Silent stub returning `0`.
///
/// @param o Particles3D handle (ignored).
///
/// @return `0`.
int8_t rt_particles3d_get_emitting(void *o) {
    (void)o;
    return 0;
}

/* PostFX3D stubs */

/// @brief Stub for `PostFX3D.New` — would normally create an empty
///        post-processing chain that can be attached to a Canvas3D's
///        offscreen render path.
///
/// Trapping stub: a NULL chain would crash on the first effect-add call.
///
/// @return Never returns normally.
void *rt_postfx3d_new(void) {
    rt_graphics_unavailable_("PostFX3D.New: graphics support not compiled in");
    return NULL;
}

/// @brief Stub for `PostFX3D.AddBloom` — append a bloom (high-pass
///        threshold + Gaussian blur + composite) pass to the chain.
///
/// Silent no-op stub.
///
/// @param o PostFX3D handle (ignored).
/// @param t Brightness threshold (luminance above which pixels bloom) (ignored).
/// @param i Bloom intensity multiplier (ignored).
/// @param b Number of Gaussian blur iterations (ignored).
void rt_postfx3d_add_bloom(void *o, double t, double i, int64_t b) {
    (void)o;
    (void)t;
    (void)i;
    (void)b;
}

/// @brief Stub for `PostFX3D.AddTonemap` — append an HDR-to-LDR tonemap
///        pass: 0=Reinhard, 1=ACES filmic.
///
/// Silent no-op stub. Required to flatten HDR scene output for display
/// on standard SDR monitors.
///
/// @param o PostFX3D handle (ignored).
/// @param m Tonemap operator index (ignored).
/// @param e Exposure multiplier applied before mapping (ignored).
void rt_postfx3d_add_tonemap(void *o, int64_t m, double e) {
    (void)o;
    (void)m;
    (void)e;
}

/// @brief Stub for `PostFX3D.AddFXAA` — append a Fast Approximate
///        Anti-Aliasing pass (cheap edge smoothing without MSAA).
///
/// Silent no-op stub.
///
/// @param o PostFX3D handle (ignored).
void rt_postfx3d_add_fxaa(void *o) {
    (void)o;
}

/// @brief Stub for `PostFX3D.AddColorGrade` — append brightness /
///        contrast / saturation color grading.
///
/// Silent no-op stub.
///
/// @param o PostFX3D handle (ignored).
/// @param b Brightness adjustment (ignored).
/// @param c Contrast adjustment (ignored).
/// @param s Saturation adjustment (ignored).
void rt_postfx3d_add_color_grade(void *o, double b, double c, double s) {
    (void)o;
    (void)b;
    (void)c;
    (void)s;
}

/// @brief Stub for `PostFX3D.AddVignette` — append a circular vignette
///        darkening pass.
///
/// Silent no-op stub. Useful for cinematic / dramatic framing.
///
/// @param o PostFX3D handle (ignored).
/// @param r Vignette inner radius (where darkening begins) (ignored).
/// @param s Falloff strength (ignored).
void rt_postfx3d_add_vignette(void *o, double r, double s) {
    (void)o;
    (void)r;
    (void)s;
}

/// @brief Stub for `PostFX3D.SetEnabled` — globally enable or disable
///        the post-processing chain. When disabled the scene renders
///        directly to the swap-chain framebuffer.
///
/// Silent no-op stub.
///
/// @param o PostFX3D handle (ignored).
/// @param e Non-zero to enable post-processing (ignored).
void rt_postfx3d_set_enabled(void *o, int8_t e) {
    (void)o;
    (void)e;
}

/// @brief Get the enabled of the postfx3d.
int8_t rt_postfx3d_get_enabled(void *o) {
    (void)o;
    return 0;
}

/// @brief Remove all entries from the postfx3d.
void rt_postfx3d_clear(void *o) {
    (void)o;
}

/// @brief Return the count of elements in the postfx3d.
int64_t rt_postfx3d_get_effect_count(void *o) {
    (void)o;
    return 0;
}

/// @brief Set the post fx of the canvas3d.
void rt_canvas3d_set_post_fx(void *c, void *fx) {
    (void)c;
    (void)fx;
}

/// @brief Apply the to canvas of the postfx3d.
void rt_postfx3d_apply_to_canvas(void *c) {
    (void)c;
}

/* Ray3D / AABB3D / RayHit3D stubs */

/// @brief Stub for `Ray3D.IntersectTriangle` — Möller-Trumbore ray-vs-
///        single-triangle intersection. Returns the parametric distance `t`
///        along the ray to the hit, or `-1` for no hit / behind origin.
///
/// Silent stub returning `-1.0`.
///
/// @param o  Vec3 ray origin (ignored).
/// @param d  Vec3 ray direction (must be normalized) (ignored).
/// @param v0 Vec3 triangle vertex 0 (ignored).
/// @param v1 Vec3 triangle vertex 1 (ignored).
/// @param v2 Vec3 triangle vertex 2 (ignored).
///
/// @return `-1.0`.
double rt_ray3d_intersect_triangle(void *o, void *d, void *v0, void *v1, void *v2) {
    (void)o;
    (void)d;
    (void)v0;
    (void)v1;
    (void)v2;
    return -1.0;
}

/// @brief Stub for `Ray3D.IntersectMesh` — would normally test a ray
///        against every triangle of `m` (after applying transform `t`)
///        and return the closest hit as a RayHit3D, or NULL for no hit.
///
/// Silent stub returning NULL.
///
/// @param o Vec3 ray origin (ignored).
/// @param d Vec3 ray direction (ignored).
/// @param m Mesh3D handle (ignored).
/// @param t Transform3D handle (ignored).
///
/// @return `NULL`.
void *rt_ray3d_intersect_mesh(void *o, void *d, void *m, void *t) {
    (void)o;
    (void)d;
    (void)m;
    (void)t;
    return NULL;
}

/// @brief Stub for `Ray3D.IntersectAABB` — slab-method ray-vs-AABB
///        intersection. Returns the parametric distance to the entry
///        face, or `-1` for no hit.
///
/// Silent stub returning `-1.0`.
///
/// @param o  Vec3 ray origin (ignored).
/// @param d  Vec3 ray direction (ignored).
/// @param mn Vec3 AABB min corner (ignored).
/// @param mx Vec3 AABB max corner (ignored).
///
/// @return `-1.0`.
double rt_ray3d_intersect_aabb(void *o, void *d, void *mn, void *mx) {
    (void)o;
    (void)d;
    (void)mn;
    (void)mx;
    return -1.0;
}

/// @brief Stub for `Ray3D.IntersectSphere` — analytic ray-vs-sphere
///        intersection (quadratic discriminant). Returns the parametric
///        distance to the entry point, or `-1` for no hit.
///
/// Silent stub returning `-1.0`.
///
/// @param o Vec3 ray origin (ignored).
/// @param d Vec3 ray direction (ignored).
/// @param c Vec3 sphere center (ignored).
/// @param r Sphere radius (ignored).
///
/// @return `-1.0`.
double rt_ray3d_intersect_sphere(void *o, void *d, void *c, double r) {
    (void)o;
    (void)d;
    (void)c;
    (void)r;
    return -1.0;
}

/// @brief Stub for `AABB3D.Overlaps` — boolean AABB-vs-AABB overlap
///        test. Each AABB is given as `(min, max)` Vec3 pairs.
///
/// Silent stub returning `0` (no overlap).
///
/// @param a0 Vec3 AABB A min corner (ignored).
/// @param a1 Vec3 AABB A max corner (ignored).
/// @param b0 Vec3 AABB B min corner (ignored).
/// @param b1 Vec3 AABB B max corner (ignored).
///
/// @return `0`.
int8_t rt_aabb3d_overlaps(void *a0, void *a1, void *b0, void *b1) {
    (void)a0;
    (void)a1;
    (void)b0;
    (void)b1;
    return 0;
}

/// @brief Stub for `AABB3D.Penetration` — would normally return a Vec3
///        representing the minimum-translation vector to separate the
///        two AABBs along the axis of least overlap.
///
/// Silent stub returning NULL.
///
/// @param a0 Vec3 AABB A min corner (ignored).
/// @param a1 Vec3 AABB A max corner (ignored).
/// @param b0 Vec3 AABB B min corner (ignored).
/// @param b1 Vec3 AABB B max corner (ignored).
///
/// @return `NULL`.
void *rt_aabb3d_penetration(void *a0, void *a1, void *b0, void *b1) {
    (void)a0;
    (void)a1;
    (void)b0;
    (void)b1;
    return NULL;
}

/// @brief Stub for `Ray3D.HitDistance` — get the parametric distance
///        stored in a RayHit3D record (`-1` for no hit).
///
/// Silent stub returning `-1.0`.
///
/// @param h RayHit3D handle (ignored).
///
/// @return `-1.0`.
double rt_ray3d_hit_distance(void *h) {
    (void)h;
    return -1.0;
}

/// @brief Stub for `Ray3D.HitPoint` — get the world-space hit point
///        stored in a RayHit3D record as a Vec3.
///
/// Silent stub returning NULL.
///
/// @param h RayHit3D handle (ignored).
///
/// @return `NULL`.
void *rt_ray3d_hit_point(void *h) {
    (void)h;
    return NULL;
}

/// @brief Stub for `Ray3D.HitNormal` — get the surface normal at the
///        hit point as a Vec3 (always points back along the ray).
///
/// Silent stub returning NULL.
///
/// @param h RayHit3D handle (ignored).
///
/// @return `NULL`.
void *rt_ray3d_hit_normal(void *h) {
    (void)h;
    return NULL;
}

/// @brief Stub for `Ray3D.HitTriangle` — get the triangle index of a
///        ray-vs-mesh hit record (`-1` if no hit / opaque hit type).
///
/// Silent stub returning `-1` (no hit).
///
/// @param h Hit record handle (ignored).
///
/// @return `-1`.
int64_t rt_ray3d_hit_triangle(void *h) {
    (void)h;
    return -1;
}

/// @brief Stub for `Sphere3D.Overlaps` — boolean sphere-vs-sphere overlap
///        test (centers `a` and `b`, radii `ra` and `rb`).
///
/// Silent stub returning `0` (no overlap). The real implementation does a
/// distance-vs-sum-of-radii comparison.
///
/// @param a  Vec3 center of sphere A (ignored).
/// @param ra Radius of sphere A (ignored).
/// @param b  Vec3 center of sphere B (ignored).
/// @param rb Radius of sphere B (ignored).
///
/// @return `0`.
int8_t rt_sphere3d_overlaps(void *a, double ra, void *b, double rb) {
    (void)a;
    (void)ra;
    (void)b;
    (void)rb;
    return 0;
}

/// @brief Stub for `Sphere3D.Penetration` — would normally return a Vec3
///        representing the minimum-translation vector to separate the two
///        spheres (zero magnitude when not overlapping).
///
/// Silent stub returning NULL.
///
/// @param a  Vec3 center of sphere A (ignored).
/// @param ra Radius of sphere A (ignored).
/// @param b  Vec3 center of sphere B (ignored).
/// @param rb Radius of sphere B (ignored).
///
/// @return `NULL`.
void *rt_sphere3d_penetration(void *a, double ra, void *b, double rb) {
    (void)a;
    (void)ra;
    (void)b;
    (void)rb;
    return NULL;
}

/// @brief Stub for `AABB3D.ClosestPoint` — would normally return the
///        point on the AABB surface closest to `p`. Used by sphere-vs-AABB
///        narrow-phase collision.
///
/// Silent stub returning NULL.
///
/// @param mn Vec3 AABB min corner (ignored).
/// @param mx Vec3 AABB max corner (ignored).
/// @param p  Vec3 query point (ignored).
///
/// @return `NULL`.
void *rt_aabb3d_closest_point(void *mn, void *mx, void *p) {
    (void)mn;
    (void)mx;
    (void)p;
    return NULL;
}

/// @brief Stub for `AABB3D.SphereOverlaps` — boolean AABB-vs-sphere
///        overlap test.
///
/// Silent stub returning `0` (no overlap).
///
/// @param mn Vec3 AABB min corner (ignored).
/// @param mx Vec3 AABB max corner (ignored).
/// @param c  Vec3 sphere center (ignored).
/// @param r  Sphere radius (ignored).
///
/// @return `0`.
int8_t rt_aabb3d_sphere_overlaps(void *mn, void *mx, void *c, double r) {
    (void)mn;
    (void)mx;
    (void)c;
    (void)r;
    return 0;
}

/// @brief Stub for `Segment3D.ClosestPoint` — would normally return the
///        point on the segment `[a, b]` closest to `p`. Used by capsule
///        narrow-phase collision.
///
/// Silent stub returning NULL.
///
/// @param a Vec3 segment start (ignored).
/// @param b Vec3 segment end (ignored).
/// @param p Vec3 query point (ignored).
///
/// @return `NULL`.
void *rt_segment3d_closest_point(void *a, void *b, void *p) {
    (void)a;
    (void)b;
    (void)p;
    return NULL;
}

/// @brief Stub for `Capsule3D.SphereOverlaps` — boolean capsule-vs-sphere
///        overlap test. Capsule is the swept volume of a sphere of radius
///        `cr` from `a` to `b`.
///
/// Silent stub returning `0`.
///
/// @param a  Vec3 capsule axis start (ignored).
/// @param b  Vec3 capsule axis end (ignored).
/// @param cr Capsule radius (ignored).
/// @param c  Vec3 sphere center (ignored).
/// @param sr Sphere radius (ignored).
///
/// @return `0`.
int8_t rt_capsule3d_sphere_overlaps(void *a, void *b, double cr, void *c, double sr) {
    (void)a;
    (void)b;
    (void)cr;
    (void)c;
    (void)sr;
    return 0;
}

/// @brief Stub for `Capsule3D.AABBOverlaps` — boolean capsule-vs-AABB
///        overlap test.
///
/// Silent stub returning `0`.
///
/// @param a  Vec3 capsule axis start (ignored).
/// @param b  Vec3 capsule axis end (ignored).
/// @param r  Capsule radius (ignored).
/// @param mn Vec3 AABB min corner (ignored).
/// @param mx Vec3 AABB max corner (ignored).
///
/// @return `0`.
int8_t rt_capsule3d_aabb_overlaps(void *a, void *b, double r, void *mn, void *mx) {
    (void)a;
    (void)b;
    (void)r;
    (void)mn;
    (void)mx;
    return 0;
}

/* FPS Camera stubs */

/// @brief Stub for `Camera3D.FPSInit` — initialize an FPS-style camera
///        controller (yaw/pitch tracked separately, no roll).
///
/// Silent no-op stub.
///
/// @param c Camera3D handle (ignored).
void rt_camera3d_fps_init(void *c) {
    (void)c;
}

/// @brief Stub for `Camera3D.FPSUpdate` — advance the FPS camera by one
///        tick. Updates yaw/pitch from mouse delta and translates along
///        the local axes from WASD input.
///
/// Silent no-op stub. Parameter shape:
///   `(camera, mouseDx, mouseDy, dt, fwd, back, left, right)`
///
/// @param c Camera3D handle (ignored).
/// @param a Mouse delta x (ignored).
/// @param b Mouse delta y (ignored).
/// @param d Delta time in seconds (ignored).
/// @param e Forward input axis (ignored).
/// @param f Back input axis (ignored).
/// @param g Left input axis (ignored).
/// @param h Right input axis (ignored).
void rt_camera3d_fps_update(
    void *c, double a, double b, double d, double e, double f, double g, double h) {
    (void)c;
    (void)a;
    (void)b;
    (void)d;
    (void)e;
    (void)f;
    (void)g;
    (void)h;
}

/// @brief Stub for `Camera3D.Yaw` — get the FPS camera's yaw (horizontal
///        rotation, in radians).
///
/// Silent stub returning `0.0`.
///
/// @param c Camera3D handle (ignored).
///
/// @return `0.0`.
double rt_camera3d_get_yaw(void *c) {
    (void)c;
    return 0.0;
}

/// @brief Stub for `Camera3D.Pitch` — get the FPS camera's pitch
///        (vertical rotation, in radians; positive looks up).
///
/// Silent stub returning `0.0`.
///
/// @param c Camera3D handle (ignored).
///
/// @return `0.0`.
double rt_camera3d_get_pitch(void *c) {
    (void)c;
    return 0.0;
}

/// @brief Stub for `Camera3D.SetYaw` — set the FPS camera's yaw rotation.
///
/// Silent no-op stub.
///
/// @param c Camera3D handle (ignored).
/// @param v Yaw in radians (ignored).
void rt_camera3d_set_yaw(void *c, double v) {
    (void)c;
    (void)v;
}

/// @brief Stub for `Camera3D.SetPitch` — set the FPS camera's pitch
///        rotation. The real implementation clamps to `[-π/2 + ε, π/2 - ε]`
///        to prevent gimbal lock.
///
/// Silent no-op stub.
///
/// @param c Camera3D handle (ignored).
/// @param v Pitch in radians (ignored).
void rt_camera3d_set_pitch(void *c, double v) {
    (void)c;
    (void)v;
}

/* HUD overlay stubs */

/// @brief Stub for `Canvas3D.DrawRect2D` — would normally draw a 2D
///        screen-space rectangle as a HUD overlay (after the 3D scene
///        renders). Coordinates are in screen pixels.
///
/// Silent no-op stub.
///
/// @param c  Canvas3D handle (ignored).
/// @param x  Top-left x in screen pixels (ignored).
/// @param y  Top-left y in screen pixels (ignored).
/// @param w  Width in pixels (ignored).
/// @param h  Height in pixels (ignored).
/// @param cl Packed 0xAARRGGBB color (ignored).
void rt_canvas3d_draw_rect2d(void *c, int64_t x, int64_t y, int64_t w, int64_t h, int64_t cl) {
    (void)c;
    (void)x;
    (void)y;
    (void)w;
    (void)h;
    (void)cl;
}

/// @brief Stub for `Canvas3D.DrawCrosshair` — would normally draw a
///        small crosshair at the center of the screen (FPS reticle).
///
/// Silent no-op stub.
///
/// @param c  Canvas3D handle (ignored).
/// @param cl Packed 0xAARRGGBB color (ignored).
/// @param sz Crosshair arm length in pixels (ignored).
void rt_canvas3d_draw_crosshair(void *c, int64_t cl, int64_t sz) {
    (void)c;
    (void)cl;
    (void)sz;
}

/// @brief Stub for `Canvas3D.DrawText2D` — would normally draw 8x8 bitmap
///        text in screen space as a HUD overlay.
///
/// Silent no-op stub.
///
/// @param c  Canvas3D handle (ignored).
/// @param x  Top-left x in screen pixels (ignored).
/// @param y  Top-left y in screen pixels (ignored).
/// @param t  Text string (ignored).
/// @param cl Packed 0xAARRGGBB color (ignored).
void rt_canvas3d_draw_text2d(void *c, int64_t x, int64_t y, rt_string t, int64_t cl) {
    (void)c;
    (void)x;
    (void)y;
    (void)t;
    (void)cl;
}

/* Debug gizmo stubs */

/// @brief Stub for `Canvas3D.DrawAABBWire` — would normally draw the
///        12 edges of an axis-aligned bounding box in world space (debug
///        visualization for collider/cull bounds).
///
/// Silent no-op stub.
///
/// @param c  Canvas3D handle (ignored).
/// @param mn Vec3 AABB min corner (ignored).
/// @param mx Vec3 AABB max corner (ignored).
/// @param cl Packed 0xAARRGGBB color (ignored).
void rt_canvas3d_draw_aabb_wire(void *c, void *mn, void *mx, int64_t cl) {
    (void)c;
    (void)mn;
    (void)mx;
    (void)cl;
}

/// @brief Stub for `Canvas3D.DrawSphereWire` — would normally draw a
///        wireframe sphere (3 great-circle rings) at world position `ctr`
///        with radius `r`.
///
/// Silent no-op stub.
///
/// @param c   Canvas3D handle (ignored).
/// @param ctr Vec3 sphere center (ignored).
/// @param r   Sphere radius (ignored).
/// @param cl  Packed 0xAARRGGBB color (ignored).
void rt_canvas3d_draw_sphere_wire(void *c, void *ctr, double r, int64_t cl) {
    (void)c;
    (void)ctr;
    (void)r;
    (void)cl;
}

/// @brief Stub for `Canvas3D.DrawDebugRay` — would normally draw a
///        finite ray starting at world position `o` in direction `d` with
///        length `l` (debug visualization for raycasts).
///
/// Silent no-op stub.
///
/// @param c  Canvas3D handle (ignored).
/// @param o  Vec3 ray origin (ignored).
/// @param d  Vec3 ray direction (ignored).
/// @param l  Ray length in world units (ignored).
/// @param cl Packed 0xAARRGGBB color (ignored).
void rt_canvas3d_draw_debug_ray(void *c, void *o, void *d, double l, int64_t cl) {
    (void)c;
    (void)o;
    (void)d;
    (void)l;
    (void)cl;
}

/// @brief Stub for `Canvas3D.DrawAxis` — would normally draw the world-
///        axes gizmo at world position `o` with length `s` (red=X, green=Y,
///        blue=Z).
///
/// Silent no-op stub.
///
/// @param c Canvas3D handle (ignored).
/// @param o Vec3 origin (ignored).
/// @param s Axis arm length in world units (ignored).
void rt_canvas3d_draw_axis(void *c, void *o, double s) {
    (void)c;
    (void)o;
    (void)s;
}

/* Fog stubs */

/// @brief Stub for `Canvas3D.SetFog` — enable linear distance fog
///        between near distance `n` and far distance `f`. Pixels beyond
///        `f` are tinted entirely with `(r, g, b)`; pixels at `n` are
///        unaffected; in between is linearly interpolated.
///
/// Silent no-op stub.
///
/// @param c Canvas3D handle (ignored).
/// @param n Fog start distance (ignored).
/// @param f Fog end distance (ignored).
/// @param r Fog tint red, 0..1 (ignored).
/// @param g Fog tint green, 0..1 (ignored).
/// @param b Fog tint blue, 0..1 (ignored).
void rt_canvas3d_set_fog(void *c, double n, double f, double r, double g, double b) {
    (void)c;
    (void)n;
    (void)f;
    (void)r;
    (void)g;
    (void)b;
}

/// @brief Stub for `Canvas3D.ClearFog` — disable linear distance fog
///        for subsequent draws.
///
/// Silent no-op stub.
///
/// @param c Canvas3D handle (ignored).
void rt_canvas3d_clear_fog(void *c) {
    (void)c;
}

/* Shadow stubs */

/// @brief Stub for `Canvas3D.EnableShadows` — enable shadow mapping at
///        the given resolution `(r x r)` for the shadow framebuffer.
///        Higher values give crisper shadows but cost more memory and
///        fillrate.
///
/// Silent no-op stub. Real implementation runs a depth-only pass per
/// shadow-casting light and samples the depth map during the lit pass
/// with PCF filtering.
///
/// @param c Canvas3D handle (ignored).
/// @param r Shadow map resolution per side in pixels (ignored).
void rt_canvas3d_enable_shadows(void *c, int64_t r) {
    (void)c;
    (void)r;
}

/// @brief Stub for `Canvas3D.DisableShadows` — turn off shadow mapping
///        entirely. Cheaper but loses contact shadows / depth cues.
///
/// Silent no-op stub.
///
/// @param c Canvas3D handle (ignored).
void rt_canvas3d_disable_shadows(void *c) {
    (void)c;
}

/// @brief Stub for `Canvas3D.SetShadowBias` — depth-bias offset added
///        to shadow-map samples to prevent self-shadow acne. Higher values
///        reduce acne but introduce "Peter Panning" (shadow detached from
///        the caster).
///
/// Silent no-op stub.
///
/// @param c Canvas3D handle (ignored).
/// @param b Shadow bias depth offset (ignored).
void rt_canvas3d_set_shadow_bias(void *c, double b) {
    (void)c;
    (void)b;
}

/* Audio3D stubs */

/// @brief Stub for `Audio3D.SetListener` — set the active listener and
///        its forward orientation. Convenience wrapper around the
///        AudioListener3D class for callers that don't need full per-
///        listener state.
///
/// Silent no-op stub.
///
/// @param p Vec3 listener position (ignored).
/// @param f Vec3 listener forward direction (ignored).
void rt_audio3d_set_listener(void *p, void *f) {
    (void)p;
    (void)f;
}

/// @brief Stub for `Audio3D.PlayAt` — would normally play sound `s` at
///        world-space position `p` with maximum-distance attenuation
///        cutoff `d` and base volume `v`. Returns the assigned voice id,
///        or `0` on failure.
///
/// Silent stub returning `0`.
///
/// @param s Sound handle (ignored).
/// @param p Vec3 spawn position (ignored).
/// @param d Maximum audible distance in world units (ignored).
/// @param v Volume 0..100 (ignored).
///
/// @return `0`.
int64_t rt_audio3d_play_at(void *s, void *p, double d, int64_t v) {
    (void)s;
    (void)p;
    (void)d;
    (void)v;
    return 0;
}

/// @brief Stub for `Audio3D.UpdateVoice` — update the world-space
///        position and max-distance of an already-playing voice (use to
///        track moving emitters that aren't bound to a SceneNode3D).
///
/// Silent no-op stub.
///
/// @param v  Voice id from `PlayAt` (ignored).
/// @param p  Vec3 new position (ignored).
/// @param md New max audible distance (ignored).
void rt_audio3d_update_voice(int64_t v, void *p, double md) {
    (void)v;
    (void)p;
    (void)md;
}

/// @brief Stub for `Audio3D.SyncBindings` — batch-update spatial
///        positions for every AudioListener3D and AudioSource3D bound to
///        a SceneNode3D or Camera3D. Should be called once per frame
///        before any draw calls so audio reflects the same world state.
///
/// Silent no-op stub.
///
/// @param dt Delta time in seconds (ignored).
void rt_audio3d_sync_bindings(double dt) {
    (void)dt;
}

/// @brief Stub for `AudioListener3D.New` — would normally create a
///        spatial-audio listener (the "ear" the mixer applies pan and
///        distance attenuation against). Typically one per scene.
///
/// Silent stub returning NULL.
///
/// @return `NULL`.
void *rt_audiolistener3d_new(void) {
    return NULL;
}

/// @brief Stub for `AudioListener3D.Position` — get the listener's
///        current world-space position as a Vec3.
///
/// Silent stub returning NULL.
///
/// @param l AudioListener3D handle (ignored).
///
/// @return `NULL`.
void *rt_audiolistener3d_get_position(void *l) {
    (void)l;
    return NULL;
}

/// @brief Stub for `AudioListener3D.SetPosition` — set the listener's
///        world-space position from a Vec3 handle.
///
/// Silent no-op stub.
///
/// @param l AudioListener3D handle (ignored).
/// @param p Vec3 position handle (ignored).
void rt_audiolistener3d_set_position(void *l, void *p) {
    (void)l;
    (void)p;
}

/// @brief Stub for `AudioListener3D.SetPositionXYZ` — set the listener's
///        world-space position from raw doubles. Convenience overload.
///
/// Silent no-op stub.
///
/// @param l AudioListener3D handle (ignored).
/// @param x World-space x (ignored).
/// @param y World-space y (ignored).
/// @param z World-space z (ignored).
void rt_audiolistener3d_set_position_vec(void *l, double x, double y, double z) {
    (void)l;
    (void)x;
    (void)y;
    (void)z;
}

/// @brief Stub for `AudioListener3D.Forward` — get the listener's
///        forward-facing direction vector (used for stereo panning math).
///
/// Silent stub returning NULL.
///
/// @param l AudioListener3D handle (ignored).
///
/// @return `NULL`.
void *rt_audiolistener3d_get_forward(void *l) {
    (void)l;
    return NULL;
}

/// @brief Stub for `AudioListener3D.SetForward` — set the listener's
///        forward direction (must be normalized).
///
/// Silent no-op stub.
///
/// @param l AudioListener3D handle (ignored).
/// @param f Vec3 forward direction (ignored).
void rt_audiolistener3d_set_forward(void *l, void *f) {
    (void)l;
    (void)f;
}

/// @brief Stub for `AudioListener3D.Velocity` — get the listener's
///        velocity vector (used by the mixer for Doppler-effect math).
///
/// Silent stub returning NULL.
///
/// @param l AudioListener3D handle (ignored).
///
/// @return `NULL`.
void *rt_audiolistener3d_get_velocity(void *l) {
    (void)l;
    return NULL;
}

/// @brief Stub for `AudioListener3D.SetVelocity` — set the listener's
///        velocity vector for Doppler computation.
///
/// Silent no-op stub.
///
/// @param l AudioListener3D handle (ignored).
/// @param v Vec3 velocity handle (ignored).
void rt_audiolistener3d_set_velocity(void *l, void *v) {
    (void)l;
    (void)v;
}

/// @brief Stub for `AudioListener3D.IsActive` — true when this listener
///        is the currently-selected listener for the audio mixer
///        (multiple listeners may exist; only one is active at a time).
///
/// Silent stub returning `0`.
///
/// @param l AudioListener3D handle (ignored).
///
/// @return `0`.
int8_t rt_audiolistener3d_get_is_active(void *l) {
    (void)l;
    return 0;
}

/// @brief Stub for `AudioListener3D.SetActive` — designate this
///        listener as the active one (deactivates any previously-active
///        listener).
///
/// Silent no-op stub.
///
/// @param l AudioListener3D handle (ignored).
/// @param a Non-zero to make this listener active (ignored).
void rt_audiolistener3d_set_is_active(void *l, int8_t a) {
    (void)l;
    (void)a;
}

/// @brief Stub for `AudioListener3D.BindNode` — attach a SceneNode3D so
///        the listener's position/orientation follow the node each frame.
///        Use `BindCamera` for the typical first-person setup.
///
/// Silent no-op stub.
///
/// @param l AudioListener3D handle (ignored).
/// @param n SceneNode3D handle, or NULL to detach (ignored).
void rt_audiolistener3d_bind_node(void *l, void *n) {
    (void)l;
    (void)n;
}

/// @brief Stub for `AudioListener3D.ClearNodeBinding` — detach the
///        SceneNode3D currently driving the listener's spatial position.
///
/// Silent no-op stub.
///
/// @param l AudioListener3D handle (ignored).
void rt_audiolistener3d_clear_node_binding(void *l) {
    (void)l;
}

/// @brief Stub for `AudioListener3D.BindCamera` — attach a Camera3D so
///        the listener's position and forward vector follow the camera
///        each frame (typical first-person audio setup).
///
/// Silent no-op stub.
///
/// @param l AudioListener3D handle (ignored).
/// @param c Camera3D handle, or NULL to detach (ignored).
void rt_audiolistener3d_bind_camera(void *l, void *c) {
    (void)l;
    (void)c;
}

/// @brief Stub for `AudioListener3D.ClearCameraBinding` — detach the
///        Camera3D currently driving the listener.
///
/// Silent no-op stub.
///
/// @param l AudioListener3D handle (ignored).
void rt_audiolistener3d_clear_camera_binding(void *l) {
    (void)l;
}

/// @brief Stub for `AudioSource3D.New` — would normally create a 3D
///        positional emitter for the given Sound resource.
///
/// Silent stub returning NULL.
///
/// @param s Sound handle (ignored).
///
/// @return `NULL`.
void *rt_audiosource3d_new(void *s) {
    (void)s;
    return NULL;
}

/// @brief Stub for `AudioSource3D.Position` — get the source's current
///        world-space position as a Vec3.
///
/// Silent stub returning NULL.
///
/// @param s AudioSource3D handle (ignored).
///
/// @return `NULL`.
void *rt_audiosource3d_get_position(void *s) {
    (void)s;
    return NULL;
}

/// @brief Stub for `AudioSource3D.SetPosition` — set the source's
///        world-space position from a Vec3 handle.
///
/// Silent no-op stub.
///
/// @param s AudioSource3D handle (ignored).
/// @param p Vec3 position handle (ignored).
void rt_audiosource3d_set_position(void *s, void *p) {
    (void)s;
    (void)p;
}

/// @brief Stub for `AudioSource3D.SetPositionXYZ` — set the source's
///        world-space position from raw doubles. Convenience overload.
///
/// Silent no-op stub.
///
/// @param s AudioSource3D handle (ignored).
/// @param x World-space x (ignored).
/// @param y World-space y (ignored).
/// @param z World-space z (ignored).
void rt_audiosource3d_set_position_vec(void *s, double x, double y, double z) {
    (void)s;
    (void)x;
    (void)y;
    (void)z;
}

/// @brief Stub for `AudioSource3D.Velocity` — get the source's current
///        velocity vector. Used by the mixer for Doppler-effect calculations.
///
/// Silent stub returning NULL.
///
/// @param s AudioSource3D handle (ignored).
///
/// @return `NULL`.
void *rt_audiosource3d_get_velocity(void *s) {
    (void)s;
    return NULL;
}

/// @brief Stub for `AudioSource3D.SetVelocity` — set the source's
///        velocity vector for Doppler computation.
///
/// Silent no-op stub.
///
/// @param s AudioSource3D handle (ignored).
/// @param v Vec3 velocity handle (ignored).
void rt_audiosource3d_set_velocity(void *s, void *v) {
    (void)s;
    (void)v;
}

/// @brief Stub for `AudioSource3D.MaxDistance` — get the cutoff distance
///        beyond which the source is silent (inverse-distance attenuation
///        upper bound).
///
/// Silent stub returning `0.0`.
///
/// @param s AudioSource3D handle (ignored).
///
/// @return `0.0`.
double rt_audiosource3d_get_max_distance(void *s) {
    (void)s;
    return 0.0;
}

/// @brief Stub for `AudioSource3D.SetMaxDistance` — set the cutoff
///        distance for attenuation.
///
/// Silent no-op stub.
///
/// @param s AudioSource3D handle (ignored).
/// @param d Max distance in world units (ignored).
void rt_audiosource3d_set_max_distance(void *s, double d) {
    (void)s;
    (void)d;
}

/// @brief Stub for `AudioSource3D.Volume` — get the per-source gain
///        multiplier (0..100, applied after distance attenuation).
///
/// Silent stub returning `0`.
///
/// @param s AudioSource3D handle (ignored).
///
/// @return `0`.
int64_t rt_audiosource3d_get_volume(void *s) {
    (void)s;
    return 0;
}

/// @brief Stub for `AudioSource3D.SetVolume` — set the per-source gain
///        multiplier.
///
/// Silent no-op stub.
///
/// @param s AudioSource3D handle (ignored).
/// @param v Volume 0..100 (ignored).
void rt_audiosource3d_set_volume(void *s, int64_t v) {
    (void)s;
    (void)v;
}

/// @brief Stub for `AudioSource3D.Looping` — get the looping flag.
///
/// Silent stub returning `0`.
///
/// @param s AudioSource3D handle (ignored).
///
/// @return `0`.
int8_t rt_audiosource3d_get_looping(void *s) {
    (void)s;
    return 0;
}

/// @brief Stub for `AudioSource3D.SetLooping` — when enabled, the
///        underlying voice loops at the end of the sound buffer.
///
/// Silent no-op stub.
///
/// @param s AudioSource3D handle (ignored).
/// @param l Non-zero to enable looping (ignored).
void rt_audiosource3d_set_looping(void *s, int8_t l) {
    (void)s;
    (void)l;
}

/// @brief Stub for `AudioSource3D.IsPlaying` — true while the active
///        voice for this source is producing audio.
///
/// Silent stub returning `0`.
///
/// @param s AudioSource3D handle (ignored).
///
/// @return `0`.
int8_t rt_audiosource3d_get_is_playing(void *s) {
    (void)s;
    return 0;
}

/// @brief Stub for `AudioSource3D.VoiceId` — get the underlying mixer
///        voice handle (`0` if not playing). Useful for debugging.
///
/// Silent stub returning `0`.
///
/// @param s AudioSource3D handle (ignored).
///
/// @return `0`.
int64_t rt_audiosource3d_get_voice_id(void *s) {
    (void)s;
    return 0;
}

/// @brief Stub for `AudioSource3D.Play` — start playback of the bound
///        Sound at the source's current position. Returns the assigned
///        voice id, or `0` on failure.
///
/// Silent stub returning `0`.
///
/// @param s AudioSource3D handle (ignored).
///
/// @return `0`.
int64_t rt_audiosource3d_play(void *s) {
    (void)s;
    return 0;
}

/// @brief Stub for `AudioSource3D.Stop` — halt the source's current
///        voice immediately. Safe no-op when not playing.
///
/// Silent no-op stub.
///
/// @param s AudioSource3D handle (ignored).
void rt_audiosource3d_stop(void *s) {
    (void)s;
}

/// @brief Stub for `AudioSource3D.BindNode` — attach a SceneNode3D so
///        the source's position follows the node each frame
///        (`Audio3D.SyncBindings(dt)`).
///
/// Silent no-op stub.
///
/// @param s AudioSource3D handle (ignored).
/// @param n SceneNode3D handle, or NULL to detach (ignored).
void rt_audiosource3d_bind_node(void *s, void *n) {
    (void)s;
    (void)n;
}

/// @brief Stub for `AudioSource3D.ClearNodeBinding` — detach the
///        SceneNode3D currently driving the source's position.
///
/// Silent no-op stub.
///
/// @param s AudioSource3D handle (ignored).
void rt_audiosource3d_clear_node_binding(void *s) {
    (void)s;
}

/* Physics3D World stubs */

/// @brief Stub for `Physics3DWorld.New` — would normally create an
///        empty physics world with the given gravity vector. Bodies are
///        added via `Add`; the simulation is advanced one step at a time
///        via `Step`.
///
/// Trapping stub: callers expect a usable handle for body management.
///
/// @param gx Gravity x in world units / second² (ignored).
/// @param gy Gravity y; typically `-9.81` for Earth-like (ignored).
/// @param gz Gravity z (ignored).
///
/// @return Never returns normally.
void *rt_world3d_new(double gx, double gy, double gz) {
    (void)gx;
    (void)gy;
    (void)gz;
    rt_graphics_unavailable_("Physics3DWorld.New: graphics support not compiled in");
    return NULL;
}

/// @brief Step the world3d.
void rt_world3d_step(void *w, double dt) {
    (void)w;
    (void)dt;
}

/// @brief Add an element to the world3d.
void rt_world3d_add(void *w, void *b) {
    (void)w;
    (void)b;
}

/// @brief Remove an entry from the world3d.
void rt_world3d_remove(void *w, void *b) {
    (void)w;
    (void)b;
}

/// @brief Return the count of elements in the world3d.
int64_t rt_world3d_body_count(void *w) {
    (void)w;
    return 0;
}

/// @brief Set the gravity of the world3d.
void rt_world3d_set_gravity(void *w, double gx, double gy, double gz) {
    (void)w;
    (void)gx;
    (void)gy;
    (void)gz;
}

/// @brief Stub for `Physics3DWorld.AddJoint` — register a joint
///        constraint with the world's solver. `jt` distinguishes the
///        joint type (Distance / Spring / future kinds) so the world
///        can dispatch to the correct solver code.
///
/// Silent no-op stub.
///
/// @param w  Physics3DWorld handle (ignored).
/// @param j  Joint handle (ignored).
/// @param jt Joint type tag (ignored).
void rt_world3d_add_joint(void *w, void *j, int64_t jt) {
    (void)w;
    (void)j;
    (void)jt;
}

/// @brief Stub for `Physics3DWorld.RemoveJoint` — unregister a joint
///        from the world. The joint object is left intact for re-add.
///
/// Silent no-op stub.
///
/// @param w Physics3DWorld handle (ignored).
/// @param j Joint handle (ignored).
void rt_world3d_remove_joint(void *w, void *j) {
    (void)w;
    (void)j;
}

/// @brief Stub for `Physics3DWorld.JointCount` — number of joints
///        currently registered in the world.
///
/// Silent stub returning `0`.
///
/// @param w Physics3DWorld handle (ignored).
///
/// @return `0`.
int64_t rt_world3d_joint_count(void *w) {
    (void)w;
    return 0;
}

/// @brief Stub for `Physics3DWorld.CollisionCount` — number of contact
///        pairs the most recent `Step` produced. Use as a queue length
///        for iterating with the indexed accessors below.
///
/// Silent stub returning `0`.
///
/// @param w Physics3DWorld handle (ignored).
///
/// @return `0`.
int64_t rt_world3d_get_collision_count(void *w) {
    (void)w;
    return 0;
}

/// @brief Stub for `Physics3DWorld.CollisionBodyA(i)` — first body in
///        the `i`th contact pair.
///
/// Silent stub returning NULL.
///
/// @param w Physics3DWorld handle (ignored).
/// @param i Contact pair index, 0..CollisionCount-1 (ignored).
///
/// @return `NULL`.
void *rt_world3d_get_collision_body_a(void *w, int64_t i) {
    (void)w;
    (void)i;
    return NULL;
}

/// @brief Stub for `Physics3DWorld.CollisionBodyB(i)` — second body in
///        the `i`th contact pair.
///
/// Silent stub returning NULL.
///
/// @param w Physics3DWorld handle (ignored).
/// @param i Contact pair index (ignored).
///
/// @return `NULL`.
void *rt_world3d_get_collision_body_b(void *w, int64_t i) {
    (void)w;
    (void)i;
    return NULL;
}

/// @brief Stub for `Physics3DWorld.CollisionNormal(i)` — contact normal
///        for the `i`th contact pair as a Vec3 (points from body A
///        toward body B).
///
/// Silent stub returning NULL.
///
/// @param w Physics3DWorld handle (ignored).
/// @param i Contact pair index (ignored).
///
/// @return `NULL`.
void *rt_world3d_get_collision_normal(void *w, int64_t i) {
    (void)w;
    (void)i;
    return NULL;
}

/// @brief Stub for `Physics3DWorld.CollisionDepth(i)` — penetration
///        depth for the `i`th contact pair (positive = bodies overlap).
///
/// Silent stub returning `0.0`.
///
/// @param w Physics3DWorld handle (ignored).
/// @param i Contact pair index (ignored).
///
/// @return `0.0`.
double rt_world3d_get_collision_depth(void *w, int64_t i) {
    (void)w;
    (void)i;
    return 0.0;
}

/// @brief Stub for `Physics3DWorld.CollisionEventCount` — number of
///        rich CollisionEvent3D records produced by the most recent
///        `Step`. Distinct from `CollisionCount` (which exposes raw
///        contact pairs); events carry contact-manifold detail.
///
/// Silent stub returning `0`.
///
/// @param w Physics3DWorld handle (ignored).
///
/// @return `0`.
int64_t rt_world3d_get_collision_event_count(void *w) {
    (void)w;
    return 0;
}

/// @brief Stub for `Physics3DWorld.CollisionEvent(i)` — get the `i`th
///        rich CollisionEvent3D from the world's event queue.
///
/// Silent stub returning NULL.
///
/// @param w Physics3DWorld handle (ignored).
/// @param i Event index, 0..CollisionEventCount-1 (ignored).
///
/// @return `NULL`.
void *rt_world3d_get_collision_event(void *w, int64_t i) {
    (void)w;
    (void)i;
    return NULL;
}

/// @brief Stub for `Physics3DWorld.EnterEventCount` — number of
///        contact-enter events from the most recent `Step` (pairs that
///        started touching this tick).
///
/// Silent stub returning `0`.
///
/// @param w Physics3DWorld handle (ignored).
///
/// @return `0`.
int64_t rt_world3d_get_enter_event_count(void *w) {
    (void)w;
    return 0;
}

/// @brief Stub for `Physics3DWorld.EnterEvent(i)` — get the `i`th
///        contact-enter CollisionEvent3D from this tick's queue.
///
/// Silent stub returning NULL.
///
/// @param w Physics3DWorld handle (ignored).
/// @param i Enter-event index (ignored).
///
/// @return `NULL`.
void *rt_world3d_get_enter_event(void *w, int64_t i) {
    (void)w;
    (void)i;
    return NULL;
}

/// @brief Stub for `Physics3DWorld.StayEventCount` — number of
///        contact-stay events from the most recent `Step` (pairs still
///        touching from a previous tick).
///
/// Silent stub returning `0`.
///
/// @param w Physics3DWorld handle (ignored).
///
/// @return `0`.
int64_t rt_world3d_get_stay_event_count(void *w) {
    (void)w;
    return 0;
}

/// @brief Stub for `Physics3DWorld.StayEvent(i)` — get the `i`th
///        contact-stay CollisionEvent3D from this tick's queue.
///
/// Silent stub returning NULL.
///
/// @param w Physics3DWorld handle (ignored).
/// @param i Stay-event index (ignored).
///
/// @return `NULL`.
void *rt_world3d_get_stay_event(void *w, int64_t i) {
    (void)w;
    (void)i;
    return NULL;
}

/// @brief Stub for `Physics3DWorld.ExitEventCount` — number of
///        contact-exit events from the most recent `Step` (pairs that
///        stopped touching this tick).
///
/// Silent stub returning `0`.
///
/// @param w Physics3DWorld handle (ignored).
///
/// @return `0`.
int64_t rt_world3d_get_exit_event_count(void *w) {
    (void)w;
    return 0;
}

/// @brief Stub for `Physics3DWorld.ExitEvent(i)` — get the `i`th
///        contact-exit CollisionEvent3D from this tick's queue.
///
/// Silent stub returning NULL.
///
/// @param w Physics3DWorld handle (ignored).
/// @param i Exit-event index (ignored).
///
/// @return `NULL`.
void *rt_world3d_get_exit_event(void *w, int64_t i) {
    (void)w;
    (void)i;
    return NULL;
}

/// @brief Stub for `Physics3DWorld.Raycast` — single-hit raycast
///        against the world. Returns the closest Physics3DHit, or NULL
///        if no body was hit within `max_distance` along the ray (also
///        filtered by `mask`).
///
/// Silent stub returning NULL.
///
/// @param w            Physics3DWorld handle (ignored).
/// @param origin       Vec3 ray origin (ignored).
/// @param direction    Vec3 ray direction (must be normalized) (ignored).
/// @param max_distance Maximum hit distance in world units (ignored).
/// @param mask         Layer-bitmask filter (ignored).
///
/// @return `NULL`.
void *rt_world3d_raycast(void *w, void *origin, void *direction, double max_distance, int64_t mask) {
    (void)w;
    (void)origin;
    (void)direction;
    (void)max_distance;
    (void)mask;
    return NULL;
}

/// @brief Stub for `Physics3DWorld.RaycastAll` — multi-hit raycast
///        that returns every body the ray passes through (up to the
///        max distance), as a Physics3DHitList.
///
/// Silent stub returning NULL.
///
/// @param w            Physics3DWorld handle (ignored).
/// @param origin       Vec3 ray origin (ignored).
/// @param direction    Vec3 ray direction (ignored).
/// @param max_distance Maximum hit distance in world units (ignored).
/// @param mask         Layer-bitmask filter (ignored).
///
/// @return `NULL`.
void *rt_world3d_raycast_all(
    void *w, void *origin, void *direction, double max_distance, int64_t mask) {
    (void)w;
    (void)origin;
    (void)direction;
    (void)max_distance;
    (void)mask;
    return NULL;
}

/// @brief Stub for `Physics3DWorld.SweepSphere` — sweep a sphere of
///        the given radius along the displacement vector `delta`,
///        returning the first contact (Physics3DHit) or NULL if none.
///
/// Silent stub returning NULL.
///
/// @param w      Physics3DWorld handle (ignored).
/// @param center Vec3 sphere starting center (ignored).
/// @param radius Sphere radius (ignored).
/// @param delta  Vec3 sweep displacement (ignored).
/// @param mask   Layer-bitmask filter (ignored).
///
/// @return `NULL`.
void *rt_world3d_sweep_sphere(void *w, void *center, double radius, void *delta, int64_t mask) {
    (void)w;
    (void)center;
    (void)radius;
    (void)delta;
    (void)mask;
    return NULL;
}

/// @brief Stub for `Physics3DWorld.SweepCapsule` — sweep a capsule
///        (axis from `a` to `b`, radius `radius`) along displacement
///        `delta`, returning the first contact or NULL.
///
/// Silent stub returning NULL.
///
/// @param w      Physics3DWorld handle (ignored).
/// @param a      Vec3 capsule axis start (ignored).
/// @param b      Vec3 capsule axis end (ignored).
/// @param radius Capsule cross-sectional radius (ignored).
/// @param delta  Vec3 sweep displacement (ignored).
/// @param mask   Layer-bitmask filter (ignored).
///
/// @return `NULL`.
void *rt_world3d_sweep_capsule(
    void *w, void *a, void *b, double radius, void *delta, int64_t mask) {
    (void)w;
    (void)a;
    (void)b;
    (void)radius;
    (void)delta;
    (void)mask;
    return NULL;
}

/// @brief Stub for `Physics3DWorld.OverlapSphere` — return every body
///        whose collider overlaps a static sphere centered at `center`
///        with the given `radius`. Useful for AOE damage queries and
///        proximity sensors.
///
/// Silent stub returning NULL.
///
/// @param w      Physics3DWorld handle (ignored).
/// @param center Vec3 sphere center (ignored).
/// @param radius Sphere radius (ignored).
/// @param mask   Layer-bitmask filter (ignored).
///
/// @return `NULL`.
void *rt_world3d_overlap_sphere(void *w, void *center, double radius, int64_t mask) {
    (void)w;
    (void)center;
    (void)radius;
    (void)mask;
    return NULL;
}

/// @brief Stub for `Physics3DWorld.OverlapAABB` — return every body
///        whose collider overlaps a static axis-aligned box `(min_corner,
///        max_corner)`. Useful for box triggers and selection regions.
///
/// Silent stub returning NULL.
///
/// @param w          Physics3DWorld handle (ignored).
/// @param min_corner Vec3 AABB min corner (ignored).
/// @param max_corner Vec3 AABB max corner (ignored).
/// @param mask       Layer-bitmask filter (ignored).
///
/// @return `NULL`.
void *rt_world3d_overlap_aabb(void *w, void *min_corner, void *max_corner, int64_t mask) {
    (void)w;
    (void)min_corner;
    (void)max_corner;
    (void)mask;
    return NULL;
}

/// @brief Stub for `Physics3DHit.Distance` — get the distance from the
///        ray/sweep origin to the hit point.
///
/// Silent stub returning `0.0`.
///
/// @param h Hit record handle (ignored).
///
/// @return `0.0`.
double rt_physics_hit3d_get_distance(void *h) {
    (void)h;
    return 0.0;
}

/// @brief Stub for `Physics3DHit.Body` — get the Body3D that was hit.
///
/// Silent stub returning NULL.
///
/// @param h Hit record handle (ignored).
///
/// @return `NULL`.
void *rt_physics_hit3d_get_body(void *h) {
    (void)h;
    return NULL;
}

/// @brief Stub for `Physics3DHit.Collider` — get the Collider3D shape
///        that was hit (a body may have multiple compound child colliders).
///
/// Silent stub returning NULL.
///
/// @param h Hit record handle (ignored).
///
/// @return `NULL`.
void *rt_physics_hit3d_get_collider(void *h) {
    (void)h;
    return NULL;
}

/// @brief Stub for `Physics3DHit.Point` — get the world-space hit point
///        as a Vec3.
///
/// Silent stub returning NULL.
///
/// @param h Hit record handle (ignored).
///
/// @return `NULL`.
void *rt_physics_hit3d_get_point(void *h) {
    (void)h;
    return NULL;
}

/// @brief Stub for `Physics3DHit.Normal` — get the surface normal at the
///        hit point as a Vec3 (always points away from the hit body).
///
/// Silent stub returning NULL.
///
/// @param h Hit record handle (ignored).
///
/// @return `NULL`.
void *rt_physics_hit3d_get_normal(void *h) {
    (void)h;
    return NULL;
}

/// @brief Stub for `Physics3DHit.Fraction` — for sweep tests, get the
///        fraction along the swept path (0..1) at which contact occurred.
///
/// Silent stub returning `0.0`.
///
/// @param h Hit record handle (ignored).
///
/// @return `0.0`.
double rt_physics_hit3d_get_fraction(void *h) {
    (void)h;
    return 0.0;
}

/// @brief Stub for `Physics3DHit.StartedPenetrating` — for sweep tests,
///        true when the swept shape was already overlapping the target at
///        the start of the sweep.
///
/// Silent stub returning `0`.
///
/// @param h Hit record handle (ignored).
///
/// @return `0`.
int8_t rt_physics_hit3d_get_started_penetrating(void *h) {
    (void)h;
    return 0;
}

/// @brief Stub for `Physics3DHit.IsTrigger` — true if the hit body's
///        collider is a sensor/trigger (no impulse exchange, just an
///        overlap notification).
///
/// Silent stub returning `0`.
///
/// @param h Hit record handle (ignored).
///
/// @return `0`.
int8_t rt_physics_hit3d_get_is_trigger(void *h) {
    (void)h;
    return 0;
}

/// @brief Stub for `Physics3DHitList.Count` — number of hits in a multi-hit
///        query result (e.g. raycast all, overlap query).
///
/// Silent stub returning `0`.
///
/// @param list Hit-list handle (ignored).
///
/// @return `0`.
int64_t rt_physics_hit_list3d_get_count(void *list) {
    (void)list;
    return 0;
}

/// @brief Stub for `Physics3DHitList.Get(i)` — access the `i`th hit
///        record in a multi-hit query result.
///
/// Silent stub returning NULL.
///
/// @param list  Hit-list handle (ignored).
/// @param index Hit index, 0..Count-1 (ignored).
///
/// @return `NULL`.
void *rt_physics_hit_list3d_get(void *list, int64_t index) {
    (void)list;
    (void)index;
    return NULL;
}

/// @brief Stub for `CollisionEvent3D.BodyA` — first body involved in the
///        collision pair.
///
/// Silent stub returning NULL.
///
/// @param event CollisionEvent3D handle (ignored).
///
/// @return `NULL`.
void *rt_collision_event3d_get_body_a(void *event) {
    (void)event;
    return NULL;
}

/// @brief Stub for `CollisionEvent3D.BodyB` — second body involved in the
///        collision pair.
///
/// Silent stub returning NULL.
///
/// @param event CollisionEvent3D handle (ignored).
///
/// @return `NULL`.
void *rt_collision_event3d_get_body_b(void *event) {
    (void)event;
    return NULL;
}

/// @brief Stub for `CollisionEvent3D.ColliderA` — specific Collider3D
///        shape on body A that was contacted (relevant for compound
///        colliders).
///
/// Silent stub returning NULL.
///
/// @param event CollisionEvent3D handle (ignored).
///
/// @return `NULL`.
void *rt_collision_event3d_get_collider_a(void *event) {
    (void)event;
    return NULL;
}

/// @brief Stub for `CollisionEvent3D.ColliderB` — specific Collider3D
///        shape on body B that was contacted.
///
/// Silent stub returning NULL.
///
/// @param event CollisionEvent3D handle (ignored).
///
/// @return `NULL`.
void *rt_collision_event3d_get_collider_b(void *event) {
    (void)event;
    return NULL;
}

/// @brief Stub for `CollisionEvent3D.IsTrigger` — true when at least one
///        collider in the pair is a sensor/trigger.
///
/// Silent stub returning `0`.
///
/// @param event CollisionEvent3D handle (ignored).
///
/// @return `0`.
int8_t rt_collision_event3d_get_is_trigger(void *event) {
    (void)event;
    return 0;
}

/// @brief Stub for `CollisionEvent3D.ContactCount` — number of contact
///        manifold points generated for this collision (typically 1..4).
///
/// Silent stub returning `0`.
///
/// @param event CollisionEvent3D handle (ignored).
///
/// @return `0`.
int64_t rt_collision_event3d_get_contact_count(void *event) {
    (void)event;
    return 0;
}

/// @brief Stub for `CollisionEvent3D.RelativeSpeed` — magnitude of the
///        relative velocity between the bodies along the contact normal at
///        the moment of contact. Useful for impact-strength SFX.
///
/// Silent stub returning `0.0`.
///
/// @param event CollisionEvent3D handle (ignored).
///
/// @return `0.0`.
double rt_collision_event3d_get_relative_speed(void *event) {
    (void)event;
    return 0.0;
}

/// @brief Stub for `CollisionEvent3D.NormalImpulse` — magnitude of the
///        impulse the constraint solver applied along the contact normal.
///
/// Silent stub returning `0.0`.
///
/// @param event CollisionEvent3D handle (ignored).
///
/// @return `0.0`.
double rt_collision_event3d_get_normal_impulse(void *event) {
    (void)event;
    return 0.0;
}

/// @brief Stub for `CollisionEvent3D.Contact(i)` — get the `i`th contact
///        manifold point as an opaque ContactPoint3D handle.
///
/// Silent stub returning NULL.
///
/// @param event CollisionEvent3D handle (ignored).
/// @param index Contact index, 0..ContactCount-1 (ignored).
///
/// @return `NULL`.
void *rt_collision_event3d_get_contact(void *event, int64_t index) {
    (void)event;
    (void)index;
    return NULL;
}

/// @brief Stub for `CollisionEvent3D.ContactPoint(i)` — convenience: get
///        the world-space position of the `i`th contact directly as a
///        Vec3 (skips the ContactPoint3D wrapper).
///
/// Silent stub returning NULL.
///
/// @param event CollisionEvent3D handle (ignored).
/// @param index Contact index (ignored).
///
/// @return `NULL`.
void *rt_collision_event3d_get_contact_point(void *event, int64_t index) {
    (void)event;
    (void)index;
    return NULL;
}

/// @brief Stub for `CollisionEvent3D.ContactNormal(i)` — convenience: get
///        the world-space normal at the `i`th contact directly as a Vec3.
///
/// Silent stub returning NULL.
///
/// @param event CollisionEvent3D handle (ignored).
/// @param index Contact index (ignored).
///
/// @return `NULL`.
void *rt_collision_event3d_get_contact_normal(void *event, int64_t index) {
    (void)event;
    (void)index;
    return NULL;
}

/// @brief Stub for `CollisionEvent3D.ContactSeparation(i)` — penetration
///        depth at the `i`th contact (negative = bodies overlap).
///
/// Silent stub returning `0.0`.
///
/// @param event CollisionEvent3D handle (ignored).
/// @param index Contact index (ignored).
///
/// @return `0.0`.
double rt_collision_event3d_get_contact_separation(void *event, int64_t index) {
    (void)event;
    (void)index;
    return 0.0;
}

/// @brief Stub for `ContactPoint3D.Point` — world-space position of the
///        contact as a Vec3.
///
/// Silent stub returning NULL.
///
/// @param contact ContactPoint3D handle (ignored).
///
/// @return `NULL`.
void *rt_contact_point3d_get_point(void *contact) {
    (void)contact;
    return NULL;
}

/// @brief Stub for `ContactPoint3D.Normal` — world-space contact normal
///        as a Vec3 (points from body A toward body B by convention).
///
/// Silent stub returning NULL.
///
/// @param contact ContactPoint3D handle (ignored).
///
/// @return `NULL`.
void *rt_contact_point3d_get_normal(void *contact) {
    (void)contact;
    return NULL;
}

/// @brief Stub for `ContactPoint3D.Separation` — penetration depth at
///        this contact (negative = bodies overlap).
///
/// Silent stub returning `0.0`.
///
/// @param contact ContactPoint3D handle (ignored).
///
/// @return `0.0`.
double rt_contact_point3d_get_separation(void *contact) {
    (void)contact;
    return 0.0;
}

/* Physics3D Joint stubs */

/// @brief Stub for `DistanceJoint3D.New` — would normally create a
///        rigid distance constraint between bodies `a` and `b` keeping
///        them exactly `d` apart (a stiff invisible rod). Solved with
///        sequential impulses (6 iterations).
///
/// Trapping stub: joints are referenced by the Physics3DWorld step
/// loop — a NULL return would crash later.
///
/// @param a First body handle (ignored).
/// @param b Second body handle (ignored).
/// @param d Target separation distance in world units (ignored).
///
/// @return Never returns normally.
void *rt_distance_joint3d_new(void *a, void *b, double d) {
    (void)a;
    (void)b;
    (void)d;
    rt_graphics_unavailable_("DistanceJoint3D.New: graphics support not compiled in");
    return NULL;
}

/// @brief Stub for `DistanceJoint3D.Distance` — get the joint's target
///        separation distance.
///
/// Silent stub returning `0.0`.
///
/// @param j DistanceJoint3D handle (ignored).
///
/// @return `0.0`.
double rt_distance_joint3d_get_distance(void *j) {
    (void)j;
    return 0.0;
}

/// @brief Stub for `DistanceJoint3D.SetDistance` — adjust the target
///        separation. Connected bodies will be pulled / pushed back to
///        the new distance over the next few simulation steps.
///
/// Silent no-op stub.
///
/// @param j DistanceJoint3D handle (ignored).
/// @param d New target distance (ignored).
void rt_distance_joint3d_set_distance(void *j, double d) {
    (void)j;
    (void)d;
}

/// @brief Stub for `SpringJoint3D.New` — would normally create a
///        damped-spring constraint between bodies `a` and `b` with rest
///        length `rl`, spring stiffness `s`, and damping coefficient `d`.
///        Implements Hooke's law with viscous damping.
///
/// Trapping stub.
///
/// @param a  First body handle (ignored).
/// @param b  Second body handle (ignored).
/// @param rl Spring rest length (ignored).
/// @param s  Spring stiffness (force per unit displacement) (ignored).
/// @param d  Damping coefficient (force per unit relative velocity) (ignored).
///
/// @return Never returns normally.
void *rt_spring_joint3d_new(void *a, void *b, double rl, double s, double d) {
    (void)a;
    (void)b;
    (void)rl;
    (void)s;
    (void)d;
    rt_graphics_unavailable_("SpringJoint3D.New: graphics support not compiled in");
    return NULL;
}

/// @brief Stub for `SpringJoint3D.Stiffness` — get the spring stiffness
///        coefficient.
///
/// Silent stub returning `0.0`.
///
/// @param j SpringJoint3D handle (ignored).
///
/// @return `0.0`.
double rt_spring_joint3d_get_stiffness(void *j) {
    (void)j;
    return 0.0;
}

/// @brief Stub for `SpringJoint3D.SetStiffness` — adjust the spring
///        stiffness coefficient. Higher = snappier oscillation.
///
/// Silent no-op stub.
///
/// @param j SpringJoint3D handle (ignored).
/// @param s New stiffness (ignored).
void rt_spring_joint3d_set_stiffness(void *j, double s) {
    (void)j;
    (void)s;
}

/// @brief Stub for `SpringJoint3D.Damping` — get the damping coefficient.
///
/// Silent stub returning `0.0`.
///
/// @param j SpringJoint3D handle (ignored).
///
/// @return `0.0`.
double rt_spring_joint3d_get_damping(void *j) {
    (void)j;
    return 0.0;
}

/// @brief Stub for `SpringJoint3D.SetDamping` — adjust the damping
///        coefficient. Higher = quicker oscillation decay (overdamped at
///        very high values).
///
/// Silent no-op stub.
///
/// @param j SpringJoint3D handle (ignored).
/// @param d New damping coefficient (ignored).
void rt_spring_joint3d_set_damping(void *j, double d) {
    (void)j;
    (void)d;
}

/// @brief Stub for `SpringJoint3D.RestLength` — get the spring's rest
///        length (the separation at which net force is zero).
///
/// Silent stub returning `0.0`.
///
/// @param j SpringJoint3D handle (ignored).
///
/// @return `0.0`.
double rt_spring_joint3d_get_rest_length(void *j) {
    (void)j;
    return 0.0;
}

/* Collider3D stubs */

/// @brief Stub for `Collider3D.NewBox` — would normally allocate an
///        axis-aligned box collider with the given half-extents (so the
///        full size is `2 * h` along each axis). Centered at the body's
///        local origin.
///
/// Silent stub returning NULL.
///
/// @param hx Half-extent along X (ignored).
/// @param hy Half-extent along Y (ignored).
/// @param hz Half-extent along Z (ignored).
///
/// @return `NULL`.
void *rt_collider3d_new_box(double hx, double hy, double hz) {
    (void)hx;
    (void)hy;
    (void)hz;
    return NULL;
}

/// @brief Stub for `Collider3D.NewSphere` — would normally allocate a
///        sphere collider with the given radius. Cheapest narrow-phase
///        shape (analytic sphere-vs-sphere is O(1)).
///
/// Silent stub returning NULL.
///
/// @param radius Sphere radius in world units (ignored).
///
/// @return `NULL`.
void *rt_collider3d_new_sphere(double radius) {
    (void)radius;
    return NULL;
}

/// @brief Stub for `Collider3D.NewCapsule` — would normally allocate a
///        Y-axis-aligned capsule collider (cylinder with hemispherical
///        end-caps). Total height along Y is `height + 2 * radius`.
///        Used for character controllers and humanoid bodies.
///
/// Silent stub returning NULL.
///
/// @param radius Capsule cross-sectional radius (ignored).
/// @param height Length of the cylindrical mid-section (ignored).
///
/// @return `NULL`.
void *rt_collider3d_new_capsule(double radius, double height) {
    (void)radius;
    (void)height;
    return NULL;
}

/// @brief Stub for `Collider3D.NewConvexHull` — would normally compute
///        the convex hull of the given Mesh3D's vertex set and use it as
///        collision geometry. Convex hulls are cheap to test against
///        (GJK / EPA narrow-phase).
///
/// Silent stub returning NULL.
///
/// @param mesh Source Mesh3D handle (ignored).
///
/// @return `NULL`.
void *rt_collider3d_new_convex_hull(void *mesh) {
    (void)mesh;
    return NULL;
}

/// @brief Stub for `Collider3D.NewMesh` — would normally use the given
///        Mesh3D's triangles directly as collision geometry. Triangle-mesh
///        colliders are static-only (no inertia tensor); typically used
///        for level geometry.
///
/// Silent stub returning NULL.
///
/// @param mesh Source Mesh3D handle (ignored).
///
/// @return `NULL`.
void *rt_collider3d_new_mesh(void *mesh) {
    (void)mesh;
    return NULL;
}

/// @brief Stub for `Collider3D.NewHeightfield` — would normally allocate
///        a heightfield collider sampling the given heightmap (Pixels
///        surface; red channel = height) at world dimensions
///        `(sx, sy, sz)`. Static-only; used for terrain.
///
/// Silent stub returning NULL.
///
/// @param heightmap Pixels handle providing height samples (ignored).
/// @param sx        World extent along X (ignored).
/// @param sy        Vertical scale (height range) (ignored).
/// @param sz        World extent along Z (ignored).
///
/// @return `NULL`.
void *rt_collider3d_new_heightfield(void *heightmap, double sx, double sy, double sz) {
    (void)heightmap;
    (void)sx;
    (void)sy;
    (void)sz;
    return NULL;
}

/// @brief Stub for `Collider3D.NewCompound` — would normally allocate
///        an empty compound collider. Children added via `AddChild` are
///        each tested individually during narrow-phase but share a
///        single body-vs-collider attachment. Used for non-convex
///        collision shapes.
///
/// Silent stub returning NULL.
///
/// @return `NULL`.
void *rt_collider3d_new_compound(void) {
    return NULL;
}

/// @brief Stub for `Collider3D.AddChild` — attach a child collider to a
///        compound parent at the given local-space transform. The child
///        moves rigidly with the compound during simulation.
///
/// Silent no-op stub.
///
/// @param compound        Compound Collider3D handle (ignored).
/// @param child           Child Collider3D handle (ignored).
/// @param local_transform Transform3D positioning child within compound (ignored).
void rt_collider3d_add_child(void *compound, void *child, void *local_transform) {
    (void)compound;
    (void)child;
    (void)local_transform;
}

/// @brief Stub for `Collider3D.Type` — get the shape type of the collider:
///        0=Box, 1=Sphere, 2=Capsule, 3=Hull, 4=Mesh, 5=Heightfield, 6=Compound.
///
/// Silent stub returning `-1` (invalid / no collider).
///
/// @param collider Collider3D handle (ignored).
///
/// @return `-1`.
int64_t rt_collider3d_get_type(void *collider) {
    (void)collider;
    return -1;
}

/// @brief Stub for `Collider3D.LocalBoundsMin` — get the min corner of
///        the collider's local-space AABB as a Vec3 (before world transform).
///
/// Silent stub returning NULL.
///
/// @param collider Collider3D handle (ignored).
///
/// @return `NULL`.
void *rt_collider3d_get_local_bounds_min(void *collider) {
    (void)collider;
    return NULL;
}

/// @brief Stub for `Collider3D.LocalBoundsMax` — get the max corner of
///        the collider's local-space AABB as a Vec3.
///
/// Silent stub returning NULL.
///
/// @param collider Collider3D handle (ignored).
///
/// @return `NULL`.
void *rt_collider3d_get_local_bounds_max(void *collider) {
    (void)collider;
    return NULL;
}

/// @brief Stub for the raw out-parameter form of `Collider3D` local-bounds
///        query. Used by C callers that want to avoid Vec3 wrapper allocation.
///
/// Silent stub: zeros both out-parameters when non-NULL.
///
/// @param collider Collider3D handle (ignored).
/// @param min_out  `double[3]` to receive the local-AABB min, or NULL (zeroed if non-NULL).
/// @param max_out  `double[3]` to receive the local-AABB max, or NULL (zeroed if non-NULL).
void rt_collider3d_get_local_bounds_raw(void *collider, double *min_out, double *max_out) {
    (void)collider;
    if (min_out) {
        min_out[0] = min_out[1] = min_out[2] = 0.0;
    }
    if (max_out) {
        max_out[0] = max_out[1] = max_out[2] = 0.0;
    }
}

/// @brief Stub for the raw form of `Collider3D.WorldAABB(transform)` —
///        would normally apply the body's `(position, rotation, scale)` to
///        the local bounds and write the resulting world AABB to out-params.
///
/// Silent stub: zeros both out-parameters when non-NULL.
///
/// @param collider Collider3D handle (ignored).
/// @param position `double[3]` body world position (ignored).
/// @param rotation `double[4]` body orientation quaternion (ignored).
/// @param scale    `double[3]` body local scale (ignored).
/// @param min_out  `double[3]` receives world-AABB min, or NULL (zeroed if non-NULL).
/// @param max_out  `double[3]` receives world-AABB max, or NULL (zeroed if non-NULL).
void rt_collider3d_compute_world_aabb_raw(void *collider,
                                          const double *position,
                                          const double *rotation,
                                          const double *scale,
                                          double *min_out,
                                          double *max_out) {
    (void)collider;
    (void)position;
    (void)rotation;
    (void)scale;
    if (min_out) {
        min_out[0] = min_out[1] = min_out[2] = 0.0;
    }
    if (max_out) {
        max_out[0] = max_out[1] = max_out[2] = 0.0;
    }
}

/// @brief Stub for `Collider3D.IsStaticOnly` — true for collider shapes
///        that can only be attached to static (non-moving) bodies — e.g.
///        triangle meshes and heightfields, which lack inertia tensors.
///
/// Silent stub returning `0`.
///
/// @param collider Collider3D handle (ignored).
///
/// @return `0`.
int8_t rt_collider3d_is_static_only_raw(void *collider) {
    (void)collider;
    return 0;
}

/// @brief Stub for `Collider3D.BoxHalfExtents` raw query — for box
///        colliders, write the half-extents (so full size is `2 * he` along
///        each axis) to `half_extents_out[0..2]`.
///
/// Silent stub: zeros the out-parameter when non-NULL. Meaningless for
/// non-box colliders even in the real implementation.
///
/// @param collider          Collider3D handle (ignored).
/// @param half_extents_out  `double[3]` receives half-extents, or NULL.
void rt_collider3d_get_box_half_extents_raw(void *collider, double *half_extents_out) {
    (void)collider;
    if (half_extents_out) {
        half_extents_out[0] = half_extents_out[1] = half_extents_out[2] = 0.0;
    }
}

/// @brief Stub for `Collider3D.Radius` raw query — for sphere and capsule
///        colliders, get the cross-sectional radius.
///
/// Silent stub returning `0.0`. Meaningless for non-radial shapes even in
/// the real implementation.
///
/// @param collider Collider3D handle (ignored).
///
/// @return `0.0`.
double rt_collider3d_get_radius_raw(void *collider) {
    (void)collider;
    return 0.0;
}

/// @brief Stub for `Collider3D.Height` raw query — for capsule colliders,
///        get the axis length between the two hemispherical caps (so full
///        capsule length is `height + 2 * radius`).
///
/// Silent stub returning `0.0`.
///
/// @param collider Collider3D handle (ignored).
///
/// @return `0.0`.
double rt_collider3d_get_height_raw(void *collider) {
    (void)collider;
    return 0.0;
}

/// @brief Stub for `Collider3D.Mesh` raw query — for hull and triangle-mesh
///        colliders, get the underlying Mesh3D used as collision geometry.
///
/// Silent stub returning NULL.
///
/// @param collider Collider3D handle (ignored).
///
/// @return `NULL`.
void *rt_collider3d_get_mesh_raw(void *collider) {
    (void)collider;
    return NULL;
}

/// @brief Stub for `Collider3D.ChildCount` raw query — for compound
///        colliders, the number of attached child colliders.
///
/// Silent stub returning `0`.
///
/// @param collider Collider3D handle (ignored).
///
/// @return `0`.
int64_t rt_collider3d_get_child_count_raw(void *collider) {
    (void)collider;
    return 0;
}

/// @brief Stub for `Collider3D.Child(i)` raw query — for compound
///        colliders, get the `i`th child collider.
///
/// Silent stub returning NULL.
///
/// @param collider Compound Collider3D handle (ignored).
/// @param index    Child index, 0..ChildCount-1 (ignored).
///
/// @return `NULL`.
void *rt_collider3d_get_child_raw(void *collider, int64_t index) {
    (void)collider;
    (void)index;
    return NULL;
}

/// @brief Stub for `Collider3D.ChildTransform(i)` raw query — for compound
///        colliders, write the `i`th child's local-space transform
///        (relative to the compound parent) to the out-parameters.
///
/// Silent stub: writes identity transform (zero translation, identity
/// quaternion, unit scale) when out-params are non-NULL. The identity is
/// meaningful — defensive callers can use the result without further checks.
///
/// @param compound      Compound Collider3D handle (ignored).
/// @param index         Child index (ignored).
/// @param position_out  `double[3]` receives local position; defaults to zero.
/// @param rotation_out  `double[4]` receives local rotation quaternion `(x,y,z,w)`;
///                      defaults to identity `(0, 0, 0, 1)`.
/// @param scale_out     `double[3]` receives local scale; defaults to `(1, 1, 1)`.
void rt_collider3d_get_child_transform_raw(void *compound,
                                           int64_t index,
                                           double *position_out,
                                           double *rotation_out,
                                           double *scale_out) {
    (void)compound;
    (void)index;
    if (position_out) {
        position_out[0] = position_out[1] = position_out[2] = 0.0;
    }
    if (rotation_out) {
        rotation_out[0] = rotation_out[1] = rotation_out[2] = 0.0;
        rotation_out[3] = 1.0;
    }
    if (scale_out) {
        scale_out[0] = scale_out[1] = scale_out[2] = 1.0;
    }
}

/// @brief Stub for `Collider3D.SampleHeightfield(x, z)` raw query — for
///        heightfield colliders, would normally sample the surface height
///        and surface normal at the local-space `(x, z)` point.
///
/// Silent stub: writes a flat ground default (height 0, normal +Y) to the
/// out-parameters and returns `0` (no hit). The flat default lets callers
/// use the values for layout math without further checks.
///
/// @param collider   Heightfield Collider3D handle (ignored).
/// @param local_x    Local-space X to sample (ignored).
/// @param local_z    Local-space Z to sample (ignored).
/// @param height_out Receives surface height (defaults to `0.0`).
/// @param normal_out `double[3]` receives surface normal (defaults to +Y).
///
/// @return `0` (sample missed / outside heightfield bounds).
int8_t rt_collider3d_sample_heightfield_raw(void *collider,
                                            double local_x,
                                            double local_z,
                                            double *height_out,
                                            double *normal_out) {
    (void)collider;
    (void)local_x;
    (void)local_z;
    if (height_out)
        *height_out = 0.0;
    if (normal_out) {
        normal_out[0] = 0.0;
        normal_out[1] = 1.0;
        normal_out[2] = 0.0;
    }
    return 0;
}

/* Physics3D Body stubs */

/// @brief Stub for `Body3D.New` — would normally allocate a rigid body
///        with the given mass and no collider attached. Use `SetCollider`
///        to bind a Collider3D shape, or use the convenience constructors
///        below (`NewAABB`, `NewSphere`, `NewCapsule`) that pre-attach a
///        primitive shape.
///
/// Silent stub returning NULL.
///
/// @param mass Body mass in kilograms; `0` for static (infinite mass) (ignored).
///
/// @return `NULL`.
void *rt_body3d_new(double mass) {
    (void)mass;
    return NULL;
}

/// @brief Stub for `Body3D.NewAABB` — convenience constructor that
///        creates a body with an axis-aligned box collider of half-extents
///        `(hx, hy, hz)` and the given mass. Pre-attaches the collider so
///        the body is ready to add to a Physics3DWorld.
///
/// Silent stub returning NULL.
///
/// @param hx   Half-extent along X (ignored).
/// @param hy   Half-extent along Y (ignored).
/// @param hz   Half-extent along Z (ignored).
/// @param mass Body mass in kilograms (ignored).
///
/// @return `NULL`.
void *rt_body3d_new_aabb(double hx, double hy, double hz, double mass) {
    (void)hx;
    (void)hy;
    (void)hz;
    (void)mass;
    return NULL;
}

/// @brief Stub for `Body3D.NewSphere` — convenience constructor that
///        creates a body with a sphere collider of the given radius and
///        the given mass. Cheapest pairwise narrow-phase shape.
///
/// Silent stub returning NULL.
///
/// @param radius Sphere radius (ignored).
/// @param mass   Body mass in kilograms (ignored).
///
/// @return `NULL`.
void *rt_body3d_new_sphere(double radius, double mass) {
    (void)radius;
    (void)mass;
    return NULL;
}

/// @brief Stub for `Body3D.NewCapsule` — convenience constructor that
///        creates a body with a Y-axis capsule collider and the given
///        mass. Standard shape for character controllers.
///
/// Silent stub returning NULL.
///
/// @param radius Capsule cross-sectional radius (ignored).
/// @param height Length of the cylindrical mid-section (ignored).
/// @param mass   Body mass in kilograms (ignored).
///
/// @return `NULL`.
void *rt_body3d_new_capsule(double radius, double height, double mass) {
    (void)radius;
    (void)height;
    (void)mass;
    return NULL;
}

/// @brief Stub for `Body3D.SetCollider` — attach a Collider3D shape to
///        this body. A body without a collider participates in the
///        simulation (gets integrated) but cannot generate contacts and
///        will pass through everything.
///
/// Silent no-op stub.
///
/// @param o        Body3D handle (ignored).
/// @param collider Collider3D handle, or NULL to detach (ignored).
void rt_body3d_set_collider(void *o, void *collider) {
    (void)o;
    (void)collider;
}

/// @brief Stub for `Body3D.Collider` — get the attached Collider3D, or
///        NULL if the body has no shape.
///
/// Silent stub returning NULL.
///
/// @param o Body3D handle (ignored).
///
/// @return `NULL`.
void *rt_body3d_get_collider(void *o) {
    (void)o;
    return NULL;
}

/// @brief Stub for `Body3D.SetPosition` — teleport the body to the
///        given world-space position. Use for spawn / respawn / portals
///        — for normal motion let the simulation integrate forces.
///
/// Silent no-op stub. Wakes the body if it was sleeping.
///
/// @param o Body3D handle (ignored).
/// @param x World x (ignored).
/// @param y World y (ignored).
/// @param z World z (ignored).
void rt_body3d_set_position(void *o, double x, double y, double z) {
    (void)o;
    (void)x;
    (void)y;
    (void)z;
}

/// @brief Stub for `Body3D.Position` — get the body's current world-
///        space position as a Vec3 (post-integration this tick).
///
/// Silent stub returning NULL.
///
/// @param o Body3D handle (ignored).
///
/// @return `NULL`.
void *rt_body3d_get_position(void *o) {
    (void)o;
    return NULL;
}

/// @brief Stub for `Body3D.SetOrientation` — set the body's rotation
///        from a Quaternion handle. As with `SetPosition`, this is
///        teleportation and wakes the body.
///
/// Silent no-op stub.
///
/// @param o Body3D handle (ignored).
/// @param q Quaternion handle (ignored).
void rt_body3d_set_orientation(void *o, void *q) {
    (void)o;
    (void)q;
}

/// @brief Stub for `Body3D.Orientation` — get the body's current
///        rotation as a Quaternion handle.
///
/// Silent stub returning NULL.
///
/// @param o Body3D handle (ignored).
///
/// @return `NULL`.
void *rt_body3d_get_orientation(void *o) {
    (void)o;
    return NULL;
}

/// @brief Stub for `Body3D.SetVelocity` — set the body's linear velocity
///        directly. Useful for character locomotion (where solver-driven
///        forces feel mushy) and one-shot launches.
///
/// Silent no-op stub.
///
/// @param o  Body3D handle (ignored).
/// @param vx Velocity x (ignored).
/// @param vy Velocity y (ignored).
/// @param vz Velocity z (ignored).
void rt_body3d_set_velocity(void *o, double vx, double vy, double vz) {
    (void)o;
    (void)vx;
    (void)vy;
    (void)vz;
}

/// @brief Stub for `Body3D.Velocity` — get the body's current linear
///        velocity as a Vec3 (world units / second).
///
/// Silent stub returning NULL.
///
/// @param o Body3D handle (ignored).
///
/// @return `NULL`.
void *rt_body3d_get_velocity(void *o) {
    (void)o;
    return NULL;
}

/// @brief Stub for `Body3D.SetAngularVelocity` — set the body's angular
///        velocity directly. Each axis is rotation rate in radians /
///        second around that axis.
///
/// Silent no-op stub.
///
/// @param o  Body3D handle (ignored).
/// @param wx Angular velocity x (ignored).
/// @param wy Angular velocity y (ignored).
/// @param wz Angular velocity z (ignored).
void rt_body3d_set_angular_velocity(void *o, double wx, double wy, double wz) {
    (void)o;
    (void)wx;
    (void)wy;
    (void)wz;
}

/// @brief Stub for `Body3D.AngularVelocity` — get the body's current
///        angular velocity as a Vec3 (radians per second around each axis).
///
/// Silent stub returning NULL.
///
/// @param o Body3D handle (ignored).
///
/// @return `NULL`.
void *rt_body3d_get_angular_velocity(void *o) {
    (void)o;
    return NULL;
}

/// @brief Stub for `Body3D.ApplyForce` — apply a continuous force this
///        tick, integrated by the simulation step over `dt`.
///
/// Silent no-op stub. Force is in world-space; for repeated forces (gravity,
/// thrust) call once per tick rather than once per frame.
///
/// @param o  Body3D handle (ignored).
/// @param fx Force x component (ignored).
/// @param fy Force y component (ignored).
/// @param fz Force z component (ignored).
void rt_body3d_apply_force(void *o, double fx, double fy, double fz) {
    (void)o;
    (void)fx;
    (void)fy;
    (void)fz;
}

/// @brief Stub for `Body3D.ApplyImpulse` — apply an instantaneous
///        velocity change (no `dt` integration). Use for one-shot events:
///        jumps, knockbacks, recoil.
///
/// Silent no-op stub.
///
/// @param o  Body3D handle (ignored).
/// @param ix Impulse x component (ignored).
/// @param iy Impulse y component (ignored).
/// @param iz Impulse z component (ignored).
void rt_body3d_apply_impulse(void *o, double ix, double iy, double iz) {
    (void)o;
    (void)ix;
    (void)iy;
    (void)iz;
}

/// @brief Stub for `Body3D.ApplyTorque` — apply a continuous torque this
///        tick, integrated over `dt`. World-space.
///
/// Silent no-op stub.
///
/// @param o  Body3D handle (ignored).
/// @param tx Torque x component (ignored).
/// @param ty Torque y component (ignored).
/// @param tz Torque z component (ignored).
void rt_body3d_apply_torque(void *o, double tx, double ty, double tz) {
    (void)o;
    (void)tx;
    (void)ty;
    (void)tz;
}

/// @brief Stub for `Body3D.ApplyAngularImpulse` — apply an instantaneous
///        angular velocity change (no `dt` integration). Use for spin-up
///        events.
///
/// Silent no-op stub.
///
/// @param o  Body3D handle (ignored).
/// @param ix Angular impulse x component (ignored).
/// @param iy Angular impulse y component (ignored).
/// @param iz Angular impulse z component (ignored).
void rt_body3d_apply_angular_impulse(void *o, double ix, double iy, double iz) {
    (void)o;
    (void)ix;
    (void)iy;
    (void)iz;
}

/// @brief Stub for `Body3D.SetRestitution` — coefficient of restitution
///        (bounciness). 0 = inelastic, 1 = perfectly elastic.
///
/// Silent no-op stub.
///
/// @param o Body3D handle (ignored).
/// @param r Restitution, 0..1 (ignored).
void rt_body3d_set_restitution(void *o, double r) {
    (void)o;
    (void)r;
}

/// @brief Stub for `Body3D.Restitution` — get the current restitution
///        coefficient.
///
/// Silent stub returning `0.0`.
///
/// @param o Body3D handle (ignored).
///
/// @return `0.0`.
double rt_body3d_get_restitution(void *o) {
    (void)o;
    return 0.0;
}

/// @brief Stub for `Body3D.SetFriction` — coefficient of friction
///        applied at contact points. 0 = frictionless, 1 = high friction.
///
/// Silent no-op stub.
///
/// @param o Body3D handle (ignored).
/// @param f Friction, 0..1+ (ignored).
void rt_body3d_set_friction(void *o, double f) {
    (void)o;
    (void)f;
}

/// @brief Stub for `Body3D.Friction` — get the current friction
///        coefficient.
///
/// Silent stub returning `0.0`.
///
/// @param o Body3D handle (ignored).
///
/// @return `0.0`.
double rt_body3d_get_friction(void *o) {
    (void)o;
    return 0.0;
}

/// @brief Stub for `Body3D.SetLinearDamping` — fraction of linear velocity
///        bled off per second (0 = no damping, simulates air resistance /
///        viscosity).
///
/// Silent no-op stub.
///
/// @param o Body3D handle (ignored).
/// @param d Damping per second, 0..1 (ignored).
void rt_body3d_set_linear_damping(void *o, double d) {
    (void)o;
    (void)d;
}

/// @brief Stub for `Body3D.LinearDamping` — get the current linear
///        damping factor.
///
/// Silent stub returning `0.0`.
///
/// @param o Body3D handle (ignored).
///
/// @return `0.0`.
double rt_body3d_get_linear_damping(void *o) {
    (void)o;
    return 0.0;
}

/// @brief Stub for `Body3D.SetAngularDamping` — fraction of angular
///        velocity bled off per second.
///
/// Silent no-op stub.
///
/// @param o Body3D handle (ignored).
/// @param d Damping per second, 0..1 (ignored).
void rt_body3d_set_angular_damping(void *o, double d) {
    (void)o;
    (void)d;
}

/// @brief Stub for `Body3D.AngularDamping` — get the current angular
///        damping factor.
///
/// Silent stub returning `0.0`.
///
/// @param o Body3D handle (ignored).
///
/// @return `0.0`.
double rt_body3d_get_angular_damping(void *o) {
    (void)o;
    return 0.0;
}

/// @brief Stub for `Body3D.SetCollisionLayer` — bitmask of which layer(s)
///        this body belongs to. Pairs only generate contacts when
///        `(BodyA.layer & BodyB.mask) != 0` and vice versa.
///
/// Silent no-op stub.
///
/// @param o Body3D handle (ignored).
/// @param l Layer bitmask (ignored).
void rt_body3d_set_collision_layer(void *o, int64_t l) {
    (void)o;
    (void)l;
}

/// @brief Stub for `Body3D.CollisionLayer` — get the body's collision
///        layer bitmask.
///
/// Silent stub returning `0`.
///
/// @param o Body3D handle (ignored).
///
/// @return `0`.
int64_t rt_body3d_get_collision_layer(void *o) {
    (void)o;
    return 0;
}

/// @brief Stub for `Body3D.SetCollisionMask` — bitmask of which layers
///        this body collides with. See `SetCollisionLayer` for the pair-
///        evaluation rule.
///
/// Silent no-op stub.
///
/// @param o Body3D handle (ignored).
/// @param m Mask bitmask (ignored).
void rt_body3d_set_collision_mask(void *o, int64_t m) {
    (void)o;
    (void)m;
}

/// @brief Stub for `Body3D.CollisionMask` — get the body's collision
///        mask bitmask.
///
/// Silent stub returning `0`.
///
/// @param o Body3D handle (ignored).
///
/// @return `0`.
int64_t rt_body3d_get_collision_mask(void *o) {
    (void)o;
    return 0;
}

/// @brief Stub for `Body3D.SetStatic` — when true, the body has infinite
///        mass and never moves; other bodies collide against it but it
///        receives no forces in return. Cheaper than dynamic bodies.
///
/// Silent no-op stub.
///
/// @param o Body3D handle (ignored).
/// @param s Non-zero for static (ignored).
void rt_body3d_set_static(void *o, int8_t s) {
    (void)o;
    (void)s;
}

/// @brief Stub for `Body3D.IsStatic` — true if the body is in static mode.
///
/// Silent stub returning `0`.
///
/// @param o Body3D handle (ignored).
///
/// @return `0`.
int8_t rt_body3d_is_static(void *o) {
    (void)o;
    return 0;
}

/// @brief Stub for `Body3D.SetKinematic` — when true, the body's
///        position/orientation are driven externally (typically by gameplay
///        code or an animation), but it still pushes dynamic bodies during
///        contact. Used for moving platforms and animated characters.
///
/// Silent no-op stub.
///
/// @param o Body3D handle (ignored).
/// @param k Non-zero for kinematic (ignored).
void rt_body3d_set_kinematic(void *o, int8_t k) {
    (void)o;
    (void)k;
}

/// @brief Stub for `Body3D.IsKinematic` — true if the body is in
///        kinematic mode.
///
/// Silent stub returning `0`.
///
/// @param o Body3D handle (ignored).
///
/// @return `0`.
int8_t rt_body3d_is_kinematic(void *o) {
    (void)o;
    return 0;
}

/// @brief Stub for `Body3D.SetTrigger` — when true, the body becomes a
///        sensor: it generates collision events but exchanges no impulses
///        with other bodies. Used for AOE volumes, item pickups, kill
///        floors.
///
/// Silent no-op stub.
///
/// @param o Body3D handle (ignored).
/// @param t Non-zero to make this body a trigger (ignored).
void rt_body3d_set_trigger(void *o, int8_t t) {
    (void)o;
    (void)t;
}

/// @brief Stub for `Body3D.IsTrigger` — true if this body is in trigger
///        (sensor) mode.
///
/// Silent stub returning `0`.
///
/// @param o Body3D handle (ignored).
///
/// @return `0`.
int8_t rt_body3d_is_trigger(void *o) {
    (void)o;
    return 0;
}

/// @brief Stub for `Body3D.SetCanSleep` — enable or disable the
///        sleep-eligibility flag. Bodies that should never sleep (e.g.
///        the player character) need `can_sleep = 0`; otherwise they may
///        freeze when stationary and stop responding to player input.
///
/// Silent no-op stub.
///
/// @param o         Body3D handle (ignored).
/// @param can_sleep Non-zero to allow this body to enter sleep state (ignored).
void rt_body3d_set_can_sleep(void *o, int8_t can_sleep) {
    (void)o;
    (void)can_sleep;
}

/// @brief Stub for `Body3D.CanSleep` — get the sleep-eligibility flag.
///        Distinct from `IsSleeping`: `CanSleep` is a configuration flag,
///        `IsSleeping` is current state.
///
/// Silent stub returning `0`.
///
/// @param o Body3D handle (ignored).
///
/// @return `0`.
int8_t rt_body3d_can_sleep(void *o) {
    (void)o;
    return 0;
}

/// @brief Stub for `Body3D.IsSleeping` — true while the body is in the
///        sleep state (skipped during simulation to save CPU).
///
/// Silent stub returning `0`.
///
/// @param o Body3D handle (ignored).
///
/// @return `0`.
int8_t rt_body3d_is_sleeping(void *o) {
    (void)o;
    return 0;
}

/// @brief Stub for `Body3D.Wake` — manually wake a sleeping body. Used
///        when external state changes invalidate the sleep assumption
///        (e.g. teleporting a body, removing supporting geometry).
///
/// Silent no-op stub.
///
/// @param o Body3D handle (ignored).
void rt_body3d_wake(void *o) {
    (void)o;
}

/// @brief Stub for `Body3D.Sleep` — manually put a body to sleep. The
///        sleep system would normally do this automatically when the
///        body's velocity has been below threshold for `PH3D_SLEEP_DELAY`
///        seconds.
///
/// Silent no-op stub.
///
/// @param o Body3D handle (ignored).
void rt_body3d_sleep(void *o) {
    (void)o;
}

/// @brief Stub for `Body3D.SetUseCCD` — enable Continuous Collision
///        Detection: fast-moving bodies are advanced in `PH3D_MAX_CCD_
///        SUBSTEPS = 16` substeps to detect tunneling through thin
///        geometry. More expensive than discrete collision.
///
/// Silent no-op stub.
///
/// @param o       Body3D handle (ignored).
/// @param use_ccd Non-zero to enable CCD (ignored).
void rt_body3d_set_use_ccd(void *o, int8_t use_ccd) {
    (void)o;
    (void)use_ccd;
}

/// @brief Stub for `Body3D.UseCCD` — get the CCD flag.
///
/// Silent stub returning `0`.
///
/// @param o Body3D handle (ignored).
///
/// @return `0`.
int8_t rt_body3d_get_use_ccd(void *o) {
    (void)o;
    return 0;
}

/// @brief Stub for `Body3D.IsGrounded` — convenience query: true when
///        the body is in contact with a surface in the gravity-down
///        direction. Updated each simulation step.
///
/// Silent stub returning `0`.
///
/// @param o Body3D handle (ignored).
///
/// @return `0`.
int8_t rt_body3d_is_grounded(void *o) {
    (void)o;
    return 0;
}

/// @brief Stub for `Body3D.GroundNormal` — get the surface normal of
///        the ground contact, as a Vec3. Useful for slope traversal.
///
/// Silent stub returning NULL.
///
/// @param o Body3D handle (ignored).
///
/// @return `NULL`.
void *rt_body3d_get_ground_normal(void *o) {
    (void)o;
    return NULL;
}

/// @brief Stub for `Body3D.Mass` — get the body's mass in kilograms.
///        Static bodies report `0.0` (representing infinite mass).
///
/// Silent stub returning `0.0`.
///
/// @param o Body3D handle (ignored).
///
/// @return `0.0`.
double rt_body3d_get_mass(void *o) {
    (void)o;
    return 0.0;
}

/* Character3D stubs */

/// @brief Stub for `Character3D.New` — would normally create a kinematic
///        character controller (capsule shape) for player/NPC movement.
///        Distinct from `Body3D` in that it uses slide-and-step movement
///        instead of pure rigid-body integration.
///
/// Silent stub returning NULL.
///
/// @param radius Capsule radius in world units (ignored).
/// @param height Capsule total height (ignored).
/// @param mass   Mass for impulse exchange with dynamic bodies (ignored).
///
/// @return `NULL`.
void *rt_character3d_new(double radius, double height, double mass) {
    (void)radius;
    (void)height;
    (void)mass;
    return NULL;
}

/// @brief Stub for `Character3D.Move` — apply the given velocity for
///        `dt` seconds with slide-and-step collision response. The
///        controller will slide along walls, step up small obstacles
///        (configurable via `SetStepHeight`), and stop at slopes steeper
///        than `SetSlopeLimit`.
///
/// Silent no-op stub.
///
/// @param c  Character3D handle (ignored).
/// @param v  Vec3 desired velocity in world units / second (ignored).
/// @param dt Delta time in seconds (ignored).
void rt_character3d_move(void *c, void *v, double dt) {
    (void)c;
    (void)v;
    (void)dt;
}

/// @brief Stub for `Character3D.SetStepHeight` — maximum vertical
///        obstacle the character can step up onto without jumping.
///        Typical values: 0.3 (humanoid) to 0.5 (heavy mech).
///
/// Silent no-op stub.
///
/// @param c Character3D handle (ignored).
/// @param h Step height in world units (ignored).
void rt_character3d_set_step_height(void *c, double h) {
    (void)c;
    (void)h;
}

/// @brief Stub for `Character3D.StepHeight` — get the configured step
///        height.
///
/// Silent stub returning `0.3` (the default value used in the real
/// implementation, so callers querying this for layout math get a usable
/// answer rather than `0`).
///
/// @param c Character3D handle (ignored).
///
/// @return `0.3`.
double rt_character3d_get_step_height(void *c) {
    (void)c;
    return 0.3;
}

/// @brief Stub for `Character3D.SetSlopeLimit` — maximum slope (in
///        radians) the character can climb. Steeper slopes cause the
///        character to slide back down.
///
/// Silent no-op stub.
///
/// @param c Character3D handle (ignored).
/// @param d Slope limit in radians (ignored).
void rt_character3d_set_slope_limit(void *c, double d) {
    (void)c;
    (void)d;
}

/// @brief Stub for `Character3D.SetWorld` — bind the character to a
///        Physics3DWorld so its movement queries hit the right collision
///        geometry.
///
/// Silent no-op stub.
///
/// @param c Character3D handle (ignored).
/// @param w Physics3DWorld handle (ignored).
void rt_character3d_set_world(void *c, void *w) {
    (void)c;
    (void)w;
}

/// @brief Stub for `Character3D.World` — get the bound Physics3DWorld.
///
/// Silent stub returning NULL.
///
/// @param c Character3D handle (ignored).
///
/// @return `NULL`.
void *rt_character3d_get_world(void *c) {
    (void)c;
    return NULL;
}

/// @brief Stub for `Character3D.IsGrounded` — true when the character
///        is in contact with a walkable surface below. Updated during
///        each `Move` call.
///
/// Silent stub returning `0`.
///
/// @param c Character3D handle (ignored).
///
/// @return `0`.
int8_t rt_character3d_is_grounded(void *c) {
    (void)c;
    return 0;
}

/// @brief Stub for `Character3D.JustLanded` — single-tick edge: true
///        on the frame the character transitioned from airborne to
///        grounded. Useful for landing FX / SFX.
///
/// Silent stub returning `0`.
///
/// @param c Character3D handle (ignored).
///
/// @return `0`.
int8_t rt_character3d_just_landed(void *c) {
    (void)c;
    return 0;
}

/// @brief Stub for `Character3D.Position` — get the character's current
///        world-space position as a Vec3.
///
/// Silent stub returning NULL.
///
/// @param c Character3D handle (ignored).
///
/// @return `NULL`.
void *rt_character3d_get_position(void *c) {
    (void)c;
    return NULL;
}

/// @brief Stub for `Character3D.SetPosition` — teleport the character
///        to the given world-space position. Use for spawn / respawn /
///        portals — for normal locomotion use `Move`.
///
/// Silent no-op stub.
///
/// @param c Character3D handle (ignored).
/// @param x World x (ignored).
/// @param y World y (ignored).
/// @param z World z (ignored).
void rt_character3d_set_position(void *c, double x, double y, double z) {
    (void)c;
    (void)x;
    (void)y;
    (void)z;
}

/* Trigger3D stubs */

/// @brief Stub for `Trigger3D.New` — would normally create an axis-
///        aligned trigger volume defined by min corner `(x0, y0, z0)`
///        and max corner `(x1, y1, z1)`. Triggers detect entry/exit but
///        do not exchange impulses with bodies passing through.
///
/// Silent stub returning NULL.
///
/// @param x0 Min corner x (ignored).
/// @param y0 Min corner y (ignored).
/// @param z0 Min corner z (ignored).
/// @param x1 Max corner x (ignored).
/// @param y1 Max corner y (ignored).
/// @param z1 Max corner z (ignored).
///
/// @return `NULL`.
void *rt_trigger3d_new(double x0, double y0, double z0, double x1, double y1, double z1) {
    (void)x0;
    (void)y0;
    (void)z0;
    (void)x1;
    (void)y1;
    (void)z1;
    return NULL;
}

/// @brief Stub for `Trigger3D.Contains` — boolean test for whether the
///        Vec3 point `p` lies inside the trigger volume.
///
/// Silent stub returning `0`.
///
/// @param t Trigger3D handle (ignored).
/// @param p Vec3 query point (ignored).
///
/// @return `0`.
int8_t rt_trigger3d_contains(void *t, void *p) {
    (void)t;
    (void)p;
    return 0;
}

/// @brief Stub for `Trigger3D.Update` — refresh the enter/exit counts
///        for this tick by testing every body in the world against the
///        trigger volume.
///
/// Silent no-op stub.
///
/// @param t Trigger3D handle (ignored).
/// @param w Physics3DWorld handle (ignored).
void rt_trigger3d_update(void *t, void *w) {
    (void)t;
    (void)w;
}

/// @brief Stub for `Trigger3D.EnterCount` — number of bodies that
///        crossed into the trigger volume during the most recent `Update`.
///        Use for one-shot pickups, area transitions.
///
/// Silent stub returning `0`.
///
/// @param t Trigger3D handle (ignored).
///
/// @return `0`.
int64_t rt_trigger3d_get_enter_count(void *t) {
    (void)t;
    return 0;
}

/// @brief Stub for `Trigger3D.ExitCount` — number of bodies that
///        crossed out of the trigger volume during the most recent
///        `Update`.
///
/// Silent stub returning `0`.
///
/// @param t Trigger3D handle (ignored).
///
/// @return `0`.
int64_t rt_trigger3d_get_exit_count(void *t) {
    (void)t;
    return 0;
}

/// @brief Stub for `Trigger3D.SetBounds` — reposition / resize the
///        trigger volume after creation. Useful for triggers that move
///        with a moving platform or scaling AOE.
///
/// Silent no-op stub.
///
/// @param t  Trigger3D handle (ignored).
/// @param x0 New min corner x (ignored).
/// @param y0 New min corner y (ignored).
/// @param z0 New min corner z (ignored).
/// @param x1 New max corner x (ignored).
/// @param y1 New max corner y (ignored).
/// @param z1 New max corner z (ignored).
void rt_trigger3d_set_bounds(
    void *t, double x0, double y0, double z0, double x1, double y1, double z1) {
    (void)t;
    (void)x0;
    (void)y0;
    (void)z0;
    (void)x1;
    (void)y1;
    (void)z1;
}

/* Camera shake/follow stubs */

/// @brief Stub for `Camera3D.Shake` — start a camera-shake effect.
///        Intensity `i` is the maximum offset magnitude (world units),
///        duration `d` is the total shake time (seconds), and decay `dc`
///        controls how quickly the intensity falls off.
///
/// Silent no-op stub. The real implementation uses Perlin-noise-driven
/// per-axis offsets that taper off over the duration.
///
/// @param c  Camera3D handle (ignored).
/// @param i  Initial intensity (ignored).
/// @param d  Total duration in seconds (ignored).
/// @param dc Decay rate, 0..1 (higher = faster falloff) (ignored).
void rt_camera3d_shake(void *c, double i, double d, double dc) {
    (void)c;
    (void)i;
    (void)d;
    (void)dc;
}

/// @brief Stub for `Camera3D.SmoothFollow` — third-person follow
///        controller. Tracks target Vec3 `t` at distance `d` behind, with
///        height offset `h`, smoothing speed `s`, advanced over `dt`
///        seconds. Camera looks at the target each tick.
///
/// Silent no-op stub.
///
/// @param c  Camera3D handle (ignored).
/// @param t  Vec3 follow target (ignored).
/// @param d  Distance behind target (ignored).
/// @param h  Height above target (ignored).
/// @param s  Smoothing speed (higher = snappier) (ignored).
/// @param dt Delta time in seconds (ignored).
void rt_camera3d_smooth_follow(void *c, void *t, double d, double h, double s, double dt) {
    (void)c;
    (void)t;
    (void)d;
    (void)h;
    (void)s;
    (void)dt;
}

/// @brief Stub for `Camera3D.SmoothLookAt` — would normally interpolate
///        the camera's view direction toward `t` over `dt` seconds at speed
///        `s` (good for cinematic camera handovers).
///
/// Silent no-op stub.
///
/// @param c  Camera3D handle (ignored).
/// @param t  Vec3 look-at target (ignored).
/// @param s  Smoothing speed; higher = snappier (ignored).
/// @param dt Delta time in seconds (ignored).
void rt_camera3d_smooth_look_at(void *c, void *t, double s, double dt) {
    (void)c;
    (void)t;
    (void)s;
    (void)dt;
}

/* Transform3D stubs */

/// @brief Stub for `Transform3D.New` — would normally allocate an identity
///        TRS transform (zero translation, identity quaternion, unit scale).
///
/// Silent stub returning NULL.
///
/// @return `NULL`.
void *rt_transform3d_new(void) {
    return NULL;
}

/// @brief Stub for `Transform3D.SetPosition` — overwrite the translation
///        component of the transform.
///
/// Silent no-op stub.
///
/// @param x Transform3D handle (ignored).
/// @param a Position x (ignored).
/// @param b Position y (ignored).
/// @param c Position z (ignored).
void rt_transform3d_set_position(void *x, double a, double b, double c) {
    (void)x;
    (void)a;
    (void)b;
    (void)c;
}

/// @brief Stub for `Transform3D.Position` — get the translation component
///        as a Vec3.
///
/// Silent stub returning NULL.
///
/// @param x Transform3D handle (ignored).
///
/// @return `NULL`.
void *rt_transform3d_get_position(void *x) {
    (void)x;
    return NULL;
}

/// @brief Stub for `Transform3D.SetRotation` — overwrite the rotation
///        component using a Quaternion handle.
///
/// Silent no-op stub. Use `SetEuler` for Euler-angle convenience.
///
/// @param x Transform3D handle (ignored).
/// @param q Quaternion handle (ignored).
void rt_transform3d_set_rotation(void *x, void *q) {
    (void)x;
    (void)q;
}

/// @brief Stub for `Transform3D.Rotation` — get the rotation component as
///        a Quaternion handle.
///
/// Silent stub returning NULL.
///
/// @param x Transform3D handle (ignored).
///
/// @return `NULL`.
void *rt_transform3d_get_rotation(void *x) {
    (void)x;
    return NULL;
}

/// @brief Stub for `Transform3D.SetEuler` — overwrite the rotation from
///        intrinsic XYZ Euler angles in radians (pitch, yaw, roll). The
///        real implementation converts to a quaternion internally.
///
/// Silent no-op stub.
///
/// @param x Transform3D handle (ignored).
/// @param p Pitch in radians (rotation around X) (ignored).
/// @param y Yaw in radians (rotation around Y) (ignored).
/// @param r Roll in radians (rotation around Z) (ignored).
void rt_transform3d_set_euler(void *x, double p, double y, double r) {
    (void)x;
    (void)p;
    (void)y;
    (void)r;
}

/// @brief Stub for `Transform3D.SetScale` — overwrite the per-axis scale
///        component.
///
/// Silent no-op stub. `(1, 1, 1)` is no-op (identity scale).
///
/// @param x Transform3D handle (ignored).
/// @param a Scale x (ignored).
/// @param b Scale y (ignored).
/// @param c Scale z (ignored).
void rt_transform3d_set_scale(void *x, double a, double b, double c) {
    (void)x;
    (void)a;
    (void)b;
    (void)c;
}

/// @brief Stub for `Transform3D.Scale` — get the scale component as a
///        Vec3.
///
/// Silent stub returning NULL.
///
/// @param x Transform3D handle (ignored).
///
/// @return `NULL`.
void *rt_transform3d_get_scale(void *x) {
    (void)x;
    return NULL;
}

/// @brief Stub for `Transform3D.Matrix` — get the composed 4x4
///        transformation matrix (TRS in column-major order).
///
/// Silent stub returning NULL.
///
/// @param x Transform3D handle (ignored).
///
/// @return `NULL`.
void *rt_transform3d_get_matrix(void *x) {
    (void)x;
    return NULL;
}

/// @brief Stub for `Transform3D.Translate` — additively translate by a
///        delta Vec3 (in world axes).
///
/// Silent no-op stub.
///
/// @param x Transform3D handle (ignored).
/// @param d Vec3 translation delta (ignored).
void rt_transform3d_translate(void *x, void *d) {
    (void)x;
    (void)d;
}

/// @brief Stub for `Transform3D.Rotate` — additively rotate by `ang`
///        radians around the axis Vec3 `a` (must be normalized).
///
/// Silent no-op stub.
///
/// @param x   Transform3D handle (ignored).
/// @param a   Vec3 rotation axis, normalized (ignored).
/// @param ang Rotation angle in radians (ignored).
void rt_transform3d_rotate(void *x, void *a, double ang) {
    (void)x;
    (void)a;
    (void)ang;
}

/// @brief Stub for `Transform3D.LookAt` — orient the transform so its
///        forward (-Z) axis points at world-space target `t`, with `u`
///        as the up reference.
///
/// Silent no-op stub.
///
/// @param x Transform3D handle (ignored).
/// @param t Vec3 look-at target (ignored).
/// @param u Vec3 up reference, typically `(0, 1, 0)` (ignored).
void rt_transform3d_look_at(void *x, void *t, void *u) {
    (void)x;
    (void)t;
    (void)u;
}

/* Path3D stubs */

/// @brief Stub for `Path3D.New` — would normally allocate an empty path
///        (waypoint list) ready to receive waypoints via `AddPoint`.
///
/// Silent stub returning NULL.
///
/// @return `NULL`.
void *rt_path3d_new(void) {
    return NULL;
}

/// @brief Stub for `Path3D.AddPoint` — append a waypoint to the path.
///
/// Silent no-op stub.
///
/// @param p Path3D handle (ignored).
/// @param v Vec3 waypoint position (ignored).
void rt_path3d_add_point(void *p, void *v) {
    (void)p;
    (void)v;
}

/// @brief Stub for `Path3D.PositionAt(t)` — sample the world-space
///        position along the path at parametric distance `t` (0 = start,
///        1 = end). Used by `PathFollower` and AI navigation.
///
/// Silent stub returning NULL.
///
/// @param p Path3D handle (ignored).
/// @param t Parametric distance, 0..1 (ignored).
///
/// @return `NULL`.
void *rt_path3d_get_position_at(void *p, double t) {
    (void)p;
    (void)t;
    return NULL;
}

/// @brief Stub for `Path3D.DirectionAt(t)` — sample the unit tangent
///        (forward direction) along the path at parametric distance `t`.
///
/// Silent stub returning NULL.
///
/// @param p Path3D handle (ignored).
/// @param t Parametric distance, 0..1 (ignored).
///
/// @return `NULL`.
void *rt_path3d_get_direction_at(void *p, double t) {
    (void)p;
    (void)t;
    return NULL;
}

/// @brief Stub for `Path3D.Length` — total arc length of the path,
///        computed by summing distances between consecutive waypoints.
///
/// Silent stub returning `0.0`.
///
/// @param p Path3D handle (ignored).
///
/// @return `0.0`.
double rt_path3d_get_length(void *p) {
    (void)p;
    return 0.0;
}

/// @brief Stub for `Path3D.PointCount` — number of waypoints in the path.
///
/// Silent stub returning `0`.
///
/// @param p Path3D handle (ignored).
///
/// @return `0`.
int64_t rt_path3d_get_point_count(void *p) {
    (void)p;
    return 0;
}

/// @brief Stub for `Path3D.SetLooping` — when enabled, sampling at
///        `t > 1.0` wraps around to the start of the path.
///
/// Silent no-op stub.
///
/// @param p Path3D handle (ignored).
/// @param l Non-zero to enable looping (ignored).
void rt_path3d_set_looping(void *p, int8_t l) {
    (void)p;
    (void)l;
}

/// @brief Remove all entries from the path3d.
void rt_path3d_clear(void *p) {
    (void)p;
}

/* InstanceBatch3D stubs */

/// @brief Stub for `InstanceBatch3D.New` — would normally create an
///        instanced-rendering batch that draws many copies of the same
///        Mesh3D + Material3D pair in a single GPU draw call. Each
///        instance has its own per-instance transform.
///
/// Silent stub returning NULL.
///
/// @param m  Mesh3D handle (the geometry to instance) (ignored).
/// @param mt Material3D handle (shared by all instances) (ignored).
///
/// @return `NULL`.
void *rt_instbatch3d_new(void *m, void *mt) {
    (void)m;
    (void)mt;
    return NULL;
}

/// @brief Stub for `InstanceBatch3D.Add` — append a new instance with
///        the given transform. Returns the assigned slot index implicitly
///        (insertion order). The batch grows as needed.
///
/// Silent no-op stub.
///
/// @param b InstanceBatch3D handle (ignored).
/// @param t Transform3D handle for this instance (ignored).
void rt_instbatch3d_add(void *b, void *t) {
    (void)b;
    (void)t;
}

/// @brief Stub for `InstanceBatch3D.Remove` — remove the instance at
///        slot `i` via swap-with-last (O(1) but reorders remaining
///        instances).
///
/// Silent no-op stub.
///
/// @param b InstanceBatch3D handle (ignored).
/// @param i Instance slot index (ignored).
void rt_instbatch3d_remove(void *b, int64_t i) {
    (void)b;
    (void)i;
}

/// @brief Stub for `InstanceBatch3D.Set` — overwrite the transform of
///        an existing instance at slot `i`. Used for animating instance
///        positions every frame (e.g., a cloud of bullets).
///
/// Silent no-op stub.
///
/// @param b InstanceBatch3D handle (ignored).
/// @param i Instance slot index (ignored).
/// @param t Transform3D handle for the new transform (ignored).
void rt_instbatch3d_set(void *b, int64_t i, void *t) {
    (void)b;
    (void)i;
    (void)t;
}

/// @brief Stub for `InstanceBatch3D.Clear` — remove all instances. The
///        underlying buffers are kept allocated for cheap re-fill.
///
/// Silent no-op stub.
///
/// @param b InstanceBatch3D handle (ignored).
void rt_instbatch3d_clear(void *b) {
    (void)b;
}

/// @brief Stub for `InstanceBatch3D.Count` — number of instances
///        currently in the batch.
///
/// Silent stub returning `0`.
///
/// @param b InstanceBatch3D handle (ignored).
///
/// @return `0`.
int64_t rt_instbatch3d_count(void *b) {
    (void)b;
    return 0;
}

/// @brief Stub for `Canvas3D.DrawInstanced` — render every instance in
///        the batch in a single GPU draw call (when GPU instancing is
///        available) or as N individual draws (software fallback).
///
/// Silent no-op stub.
///
/// @param c Canvas3D handle (ignored).
/// @param b InstanceBatch3D handle (ignored).
void rt_canvas3d_draw_instanced(void *c, void *b) {
    (void)c;
    (void)b;
}

/* Terrain3D stubs */

/// @brief Stub for `Terrain3D.New` — would normally allocate a `(w x d)`
///        terrain grid. Subsequent calls to `SetHeightmap` /
///        `GeneratePerlin` populate the heights; without those, the
///        terrain is flat at y=0.
///
/// Trapping stub: terrain is referenced by Canvas3D draw calls and
/// vegetation systems — a NULL return would crash later.
///
/// @param w Grid width in vertices (ignored).
/// @param d Grid depth in vertices (ignored).
///
/// @return Never returns normally.
void *rt_terrain3d_new(int64_t w, int64_t d) {
    (void)w;
    (void)d;
    rt_graphics_unavailable_("Terrain3D.New: graphics support not compiled in");
    return NULL;
}

/// @brief Stub for `Terrain3D.GeneratePerlin` — would normally fill the
///        heightmap from a PerlinNoise object with the given scale,
///        octave count, and persistence (per-octave amplitude falloff).
///        Native fast path that bypasses the Pixels intermediate.
///
/// Silent no-op stub.
///
/// @param t  Terrain3D handle (ignored).
/// @param p  PerlinNoise handle (ignored).
/// @param s  World-space scale of the noise lookup (ignored).
/// @param o  Octave count (ignored).
/// @param pe Per-octave persistence, 0..1 (ignored).
void rt_terrain3d_generate_perlin(void *t, void *p, double s, int64_t o, double pe) {
    (void)t;
    (void)p;
    (void)s;
    (void)o;
    (void)pe;
}

/// @brief Stub for `Terrain3D.SetLODDistances` — configure the near/far
///        distance thresholds for terrain chunk LOD switching. Chunks
///        within `n` use full resolution; chunks beyond `f` use the
///        coarsest resolution; chunks in between scale linearly.
///
/// Silent no-op stub.
///
/// @param t Terrain3D handle (ignored).
/// @param n Near LOD distance (ignored).
/// @param f Far LOD distance (ignored).
void rt_terrain3d_set_lod_distances(void *t, double n, double f) {
    (void)t;
    (void)n;
    (void)f;
}

/// @brief Stub for `Terrain3D.SetSkirtDepth` — height of the downward-
///        facing skirt triangles inserted at chunk edges to hide
///        T-junction cracks at LOD boundaries.
///
/// Silent no-op stub.
///
/// @param t Terrain3D handle (ignored).
/// @param d Skirt depth in world units below the lowest edge vertex (ignored).
void rt_terrain3d_set_skirt_depth(void *t, double d) {
    (void)t;
    (void)d;
}

/// @brief Stub for `Terrain3D.SetHeightmap` — would normally upload a
///        Pixels surface as the source heightmap. R+G channels combine
///        for 16-bit precision; the heightmap is sampled per vertex.
///
/// Silent no-op stub.
///
/// @param t Terrain3D handle (ignored).
/// @param p Pixels handle for the heightmap (ignored).
void rt_terrain3d_set_heightmap(void *t, void *p) {
    (void)t;
    (void)p;
}

/// @brief Stub for `Terrain3D.SetMaterial` — bind a Material3D used
///        when rendering the terrain. Often paired with a splat-map for
///        per-pixel layer blending.
///
/// Silent no-op stub.
///
/// @param t Terrain3D handle (ignored).
/// @param m Material3D handle (ignored).
void rt_terrain3d_set_material(void *t, void *m) {
    (void)t;
    (void)m;
}

/// @brief Stub for `Terrain3D.SetScale` — world-space scale factors
///        along each axis. `sy` controls the height range; `sx` and `sz`
///        control the planar footprint.
///
/// Silent no-op stub.
///
/// @param t  Terrain3D handle (ignored).
/// @param sx Scale along X (ignored).
/// @param sy Scale along Y (ignored).
/// @param sz Scale along Z (ignored).
void rt_terrain3d_set_scale(void *t, double sx, double sy, double sz) {
    (void)t;
    (void)sx;
    (void)sy;
    (void)sz;
}

/// @brief Stub for `Terrain3D.SetSplatMap` — bind a 4-channel RGBA
///        Pixels surface that controls per-pixel blending between up to
///        4 layer textures. R = layer 0 weight, G = layer 1, etc.
///
/// Silent no-op stub.
///
/// @param t Terrain3D handle (ignored).
/// @param p Pixels handle for the splat map (ignored).
void rt_terrain3d_set_splat_map(void *t, void *p) {
    (void)t;
    (void)p;
}

/// @brief Stub for `Terrain3D.SetLayerTexture` — bind a Pixels texture
///        as one of the 4 splat-map layers (`l = 0..3`). Each layer is
///        sampled independently with its own UV scale.
///
/// Silent no-op stub.
///
/// @param t Terrain3D handle (ignored).
/// @param l Layer index 0..3 (ignored).
/// @param p Pixels handle for the layer texture (ignored).
void rt_terrain3d_set_layer_texture(void *t, int64_t l, void *p) {
    (void)t;
    (void)l;
    (void)p;
}

/// @brief Stub for `Terrain3D.SetLayerScale` — UV tiling scale for a
///        splat layer. Higher = the layer texture tiles more times across
///        the terrain footprint.
///
/// Silent no-op stub.
///
/// @param t Terrain3D handle (ignored).
/// @param l Layer index 0..3 (ignored).
/// @param s UV tiling factor (ignored).
void rt_terrain3d_set_layer_scale(void *t, int64_t l, double s) {
    (void)t;
    (void)l;
    (void)s;
}

/// @brief Stub for `Terrain3D.HeightAt` — sample the terrain surface
///        height at world-space `(x, z)`. Bilinear interpolation between
///        the 4 nearest grid vertices.
///
/// Silent stub returning `0.0` (flat ground default).
///
/// @param t Terrain3D handle (ignored).
/// @param x World-space X (ignored).
/// @param z World-space Z (ignored).
///
/// @return `0.0`.
double rt_terrain3d_get_height_at(void *t, double x, double z) {
    (void)t;
    (void)x;
    (void)z;
    return 0.0;
}

/// @brief Stub for `Terrain3D.NormalAt` — sample the terrain surface
///        normal at world-space `(x, z)` as a Vec3.
///
/// Silent stub returning NULL.
///
/// @param t Terrain3D handle (ignored).
/// @param x World-space X (ignored).
/// @param z World-space Z (ignored).
///
/// @return `NULL`.
void *rt_terrain3d_get_normal_at(void *t, double x, double z) {
    (void)t;
    (void)x;
    (void)z;
    return NULL;
}

/// @brief Stub for `Canvas3D.DrawTerrain` — render the Terrain3D using
///        LOD selection, frustum culling, splat-map sampling, and skirt
///        triangles.
///
/// Silent no-op stub.
///
/// @param c Canvas3D handle (ignored).
/// @param t Terrain3D handle (ignored).
void rt_canvas3d_draw_terrain(void *c, void *t) {
    (void)c;
    (void)t;
}

/* NavMesh3D stubs */

/// @brief Stub for `NavMesh3D.Build` — would normally bake a navmesh
///        from the given Mesh3D, accounting for the agent's collision
///        cylinder (radius `r`, height `h`) so all walkable polygons can
///        actually fit the agent.
///
/// Silent stub returning NULL.
///
/// @param m Source Mesh3D representing the world geometry (ignored).
/// @param r Agent collision radius (ignored).
/// @param h Agent collision height (ignored).
///
/// @return `NULL`.
void *rt_navmesh3d_build(void *m, double r, double h) {
    (void)m;
    (void)r;
    (void)h;
    return NULL;
}

/// @brief Stub for `NavMesh3D.FindPath` — would normally run an A*
///        path query from world position `f` to world position `t`,
///        returning a Path3D-like object representing the corridor.
///
/// Silent stub returning NULL.
///
/// @param n NavMesh3D handle (ignored).
/// @param f Vec3 path start (ignored).
/// @param t Vec3 path target (ignored).
///
/// @return `NULL`.
void *rt_navmesh3d_find_path(void *n, void *f, void *t) {
    (void)n;
    (void)f;
    (void)t;
    return NULL;
}

/// @brief Stub for `NavMesh3D.SamplePosition` — would normally project
///        an arbitrary world-space point onto the nearest walkable navmesh
///        polygon, returning the snapped position as a Vec3 (or NULL when
///        no polygon is within the search radius).
///
/// Silent stub returning NULL.
///
/// @param n NavMesh3D handle (ignored).
/// @param p Vec3 query position (ignored).
///
/// @return `NULL`.
void *rt_navmesh3d_sample_position(void *n, void *p) {
    (void)n;
    (void)p;
    return NULL;
}

/// @brief Stub for `NavMesh3D.IsWalkable` — boolean test for whether the
///        given position lies on a walkable polygon (within tolerance).
///
/// Silent stub returning `0`.
///
/// @param n NavMesh3D handle (ignored).
/// @param p Vec3 query position (ignored).
///
/// @return `0`.
int8_t rt_navmesh3d_is_walkable(void *n, void *p) {
    (void)n;
    (void)p;
    return 0;
}

/// @brief Stub for `NavMesh3D.TriangleCount` — number of walkable
///        triangles in the baked navmesh.
///
/// Silent stub returning `0`.
///
/// @param n NavMesh3D handle (ignored).
///
/// @return `0`.
int64_t rt_navmesh3d_get_triangle_count(void *n) {
    (void)n;
    return 0;
}

/// @brief Stub for `NavMesh3D.SetMaxSlope` — maximum slope (in radians)
///        a triangle can have and still be considered walkable. Steeper
///        triangles are excluded from the navmesh during bake.
///
/// Silent no-op stub.
///
/// @param n NavMesh3D handle (ignored).
/// @param d Max slope in radians (ignored).
void rt_navmesh3d_set_max_slope(void *n, double d) {
    (void)n;
    (void)d;
}

/// @brief Stub for `NavMesh3D.DebugDraw` — would normally render the
///        navmesh as a wireframe overlay on the given Canvas3D for visual
///        debugging.
///
/// Silent no-op stub.
///
/// @param n NavMesh3D handle (ignored).
/// @param c Canvas3D handle (ignored).
void rt_navmesh3d_debug_draw(void *n, void *c) {
    (void)n;
    (void)c;
}

/// @brief Stub for `NavMesh3D.CopyPathPoints` — internal helper for
///        path-finding that copies the smoothed path from `f` to `t` into
///        a freshly malloc'd `(x, y, z)` array. Returns waypoint count.
///
/// Silent stub: writes NULL to `*out_points_xyz` and returns `0` (no path).
///
/// @param n              NavMesh3D handle (ignored).
/// @param f              Vec3 path start (ignored).
/// @param t              Vec3 path target (ignored).
/// @param out_points_xyz Out-param receiving a malloc'd array; set to NULL.
///
/// @return `0`.
int64_t rt_navmesh3d_copy_path_points(void *n, void *f, void *t, double **out_points_xyz) {
    (void)n;
    (void)f;
    (void)t;
    if (out_points_xyz)
        *out_points_xyz = NULL;
    return 0;
}

/* NavAgent3D stubs */

/// @brief Stub for `NavAgent3D.New` — would normally create an
///        autonomous pathfinding agent with the given collision cylinder
///        (radius `r`, height `h`) bound to the given NavMesh3D.
///
/// Silent stub returning NULL.
///
/// @param n NavMesh3D handle (ignored).
/// @param r Agent collision radius in world units (ignored).
/// @param h Agent collision height in world units (ignored).
///
/// @return `NULL`.
void *rt_navagent3d_new(void *n, double r, double h) {
    (void)n;
    (void)r;
    (void)h;
    return NULL;
}

/// @brief Stub for `NavAgent3D.SetTarget` — request a new path to the
///        given world-space destination. Triggers an A* query at the next
///        `Update` tick.
///
/// Silent no-op stub.
///
/// @param a NavAgent3D handle (ignored).
/// @param p Vec3 destination (ignored).
void rt_navagent3d_set_target(void *a, void *p) {
    (void)a;
    (void)p;
}

/// @brief Stub for `NavAgent3D.ClearTarget` — abandon the current
///        destination. The agent stops at its current position.
///
/// Silent no-op stub.
///
/// @param a NavAgent3D handle (ignored).
void rt_navagent3d_clear_target(void *a) {
    (void)a;
}

/// @brief Stub for `NavAgent3D.Update` — advance the agent by `dt`
///        seconds: re-evaluate steering toward the next corridor waypoint,
///        integrate motion, and detect arrival at the destination.
///
/// Silent no-op stub.
///
/// @param a  NavAgent3D handle (ignored).
/// @param dt Delta time in seconds (ignored).
void rt_navagent3d_update(void *a, double dt) {
    (void)a;
    (void)dt;
}

/// @brief Stub for `NavAgent3D.Warp` — teleport the agent to `p` without
///        running steering / collision response. Use for spawn / respawn.
///
/// Silent no-op stub.
///
/// @param a NavAgent3D handle (ignored).
/// @param p Vec3 destination (ignored).
void rt_navagent3d_warp(void *a, void *p) {
    (void)a;
    (void)p;
}

/// @brief Stub for `NavAgent3D.Position` — get the agent's current
///        world-space position as a Vec3.
///
/// Silent stub returning NULL.
///
/// @param a NavAgent3D handle (ignored).
///
/// @return `NULL`.
void *rt_navagent3d_get_position(void *a) {
    (void)a;
    return NULL;
}

/// @brief Stub for `NavAgent3D.Velocity` — get the agent's current
///        velocity vector as a Vec3 (post-steering, post-clamp).
///
/// Silent stub returning NULL.
///
/// @param a NavAgent3D handle (ignored).
///
/// @return `NULL`.
void *rt_navagent3d_get_velocity(void *a) {
    (void)a;
    return NULL;
}

/// @brief Stub for `NavAgent3D.DesiredVelocity` — get the velocity the
///        steering controller wants this tick, before clamping by max-speed
///        / collision response. Useful for animation blending.
///
/// Silent stub returning NULL.
///
/// @param a NavAgent3D handle (ignored).
///
/// @return `NULL`.
void *rt_navagent3d_get_desired_velocity(void *a) {
    (void)a;
    return NULL;
}

/// @brief Stub for `NavAgent3D.HasPath` — true while the agent has a
///        valid path to its target and is steering along it.
///
/// Silent stub returning `0`.
///
/// @param a NavAgent3D handle (ignored).
///
/// @return `0`.
int8_t rt_navagent3d_get_has_path(void *a) {
    (void)a;
    return 0;
}

/// @brief Stub for `NavAgent3D.RemainingDistance` — distance along the
///        current corridor from the agent's position to the target.
///
/// Silent stub returning `0.0`.
///
/// @param a NavAgent3D handle (ignored).
///
/// @return `0.0`.
double rt_navagent3d_get_remaining_distance(void *a) {
    (void)a;
    return 0.0;
}

/// @brief Stub for `NavAgent3D.StoppingDistance` — get the radius around
///        the destination at which the agent declares itself "arrived"
///        and stops steering.
///
/// Silent stub returning `0.0`.
///
/// @param a NavAgent3D handle (ignored).
///
/// @return `0.0`.
double rt_navagent3d_get_stopping_distance(void *a) {
    (void)a;
    return 0.0;
}

/// @brief Stub for `NavAgent3D.SetStoppingDistance` — adjust the arrival
///        radius.
///
/// Silent no-op stub.
///
/// @param a NavAgent3D handle (ignored).
/// @param d Stopping distance in world units (ignored).
void rt_navagent3d_set_stopping_distance(void *a, double d) {
    (void)a;
    (void)d;
}

/// @brief Stub for `NavAgent3D.DesiredSpeed` — get the agent's
///        nominal-cruise speed in world units per second. Steering targets
///        this magnitude when far from the destination.
///
/// Silent stub returning `0.0`.
///
/// @param a NavAgent3D handle (ignored).
///
/// @return `0.0`.
double rt_navagent3d_get_desired_speed(void *a) {
    (void)a;
    return 0.0;
}

/// @brief Stub for `NavAgent3D.SetDesiredSpeed` — adjust the nominal
///        cruise speed.
///
/// Silent no-op stub.
///
/// @param a NavAgent3D handle (ignored).
/// @param s Speed in world units per second (ignored).
void rt_navagent3d_set_desired_speed(void *a, double s) {
    (void)a;
    (void)s;
}

/// @brief Stub for `NavAgent3D.AutoRepath` — get the auto-repath flag.
///        When enabled, the agent re-runs A* if the current path becomes
///        invalid (e.g. dynamic obstacle blocked it).
///
/// Silent stub returning `0`.
///
/// @param a NavAgent3D handle (ignored).
///
/// @return `0`.
int8_t rt_navagent3d_get_auto_repath(void *a) {
    (void)a;
    return 0;
}

/// @brief Stub for `NavAgent3D.SetAutoRepath` — enable or disable
///        automatic path recomputation on path invalidation.
///
/// Silent no-op stub.
///
/// @param a NavAgent3D handle (ignored).
/// @param e Non-zero to enable auto-repath (ignored).
void rt_navagent3d_set_auto_repath(void *a, int8_t e) {
    (void)a;
    (void)e;
}

/// @brief Stub for `NavAgent3D.BindCharacter` — attach a CharacterController
///        so the agent's steering output drives the character's locomotion
///        (recommended for production over raw `Update` polling).
///
/// Silent no-op stub.
///
/// @param a NavAgent3D handle (ignored).
/// @param c CharacterController3D handle (ignored).
void rt_navagent3d_bind_character(void *a, void *c) {
    (void)a;
    (void)c;
}

/// @brief Stub for `NavAgent3D.BindNode` — attach a SceneNode3D so the
///        agent's position drives the node's transform each frame.
///        Convenience binding that avoids manual `Position`/`SetPosition`
///        calls every tick.
///
/// Silent no-op stub.
///
/// @param a NavAgent3D handle (ignored).
/// @param n SceneNode3D handle, or NULL to detach (ignored).
void rt_navagent3d_bind_node(void *a, void *n) {
    (void)a;
    (void)n;
}

/* AnimBlend3D stubs */

/// @brief Stub for `AnimBlend3D.New` — would normally create a
///        weighted-blend animation tree for the given Skeleton3D.
///        Distinct from `AnimController3D` (which switches between
///        discrete states); blends combine multiple animations
///        simultaneously with per-state weights.
///
/// Silent stub returning NULL.
///
/// @param s Skeleton3D handle (ignored).
///
/// @return `NULL`.
void *rt_anim_blend3d_new(void *s) {
    (void)s;
    return NULL;
}

/// @brief Stub for `AnimBlend3D.AddState` — register a named state
///        backed by an Animation3D. Returns the assigned state index.
///
/// Silent stub returning `-1`.
///
/// @param b AnimBlend3D handle (ignored).
/// @param n State name (ignored).
/// @param a Animation3D handle (ignored).
///
/// @return `-1`.
int64_t rt_anim_blend3d_add_state(void *b, rt_string n, void *a) {
    (void)b;
    (void)n;
    (void)a;
    return -1;
}

/// @brief Stub for `AnimBlend3D.SetWeight` — set the contribution of
///        state `s` to the final pose. Weights across all states should
///        sum to 1.0 for normalized blending; the renderer doesn't
///        enforce this.
///
/// Silent no-op stub.
///
/// @param b AnimBlend3D handle (ignored).
/// @param s State index from `AddState` (ignored).
/// @param w Blend weight 0..1 (ignored).
void rt_anim_blend3d_set_weight(void *b, int64_t s, double w) {
    (void)b;
    (void)s;
    (void)w;
}

/// @brief Stub for `AnimBlend3D.SetWeightByName` — convenience wrapper
///        around `SetWeight` that looks up the state index by name.
///
/// Silent no-op stub.
///
/// @param b AnimBlend3D handle (ignored).
/// @param n State name (ignored).
/// @param w Blend weight 0..1 (ignored).
void rt_anim_blend3d_set_weight_by_name(void *b, rt_string n, double w) {
    (void)b;
    (void)n;
    (void)w;
}

/// @brief Stub for `AnimBlend3D.Weight` — get the current blend weight
///        of state `s`.
///
/// Silent stub returning `0.0`.
///
/// @param b AnimBlend3D handle (ignored).
/// @param s State index (ignored).
///
/// @return `0.0`.
double rt_anim_blend3d_get_weight(void *b, int64_t s) {
    (void)b;
    (void)s;
    return 0.0;
}

/// @brief Stub for `AnimBlend3D.SetSpeed` — per-state playback speed
///        multiplier. Each state in the blend tree advances independently
///        at its own rate; useful for blending walk/run cycles whose
///        natural durations differ.
///
/// Silent no-op stub.
///
/// @param b  AnimBlend3D handle (ignored).
/// @param s  State index (ignored).
/// @param sp Speed multiplier (ignored).
void rt_anim_blend3d_set_speed(void *b, int64_t s, double sp) {
    (void)b;
    (void)s;
    (void)sp;
}

/// @brief Stub for `AnimBlend3D.Update` — advance every state's playback
///        clock by `dt` (scaled by the state's per-state speed) and
///        recompute the blended pose.
///
/// Silent no-op stub.
///
/// @param b  AnimBlend3D handle (ignored).
/// @param dt Delta time in seconds (ignored).
void rt_anim_blend3d_update(void *b, double dt) {
    (void)b;
    (void)dt;
}

/// @brief Stub for `AnimBlend3D.StateCount` — number of registered
///        blend states.
///
/// Silent stub returning `0`.
///
/// @param b AnimBlend3D handle (ignored).
///
/// @return `0`.
int64_t rt_anim_blend3d_state_count(void *b) {
    (void)b;
    return 0;
}

/// @brief Stub for `Canvas3D.DrawMeshBlended` — variant of `DrawMesh`
///        that uses an AnimBlend3D's blended pose for skinning instead
///        of a single AnimPlayer3D's pose.
///
/// Silent no-op stub.
///
/// @param c  Canvas3D handle (ignored).
/// @param m  Mesh3D handle (ignored).
/// @param t  Transform3D handle (ignored).
/// @param mt Material3D handle (ignored).
/// @param bl AnimBlend3D handle providing the blended pose (ignored).
void rt_canvas3d_draw_mesh_blended(void *c, void *m, void *t, void *mt, void *bl) {
    (void)c;
    (void)m;
    (void)t;
    (void)mt;
    (void)bl;
}

/* AnimController3D stubs */

/// @brief Stub for `AnimController3D.New` — would normally create a
///        named-state animation controller bound to the given Skeleton3D.
///        States hold Animation3D references; transitions define crossfade
///        durations between states.
///
/// Trapping stub: controllers are bound to scene nodes via `BindAnimator`
/// and would crash later if NULL.
///
/// @param s Skeleton3D handle (ignored).
///
/// @return Never returns normally.
void *rt_anim_controller3d_new(void *s) {
    (void)s;
    rt_graphics_unavailable_("AnimController3D.New: graphics support not compiled in");
    return NULL;
}

/// @brief Stub for `AnimController3D.AddState` — register a named state
///        backed by an Animation3D. Returns the assigned state index, or
///        `-1` on failure (duplicate name, NULL animation).
///
/// Silent stub returning `-1`.
///
/// @param c AnimController3D handle (ignored).
/// @param n State name (ignored).
/// @param a Animation3D handle (ignored).
///
/// @return `-1`.
int64_t rt_anim_controller3d_add_state(void *c, rt_string n, void *a) {
    (void)c;
    (void)n;
    (void)a;
    return -1;
}

/// @brief Stub for `AnimController3D.AddTransition` — define a named
///        transition between states `f` and `t` with crossfade duration
///        `d` seconds. Used so `Crossfade(name)` knows how long to blend.
///
/// Silent stub returning `0` (failure).
///
/// @param c AnimController3D handle (ignored).
/// @param f From-state name (ignored).
/// @param t To-state name (ignored).
/// @param d Crossfade duration in seconds (ignored).
///
/// @return `0`.
int8_t rt_anim_controller3d_add_transition(void *c, rt_string f, rt_string t, double d) {
    (void)c;
    (void)f;
    (void)t;
    (void)d;
    return 0;
}

/// @brief Stub for `AnimController3D.Play` — switch immediately to the
///        named state (no crossfade, instant pose snap). Returns 1 on
///        success, 0 if the state name is unknown.
///
/// Silent stub returning `0`.
///
/// @param c AnimController3D handle (ignored).
/// @param n State name (ignored).
///
/// @return `0`.
int8_t rt_anim_controller3d_play(void *c, rt_string n) {
    (void)c;
    (void)n;
    return 0;
}

/// @brief Stub for `AnimController3D.Crossfade` — blend into the named
///        state over `d` seconds. The previous state continues to drive
///        the pose (with diminishing weight) until the blend completes.
///
/// Silent stub returning `0`.
///
/// @param c AnimController3D handle (ignored).
/// @param n Target state name (ignored).
/// @param d Crossfade duration in seconds (ignored).
///
/// @return `0`.
int8_t rt_anim_controller3d_crossfade(void *c, rt_string n, double d) {
    (void)c;
    (void)n;
    (void)d;
    return 0;
}

/// @brief Stub for `AnimController3D.Stop` — halt animation playback.
///        The skeleton freezes at the current pose.
///
/// Silent no-op stub.
///
/// @param c AnimController3D handle (ignored).
void rt_anim_controller3d_stop(void *c) {
    (void)c;
}

/// @brief Stub for `AnimController3D.Update` — advance the controller
///        by `dt` seconds: progress the active state's animation,
///        advance crossfade blend, fire event-frame callbacks.
///
/// Silent no-op stub.
///
/// @param c  AnimController3D handle (ignored).
/// @param dt Delta time in seconds (ignored).
void rt_anim_controller3d_update(void *c, double dt) {
    (void)c;
    (void)dt;
}

/// @brief Stub for `AnimController3D.CurrentState` — get the name of
///        the state currently driving the pose. During a crossfade this
///        is the destination state.
///
/// Silent stub returning NULL.
///
/// @param c AnimController3D handle (ignored).
///
/// @return `NULL`.
rt_string rt_anim_controller3d_get_current_state(void *c) {
    (void)c;
    return NULL;
}

/// @brief Stub for `AnimController3D.PreviousState` — get the name of
///        the state that was previously active (the source of the most
///        recent crossfade).
///
/// Silent stub returning NULL.
///
/// @param c AnimController3D handle (ignored).
///
/// @return `NULL`.
rt_string rt_anim_controller3d_get_previous_state(void *c) {
    (void)c;
    return NULL;
}

/// @brief Stub for `AnimController3D.IsTransitioning` — true while a
///        crossfade is in progress (between `Crossfade` start and the
///        end of its duration).
///
/// Silent stub returning `0`.
///
/// @param c AnimController3D handle (ignored).
///
/// @return `0`.
int8_t rt_anim_controller3d_get_is_transitioning(void *c) {
    (void)c;
    return 0;
}

/// @brief Stub for `AnimController3D.StateCount` — number of registered
///        states.
///
/// Silent stub returning `0`.
///
/// @param c AnimController3D handle (ignored).
///
/// @return `0`.
int64_t rt_anim_controller3d_get_state_count(void *c) {
    (void)c;
    return 0;
}

/// @brief Stub for `AnimController3D.SetStateSpeed` — per-state playback
///        speed multiplier. `1.0` is normal; values <1 slow the state
///        down, >1 speed it up.
///
/// Silent no-op stub.
///
/// @param c AnimController3D handle (ignored).
/// @param n State name (ignored).
/// @param s Speed multiplier (ignored).
void rt_anim_controller3d_set_state_speed(void *c, rt_string n, double s) {
    (void)c;
    (void)n;
    (void)s;
}

/// @brief Stub for `AnimController3D.SetStateLooping` — per-state
///        looping flag. Disabled states play once and stop at the last
///        frame; enabled states wrap around.
///
/// Silent no-op stub.
///
/// @param c AnimController3D handle (ignored).
/// @param n State name (ignored).
/// @param l Non-zero to enable looping (ignored).
void rt_anim_controller3d_set_state_looping(void *c, rt_string n, int8_t l) {
    (void)c;
    (void)n;
    (void)l;
}

/// @brief Stub for `AnimController3D.AddEvent` — register a tagged
///        event frame `e` at time `t` within state `s`. When playback
///        crosses time `t`, the event is queued for `PollEvent`.
///
/// Silent no-op stub. Used for triggering footstep SFX, weapon-swing
/// hit windows, particle spawns synced to animation.
///
/// @param c AnimController3D handle (ignored).
/// @param s State name (ignored).
/// @param t Event time within state in seconds (ignored).
/// @param e Event tag string (ignored).
void rt_anim_controller3d_add_event(void *c, rt_string s, double t, rt_string e) {
    (void)c;
    (void)s;
    (void)t;
    (void)e;
}

/// @brief Stub for `AnimController3D.PollEvent` — dequeue the next
///        pending event tag, or NULL if no events have fired since the
///        last call. Drains one event at a time so callers can loop.
///
/// Silent stub returning NULL.
///
/// @param c AnimController3D handle (ignored).
///
/// @return `NULL`.
rt_string rt_anim_controller3d_poll_event(void *c) {
    (void)c;
    return NULL;
}

/// @brief Stub for `AnimController3D.SetRootMotionBone` — designate a
///        bone whose displacement is tracked separately as "root motion"
///        rather than being applied to the rendered pose. Use for
///        animation-driven character locomotion.
///
/// Silent no-op stub.
///
/// @param c AnimController3D handle (ignored).
/// @param b Bone index in the bound skeleton (ignored).
void rt_anim_controller3d_set_root_motion_bone(void *c, int64_t b) {
    (void)c;
    (void)b;
}

/// @brief Stub for `AnimController3D.RootMotionDelta` — get the
///        accumulated root-motion translation since the last
///        `ConsumeRootMotion` call as a Vec3.
///
/// Silent stub returning NULL.
///
/// @param c AnimController3D handle (ignored).
///
/// @return `NULL`.
void *rt_anim_controller3d_get_root_motion_delta(void *c) {
    (void)c;
    return NULL;
}

/// @brief Stub for `AnimController3D.ConsumeRootMotion` — read and
///        zero the root-motion accumulator in one operation. Pattern:
///        gameplay code calls this once per tick to translate the
///        character body, then continues using the bone-driven pose.
///
/// Silent stub returning NULL.
///
/// @param c AnimController3D handle (ignored).
///
/// @return `NULL`.
void *rt_anim_controller3d_consume_root_motion(void *c) {
    (void)c;
    return NULL;
}

/// @brief Stub for `AnimController3D.SetLayerWeight` — per-layer blend
///        weight, 0..1. Layers compose additively so layer 0 is typically
///        "full body" at weight 1.0 and additional layers are partial-
///        body overlays (e.g. upper-body shoot pose blended over locomotion).
///
/// Silent no-op stub.
///
/// @param c AnimController3D handle (ignored).
/// @param l Layer index, 0..LayerCount-1 (ignored).
/// @param w Layer weight, 0..1 (ignored).
void rt_anim_controller3d_set_layer_weight(void *c, int64_t l, double w) {
    (void)c;
    (void)l;
    (void)w;
}

/// @brief Stub for `AnimController3D.SetLayerMask` — per-layer bone
///        bitmask. Only bones whose index is set in `b` are affected
///        by this layer — useful for upper-body / lower-body splits.
///
/// Silent no-op stub.
///
/// @param c AnimController3D handle (ignored).
/// @param l Layer index (ignored).
/// @param b Bitmask of affected bone indices (ignored).
void rt_anim_controller3d_set_layer_mask(void *c, int64_t l, int64_t b) {
    (void)c;
    (void)l;
    (void)b;
}

/// @brief Stub for `AnimController3D.PlayLayer` — instantly switch
///        layer `l` to state `s`. Per-layer `Play` allows independent
///        upper-body and lower-body animations.
///
/// Silent stub returning `0` (failure).
///
/// @param c AnimController3D handle (ignored).
/// @param l Layer index, 0..LayerCount-1 (ignored).
/// @param s State name (ignored).
///
/// @return `0`.
int8_t rt_anim_controller3d_play_layer(void *c, int64_t l, rt_string s) {
    (void)c;
    (void)l;
    (void)s;
    return 0;
}

/// @brief Stub for `AnimController3D.CrossfadeLayer` — blend layer `l`
///        toward state `s` over `d` seconds. Each layer maintains its
///        own crossfade clock independent of other layers.
///
/// Silent stub returning `0`.
///
/// @param c AnimController3D handle (ignored).
/// @param l Layer index (ignored).
/// @param s Target state name (ignored).
/// @param d Crossfade duration in seconds (ignored).
///
/// @return `0`.
int8_t rt_anim_controller3d_crossfade_layer(void *c, int64_t l, rt_string s, double d) {
    (void)c;
    (void)l;
    (void)s;
    (void)d;
    return 0;
}

/// @brief Stub for `AnimController3D.StopLayer` — halt animation in
///        the given layer. Bones masked into this layer freeze at their
///        current pose contribution.
///
/// Silent no-op stub.
///
/// @param c AnimController3D handle (ignored).
/// @param l Layer index (ignored).
void rt_anim_controller3d_stop_layer(void *c, int64_t l) {
    (void)c;
    (void)l;
}

/// @brief Stub for `AnimController3D.BoneMatrix(i)` — get the world-
///        space matrix for bone `i` after blending all active layers.
///        Used by the renderer to compute final per-vertex skinning.
///
/// Silent stub returning NULL.
///
/// @param c AnimController3D handle (ignored).
/// @param i Bone index (ignored).
///
/// @return `NULL`.
void *rt_anim_controller3d_get_bone_matrix(void *c, int64_t i) {
    (void)c;
    (void)i;
    return NULL;
}

/// @brief Stub for the C accessor returning the controller's final
///        bone palette (column-major float matrices, one per bone) used
///        by the GPU vertex shader for skinning.
///
/// Silent stub: writes `0` to `*bone_count` and returns NULL.
///
/// @param c          AnimController3D handle (ignored).
/// @param bone_count Out-param receiving the bone count; defaults to `0`.
///
/// @return `NULL`.
const float *rt_anim_controller3d_get_final_palette_data(void *c, int32_t *bone_count) {
    (void)c;
    if (bone_count)
        *bone_count = 0;
    return NULL;
}

/// @brief Stub for the C accessor returning the previous-frame bone
///        palette (used by motion-blur post-FX to compute per-vertex
///        motion vectors).
///
/// Silent stub: writes `0` to `*bone_count` and returns NULL.
///
/// @param c          AnimController3D handle (ignored).
/// @param bone_count Out-param receiving the bone count; defaults to `0`.
///
/// @return `NULL`.
const float *rt_anim_controller3d_get_previous_palette_data(void *c, int32_t *bone_count) {
    (void)c;
    if (bone_count)
        *bone_count = 0;
    return NULL;
}

/* Decal3D stubs */

/// @brief Stub for `Decal3D.New` — would normally create a projected
///        texture decal at world position `p` with normal `n`, world-space
///        size `s`, and texture `t`. Used for bullet holes, paint splats,
///        AOE indicators.
///
/// Silent stub returning NULL.
///
/// @param p Vec3 world-space position (ignored).
/// @param n Vec3 surface normal (must be normalized) (ignored).
/// @param s Decal world-space size (ignored).
/// @param t Pixels handle for the decal texture (ignored).
///
/// @return `NULL`.
void *rt_decal3d_new(void *p, void *n, double s, void *t) {
    (void)p;
    (void)n;
    (void)s;
    (void)t;
    return NULL;
}

/// @brief Stub for `Decal3D.SetLifetime` — set the decal's remaining
///        lifetime in seconds. After expiry the decal is no longer rendered
///        and `IsExpired` returns true.
///
/// Silent no-op stub. `s = 0` means infinite (persistent decal).
///
/// @param d Decal3D handle (ignored).
/// @param s Lifetime in seconds (ignored).
void rt_decal3d_set_lifetime(void *d, double s) {
    (void)d;
    (void)s;
}

/// @brief Stub for `Decal3D.Update` — advance the decal's age by `dt`
///        seconds. Should be called once per frame.
///
/// Silent no-op stub.
///
/// @param d  Decal3D handle (ignored).
/// @param dt Delta time in seconds (ignored).
void rt_decal3d_update(void *d, double dt) {
    (void)d;
    (void)dt;
}

/// @brief Stub for `Decal3D.IsExpired` — true once the decal's lifetime
///        has elapsed.
///
/// Silent stub returning `1` (expired) so caller-driven cleanup loops
/// don't accidentally retain decal handles forever in the headless build.
///
/// @param d Decal3D handle (ignored).
///
/// @return `1`.
int8_t rt_decal3d_is_expired(void *d) {
    (void)d;
    return 1;
}

/// @brief Stub for `Canvas3D.DrawDecal` — would normally render the
///        given Decal3D as a projected-texture pass on top of the scene.
///
/// Silent no-op stub.
///
/// @param c Canvas3D handle (ignored).
/// @param d Decal3D handle (ignored).
void rt_canvas3d_draw_decal(void *c, void *d) {
    (void)c;
    (void)d;
}

/* Sprite3D stubs */

/// @brief Stub for `Sprite3D.New` — would normally create a 3D billboard
///        sprite (always camera-facing) bound to the given Pixels texture.
///
/// Silent stub returning NULL.
///
/// @param t Pixels handle for the sprite texture (ignored).
///
/// @return `NULL`.
void *rt_sprite3d_new(void *t) {
    (void)t;
    return NULL;
}

/// @brief Stub for `Sprite3D.SetPosition` — set the sprite's world-space
///        position.
///
/// Silent no-op stub.
///
/// @param s Sprite3D handle (ignored).
/// @param x World x (ignored).
/// @param y World y (ignored).
/// @param z World z (ignored).
void rt_sprite3d_set_position(void *s, double x, double y, double z) {
    (void)s;
    (void)x;
    (void)y;
    (void)z;
}

/// @brief Stub for `Sprite3D.SetScale` — set the sprite's world-space
///        size in world units.
///
/// Silent no-op stub.
///
/// @param s Sprite3D handle (ignored).
/// @param w Width in world units (ignored).
/// @param h Height in world units (ignored).
void rt_sprite3d_set_scale(void *s, double w, double h) {
    (void)s;
    (void)w;
    (void)h;
}

/// @brief Stub for `Sprite3D.SetAnchor` — set the normalized anchor point
///        within the sprite quad. `(0.5, 0.5)` is centered; `(0.5, 1.0)` is
///        bottom-center (good for ground-anchored billboards).
///
/// Silent no-op stub.
///
/// @param s  Sprite3D handle (ignored).
/// @param ax Horizontal anchor, 0..1 (ignored).
/// @param ay Vertical anchor, 0..1 (ignored).
void rt_sprite3d_set_anchor(void *s, double ax, double ay) {
    (void)s;
    (void)ax;
    (void)ay;
}

/// @brief Stub for `Sprite3D.SetFrame` — select a sub-rectangle of the
///        bound texture as the visible frame (sprite-sheet animation).
///
/// Silent no-op stub.
///
/// @param s  Sprite3D handle (ignored).
/// @param fx Frame top-left x in texture pixels (ignored).
/// @param fy Frame top-left y in texture pixels (ignored).
/// @param fw Frame width in texture pixels (ignored).
/// @param fh Frame height in texture pixels (ignored).
void rt_sprite3d_set_frame(void *s, int64_t fx, int64_t fy, int64_t fw, int64_t fh) {
    (void)s;
    (void)fx;
    (void)fy;
    (void)fw;
    (void)fh;
}

/// @brief Stub for `Canvas3D.DrawSprite3D` — would normally render the
///        given Sprite3D as a camera-facing textured quad at its world
///        position.
///
/// Silent no-op stub.
///
/// @param c   Canvas3D handle (ignored).
/// @param s   Sprite3D handle (ignored).
/// @param cam Camera3D handle (used for billboard orientation) (ignored).
void rt_canvas3d_draw_sprite3d(void *c, void *s, void *cam) {
    (void)c;
    (void)s;
    (void)cam;
}

/* Water3D stubs */

/// @brief Stub for `Water3D.New` — would normally create a horizontal
///        water plane of the given world dimensions.
///
/// Silent stub returning NULL.
///
/// @param w Width along X in world units (ignored).
/// @param d Depth along Z in world units (ignored).
///
/// @return `NULL`.
void *rt_water3d_new(double w, double d) {
    (void)w;
    (void)d;
    return NULL;
}

/// @brief Stub for `Water3D.SetHeight` — set the water plane's Y position
///        (world-space sea level).
///
/// Silent no-op stub.
///
/// @param w Water3D handle (ignored).
/// @param y World-space Y for the water surface (ignored).
void rt_water3d_set_height(void *w, double y) {
    (void)w;
    (void)y;
}

/// @brief Stub for `Water3D.SetWaveParams` — legacy single-wave control.
///        Use `Water3D.AddWave` for the modern Gerstner multi-wave system.
///
/// Silent no-op stub.
///
/// @param w Water3D handle (ignored).
/// @param s Wave speed (ignored).
/// @param a Wave amplitude (ignored).
/// @param f Wave frequency (ignored).
void rt_water3d_set_wave_params(void *w, double s, double a, double f) {
    (void)w;
    (void)s;
    (void)a;
    (void)f;
}

/// @brief Stub for `Water3D.SetColor` — base water tint applied as a
///        multiplier over the lit surface.
///
/// Silent no-op stub.
///
/// @param w Water3D handle (ignored).
/// @param r Tint red, 0..1 (ignored).
/// @param g Tint green, 0..1 (ignored).
/// @param b Tint blue, 0..1 (ignored).
/// @param a Overall opacity, 0..1 (ignored).
void rt_water3d_set_color(void *w, double r, double g, double b, double a) {
    (void)w;
    (void)r;
    (void)g;
    (void)b;
    (void)a;
}

/// @brief Stub for `Water3D.SetTexture` — bind a Pixels surface as the
///        water's diffuse texture (typically tiled small ripples).
///
/// Silent no-op stub.
///
/// @param w Water3D handle (ignored).
/// @param p Pixels handle for the diffuse texture, or NULL (ignored).
void rt_water3d_set_texture(void *w, void *p) {
    (void)w;
    (void)p;
}

/// @brief Stub for `Water3D.SetNormalMap` — bind a tangent-space normal
///        map for water surface micro-perturbation. Combined with the
///        Gerstner deformation this gives detail at multiple scales.
///
/// Silent no-op stub.
///
/// @param w Water3D handle (ignored).
/// @param p Pixels handle for the normal map, or NULL (ignored).
void rt_water3d_set_normal_map(void *w, void *p) {
    (void)w;
    (void)p;
}

/// @brief Stub for `Water3D.SetEnvMap` — bind a CubeMap3D as the
///        environment map sampled by the water's reflection contribution.
///
/// Silent no-op stub. Combined with `SetReflectivity` to control mix.
///
/// @param w Water3D handle (ignored).
/// @param c CubeMap3D handle, or NULL (ignored).
void rt_water3d_set_env_map(void *w, void *c) {
    (void)w;
    (void)c;
}

/// @brief Stub for `Water3D.SetReflectivity` — fraction of the
///        environment map color blended into the surface (0 = no
///        reflection, 1 = mirror-like).
///
/// Silent no-op stub.
///
/// @param w Water3D handle (ignored).
/// @param r Reflectivity, 0..1 (ignored).
void rt_water3d_set_reflectivity(void *w, double r) {
    (void)w;
    (void)r;
}

/// @brief Stub for `Water3D.SetResolution` — grid density of the water
///        surface mesh. Higher values give finer wave detail at higher
///        rendering cost. Default 64; range 8..256.
///
/// Silent no-op stub.
///
/// @param w Water3D handle (ignored).
/// @param r Grid density per side (ignored).
void rt_water3d_set_resolution(void *w, int64_t r) {
    (void)w;
    (void)r;
}

/// @brief Stub for `Water3D.AddWave` — add a directional Gerstner wave
///        to the water simulation. Up to 8 waves can be summed; each
///        contributes per-direction sinusoidal displacement and
///        derivative-based normal perturbation.
///
/// Silent no-op stub.
///
/// @param w  Water3D handle (ignored).
/// @param dx Wave direction x component (ignored).
/// @param dz Wave direction z component (ignored).
/// @param s  Wave speed in world units / second (ignored).
/// @param a  Wave amplitude (peak-to-trough/2) (ignored).
/// @param wl Wavelength in world units (ignored).
void rt_water3d_add_wave(void *w, double dx, double dz, double s, double a, double wl) {
    (void)w;
    (void)dx;
    (void)dz;
    (void)s;
    (void)a;
    (void)wl;
}

/// @brief Stub for `Water3D.ClearWaves` — remove all Gerstner waves
///        previously added via `AddWave`. The surface returns to a
///        flat plane.
///
/// Silent no-op stub.
///
/// @param w Water3D handle (ignored).
void rt_water3d_clear_waves(void *w) {
    (void)w;
}

/// @brief Stub for `Water3D.Update` — advance the water simulation by
///        `dt` seconds: increment wave phase and rebuild the surface
///        mesh / normals.
///
/// Silent no-op stub.
///
/// @param w  Water3D handle (ignored).
/// @param dt Delta time in seconds (ignored).
void rt_water3d_update(void *w, double dt) {
    (void)w;
    (void)dt;
}

/// @brief Stub for `Canvas3D.DrawWater` — render the Water3D surface
///        with refraction, reflection (when env-map bound), and lighting.
///
/// Silent no-op stub.
///
/// @param c   Canvas3D handle (ignored).
/// @param w   Water3D handle (ignored).
/// @param cam Camera3D handle (ignored).
void rt_canvas3d_draw_water(void *c, void *w, void *cam) {
    (void)c;
    (void)w;
    (void)cam;
}

/* Vegetation3D stubs */

/// @brief Stub for `Vegetation3D.New` — would normally create an
///        instanced grass/foliage system bound to the given Pixels
///        texture (the per-blade billboard).
///
/// Silent stub returning NULL.
///
/// @param t Pixels handle for the blade texture (ignored).
///
/// @return `NULL`.
void *rt_vegetation3d_new(void *t) {
    (void)t;
    return NULL;
}

/// @brief Stub for `Vegetation3D.SetDensityMap` — bind a Pixels surface
///        whose red channel modulates spawn probability. Lets users paint
///        vegetation density (e.g., dense in valleys, sparse on hills).
///
/// Silent no-op stub.
///
/// @param v Vegetation3D handle (ignored).
/// @param p Pixels handle for the density map, or NULL for uniform (ignored).
void rt_vegetation3d_set_density_map(void *v, void *p) {
    (void)v;
    (void)p;
}

/// @brief Stub for `Vegetation3D.SetWindParams` — configure wind
///        animation: per-blade Y-axis shear via `sin(position + time)`.
///        Speed `s`, strength `st`, and turbulence `t` shape the motion.
///
/// Silent no-op stub.
///
/// @param v  Vegetation3D handle (ignored).
/// @param s  Wind speed (ignored).
/// @param st Wind strength (sway amplitude) (ignored).
/// @param t  Turbulence factor (ignored).
void rt_vegetation3d_set_wind_params(void *v, double s, double st, double t) {
    (void)v;
    (void)s;
    (void)st;
    (void)t;
}

/// @brief Stub for `Vegetation3D.SetLODDistances` — configure progressive
///        thinning between near distance `n` and far distance `f`. Beyond
///        `f` blades are hard-culled. The thinning is randomized per blade
///        so the visible density falls off smoothly.
///
/// Silent no-op stub.
///
/// @param v Vegetation3D handle (ignored).
/// @param n Near LOD distance (full density before this) (ignored).
/// @param f Far LOD distance (zero density beyond this) (ignored).
void rt_vegetation3d_set_lod_distances(void *v, double n, double f) {
    (void)v;
    (void)n;
    (void)f;
}

/// @brief Stub for `Vegetation3D.SetBladeSize` — per-blade quad
///        dimensions and randomized variance. Each blade is a
///        cross-billboard of two perpendicular `(w x h)` quads.
///
/// Silent no-op stub.
///
/// @param v  Vegetation3D handle (ignored).
/// @param w  Blade width in world units (ignored).
/// @param h  Blade height in world units (ignored).
/// @param va Variance fraction applied to size at spawn (ignored).
void rt_vegetation3d_set_blade_size(void *v, double w, double h, double va) {
    (void)v;
    (void)w;
    (void)h;
    (void)va;
}

/// @brief Stub for `Vegetation3D.Populate` — scatter `c` blade
///        instances across the surface of the given Terrain3D using LCG
///        random sampling, optionally filtered by the bound density map.
///
/// Silent no-op stub. Idempotent: re-running clears the previous
/// population first.
///
/// @param v Vegetation3D handle (ignored).
/// @param t Terrain3D handle providing the surface (ignored).
/// @param c Target instance count (ignored).
void rt_vegetation3d_populate(void *v, void *t, int64_t c) {
    (void)v;
    (void)t;
    (void)c;
}

/// @brief Stub for `Vegetation3D.Update` — per-frame update: advance
///        the wind animation phase by `dt` and update LOD selection
///        based on camera position `(cx, cy, cz)`.
///
/// Silent no-op stub.
///
/// @param v  Vegetation3D handle (ignored).
/// @param dt Delta time in seconds (ignored).
/// @param cx Camera x (ignored).
/// @param cy Camera y (ignored).
/// @param cz Camera z (ignored).
void rt_vegetation3d_update(void *v, double dt, double cx, double cy, double cz) {
    (void)v;
    (void)dt;
    (void)cx;
    (void)cy;
    (void)cz;
}

/// @brief Stub for `Canvas3D.DrawVegetation` — render every visible
///        blade in a single instanced GPU draw call (when the backend
///        supports `submit_draw_instanced`) or as N individual draws
///        on the software fallback.
///
/// Silent no-op stub.
///
/// @param c Canvas3D handle (ignored).
/// @param v Vegetation3D handle (ignored).
void rt_canvas3d_draw_vegetation(void *c, void *v) {
    (void)c;
    (void)v;
}

/* VideoWidget stubs */

/// @brief Stub for `VideoWidget.New` — would normally create a GUI
///        Image widget bound to a VideoPlayer that decodes the file at
///        `path`. The widget's pixels are refreshed each `Update` call
///        with the latest decoded video frame.
///
/// Silent stub returning NULL.
///
/// @param p    Parent widget handle (ignored).
/// @param path Filesystem path to the video file (ignored).
///
/// @return `NULL`.
void *rt_videowidget_new(void *p, void *path) {
    (void)p;
    (void)path;
    return NULL;
}

/// @brief Stub for `VideoWidget.Play` — start or resume video playback
///        (delegates to the embedded VideoPlayer).
///
/// Silent no-op stub.
///
/// @param v VideoWidget handle (ignored).
void rt_videowidget_play(void *v) {
    (void)v;
}

/// @brief Stub for `VideoWidget.Pause` — pause playback. The widget
///        continues to display the current frame.
///
/// Silent no-op stub.
///
/// @param v VideoWidget handle (ignored).
void rt_videowidget_pause(void *v) {
    (void)v;
}

/// @brief Stub for `VideoWidget.Stop` — stop playback and rewind to
///        frame 0. The widget displays the first frame (or a placeholder
///        if no frame decoded yet).
///
/// Silent no-op stub.
///
/// @param v VideoWidget handle (ignored).
void rt_videowidget_stop(void *v) {
    (void)v;
}

/// @brief Stub for `VideoWidget.Update` — advance video playback by
///        `dt` seconds and refresh the widget's image. Should be called
///        once per GUI frame (typically from the app's frame callback).
///
/// Silent no-op stub.
///
/// @param v  VideoWidget handle (ignored).
/// @param dt Delta time in seconds (ignored).
void rt_videowidget_update(void *v, double dt) {
    (void)v;
    (void)dt;
}

/// @brief Stub for `VideoWidget.SetShowControls` — toggle the overlay
///        playback controls (play/pause button, scrubber bar) inside
///        the widget bounds.
///
/// Silent no-op stub.
///
/// @param v VideoWidget handle (ignored).
/// @param s Non-zero to show controls (ignored).
void rt_videowidget_set_show_controls(void *v, int8_t s) {
    (void)v;
    (void)s;
}

/// @brief Stub for `VideoWidget.SetLoop` — when enabled, the widget
///        seeks back to frame 0 on EOF and continues playback (no
///        explicit `Play` call needed).
///
/// Silent no-op stub.
///
/// @param v VideoWidget handle (ignored).
/// @param l Non-zero to enable looping (ignored).
void rt_videowidget_set_loop(void *v, int8_t l) {
    (void)v;
    (void)l;
}

/// @brief Stub for `VideoWidget.SetVolume` — set the audio track's
///        playback volume, 0..1. Delegates to the embedded VideoPlayer.
///
/// Silent no-op stub.
///
/// @param v   VideoWidget handle (ignored).
/// @param vol Volume, 0..1 (ignored).
void rt_videowidget_set_volume(void *v, double vol) {
    (void)v;
    (void)vol;
}

/// @brief Stub for `VideoWidget.IsPlaying` — true while the widget's
///        embedded VideoPlayer is actively decoding frames.
///
/// Silent stub returning `0`.
///
/// @param v VideoWidget handle (ignored).
///
/// @return `0`.
int64_t rt_videowidget_get_is_playing(void *v) {
    (void)v;
    return 0;
}

/// @brief Stub for `VideoWidget.Position` — get the current playback
///        position in seconds.
///
/// Silent stub returning `0.0`.
///
/// @param v VideoWidget handle (ignored).
///
/// @return `0.0`.
double rt_videowidget_get_position(void *v) {
    (void)v;
    return 0.0;
}

/// @brief Stub for `VideoWidget.Duration` — get the total length of the
///        loaded video in seconds.
///
/// Silent stub returning `0.0`.
///
/// @param v VideoWidget handle (ignored).
///
/// @return `0.0`.
double rt_videowidget_get_duration(void *v) {
    (void)v;
    return 0.0;
}

/* VideoPlayer stubs */

/// @brief Stub for `VideoPlayer.Open` — would normally parse the video
///        file at `p` (.avi via MJPEG decoder, or .ogv via Theora
///        infrastructure) and return a player ready to `Play`.
///
/// Silent stub returning NULL.
///
/// @param p Filesystem path to the video file (ignored).
///
/// @return `NULL`.
void *rt_videoplayer_open(void *p) {
    (void)p;
    return NULL;
}

/// @brief Stub for `VideoPlayer.Play` — start or resume playback.
///        From a stopped state, restarts at frame 0; from a paused state,
///        resumes from the current position.
///
/// Silent no-op stub.
///
/// @param v VideoPlayer handle (ignored).
void rt_videoplayer_play(void *v) {
    (void)v;
}

/// @brief Stub for `VideoPlayer.Pause` — pause playback. Frames stop
///        decoding but the audio cursor and `Position` are preserved.
///        Resume with `Play`.
///
/// Silent no-op stub.
///
/// @param v VideoPlayer handle (ignored).
void rt_videoplayer_pause(void *v) {
    (void)v;
}

/// @brief Stub for `VideoPlayer.Stop` — stop playback and rewind to
///        frame 0. Drops any decoded frames in the queue.
///
/// Silent no-op stub.
///
/// @param v VideoPlayer handle (ignored).
void rt_videoplayer_stop(void *v) {
    (void)v;
}

/// @brief Stub for `VideoPlayer.Seek` — jump to time position `s` in
///        seconds. Audio resyncs immediately; video resyncs to the next
///        keyframe (with a brief catch-up decode).
///
/// Silent no-op stub.
///
/// @param v VideoPlayer handle (ignored).
/// @param s Target time in seconds (ignored).
void rt_videoplayer_seek(void *v, double s) {
    (void)v;
    (void)s;
}

/// @brief Stub for `VideoPlayer.Update` — advance video playback by `dt`
///        seconds: decode pending video frames, advance audio cursor,
///        maintain A/V sync.
///
/// Silent no-op stub.
///
/// @param v  VideoPlayer handle (ignored).
/// @param dt Delta time in seconds (ignored).
void rt_videoplayer_update(void *v, double dt) {
    (void)v;
    (void)dt;
}

/// @brief Stub for `VideoPlayer.SetVolume` — set the audio track's
///        playback volume, 0..1.
///
/// Silent no-op stub.
///
/// @param v   VideoPlayer handle (ignored).
/// @param vol Volume, 0..1 (ignored).
void rt_videoplayer_set_volume(void *v, double vol) {
    (void)v;
    (void)vol;
}

/// @brief Stub for `VideoPlayer.Width` — get the video's frame width in
///        pixels (parsed from the stream header at `Open` time).
///
/// Silent stub returning `0`.
///
/// @param v VideoPlayer handle (ignored).
///
/// @return `0`.
int64_t rt_videoplayer_get_width(void *v) {
    (void)v;
    return 0;
}

/// @brief Stub for `VideoPlayer.Height` — get the video's frame height
///        in pixels.
///
/// Silent stub returning `0`.
///
/// @param v VideoPlayer handle (ignored).
///
/// @return `0`.
int64_t rt_videoplayer_get_height(void *v) {
    (void)v;
    return 0;
}

/// @brief Stub for `VideoPlayer.Duration` — get the total length of the
///        video in seconds.
///
/// Silent stub returning `0.0`.
///
/// @param v VideoPlayer handle (ignored).
///
/// @return `0.0`.
double rt_videoplayer_get_duration(void *v) {
    (void)v;
    return 0.0;
}

/// @brief Stub for `VideoPlayer.Position` — get the current playback
///        position in seconds.
///
/// Silent stub returning `0.0`.
///
/// @param v VideoPlayer handle (ignored).
///
/// @return `0.0`.
double rt_videoplayer_get_position(void *v) {
    (void)v;
    return 0.0;
}

/// @brief Stub for `VideoPlayer.IsPlaying` — true while playback is
///        actively advancing (not paused, not stopped, not at EOF).
///
/// Silent stub returning `0`.
///
/// @param v VideoPlayer handle (ignored).
///
/// @return `0`.
int64_t rt_videoplayer_get_is_playing(void *v) {
    (void)v;
    return 0;
}

/// @brief Stub for `VideoPlayer.Frame` — get the most recently decoded
///        video frame as a Pixels surface (RGBA, frame size).
///
/// Silent stub returning NULL.
///
/// @param v VideoPlayer handle (ignored).
///
/// @return `NULL`.
void *rt_videoplayer_get_frame(void *v) {
    (void)v;
    return NULL;
}

/* PostFX F5-F7 stubs */

/// @brief Stub for `PostFX3D.AddSSAO` — append a Screen-Space Ambient
///        Occlusion pass to the chain. Darkens crevices and concave
///        regions based on geometry from the depth buffer.
///
/// Silent no-op stub.
///
/// @param p PostFX3D handle (ignored).
/// @param r Sample radius in screen pixels (ignored).
/// @param i Effect intensity (ignored).
/// @param s Sample count per fragment (higher = better quality, slower) (ignored).
void rt_postfx3d_add_ssao(void *p, double r, double i, int64_t s) {
    (void)p;
    (void)r;
    (void)i;
    (void)s;
}

/// @brief Stub for `PostFX3D.AddDOF` — append a Depth-of-Field blur
///        pass. Pixels at distances other than `f` (focus distance) are
///        blurred with circle-of-confusion radius proportional to depth
///        delta and aperture `a`.
///
/// Silent no-op stub.
///
/// @param p PostFX3D handle (ignored).
/// @param f Focus distance in world units (ignored).
/// @param a Aperture / blur strength (ignored).
/// @param m Maximum blur radius in screen pixels (ignored).
void rt_postfx3d_add_dof(void *p, double f, double a, double m) {
    (void)p;
    (void)f;
    (void)a;
    (void)m;
}

/// @brief Stub for `PostFX3D.AddMotionBlur` — append a per-pixel motion
///        blur pass driven by per-fragment motion vectors (`currClip` vs
///        `prevClip` from the GBuffer).
///
/// Silent no-op stub.
///
/// @param p PostFX3D handle (ignored).
/// @param i Blur intensity multiplier (ignored).
/// @param s Sample count along motion vector (ignored).
void rt_postfx3d_add_motion_blur(void *p, double i, int64_t s) {
    (void)p;
    (void)i;
    (void)s;
}

/// @brief Stub for `vgfx3d_postfx_get_snapshot` — backend-facing accessor
///        that copies the active PostFX3D parameters into an opaque
///        snapshot struct. Decouples GPU backends from PostFX3D's private
///        layout so the same snapshot can drive Metal / D3D11 / OpenGL.
///
/// Silent stub returning `0` (nothing copied) and ignoring `out`.
///
/// @param postfx PostFX3D handle (ignored).
/// @param out    Snapshot destination (ignored).
///
/// @return `0`.
int vgfx3d_postfx_get_snapshot(void *postfx, vgfx3d_postfx_snapshot_t *out) {
    (void)postfx;
    (void)out;
    return 0;
}

/* Opaque front-to-back sorting stub */

/// @brief Stub for `Canvas3D.SetOcclusionCulling` — when enabled, opaque
///        meshes are sorted front-to-back so early-Z rejects far fragments
///        before fragment shading.
///
/// Silent no-op stub.
///
/// @param c Canvas3D handle (ignored).
/// @param e Non-zero to enable (ignored).
void rt_canvas3d_set_occlusion_culling(void *c, int8_t e) {
    (void)c;
    (void)e;
}

/// @brief Stub for `Canvas3D.Begin2D` — switch into 2D overlay mode for
///        screen-space draws (HUD / UI). Disables depth testing and binds
///        an orthographic projection matrix sized to the viewport.
///
/// Silent no-op stub.
///
/// @param c Canvas3D handle (ignored).
void rt_canvas3d_begin_2d(void *c) {
    (void)c;
}

/// @brief Stub for `Canvas3D.DrawRect3D` — would normally draw a 2D rect
///        in screen space (after `Begin2D`). The `_3d` suffix is historical
///        — the call is screen-space, not world-space.
///
/// Silent no-op stub.
///
/// @param c  Canvas3D handle (ignored).
/// @param x  Top-left x in screen pixels (ignored).
/// @param y  Top-left y in screen pixels (ignored).
/// @param w  Width in pixels (ignored).
/// @param h  Height in pixels (ignored).
/// @param cl Packed 0xAARRGGBB color (ignored).
void rt_canvas3d_draw_rect_3d(void *c, int64_t x, int64_t y, int64_t w, int64_t h, int64_t cl) {
    (void)c;
    (void)x;
    (void)y;
    (void)w;
    (void)h;
    (void)cl;
}

/// @brief Stub for `Canvas3D.DrawText3D` — would normally draw screen-space
///        text in the 8x8 bitmap font during a 2D overlay pass.
///
/// Silent no-op stub.
///
/// @param c  Canvas3D handle (ignored).
/// @param x  Top-left x in screen pixels (ignored).
/// @param y  Top-left y in screen pixels (ignored).
/// @param t  Glyph source string (ignored).
/// @param cl Packed 0xAARRGGBB color (ignored).
void rt_canvas3d_draw_text_3d(void *c, int64_t x, int64_t y, rt_string t, int64_t cl) {
    (void)c;
    (void)x;
    (void)y;
    (void)t;
    (void)cl;
}

/* TextureAtlas3D stubs (F4) */

/// @brief Stub for `TextureAtlas3D.New` — would normally allocate a
///        `(w x h)` atlas surface with a packing strategy (skyline / shelf)
///        ready to receive sub-textures via `Add`.
///
/// Silent stub returning NULL.
///
/// @param w Atlas width in pixels (ignored).
/// @param h Atlas height in pixels (ignored).
///
/// @return `NULL`.
void *rt_texatlas3d_new(int64_t w, int64_t h) {
    (void)w;
    (void)h;
    return NULL;
}

/// @brief Stub for `TextureAtlas3D.Add` — pack a Pixels surface into the
///        atlas at the next available position. Returns an integer ID
///        used by `GetUVRect` to locate the sub-region later, or `-1` on
///        pack failure.
///
/// Silent stub returning `-1` (atlas full).
///
/// @param a Atlas handle (ignored).
/// @param p Pixels handle for the sub-texture (ignored).
///
/// @return `-1`.
int64_t rt_texatlas3d_add(void *a, void *p) {
    (void)a;
    (void)p;
    return -1;
}

/// @brief Stub for `TextureAtlas3D.Texture` — get the underlying Pixels
///        surface for binding to materials.
///
/// Silent stub returning NULL.
///
/// @param a Atlas handle (ignored).
///
/// @return `NULL`.
void *rt_texatlas3d_get_texture(void *a) {
    (void)a;
    return NULL;
}

/// @brief Stub for `TextureAtlas3D.GetUVRect(id)` — write the UV
///        coordinates `(u0, v0, u1, v1)` for sub-texture `id` to the
///        out-parameters.
///
/// Silent stub: writes the full-atlas defaults `(0, 0, 1, 1)` to non-NULL
/// out-params so callers get a usable (full-coverage) result rather than
/// an uninitialized read.
///
/// @param a  Atlas handle (ignored).
/// @param id Sub-texture id from `Add`, or invalid (ignored).
/// @param u0 Out-param: top-left u; defaults to `0`.
/// @param v0 Out-param: top-left v; defaults to `0`.
/// @param u1 Out-param: bottom-right u; defaults to `1`.
/// @param v1 Out-param: bottom-right v; defaults to `1`.
void rt_texatlas3d_get_uv_rect(
    void *a, int64_t id, double *u0, double *v0, double *u1, double *v1) {
    (void)a;
    (void)id;
    if (u0)
        *u0 = 0;
    if (v0)
        *v0 = 0;
    if (u1)
        *u1 = 1;
    if (v1)
        *v1 = 1;
}
