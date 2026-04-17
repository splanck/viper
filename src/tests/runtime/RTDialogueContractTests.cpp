//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//

extern "C" {
#include "rt_dialogue.h"
}

#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <cstring>

namespace {

struct ObjHeader {
    int32_t refcount;
    void (*finalizer)(void *);
};

struct FakeFont {
    int32_t glyph_width;
    int32_t glyph_height;
};

int g_builtin_text_calls = 0;
int g_font_text_calls = 0;
int g_frame_calls = 0;
int g_font_finalizer_calls = 0;

ObjHeader *header_from_payload(void *obj) {
    return reinterpret_cast<ObjHeader *>(obj) - 1;
}

int count_codepoints(const char *text) {
    if (!text)
        return 0;
    int count = 0;
    for (size_t i = 0; text[i] != '\0';) {
        unsigned char c = static_cast<unsigned char>(text[i]);
        size_t step = 1;
        if ((c & 0x80) == 0)
            step = 1;
        else if ((c & 0xE0) == 0xC0)
            step = 2;
        else if ((c & 0xF0) == 0xE0)
            step = 3;
        else if ((c & 0xF8) == 0xF0)
            step = 4;
        i += step;
        count++;
    }
    return count;
}

void reset_draw_counters() {
    g_builtin_text_calls = 0;
    g_font_text_calls = 0;
    g_frame_calls = 0;
}

void fake_font_finalizer(void *) {
    g_font_finalizer_calls++;
}

} // namespace

extern "C" void *rt_obj_new_i64(int64_t, int64_t byte_size) {
    auto *header =
        static_cast<ObjHeader *>(std::calloc(1, sizeof(ObjHeader) + static_cast<size_t>(byte_size)));
    assert(header != nullptr);
    header->refcount = 1;
    return header + 1;
}

extern "C" void rt_obj_set_finalizer(void *obj, void (*finalizer)(void *)) {
    if (!obj)
        return;
    header_from_payload(obj)->finalizer = finalizer;
}

extern "C" void rt_obj_retain_maybe(void *obj) {
    if (!obj)
        return;
    header_from_payload(obj)->refcount++;
}

extern "C" int32_t rt_obj_release_check0(void *obj) {
    if (!obj)
        return 0;
    ObjHeader *header = header_from_payload(obj);
    assert(header->refcount > 0);
    header->refcount--;
    return header->refcount == 0;
}

extern "C" void rt_obj_free(void *obj) {
    if (!obj)
        return;
    ObjHeader *header = header_from_payload(obj);
    if (header->finalizer)
        header->finalizer(obj);
    std::free(header);
}

extern "C" rt_string rt_string_from_bytes(const char *bytes, size_t len) {
    char *copy = static_cast<char *>(std::malloc(len + 1));
    assert(copy != nullptr);
    if (len > 0)
        std::memcpy(copy, bytes, len);
    copy[len] = '\0';
    return reinterpret_cast<rt_string>(copy);
}

extern "C" rt_string rt_const_cstr(const char *s) {
    return reinterpret_cast<rt_string>(const_cast<char *>(s));
}

extern "C" const char *rt_string_cstr(rt_string s) {
    return reinterpret_cast<const char *>(s);
}

extern "C" int64_t rt_str_len(rt_string s) {
    return s ? static_cast<int64_t>(std::strlen(reinterpret_cast<const char *>(s))) : 0;
}

extern "C" void rt_str_release_maybe(rt_string) {}

extern "C" void rt_trap(const char *) {
    std::abort();
}

extern "C" int64_t rt_canvas_text_width(rt_string text) {
    return count_codepoints(rt_string_cstr(text)) * 8;
}

extern "C" int64_t rt_canvas_text_height(void) {
    return 8;
}

extern "C" int64_t rt_bitmapfont_text_width(void *font, rt_string text) {
    auto *fake = static_cast<FakeFont *>(font);
    return fake ? count_codepoints(rt_string_cstr(text)) * fake->glyph_width : 0;
}

extern "C" int64_t rt_bitmapfont_text_height(void *font) {
    auto *fake = static_cast<FakeFont *>(font);
    return fake ? fake->glyph_height : 0;
}

extern "C" void rt_canvas_box_alpha(void *, int64_t, int64_t, int64_t, int64_t, int64_t, int64_t) {}

extern "C" void rt_canvas_frame(void *, int64_t, int64_t, int64_t, int64_t, int64_t) {
    g_frame_calls++;
}

extern "C" void rt_canvas_text(void *, int64_t, int64_t, rt_string, int64_t) {
    g_builtin_text_calls++;
}

extern "C" void rt_canvas_text_scaled(void *, int64_t, int64_t, rt_string, int64_t, int64_t) {
    g_builtin_text_calls++;
}

extern "C" void rt_canvas_text_font(void *, int64_t, int64_t, rt_string, void *, int64_t) {
    g_font_text_calls++;
}

extern "C" void rt_canvas_text_font_scaled(
    void *, int64_t, int64_t, rt_string, void *, int64_t, int64_t) {
    g_font_text_calls++;
}

static void test_draw_uses_custom_font_path() {
    reset_draw_counters();

    void *dlg = rt_dialogue_new(0, 0, 200, 80);
    auto *font = static_cast<FakeFont *>(rt_obj_new_i64(0, sizeof(FakeFont)));
    font->glyph_width = 6;
    font->glyph_height = 9;

    rt_dialogue_set_font(dlg, font);
    rt_dialogue_say(dlg, rt_const_cstr("NPC"), rt_const_cstr("Hi"));
    rt_dialogue_set_speed(dlg, 0);
    rt_dialogue_update(dlg, 1);
    rt_dialogue_draw(dlg, reinterpret_cast<void *>(1));

    assert(g_font_text_calls >= 3);
    assert(g_builtin_text_calls == 0);

    assert(rt_obj_release_check0(font) == 0);
    if (rt_obj_release_check0(dlg))
        rt_obj_free(dlg);
}

static void test_border_color_minus_one_disables_frame() {
    reset_draw_counters();

    void *dlg = rt_dialogue_new(0, 0, 200, 80);
    rt_dialogue_set_border_color(dlg, -1);
    rt_dialogue_say_text(dlg, rt_const_cstr("No border"));
    rt_dialogue_set_speed(dlg, 0);
    rt_dialogue_update(dlg, 1);
    rt_dialogue_draw(dlg, reinterpret_cast<void *>(1));

    assert(g_frame_calls == 0);

    if (rt_obj_release_check0(dlg))
        rt_obj_free(dlg);
}

static void test_dialogue_finalizer_releases_font() {
    g_font_finalizer_calls = 0;

    void *dlg = rt_dialogue_new(0, 0, 200, 80);
    auto *font = static_cast<FakeFont *>(rt_obj_new_i64(0, sizeof(FakeFont)));
    font->glyph_width = 6;
    font->glyph_height = 9;
    rt_obj_set_finalizer(font, fake_font_finalizer);

    rt_dialogue_set_font(dlg, font);

    // Drop the caller's reference; the dialogue should own the last live reference now.
    assert(rt_obj_release_check0(font) == 0);

    if (rt_obj_release_check0(dlg))
        rt_obj_free(dlg);

    assert(g_font_finalizer_calls == 1);
}

int main() {
    test_draw_uses_custom_font_path();
    test_border_color_minus_one_disables_frame();
    test_dialogue_finalizer_releases_font();
    return 0;
}
