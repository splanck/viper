//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/game/rt_gameui_textinput.c
// Purpose: UITextInput widget for the immediate-mode GameUI — editable single-
//          line text field with UTF-8-aware cursor/selection handling. Split
//          out of rt_gameui.c; shares helpers + key codes via
//          rt_gameui_internal.h.
//
// Key invariants:
//   - Cursor and selection indices are byte offsets kept on UTF-8 codepoint
//     boundaries via the shared ui_*_codepoint_byte helpers.
//   - Immediate-mode: validates its canvas and draws against the current frame.
//
// Ownership/Lifetime:
//   - Borrows the caller's canvas/font; owns its editable text buffer.
//
// Links: src/runtime/game/rt_gameui.c (other widgets + shared helpers),
//        src/runtime/game/rt_gameui_internal.h (shared helpers + key codes)
//
//===----------------------------------------------------------------------===//

#include "rt_gameui.h"
#include "rt_gameui_internal.h"

#include "rt_bitmapfont.h"
#include "rt_graphics.h"
#include "rt_internal.h"
#include "rt_object.h"
#include "rt_pixels.h"

#include <float.h>
#include <limits.h>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

//=============================================================================
// UITextInput
//=============================================================================

#define RT_UITEXTINPUT_MAX_BYTES 512
#define RT_UITEXTINPUT_DEFAULT_CURSOR_BLINK_MS 530

typedef struct {
    void *vptr;
    int64_t x, y, w, h;
    char text[RT_UITEXTINPUT_MAX_BYTES];
    int64_t text_bytes;
    int64_t cursor_byte;
    int64_t selection_anchor;
    int64_t scroll_byte;
    int64_t text_color;
    int64_t bg_color;
    int64_t cursor_color;
    int64_t selection_color;
    int64_t border_color;
    int64_t border_color_focused;
    int64_t cursor_blink_ms;
    int64_t cursor_blink_elapsed;
    void *font;
    int8_t visible;
    int8_t enabled;
    int8_t focused;
    int8_t password_mode;
    int8_t multiline;
    int64_t max_codepoints;
    char placeholder[RT_UITEXTINPUT_MAX_BYTES];
} rt_uitextinput_impl;

/// @brief Safe-cast a handle to the UITextInput impl, trapping @p api on a
///        class-id mismatch. @return The impl, or NULL if @p ptr is NULL.
static rt_uitextinput_impl *checked_textinput(void *ptr, const char *api) {
    if (!ptr)
        return NULL;
    if (rt_obj_class_id(ptr) != RT_UITEXTINPUT_CLASS_ID) {
        rt_trap(api);
        return NULL;
    }
    return (rt_uitextinput_impl *)ptr;
}

/// @brief GC finalizer: free the text buffer and release the field's font.
static void uitextinput_finalizer(void *obj) {
    rt_uitextinput_impl *ti = (rt_uitextinput_impl *)obj;
    if (!ti)
        return;
    ui_release_obj(ti->font);
    ti->font = NULL;
}

/// @brief Replace the field's buffer with @p len bytes of @p text, resetting
///        caret/selection (respects the max-codepoints cap).
static void textinput_set_bytes(rt_uitextinput_impl *ti, const char *text, size_t len) {
    if (!ti)
        return;
    if (!text)
        len = 0;
    else
        len = ui_visible_len(text, len);
    len = ui_utf8_trunc_len(text, len, RT_UITEXTINPUT_MAX_BYTES - 1);
    if (ti->max_codepoints > 0)
        len = ui_utf8_trunc_codepoints(text, len, (size_t)ti->max_codepoints);
    if (len > 0)
        memmove(ti->text, text, len);
    ti->text[len] = '\0';
    ti->text_bytes = (int64_t)len;
    ti->cursor_byte = ti->text_bytes;
    ti->selection_anchor = -1;
    ti->scroll_byte = 0;
}

/// @brief Get the normalized selection byte range (start <= end) via out
///        params. @return non-zero if a non-empty selection exists.
static int8_t textinput_selection_range(rt_uitextinput_impl *ti, int64_t *start, int64_t *end) {
    if (!ti || ti->selection_anchor < 0 || ti->selection_anchor == ti->cursor_byte)
        return 0;
    int64_t a = ti->selection_anchor;
    int64_t b = ti->cursor_byte;
    if (a > b) {
        int64_t tmp = a;
        a = b;
        b = tmp;
    }
    if (a < 0)
        a = 0;
    if (b > ti->text_bytes)
        b = ti->text_bytes;
    if (a >= b)
        return 0;
    if (start)
        *start = a;
    if (end)
        *end = b;
    return 1;
}

/// @brief Delete bytes [start, end) from the field and fix up the caret.
/// @return non-zero if anything was removed.
static int8_t textinput_delete_range(rt_uitextinput_impl *ti, int64_t start, int64_t end) {
    if (!ti || start < 0 || end <= start || start >= ti->text_bytes)
        return 0;
    if (end > ti->text_bytes)
        end = ti->text_bytes;
    memmove(ti->text + start, ti->text + end, (size_t)(ti->text_bytes - end + 1));
    ti->text_bytes -= end - start;
    ti->cursor_byte = start;
    ti->selection_anchor = -1;
    if (ti->scroll_byte > ti->cursor_byte)
        ti->scroll_byte = ti->cursor_byte;
    return 1;
}

/// @brief Delete the active selection (if any). @return non-zero if removed.
static int8_t textinput_delete_selection(rt_uitextinput_impl *ti) {
    int64_t start = 0;
    int64_t end = 0;
    return textinput_selection_range(ti, &start, &end) ? textinput_delete_range(ti, start, end) : 0;
}

/// @brief Insert @p src_len bytes at the caret, replacing any selection first
///        and enforcing the max-codepoints cap. @return non-zero if inserted.
static int8_t textinput_insert_bytes(rt_uitextinput_impl *ti, const char *src, size_t src_len) {
    if (!ti || !src || src_len == 0)
        return 0;
    src_len = ui_utf8_trunc_len(src, src_len, src_len);
    if (src_len == 0)
        return 0;
    int8_t changed = textinput_delete_selection(ti);
    int64_t current_cps = ui_codepoint_count_bytes(ti->text, ti->text_bytes);
    int64_t remaining_cps = ti->max_codepoints > 0 ? (ti->max_codepoints - current_cps) : INT64_MAX;
    if (remaining_cps <= 0)
        return changed;
    size_t accepted = 0;
    int64_t cps = 0;
    while (accepted < src_len && cps < remaining_cps) {
        size_t cp_len = ui_utf8_cp_len(src, src_len, accepted);
        if (accepted + cp_len > src_len)
            break;
        if ((int64_t)cp_len > (RT_UITEXTINPUT_MAX_BYTES - 1) - ti->text_bytes)
            break;
        if (src[accepted] == '\0' ||
            (!ti->multiline && (src[accepted] == '\n' || src[accepted] == '\r'))) {
            accepted += cp_len;
            continue;
        }
        memmove(ti->text + ti->cursor_byte + (int64_t)cp_len,
                ti->text + ti->cursor_byte,
                (size_t)(ti->text_bytes - ti->cursor_byte + 1));
        memcpy(ti->text + ti->cursor_byte, src + accepted, cp_len);
        ti->cursor_byte += (int64_t)cp_len;
        ti->text_bytes += (int64_t)cp_len;
        accepted += cp_len;
        cps++;
        changed = 1;
    }
    ti->selection_anchor = -1;
    return changed;
}

/// @brief Move the caret to byte offset @p byte_pos; @p shift_held extends the
///        selection, otherwise the selection is collapsed.
static void textinput_move_cursor(rt_uitextinput_impl *ti, int64_t byte_pos, int8_t shift_held) {
    if (!ti)
        return;
    if (byte_pos < 0)
        byte_pos = 0;
    if (byte_pos > ti->text_bytes)
        byte_pos = ti->text_bytes;
    if (shift_held) {
        if (ti->selection_anchor < 0)
            ti->selection_anchor = ti->cursor_byte;
    } else {
        ti->selection_anchor = -1;
    }
    ti->cursor_byte = byte_pos;
    ti->cursor_blink_elapsed = 0;
}

/// @brief Map a mouse x-coordinate to the nearest caret byte offset
///        (accounting for scroll, font metrics, and codepoint boundaries).
static int64_t textinput_byte_from_mouse(rt_uitextinput_impl *ti, int64_t mx) {
    if (!ti || ti->text_bytes <= 0)
        return 0;
    int64_t origin = ui_add_sat_i64(ti->x, 4);
    if (mx <= origin)
        return 0;
    uint64_t local_offset = (uint64_t)mx - (uint64_t)origin;
    int64_t local = local_offset > (uint64_t)INT64_MAX ? INT64_MAX : (int64_t)local_offset;
    int64_t pos = 0;
    int64_t best = ti->text_bytes;
    while (pos < ti->text_bytes) {
        int64_t next = ui_next_codepoint_byte(ti->text, ti->text_bytes, pos);
        int64_t mid = ui_text_prefix_width(ti->text, pos, ti->font, 1) +
                      (ui_text_prefix_width(ti->text + pos, next - pos, ti->font, 1) / 2);
        if (local < mid) {
            best = pos;
            break;
        }
        pos = next;
    }
    return best;
}

void *rt_uitextinput_new(int64_t x, int64_t y, int64_t w, int64_t h) {
    rt_uitextinput_impl *ti = (rt_uitextinput_impl *)rt_obj_new_i64(
        RT_UITEXTINPUT_CLASS_ID, (int64_t)sizeof(rt_uitextinput_impl));
    if (!ti)
        return NULL;
    memset(ti, 0, sizeof(*ti));
    ti->x = x;
    ti->y = y;
    ti->w = ui_clamp_dim(w);
    ti->h = ui_clamp_dim(h);
    ti->selection_anchor = -1;
    ti->text_color = 0xFFFFFF;
    ti->bg_color = 0x202020;
    ti->cursor_color = 0xFFFFFF;
    ti->selection_color = 0x3355AA;
    ti->border_color = 0x606060;
    ti->border_color_focused = 0x88AAFF;
    ti->cursor_blink_ms = RT_UITEXTINPUT_DEFAULT_CURSOR_BLINK_MS;
    ti->visible = 1;
    ti->enabled = 1;
    rt_obj_set_finalizer(ti, uitextinput_finalizer);
    return ti;
}

void rt_uitextinput_set_text(void *ptr, rt_string text) {
    rt_uitextinput_impl *ti =
        checked_textinput(ptr, "UITextInput.SetText: expected Viper.Game.UI.HudTextInput");
    if (!ti)
        return;
    const char *s = text ? rt_string_cstr(text) : "";
    size_t len = text ? (size_t)rt_str_len(text) : 0;
    textinput_set_bytes(ti, s, len);
}

rt_string rt_uitextinput_get_text(void *ptr) {
    rt_uitextinput_impl *ti =
        checked_textinput(ptr, "UITextInput.GetText: expected Viper.Game.UI.HudTextInput");
    return ti ? rt_const_cstr(ti->text) : rt_str_empty();
}

int64_t rt_uitextinput_text_length(void *ptr) {
    rt_uitextinput_impl *ti =
        checked_textinput(ptr, "UITextInput.TextLength: expected Viper.Game.UI.HudTextInput");
    return ti ? ui_codepoint_count_bytes(ti->text, ti->text_bytes) : 0;
}

int64_t rt_uitextinput_get_cursor(void *ptr) {
    rt_uitextinput_impl *ti =
        checked_textinput(ptr, "UITextInput.GetCursor: expected Viper.Game.UI.HudTextInput");
    return ti ? ui_codepoint_for_byte(ti->text, ti->text_bytes, ti->cursor_byte) : 0;
}

void rt_uitextinput_set_cursor(void *ptr, int64_t pos) {
    rt_uitextinput_impl *ti =
        checked_textinput(ptr, "UITextInput.SetCursor: expected Viper.Game.UI.HudTextInput");
    if (ti)
        textinput_move_cursor(ti, ui_byte_for_codepoint(ti->text, ti->text_bytes, pos), 0);
}

void rt_uitextinput_select_all(void *ptr) {
    rt_uitextinput_impl *ti =
        checked_textinput(ptr, "UITextInput.SelectAll: expected Viper.Game.UI.HudTextInput");
    if (!ti)
        return;
    ti->selection_anchor = 0;
    ti->cursor_byte = ti->text_bytes;
}

void rt_uitextinput_clear_selection(void *ptr) {
    rt_uitextinput_impl *ti =
        checked_textinput(ptr, "UITextInput.ClearSelection: expected Viper.Game.UI.HudTextInput");
    if (ti)
        ti->selection_anchor = -1;
}

int8_t rt_uitextinput_has_selection(void *ptr) {
    rt_uitextinput_impl *ti =
        checked_textinput(ptr, "UITextInput.HasSelection: expected Viper.Game.UI.HudTextInput");
    return textinput_selection_range(ti, NULL, NULL);
}

rt_string rt_uitextinput_get_selected_text(void *ptr) {
    rt_uitextinput_impl *ti =
        checked_textinput(ptr, "UITextInput.GetSelectedText: expected Viper.Game.UI.HudTextInput");
    int64_t start = 0;
    int64_t end = 0;
    if (!textinput_selection_range(ti, &start, &end))
        return rt_str_empty();
    return rt_string_from_bytes(ti->text + start, (size_t)(end - start));
}

void rt_uitextinput_delete_selection(void *ptr) {
    rt_uitextinput_impl *ti =
        checked_textinput(ptr, "UITextInput.DeleteSelection: expected Viper.Game.UI.HudTextInput");
    textinput_delete_selection(ti);
}

int64_t rt_uitextinput_handle_key(void *ptr, int64_t key_code, int8_t shift_held) {
    rt_uitextinput_impl *ti =
        checked_textinput(ptr, "UITextInput.HandleKey: expected Viper.Game.UI.HudTextInput");
    if (!ti || !ti->enabled || !ti->focused)
        return 0;
    if (key_code == UI_KEY_BACKSPACE) {
        if (textinput_delete_selection(ti))
            return 1;
        if (ti->cursor_byte <= 0)
            return 0;
        return textinput_delete_range(
            ti, ui_prev_codepoint_byte(ti->text, ti->text_bytes, ti->cursor_byte), ti->cursor_byte);
    }
    if (key_code == UI_KEY_DELETE) {
        if (textinput_delete_selection(ti))
            return 1;
        if (ti->cursor_byte >= ti->text_bytes)
            return 0;
        return textinput_delete_range(
            ti, ti->cursor_byte, ui_next_codepoint_byte(ti->text, ti->text_bytes, ti->cursor_byte));
    }
    if (key_code == UI_KEY_LEFT) {
        if (!shift_held && rt_uitextinput_has_selection(ptr)) {
            int64_t start = 0;
            int64_t end = 0;
            textinput_selection_range(ti, &start, &end);
            (void)end;
            textinput_move_cursor(ti, start, 0);
        } else {
            textinput_move_cursor(
                ti, ui_prev_codepoint_byte(ti->text, ti->text_bytes, ti->cursor_byte), shift_held);
        }
        return 0;
    }
    if (key_code == UI_KEY_RIGHT) {
        if (!shift_held && rt_uitextinput_has_selection(ptr)) {
            int64_t start = 0;
            int64_t end = 0;
            (void)start;
            textinput_selection_range(ti, &start, &end);
            textinput_move_cursor(ti, end, 0);
        } else {
            textinput_move_cursor(
                ti, ui_next_codepoint_byte(ti->text, ti->text_bytes, ti->cursor_byte), shift_held);
        }
        return 0;
    }
    if (key_code == UI_KEY_HOME) {
        textinput_move_cursor(ti, 0, shift_held);
        return 0;
    }
    if (key_code == UI_KEY_END) {
        textinput_move_cursor(ti, ti->text_bytes, shift_held);
        return 0;
    }
    if (key_code == 1) {
        rt_uitextinput_select_all(ptr);
        return 0;
    }
    return 0;
}

int64_t rt_uitextinput_handle_text(void *ptr, rt_string typed_text) {
    rt_uitextinput_impl *ti =
        checked_textinput(ptr, "UITextInput.HandleText: expected Viper.Game.UI.HudTextInput");
    if (!ti || !typed_text || !ti->enabled || !ti->focused)
        return 0;
    const char *s = rt_string_cstr(typed_text);
    return textinput_insert_bytes(ti, s, (size_t)rt_str_len(typed_text));
}

void rt_uitextinput_handle_mouse_click(void *ptr, int64_t mx, int64_t my, int8_t shift_held) {
    rt_uitextinput_impl *ti =
        checked_textinput(ptr, "UITextInput.HandleMouseClick: expected Viper.Game.UI.HudTextInput");
    if (!ti || !ti->enabled || !ti->visible)
        return;
    ti->focused = ui_point_inside(ti->x, ti->y, ti->w, ti->h, mx, my);
    if (ti->focused)
        textinput_move_cursor(ti, textinput_byte_from_mouse(ti, mx), shift_held);
}

void rt_uitextinput_handle_mouse_drag(void *ptr, int64_t mx, int64_t my) {
    (void)my;
    rt_uitextinput_impl *ti =
        checked_textinput(ptr, "UITextInput.HandleMouseDrag: expected Viper.Game.UI.HudTextInput");
    if (!ti || !ti->enabled || !ti->focused)
        return;
    if (ti->selection_anchor < 0)
        ti->selection_anchor = ti->cursor_byte;
    ti->cursor_byte = textinput_byte_from_mouse(ti, mx);
}

void rt_uitextinput_update(void *ptr, int64_t delta_ms) {
    rt_uitextinput_impl *ti =
        checked_textinput(ptr, "UITextInput.Update: expected Viper.Game.UI.HudTextInput");
    if (!ti || delta_ms <= 0)
        return;
    if (delta_ms > INT64_MAX - ti->cursor_blink_elapsed)
        ti->cursor_blink_elapsed = 0;
    else
        ti->cursor_blink_elapsed += delta_ms;
}

void rt_uitextinput_draw(void *ptr, void *canvas) {
    rt_uitextinput_impl *ti =
        checked_textinput(ptr, "UITextInput.Draw: expected Viper.Game.UI.HudTextInput");
    if (!ti || !canvas || !ui_validate_canvas(canvas, "UITextInput.Draw: expected Canvas"))
        return;
    if (!ti->visible)
        return;
    rt_canvas_box(canvas, ti->x, ti->y, ti->w, ti->h, ti->bg_color);
    rt_canvas_frame(canvas,
                    ti->x,
                    ti->y,
                    ti->w,
                    ti->h,
                    ti->focused ? ti->border_color_focused : ti->border_color);

    const char *draw_text = ti->text;
    char password[RT_UITEXTINPUT_MAX_BYTES];
    if (ti->password_mode && ti->text_bytes > 0) {
        int64_t cps = ui_codepoint_count_bytes(ti->text, ti->text_bytes);
        if (cps >= RT_UITEXTINPUT_MAX_BYTES)
            cps = RT_UITEXTINPUT_MAX_BYTES - 1;
        memset(password, '*', (size_t)cps);
        password[cps] = '\0';
        draw_text = password;
    } else if (ti->text_bytes == 0 && !ti->focused && ti->placeholder[0] != '\0') {
        draw_text = ti->placeholder;
    }

    int64_t start = 0;
    int64_t end = 0;
    if (textinput_selection_range(ti, &start, &end)) {
        int64_t x0 = ti->x + 4 + ui_text_prefix_width(ti->text, start, ti->font, 1);
        int64_t sel_w = ui_text_prefix_width(ti->text + start, end - start, ti->font, 1);
        rt_canvas_box_alpha(canvas, x0, ti->y + 2, sel_w, ti->h - 4, ti->selection_color, 160);
    }
    ui_draw_text_basic(canvas,
                       ti->x + 4,
                       ti->y + (ti->h - 8) / 2,
                       draw_text,
                       ti->font,
                       1,
                       ti->text_bytes == 0 && !ti->focused ? 0x909090 : ti->text_color);
    if (ti->focused && ti->enabled && ti->cursor_blink_ms > 0 &&
        ((ti->cursor_blink_elapsed / ti->cursor_blink_ms) % 2) == 0) {
        int64_t cx = ti->x + 4 + ui_text_prefix_width(ti->text, ti->cursor_byte, ti->font, 1);
        rt_canvas_line(canvas, cx, ti->y + 3, cx, ti->y + ti->h - 4, ti->cursor_color);
    }
}

void rt_uitextinput_set_text_color(void *ptr, int64_t color) {
    rt_uitextinput_impl *ti =
        checked_textinput(ptr, "UITextInput.SetTextColor: expected Viper.Game.UI.HudTextInput");
    if (ti)
        ti->text_color = color;
}

int64_t rt_uitextinput_get_text_color(void *ptr) {
    rt_uitextinput_impl *ti =
        checked_textinput(ptr, "UITextInput.GetTextColor: expected Viper.Game.UI.HudTextInput");
    return ti ? ti->text_color : 0;
}

void rt_uitextinput_set_bg_color(void *ptr, int64_t color) {
    rt_uitextinput_impl *ti =
        checked_textinput(ptr, "UITextInput.SetBgColor: expected Viper.Game.UI.HudTextInput");
    if (ti)
        ti->bg_color = color;
}

int64_t rt_uitextinput_get_bg_color(void *ptr) {
    rt_uitextinput_impl *ti =
        checked_textinput(ptr, "UITextInput.GetBgColor: expected Viper.Game.UI.HudTextInput");
    return ti ? ti->bg_color : 0;
}

void rt_uitextinput_set_cursor_color(void *ptr, int64_t color) {
    rt_uitextinput_impl *ti =
        checked_textinput(ptr, "UITextInput.SetCursorColor: expected Viper.Game.UI.HudTextInput");
    if (ti)
        ti->cursor_color = color;
}

void rt_uitextinput_set_selection_color(void *ptr, int64_t color) {
    rt_uitextinput_impl *ti =
        checked_textinput(ptr, "UITextInput.SetSelectionColor: expected Viper.Game.UI.HudTextInput");
    if (ti)
        ti->selection_color = color;
}

void rt_uitextinput_set_border_color(void *ptr, int64_t color) {
    rt_uitextinput_impl *ti =
        checked_textinput(ptr, "UITextInput.SetBorderColor: expected Viper.Game.UI.HudTextInput");
    if (ti)
        ti->border_color = color;
}

void rt_uitextinput_set_border_color_focused(void *ptr, int64_t color) {
    rt_uitextinput_impl *ti = checked_textinput(
        ptr, "UITextInput.SetBorderColorFocused: expected Viper.Game.UI.HudTextInput");
    if (ti)
        ti->border_color_focused = color;
}

void rt_uitextinput_set_font(void *ptr, void *font) {
    rt_uitextinput_impl *ti =
        checked_textinput(ptr, "UITextInput.SetFont: expected Viper.Game.UI.HudTextInput");
    if (!ti || !ui_validate_bitmapfont(font, "UITextInput.SetFont: expected BitmapFont"))
        return;
    ui_replace_ref(&ti->font, font);
}

void rt_uitextinput_set_visible(void *ptr, int8_t visible) {
    rt_uitextinput_impl *ti =
        checked_textinput(ptr, "UITextInput.SetVisible: expected Viper.Game.UI.HudTextInput");
    if (ti)
        ti->visible = visible ? 1 : 0;
}

int8_t rt_uitextinput_get_visible(void *ptr) {
    rt_uitextinput_impl *ti =
        checked_textinput(ptr, "UITextInput.GetVisible: expected Viper.Game.UI.HudTextInput");
    return ti ? ti->visible : 0;
}

void rt_uitextinput_set_enabled(void *ptr, int8_t enabled) {
    rt_uitextinput_impl *ti =
        checked_textinput(ptr, "UITextInput.SetEnabled: expected Viper.Game.UI.HudTextInput");
    if (ti)
        ti->enabled = enabled ? 1 : 0;
}

int8_t rt_uitextinput_get_enabled(void *ptr) {
    rt_uitextinput_impl *ti =
        checked_textinput(ptr, "UITextInput.GetEnabled: expected Viper.Game.UI.HudTextInput");
    return ti ? ti->enabled : 0;
}

void rt_uitextinput_set_focused(void *ptr, int8_t focused) {
    rt_uitextinput_impl *ti =
        checked_textinput(ptr, "UITextInput.SetFocused: expected Viper.Game.UI.HudTextInput");
    if (!ti)
        return;
    ti->focused = focused ? 1 : 0;
    ti->cursor_blink_elapsed = 0;
    if (!ti->focused)
        ti->selection_anchor = -1;
}

int8_t rt_uitextinput_get_focused(void *ptr) {
    rt_uitextinput_impl *ti =
        checked_textinput(ptr, "UITextInput.GetFocused: expected Viper.Game.UI.HudTextInput");
    return ti ? ti->focused : 0;
}

void rt_uitextinput_set_password_mode(void *ptr, int8_t password) {
    rt_uitextinput_impl *ti =
        checked_textinput(ptr, "UITextInput.SetPasswordMode: expected Viper.Game.UI.HudTextInput");
    if (ti)
        ti->password_mode = password ? 1 : 0;
}

void rt_uitextinput_set_placeholder(void *ptr, rt_string placeholder) {
    rt_uitextinput_impl *ti =
        checked_textinput(ptr, "UITextInput.SetPlaceholder: expected Viper.Game.UI.HudTextInput");
    if (ti)
        ui_copy_text(ti->placeholder, sizeof(ti->placeholder), placeholder);
}

void rt_uitextinput_set_max_codepoints(void *ptr, int64_t max_cps) {
    rt_uitextinput_impl *ti =
        checked_textinput(ptr, "UITextInput.SetMaxCodepoints: expected Viper.Game.UI.HudTextInput");
    if (!ti)
        return;
    ti->max_codepoints = max_cps > 0 ? max_cps : 0;
    if (ti->max_codepoints > 0 &&
        ui_codepoint_count_bytes(ti->text, ti->text_bytes) > ti->max_codepoints)
        textinput_set_bytes(ti, ti->text, (size_t)ti->text_bytes);
}
