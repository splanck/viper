//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/2d/rt_color.c
// Purpose: Color utilities for the 2D graphics API: HSL<->RGB conversion, component
//   getters, lerp, brighten/darken, saturate/desaturate, complement,
//   grayscale, invert, and hex parse/format. Pure color math (no canvas).
//
// Links: rt_graphics.h (public rt_color_* API), rt_graphics2d.h,
//        rt_drawing_advanced.c (drawing primitives that consume colors)
//
//===----------------------------------------------------------------------===//

#include "rt_graphics2d.h"
#include "rt_graphics_internal.h"
#include "rt_heap.h"

#include <limits.h>

#ifdef VIPER_ENABLE_GRAPHICS

//=============================================================================
// Extended Color Functions
//=============================================================================

/// @brief Hsl the from.
int64_t rt_color_from_hsl(int64_t h, int64_t s, int64_t l) {
    // Clamp inputs
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

/// @brief Get the l value.
/// @param color
/// @return Result value.
int64_t rt_color_get_l(int64_t color) {
    int64_t r = (color >> 16) & 0xFF;
    int64_t g = (color >> 8) & 0xFF;
    int64_t b = color & 0xFF;
    int64_t h, s, l;
    rtg_rgb_to_hsl(r, g, b, &h, &s, &l);
    return l;
}

/// @brief Test the runtime "explicit alpha" tag bit on a 64-bit color value.
/// @details Color literals built via Color.RGBA(...) carry RT_COLOR_EXPLICIT_ALPHA_FLAG
///          (bit 56) so downstream transforms (Brighten, Darken, Lerp, …) can
///          tell user-supplied alpha apart from "no alpha specified, treat as
///          opaque". Plain Color.RGB(...) values do NOT have this flag.
/// @return 1 if the color carries a user-specified alpha, 0 if alpha was implicit.
static int8_t rt_color_has_explicit_alpha(int64_t color) {
    return (color & RT_COLOR_EXPLICIT_ALPHA_FLAG) != 0;
}

/// @brief Decompose a tagged color value into r/g/b/a components plus the alpha-tag flag.
/// @details Reads bytes 0-2 as B/G/R, byte 3 as A (or 255 when the explicit-alpha
///          flag is unset), and exposes the flag itself so callers can re-pack
///          the result through rt_color_pack_rgba_like and preserve user intent.
///          Each out-pointer is individually NULL-safe (skipped if NULL).
static void rt_color_split_rgba(
    int64_t color, int64_t *r, int64_t *g, int64_t *b, int64_t *a, int8_t *has_alpha) {
    int8_t explicit_alpha = rt_color_has_explicit_alpha(color);
    if (r)
        *r = (color >> 16) & 0xFF;
    if (g)
        *g = (color >> 8) & 0xFF;
    if (b)
        *b = color & 0xFF;
    if (a)
        *a = explicit_alpha ? ((color >> 24) & 0xFF) : 255;
    if (has_alpha)
        *has_alpha = explicit_alpha;
}

/// @brief Re-pack r/g/b/a back into a tagged color value, preserving the explicit-alpha tag.
/// @details The inverse of rt_color_split_rgba. When @p has_alpha is non-zero the
///          result carries RT_COLOR_EXPLICIT_ALPHA_FLAG so downstream transforms
///          continue to honor the user-provided alpha; when zero, the alpha
///          byte is dropped and a plain RGB value is returned. Components are
///          masked to 8 bits — out-of-range inputs are silently truncated.
static int64_t rt_color_pack_rgba_like(
    int64_t r, int64_t g, int64_t b, int64_t a, int8_t has_alpha) {
    int64_t rgb = ((r & 0xFF) << 16) | ((g & 0xFF) << 8) | (b & 0xFF);
    if (!has_alpha)
        return rgb;
    return (((a & 0xFF) << 24) | rgb) | RT_COLOR_EXPLICIT_ALPHA_FLAG;
}

/// @brief Lerp operation.
int64_t rt_color_lerp(int64_t c1, int64_t c2, int64_t t) {
    if (t < 0)
        t = 0;
    if (t > 100)
        t = 100;

    int64_t r1 = 0, g1 = 0, b1 = 0, a1 = 255;
    int64_t r2 = 0, g2 = 0, b2 = 0, a2 = 255;
    int8_t alpha1 = 0, alpha2 = 0;
    rt_color_split_rgba(c1, &r1, &g1, &b1, &a1, &alpha1);
    rt_color_split_rgba(c2, &r2, &g2, &b2, &a2, &alpha2);

    int64_t r = r1 + (r2 - r1) * t / 100;
    int64_t g = g1 + (g2 - g1) * t / 100;
    int64_t b = b1 + (b2 - b1) * t / 100;
    int64_t a = a1 + (a2 - a1) * t / 100;

    return rt_color_pack_rgba_like(r, g, b, a, alpha1 || alpha2);
}

/// @brief Get the r value.
/// @param color
/// @return Result value.
int64_t rt_color_get_r(int64_t color) {
    return (color >> 16) & 0xFF;
}

/// @brief Get the g value.
/// @param color
/// @return Result value.
int64_t rt_color_get_g(int64_t color) {
    return (color >> 8) & 0xFF;
}

/// @brief Get the b value.
/// @param color
/// @return Result value.
int64_t rt_color_get_b(int64_t color) {
    return color & 0xFF;
}

/// @brief Get the stored alpha byte.
/// @details Plain RGB colors have no stored alpha, so this returns 0 for
///          Color.RGB values. Drawing and transform helpers still treat plain
///          RGB as opaque through rt_color_split_rgba().
/// @param color
/// @return Result value.
int64_t rt_color_get_a(int64_t color) {
    return (color >> 24) & 0xFF;
}

/// @brief Brighten operation.
int64_t rt_color_brighten(int64_t color, int64_t amount) {
    if (amount < 0)
        amount = 0;
    if (amount > 100)
        amount = 100;

    int64_t r = 0, g = 0, b = 0, a = 255;
    int8_t has_alpha = 0;
    rt_color_split_rgba(color, &r, &g, &b, &a, &has_alpha);

    // Increase each channel toward 255
    r = r + (255 - r) * amount / 100;
    g = g + (255 - g) * amount / 100;
    b = b + (255 - b) * amount / 100;

    return rt_color_pack_rgba_like(r, g, b, a, has_alpha);
}

/// @brief Darken operation.
int64_t rt_color_darken(int64_t color, int64_t amount) {
    if (amount < 0)
        amount = 0;
    if (amount > 100)
        amount = 100;

    int64_t r = 0, g = 0, b = 0, a = 255;
    int8_t has_alpha = 0;
    rt_color_split_rgba(color, &r, &g, &b, &a, &has_alpha);

    // Decrease each channel toward 0
    r = r - r * amount / 100;
    g = g - g * amount / 100;
    b = b - b * amount / 100;

    return rt_color_pack_rgba_like(r, g, b, a, has_alpha);
}

/// @brief Convert a single hex character ('0'-'9', 'a'-'f', 'A'-'F') to its 0-15 value; returns -1
/// on invalid input.
static int rt_color_hex_digit(char c) {
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

/// @brief Parse exactly `len` hex characters from `s` into `*out`; returns 1 on success, 0 on
/// invalid char or NULL input.
static int rt_color_parse_hex_n(const char *s, size_t len, uint64_t *out) {
    if (!s || !out)
        return 0;

    uint64_t value = 0;
    for (size_t i = 0; i < len; ++i) {
        int digit = rt_color_hex_digit(s[i]);
        if (digit < 0)
            return 0;
        value = (value << 4) | (uint64_t)digit;
    }

    *out = value;
    return 1;
}

/// @brief Hex the from.
int64_t rt_color_from_hex(rt_string hex) {
    if (!hex)
        return 0;
    const char *s = rt_string_cstr(hex);
    if (!s)
        return 0;
    int64_t raw_len = rt_str_len(hex);
    if (raw_len <= 0)
        return 0;
    size_t offset = (s[0] == '#') ? 1u : 0u;
    if ((uint64_t)raw_len < offset)
        return 0;
    s += offset;
    size_t len = (size_t)((uint64_t)raw_len - offset);
    uint64_t val = 0;
    if (len == 6) {
        if (!rt_color_parse_hex_n(s, len, &val))
            return 0;
        return (int64_t)val; // 0xRRGGBB
    }
    if (len == 8) {
        if (!rt_color_parse_hex_n(s, len, &val))
            return 0;
        // Input is RRGGBBAA, store as AARRGGBB
        int64_t r = (val >> 24) & 0xFF;
        int64_t g = (val >> 16) & 0xFF;
        int64_t b = (val >> 8) & 0xFF;
        int64_t a = val & 0xFF;
        int64_t packed = (a << 24) | (r << 16) | (g << 8) | b;
        return packed | RT_COLOR_EXPLICIT_ALPHA_FLAG;
    }
    if (len == 3) {
        if (!rt_color_parse_hex_n(s, len, &val))
            return 0;
        // Shorthand: RGB -> RRGGBB
        int64_t r = (val >> 8) & 0xF;
        int64_t g = (val >> 4) & 0xF;
        int64_t b = val & 0xF;
        return ((r | (r << 4)) << 16) | ((g | (g << 4)) << 8) | (b | (b << 4));
    }
    return 0;
}

/// @brief Hex the to.
rt_string rt_color_to_hex(int64_t color) {
    char buf[10];
    int8_t has_explicit_alpha = (color & RT_COLOR_EXPLICIT_ALPHA_FLAG) != 0;
    int64_t a = (color >> 24) & 0xFF;
    int64_t r = (color >> 16) & 0xFF;
    int64_t g = (color >> 8) & 0xFF;
    int64_t b = color & 0xFF;
    int len;
    if (has_explicit_alpha || a != 0)
        len = snprintf(buf, sizeof(buf), "#%02X%02X%02X%02X", (int)r, (int)g, (int)b, (int)a);
    else
        len = snprintf(buf, sizeof(buf), "#%02X%02X%02X", (int)r, (int)g, (int)b);
    if (len < 0 || (size_t)len >= sizeof(buf))
        return rt_string_from_bytes("", 0);
    return rt_string_from_bytes(buf, (size_t)len);
}

/// @brief Saturate operation.
int64_t rt_color_saturate(int64_t color, int64_t amount) {
    if (amount < 0)
        amount = 0;
    if (amount > 100)
        amount = 100;
    int64_t r = 0, g = 0, b = 0, a = 255;
    int8_t has_alpha = 0;
    rt_color_split_rgba(color, &r, &g, &b, &a, &has_alpha);
    int64_t h, s, l;
    rtg_rgb_to_hsl(r, g, b, &h, &s, &l);
    s = s + amount;
    if (s > 100)
        s = 100;
    rtg_hsl_to_rgb(h, s, l, &r, &g, &b);
    return rt_color_pack_rgba_like(r, g, b, a, has_alpha);
}

/// @brief Desaturate operation.
int64_t rt_color_desaturate(int64_t color, int64_t amount) {
    if (amount < 0)
        amount = 0;
    if (amount > 100)
        amount = 100;
    int64_t r = 0, g = 0, b = 0, a = 255;
    int8_t has_alpha = 0;
    rt_color_split_rgba(color, &r, &g, &b, &a, &has_alpha);
    int64_t h, s, l;
    rtg_rgb_to_hsl(r, g, b, &h, &s, &l);
    s = s - amount;
    if (s < 0)
        s = 0;
    rtg_hsl_to_rgb(h, s, l, &r, &g, &b);
    return rt_color_pack_rgba_like(r, g, b, a, has_alpha);
}

/// @brief Complement operation.
int64_t rt_color_complement(int64_t color) {
    int64_t r = 0, g = 0, b = 0, a = 255;
    int8_t has_alpha = 0;
    rt_color_split_rgba(color, &r, &g, &b, &a, &has_alpha);
    int64_t h, s, l;
    rtg_rgb_to_hsl(r, g, b, &h, &s, &l);
    h = (h + 180) % 360;
    rtg_hsl_to_rgb(h, s, l, &r, &g, &b);
    return rt_color_pack_rgba_like(r, g, b, a, has_alpha);
}

/// @brief Grayscale operation.
int64_t rt_color_grayscale(int64_t color) {
    int64_t r = 0, g = 0, b = 0, a = 255;
    int8_t has_alpha = 0;
    rt_color_split_rgba(color, &r, &g, &b, &a, &has_alpha);
    // Luminance formula: 0.299R + 0.587G + 0.114B
    int64_t gray = (r * 299 + g * 587 + b * 114) / 1000;
    return rt_color_pack_rgba_like(gray, gray, gray, a, has_alpha);
}

/// @brief Invert operation.
int64_t rt_color_invert(int64_t color) {
    int64_t r = 0, g = 0, b = 0, a = 255;
    int8_t has_alpha = 0;
    rt_color_split_rgba(color, &r, &g, &b, &a, &has_alpha);
    r = 255 - r;
    g = 255 - g;
    b = 255 - b;
    return rt_color_pack_rgba_like(r, g, b, a, has_alpha);
}

#else
typedef int rt_color_disabled_tu_guard;
#endif /* VIPER_ENABLE_GRAPHICS */
