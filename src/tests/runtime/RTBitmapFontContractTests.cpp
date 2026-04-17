//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//

extern "C" {
#include "rt_graphics_internal.h"
#include "rt_bitmapfont.h"
}

#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <cstring>

namespace {

struct TestGlyph {
    uint8_t *bitmap;
    int16_t width;
    int16_t height;
    int16_t x_offset;
    int16_t y_offset;
    int16_t advance;
};

struct TestBitmapFont {
    TestGlyph glyphs[65536];
    int16_t line_height;
    int16_t max_width;
    int16_t ascent;
    int8_t monospace;
    int64_t glyph_count;
};

struct FakeWindow {
    int32_t width;
    int32_t height;
    float scale;
    int64_t min_plot_x;
    int64_t max_plot_x;
    int plot_count;
    int fill_count;
    int32_t last_fill_x;
    int32_t last_fill_y;
    int32_t last_fill_w;
    int32_t last_fill_h;
};

static rt_string S(const char *s) {
    return reinterpret_cast<rt_string>(const_cast<char *>(s));
}

static rt_canvas *make_canvas(int32_t width, int32_t height) {
    auto *canvas = static_cast<rt_canvas *>(std::calloc(1, sizeof(rt_canvas)));
    assert(canvas != nullptr);
    auto *window = static_cast<FakeWindow *>(std::calloc(1, sizeof(FakeWindow)));
    assert(window != nullptr);
    window->width = width;
    window->height = height;
    window->scale = 1.0f;
    window->min_plot_x = INT64_MAX;
    window->max_plot_x = INT64_MIN;
    canvas->magic = RT_CANVAS_MAGIC;
    canvas->gfx_win = reinterpret_cast<vgfx_window_t>(window);
    return canvas;
}

static TestBitmapFont *make_font() {
    auto *font = static_cast<TestBitmapFont *>(std::calloc(1, sizeof(TestBitmapFont)));
    assert(font != nullptr);
    font->line_height = 1;
    font->max_width = 5;
    font->ascent = 1;
    return font;
}

static void set_glyph(
    TestBitmapFont *font, int codepoint, int width, int x_offset, int advance, uint8_t bits) {
    assert(font != nullptr);
    assert(codepoint >= 0 && codepoint < 65536);
    auto *glyph = &font->glyphs[codepoint];
    glyph->bitmap = static_cast<uint8_t *>(std::malloc(1));
    assert(glyph->bitmap != nullptr);
    glyph->bitmap[0] = bits;
    glyph->width = (int16_t)width;
    glyph->height = 1;
    glyph->x_offset = (int16_t)x_offset;
    glyph->y_offset = 0;
    glyph->advance = (int16_t)advance;
    font->glyph_count++;
}

static void reset_window(FakeWindow *window) {
    assert(window != nullptr);
    window->min_plot_x = INT64_MAX;
    window->max_plot_x = INT64_MIN;
    window->plot_count = 0;
    window->fill_count = 0;
    window->last_fill_x = 0;
    window->last_fill_y = 0;
    window->last_fill_w = 0;
    window->last_fill_h = 0;
}

static void test_text_width_accounts_for_right_overhang_and_bmp_codepoints() {
    auto *font = make_font();
    set_glyph(font, 'A', 3, 0, 2, 0xE0);
    set_glyph(font, 0x03A9, 1, 0, 2, 0x80);

    assert(rt_bitmapfont_text_width(font, S("A")) == 3);
    assert(rt_bitmapfont_text_width(font, S("\xCE\xA9")) == 2);
}

static void test_text_font_right_uses_visual_bounds() {
    auto *font = make_font();
    set_glyph(font, 'A', 3, 0, 2, 0xE0);
    auto *canvas = make_canvas(20, 4);
    auto *window = reinterpret_cast<FakeWindow *>(canvas->gfx_win);

    reset_window(window);
    rt_canvas_text_font_right(canvas, 0, 0, S("A"), font, 0x00FFFFFF);

    assert(window->plot_count == 3);
    assert(window->min_plot_x == 17);
    assert(window->max_plot_x == 19);
}

static void test_text_font_bg_covers_glyph_overhang() {
    auto *font = make_font();
    set_glyph(font, 'A', 3, 0, 2, 0xE0);
    auto *canvas = make_canvas(20, 4);
    auto *window = reinterpret_cast<FakeWindow *>(canvas->gfx_win);

    reset_window(window);
    rt_canvas_text_font_bg(canvas, 10, 0, S("A"), font, 0x00FFFFFF, 0x000000FF);

    assert(window->fill_count == 1);
    assert(window->last_fill_x == 10);
    assert(window->last_fill_w == 3);
}

} // namespace

extern "C" void *rt_obj_new_i64(int64_t, int64_t byte_size) {
    return std::calloc(1, static_cast<size_t>(byte_size));
}

extern "C" void rt_obj_set_finalizer(void *, void (*)(void *)) {}

extern "C" int32_t rt_obj_release_check0(void *) {
    return 1;
}

extern "C" void rt_obj_free(void *obj) {
    std::free(obj);
}

extern "C" const char *rt_string_cstr(rt_string s) {
    return reinterpret_cast<const char *>(s);
}

extern "C" int64_t rt_str_len(rt_string s) {
    return s ? (int64_t)std::strlen(reinterpret_cast<const char *>(s)) : 0;
}

extern "C" void rt_trap_raise_kind(int32_t, int32_t, int32_t, const char *) {
    std::abort();
}

extern "C" float vgfx_window_get_scale(vgfx_window_t window) {
    auto *fake = reinterpret_cast<FakeWindow *>(window);
    return fake ? fake->scale : 1.0f;
}

extern "C" void vgfx_set_coord_scale(vgfx_window_t, float) {}
extern "C" void vgfx_set_clip(vgfx_window_t, int32_t, int32_t, int32_t, int32_t) {}
extern "C" void vgfx_clear_clip(vgfx_window_t) {}

extern "C" int32_t vgfx_get_size(vgfx_window_t window, int32_t *width, int32_t *height) {
    auto *fake = reinterpret_cast<FakeWindow *>(window);
    if (!fake)
        return 0;
    if (width)
        *width = fake->width;
    if (height)
        *height = fake->height;
    return 1;
}

extern "C" void vgfx_pset(vgfx_window_t window, int32_t x, int32_t, vgfx_color_t) {
    auto *fake = reinterpret_cast<FakeWindow *>(window);
    assert(fake != nullptr);
    if (x < fake->min_plot_x)
        fake->min_plot_x = x;
    if (x > fake->max_plot_x)
        fake->max_plot_x = x;
    fake->plot_count++;
}

extern "C" void vgfx_fill_rect(
    vgfx_window_t window, int32_t x, int32_t y, int32_t w, int32_t h, vgfx_color_t) {
    auto *fake = reinterpret_cast<FakeWindow *>(window);
    assert(fake != nullptr);
    fake->fill_count++;
    fake->last_fill_x = x;
    fake->last_fill_y = y;
    fake->last_fill_w = w;
    fake->last_fill_h = h;
}

int main() {
    test_text_width_accounts_for_right_overhang_and_bmp_codepoints();
    test_text_font_right_uses_visual_bounds();
    test_text_font_bg_covers_glyph_overhang();
    return 0;
}
