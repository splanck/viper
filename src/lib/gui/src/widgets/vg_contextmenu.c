//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: lib/gui/src/widgets/vg_contextmenu.c
// Purpose: Right-click context menu widget with submenu nesting, icon/check
//          column support, keyboard navigation, and a global widget→menu
//          registration registry.
// Key invariants:
//   - s_registry is a dynamically-grown flat array mapping widget pointers to
//     menus; stale entries are purged inline on every
//     traversal using swap-with-last.
//   - items[] is a heap-allocated pointer array; capacity doubles from 8 when
//     exhausted.
//   - hovered_index and active_submenu are reset on dismiss; parent_item tracks
//     submenu ownership and is cleared when either side is destroyed.
//   - Input capture is transferred to the parent menu (not released) when a
//     child submenu is dismissed while the parent is still visible.
//   - Window-edge clamping is intentionally deferred to contextmenu_paint where
//     the canvas/window handle is available, so the menu self-corrects on resize.
// Ownership/Lifetime:
//   - items[] and each vg_menu_item_t (text, shortcut strings) are owned by the
//     menu and freed in contextmenu_destroy / vg_contextmenu_clear.
//   - Submenu pointers in vg_menu_item_t.submenu are owned by the parent context
//     menu item. Destroying either side detaches the other to avoid stale pointers.
// Links: lib/gui/include/vg_ide_widgets.h,
//        lib/gui/include/vg_theme.h,
//        lib/gui/include/vg_event.h
//
//===----------------------------------------------------------------------===//
#include "../../../graphics/include/vgfx.h"
#include "../../include/vg_draw.h"
#include "../../include/vg_event.h"
#include "../../include/vg_ide_widgets.h"
#include "../../include/vg_theme.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

//=============================================================================
// Forward Declarations
//=============================================================================

static void contextmenu_destroy(vg_widget_t *widget);
static void contextmenu_measure(vg_widget_t *widget, float available_width, float available_height);
static void contextmenu_paint(vg_widget_t *widget, void *canvas);
static bool contextmenu_handle_event(vg_widget_t *widget, vg_event_t *event);

//=============================================================================
// ContextMenu VTable
//=============================================================================

static vg_widget_vtable_t g_contextmenu_vtable = {.destroy = contextmenu_destroy,
                                                  .measure = contextmenu_measure,
                                                  .arrange = NULL,
                                                  .paint = contextmenu_paint,
                                                  .handle_event = contextmenu_handle_event,
                                                  .can_focus = NULL,
                                                  .on_focus = NULL};

//=============================================================================
// Right-click Registry
//=============================================================================

typedef struct {
    vg_widget_t *widget;
    vg_contextmenu_t *menu;
} contextmenu_registry_entry_t;

static contextmenu_registry_entry_t *s_registry = NULL;
static size_t s_registry_count = 0;
static size_t s_registry_cap = 0;

static bool contextmenu_ensure_registry_capacity(size_t needed) {
    if (needed <= s_registry_cap)
        return true;
    size_t new_cap = s_registry_cap ? s_registry_cap : 64;
    while (new_cap < needed) {
        if (new_cap > SIZE_MAX / 2)
            return false;
        new_cap *= 2;
    }
    if (new_cap > SIZE_MAX / sizeof(*s_registry))
        return false;
    contextmenu_registry_entry_t *entries = realloc(s_registry, new_cap * sizeof(*s_registry));
    if (!entries)
        return false;
    s_registry = entries;
    s_registry_cap = new_cap;
    return true;
}

/// @brief Return true if menu is non-NULL and its base widget is still alive.
bool vg_contextmenu_is_live(const vg_contextmenu_t *menu) {
    return menu && vg_widget_is_live(&menu->base) && menu->base.type == VG_WIDGET_MENU;
}

/// @brief Remove all registry entries that reference menu, evicting stale entries inline.
static void contextmenu_unregister_menu(vg_contextmenu_t *menu) {
    if (!menu)
        return;
    for (size_t i = 0; i < s_registry_count;) {
        if (s_registry[i].menu == menu || !vg_widget_is_live(s_registry[i].widget)) {
            s_registry[i] = s_registry[--s_registry_count];
        } else {
            i++;
        }
    }
}

//=============================================================================
// Constants
//=============================================================================

#define ITEM_HEIGHT 28.0f
#define ITEM_PADDING_X 12.0f
#define ITEM_PADDING_Y 4.0f
#define SEPARATOR_HEIGHT 9.0f
#define SUBMENU_ARROW_WIDTH 20.0f
#define SHORTCUT_GAP 30.0f
#define SUBMENU_DELAY_MS 200
#define ICON_SLOT_WIDTH 22.0f
#define ICON_TEXT_GAP 8.0f
#define ICON_DRAW_SIZE 16.0f
#define VG_MENU_ITEM_MAGIC UINT64_C(0x56474D454E554954)
#define VG_MENU_ITEM_RETIRED_MAGIC UINT64_C(0x56474D454E555258)

//=============================================================================
// Helper Functions
//=============================================================================

/// @brief Allocate and initialise a menu item with the given label, shortcut, and action.
static vg_menu_item_t *create_menu_item(const char *label,
                                        const char *shortcut,
                                        void (*action)(void *),
                                        void *user_data) {
    char *label_copy = label ? vg_strdup(label) : NULL;
    char *shortcut_copy = shortcut ? vg_strdup(shortcut) : NULL;
    if ((label && !label_copy) || (shortcut && !shortcut_copy)) {
        free(label_copy);
        free(shortcut_copy);
        return NULL;
    }

    vg_menu_item_t *item = calloc(1, sizeof(vg_menu_item_t));
    if (!item) {
        free(label_copy);
        free(shortcut_copy);
        return NULL;
    }

    item->magic = VG_MENU_ITEM_MAGIC;
    item->text = label_copy;
    item->shortcut = shortcut_copy;
    item->action = action;
    item->action_data = user_data;
    item->enabled = true;
    item->checked = false;
    item->checkable = false;
    item->separator = false;
    item->icon.type = VG_ICON_NONE;
    item->submenu = NULL;

    return item;
}

/// @brief Free a menu item's text, shortcut, icon, and the item struct itself.
static void free_menu_item(vg_menu_item_t *item) {
    if (item) {
        if (item->submenu) {
            vg_contextmenu_t *submenu = (vg_contextmenu_t *)item->submenu;
            item->submenu = NULL;
            if (submenu->parent_item == item) {
                submenu->parent_item = NULL;
                submenu->parent_menu = NULL;
            }
            vg_contextmenu_destroy(submenu);
        }
        free(item->text);
        free(item->shortcut);
        vg_icon_destroy(&item->icon);
        free(item);
    }
}

static bool contextmenu_ensure_item_capacity(vg_contextmenu_t *menu, size_t needed) {
    if (!menu)
        return false;
    if (needed <= menu->item_capacity)
        return true;
    size_t new_cap = menu->item_capacity ? menu->item_capacity : 8;
    while (new_cap < needed) {
        if (new_cap > SIZE_MAX / 2)
            return false;
        new_cap *= 2;
    }
    if (new_cap > SIZE_MAX / sizeof(vg_menu_item_t *))
        return false;
    vg_menu_item_t **new_items = realloc(menu->items, new_cap * sizeof(vg_menu_item_t *));
    if (!new_items)
        return false;
    menu->items = new_items;
    menu->item_capacity = new_cap;
    return true;
}

/// @brief Retire a context-menu item without freeing its struct so stale handles become inert.
static void retire_menu_item(vg_contextmenu_t *menu, vg_menu_item_t *item) {
    if (!menu || !item)
        return;
    if (item->submenu) {
        vg_contextmenu_t *submenu = (vg_contextmenu_t *)item->submenu;
        item->submenu = NULL;
        if (submenu->parent_item == item) {
            submenu->parent_item = NULL;
            submenu->parent_menu = NULL;
        }
        vg_contextmenu_destroy(submenu);
    }
    free(item->text);
    item->text = NULL;
    free(item->shortcut);
    item->shortcut = NULL;
    vg_icon_destroy(&item->icon);
    item->action = NULL;
    item->action_data = NULL;
    item->owner_contextmenu = NULL;
    item->parent_menu = NULL;
    item->next = NULL;
    item->prev = NULL;
    item->magic = VG_MENU_ITEM_RETIRED_MAGIC;
    item->retired_next = menu->retired_items;
    menu->retired_items = item;
}

/// @brief Return true if the item occupies the leading icon/check column.
static bool item_uses_leading_column(const vg_menu_item_t *item) {
    return item && !item->separator &&
           (item->checkable || item->checked || item->icon.type != VG_ICON_NONE);
}

/// @brief Return true if any item in the menu uses the leading icon/check column.
static bool menu_uses_leading_column(const vg_contextmenu_t *menu) {
    if (!menu)
        return false;
    for (size_t i = 0; i < menu->item_count; i++) {
        if (item_uses_leading_column(menu->items[i]))
            return true;
    }
    return false;
}

/// @brief Clamp value to [min_value, max_value].
static int contextmenu_clampi(int value, int min_value, int max_value) {
    if (value < min_value)
        return min_value;
    if (value > max_value)
        return max_value;
    return value;
}

/// @brief Alpha-blend one RGBA pixel src into dst at the given per-pixel alpha.
static void contextmenu_blend_pixel(uint8_t *dst, const uint8_t *src, uint8_t alpha) {
    if (alpha == 0)
        return;

    if (alpha == 255) {
        dst[0] = src[0];
        dst[1] = src[1];
        dst[2] = src[2];
        dst[3] = 255;
        return;
    }

    const uint32_t inv = (uint32_t)(255 - alpha);
    dst[0] = (uint8_t)(((uint32_t)src[0] * alpha + (uint32_t)dst[0] * inv + 127u) / 255u);
    dst[1] = (uint8_t)(((uint32_t)src[1] * alpha + (uint32_t)dst[1] * inv + 127u) / 255u);
    dst[2] = (uint8_t)(((uint32_t)src[2] * alpha + (uint32_t)dst[2] * inv + 127u) / 255u);
    dst[3] = (uint8_t)(alpha + (((uint32_t)dst[3] * inv + 127u) / 255u));
}

/// @brief Blit a VG_ICON_IMAGE pixel array into the framebuffer with nearest-neighbour scaling.
static void contextmenu_draw_image_icon(
    vgfx_window_t win, const vg_icon_t *icon, float x, float y, float w, float h, bool enabled) {
    if (!icon || icon->type != VG_ICON_IMAGE || !icon->data.image.pixels || w <= 0.0f || h <= 0.0f)
        return;

    vgfx_framebuffer_t fb;
    if (!vgfx_get_framebuffer(win, &fb))
        return;

    const int sw = (int)icon->data.image.width;
    const int sh = (int)icon->data.image.height;
    if (sw <= 0 || sh <= 0)
        return;

    float draw_w = w;
    float draw_h = h;
    float scale = w / (float)sw;
    if ((float)sh * scale > h)
        scale = h / (float)sh;
    if (scale <= 0.0f)
        return;

    draw_w = (float)sw * scale;
    draw_h = (float)sh * scale;
    float draw_x = x + (w - draw_w) * 0.5f;
    float draw_y = y + (h - draw_h) * 0.5f;

    int start_x = contextmenu_clampi((int)draw_x, 0, fb.width);
    int start_y = contextmenu_clampi((int)draw_y, 0, fb.height);
    int end_x = contextmenu_clampi((int)(draw_x + draw_w), 0, fb.width);
    int end_y = contextmenu_clampi((int)(draw_y + draw_h), 0, fb.height);
    int clip_x = 0;
    int clip_y = 0;
    int clip_w = fb.width;
    int clip_h = fb.height;

    (void)vgfx_get_clip(win, &clip_x, &clip_y, &clip_w, &clip_h);
    if (start_x < clip_x)
        start_x = clip_x;
    if (start_y < clip_y)
        start_y = clip_y;
    if (end_x > clip_x + clip_w)
        end_x = clip_x + clip_w;
    if (end_y > clip_y + clip_h)
        end_y = clip_y + clip_h;

    for (int fb_y = start_y; fb_y < end_y; fb_y++) {
        for (int fb_x = start_x; fb_x < end_x; fb_x++) {
            float u = draw_w > 0.0f ? ((float)fb_x - draw_x) / draw_w : 0.0f;
            float v = draw_h > 0.0f ? ((float)fb_y - draw_y) / draw_h : 0.0f;
            int sx = contextmenu_clampi((int)(u * (float)sw), 0, sw - 1);
            int sy = contextmenu_clampi((int)(v * (float)sh), 0, sh - 1);
            const uint8_t *src = &icon->data.image.pixels[(sy * sw + sx) * 4];
            uint8_t alpha = src[3];
            if (!enabled)
                alpha = (uint8_t)(((uint32_t)alpha * 144u + 127u) / 255u);
            if (alpha == 0)
                continue;

            uint8_t *dst = &fb.pixels[fb_y * fb.stride + fb_x * 4];
            contextmenu_blend_pixel(dst, src, alpha);
        }
    }
}

/// @brief Encode a Unicode codepoint into a NUL-terminated UTF-8 byte sequence (out must be 5
/// bytes).
static bool contextmenu_encode_utf8(uint32_t cp, char out[5]) {
    if (!out)
        return false;
    memset(out, 0, 5);
    if (cp >= 0xD800 && cp <= 0xDFFF)
        return false;
    if (cp < 0x80) {
        out[0] = (char)cp;
    } else if (cp < 0x800) {
        out[0] = (char)(0xC0 | (cp >> 6));
        out[1] = (char)(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        out[0] = (char)(0xE0 | (cp >> 12));
        out[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        out[2] = (char)(0x80 | (cp & 0x3F));
    } else if (cp <= 0x10FFFF) {
        out[0] = (char)(0xF0 | (cp >> 18));
        out[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
        out[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
        out[3] = (char)(0x80 | (cp & 0x3F));
    } else {
        return false;
    }
    return true;
}

/// @brief Draw a single Unicode glyph centred in the given bounding rectangle.
static void contextmenu_draw_glyph(void *canvas,
                                   vg_font_t *font,
                                   float font_size,
                                   uint32_t codepoint,
                                   float x,
                                   float y,
                                   float w,
                                   float h,
                                   uint32_t color) {
    if (!font)
        return;

    char buf[5];
    if (!contextmenu_encode_utf8(codepoint, buf))
        return;

    vg_font_metrics_t font_metrics;
    vg_font_get_metrics(font, font_size, &font_metrics);
    vg_text_metrics_t text_metrics = {0};
    vg_font_measure_text(font, font_size, buf, &text_metrics);

    float glyph_x = x + (w - text_metrics.width) * 0.5f;
    float glyph_y = y + (h + font_metrics.ascent + font_metrics.descent) * 0.5f;
    vg_font_draw_text(canvas, font, font_size, glyph_x, glyph_y, buf, color);
}

/// @brief Trigger layout and/or paint updates on the owning menu after an item mutates.
static void contextmenu_mark_item_changed(vg_menu_item_t *item, bool layout_changed) {
    if (!item || !item->owner_contextmenu)
        return;

    vg_contextmenu_t *menu = item->owner_contextmenu;
    if (layout_changed && menu->is_visible) {
        contextmenu_measure(&menu->base, 0, 0);
        menu->base.width = menu->base.measured_width;
        menu->base.height = menu->base.measured_height;
    }
    if (layout_changed)
        menu->base.needs_layout = true;
    menu->base.needs_paint = true;
}

/// @brief Return the row height for item — SEPARATOR_HEIGHT for separators, ITEM_HEIGHT otherwise.
static float get_item_height(vg_menu_item_t *item) {
    return item->separator ? SEPARATOR_HEIGHT : ITEM_HEIGHT;
}

/// @brief Sum all item heights plus top/bottom ITEM_PADDING_Y to get the total menu height.
static float calculate_menu_height(vg_contextmenu_t *menu) {
    float height = ITEM_PADDING_Y * 2; // Top and bottom padding
    for (size_t i = 0; i < menu->item_count; i++) {
        height += get_item_height(menu->items[i]);
    }
    return height;
}

/// @brief Measure the widest item (icon + text + shortcut + arrow) clamped to min_width.
static float calculate_menu_width(vg_contextmenu_t *menu) {
    float max_width = (float)menu->min_width;
    vg_font_t *font = menu->font;
    float font_size = menu->font_size;
    bool has_leading_column = menu_uses_leading_column(menu);

    for (size_t i = 0; i < menu->item_count; i++) {
        vg_menu_item_t *item = menu->items[i];
        if (item->separator)
            continue;

        float width = ITEM_PADDING_X * 2;
        if (has_leading_column)
            width += ICON_SLOT_WIDTH + ICON_TEXT_GAP;

        if (font && item->text) {
            vg_text_metrics_t metrics;
            vg_font_measure_text(font, font_size, item->text, &metrics);
            width += metrics.width;
        }

        if (font && item->shortcut) {
            vg_text_metrics_t metrics;
            vg_font_measure_text(font, font_size, item->shortcut, &metrics);
            width += SHORTCUT_GAP + metrics.width;
        }

        if (item->submenu) {
            width += SUBMENU_ARROW_WIDTH;
        }

        if (width > max_width)
            max_width = width;
    }

    return max_width;
}

/// @brief Return the item index at local Y coordinate, or -1 if Y is outside all rows.
static int get_item_at_y(vg_contextmenu_t *menu, float y) {
    float current_y = ITEM_PADDING_Y;
    for (size_t i = 0; i < menu->item_count; i++) {
        float item_height = get_item_height(menu->items[i]);
        if (y >= current_y && y < current_y + item_height) {
            return (int)i;
        }
        current_y += item_height;
    }
    return -1;
}

//=============================================================================
// ContextMenu Implementation
//=============================================================================

/// @brief Create a context menu widget with default theme colours.
///
/// @details Allocates and zero-initialises a vg_contextmenu_t, wires the vtable,
///          and seeds all colour fields from the current theme. Items[], filters,
///          and bookmarks are empty; hovered_index and clicked_index are -1.
///          min_width defaults to 150, max_height to 400.
///
/// @return Heap-allocated context menu, or NULL on allocation failure.
vg_contextmenu_t *vg_contextmenu_create(void) {
    vg_contextmenu_t *menu = calloc(1, sizeof(vg_contextmenu_t));
    if (!menu)
        return NULL;

    // Initialize base widget
    vg_widget_init(&menu->base, VG_WIDGET_MENU, &g_contextmenu_vtable);

    vg_theme_t *theme = vg_theme_get_current();

    // Initialize context menu fields
    menu->items = NULL;
    menu->item_count = 0;
    menu->item_capacity = 0;

    menu->anchor_x = 0;
    menu->anchor_y = 0;

    menu->is_visible = false;
    menu->hovered_index = -1;
    menu->clicked_index = -1;
    menu->active_submenu = NULL;
    menu->parent_menu = NULL;
    menu->parent_item = NULL;

    menu->min_width = 150;
    menu->max_height = 400;

    menu->font = NULL;
    menu->font_size = theme->typography.size_normal;

    menu->bg_color = theme->colors.bg_primary;
    menu->hover_color = theme->colors.bg_hover;
    menu->text_color = theme->colors.fg_primary;
    menu->disabled_color = theme->colors.fg_secondary;
    menu->border_color = theme->colors.border_primary;
    menu->separator_color = theme->colors.border_secondary;

    menu->user_data = NULL;
    menu->on_select_data = NULL;
    menu->on_dismiss_data = NULL;
    menu->on_select = NULL;
    menu->on_dismiss = NULL;

    return menu;
}

/// @brief vtable destroy — unregisters from the right-click registry, releases input capture, frees
/// all items.
static void contextmenu_destroy(vg_widget_t *widget) {
    vg_contextmenu_t *menu = (vg_contextmenu_t *)widget;
    contextmenu_unregister_menu(menu);
    if (vg_widget_get_input_capture() == &menu->base)
        vg_widget_release_input_capture();

    vg_contextmenu_t *parent =
        menu->parent_item ? menu->parent_item->owner_contextmenu : menu->parent_menu;
    if (parent && parent->active_submenu == menu)
        parent->active_submenu = NULL;
    if (menu->parent_item && menu->parent_item->submenu == (struct vg_menu *)menu)
        menu->parent_item->submenu = NULL;
    menu->parent_item = NULL;
    menu->parent_menu = NULL;

    // Free all items
    for (size_t i = 0; i < menu->item_count; i++) {
        free_menu_item(menu->items[i]);
    }
    free(menu->items);
    vg_menu_item_t *retired = menu->retired_items;
    while (retired) {
        vg_menu_item_t *next = retired->retired_next;
        retired->retired_next = NULL;
        free_menu_item(retired);
        retired = next;
    }
    menu->retired_items = NULL;
}

/// @brief Destroy the context menu and free all items and their strings.
///
/// @param menu The menu to destroy; may be NULL.
void vg_contextmenu_destroy(vg_contextmenu_t *menu) {
    if (!menu)
        return;

    vg_widget_destroy(&menu->base);
}

/// @brief vtable measure — computes measured_width/height from item layout, capped at max_height.
static void contextmenu_measure(vg_widget_t *widget,
                                float available_width,
                                float available_height) {
    vg_contextmenu_t *menu = (vg_contextmenu_t *)widget;
    (void)available_width;
    (void)available_height;

    widget->measured_width = calculate_menu_width(menu);
    widget->measured_height = calculate_menu_height(menu);

    // Apply max height
    if (menu->max_height > 0 && widget->measured_height > menu->max_height) {
        widget->measured_height = (float)menu->max_height;
    }
}

/// @brief vtable paint — draws shadow, background, border, and all items; clamps position to
/// window.
static void contextmenu_paint(vg_widget_t *widget, void *canvas) {
    vg_contextmenu_t *menu = (vg_contextmenu_t *)widget;
    vg_theme_t *theme = vg_theme_get_current();

    if (!menu->is_visible)
        return;

    vgfx_window_t win = (vgfx_window_t)canvas;

    // Clamp menu position to keep it on-screen. show_at runs without a window
    // pointer, so we resolve and apply the clamp here on every paint — it also
    // adjusts correctly if the window is resized while the menu is open.
    int32_t win_w = 0, win_h = 0;
    if (win && vgfx_get_size(win, &win_w, &win_h)) {
        float mw = widget->width;
        float mh = widget->height;
        if (widget->x + mw > (float)win_w)
            widget->x = (float)win_w - mw;
        if (widget->y + mh > (float)win_h)
            widget->y = (float)win_h - mh;
        if (widget->x < 0.0f)
            widget->x = 0.0f;
        if (widget->y < 0.0f)
            widget->y = 0.0f;
    }

    float x = widget->x;
    float y = widget->y;
    float w = widget->width;
    float h = widget->height;

    // Real soft drop shadow + anti-aliased rounded panel (Refined Depth).
    float crad = theme->radius.md;
    vg_elevation_t el = theme->elevation.level2;
    vg_draw_round_rect_shadow(
        win, x, y, w, h, crad, el.blur, el.dx, el.dy, el.alpha, theme->elevation.shadow_rgb);
    vg_draw_round_rect_fill(win, x, y, w, h, crad, menu->bg_color);
    vg_draw_round_rect_stroke(win, x, y, w, h, crad, 1.0f, menu->border_color);

    // Draw items
    bool has_leading_column = menu_uses_leading_column(menu);
    float item_y = y + ITEM_PADDING_Y;
    for (size_t i = 0; i < menu->item_count; i++) {
        vg_menu_item_t *item = menu->items[i];
        float item_height = get_item_height(item);

        if (item->separator) {
            // Draw separator line
            float sep_y = item_y + item_height / 2;
            vgfx_fill_rect(
                win, (int32_t)(x + 4), (int32_t)sep_y, (int32_t)(w - 8), 1, menu->separator_color);
        } else {
            // Draw hover background
            if ((int)i == menu->hovered_index && item->enabled) {
                vgfx_fill_rect(win,
                               (int32_t)x,
                               (int32_t)item_y,
                               (int32_t)w,
                               (int32_t)item_height,
                               menu->hover_color);
            }

            uint32_t text_color = item->enabled ? menu->text_color : menu->disabled_color;
            float text_x = x + ITEM_PADDING_X;
            if (has_leading_column) {
                float icon_slot_x = x + ITEM_PADDING_X;
                if (item->checked) {
                    contextmenu_draw_glyph(canvas,
                                           menu->font,
                                           menu->font_size,
                                           0x2713u,
                                           icon_slot_x,
                                           item_y,
                                           ICON_SLOT_WIDTH,
                                           item_height,
                                           text_color);
                } else if (item->icon.type == VG_ICON_GLYPH) {
                    contextmenu_draw_glyph(canvas,
                                           menu->font,
                                           menu->font_size,
                                           item->icon.data.glyph,
                                           icon_slot_x,
                                           item_y,
                                           ICON_SLOT_WIDTH,
                                           item_height,
                                           text_color);
                } else if (item->icon.type == VG_ICON_IMAGE) {
                    float icon_x = icon_slot_x + (ICON_SLOT_WIDTH - ICON_DRAW_SIZE) * 0.5f;
                    float icon_y = item_y + (item_height - ICON_DRAW_SIZE) * 0.5f;
                    contextmenu_draw_image_icon(win,
                                                &item->icon,
                                                icon_x,
                                                icon_y,
                                                ICON_DRAW_SIZE,
                                                ICON_DRAW_SIZE,
                                                item->enabled);
                }
                text_x += ICON_SLOT_WIDTH + ICON_TEXT_GAP;
            }

            if (menu->font) {
                vg_font_metrics_t font_metrics;
                vg_font_get_metrics(menu->font, menu->font_size, &font_metrics);
                float text_y =
                    item_y + (item_height + font_metrics.ascent + font_metrics.descent) / 2;

                // Draw label
                if (item->text) {
                    vg_font_draw_text(canvas,
                                      menu->font,
                                      menu->font_size,
                                      text_x,
                                      text_y,
                                      item->text,
                                      text_color);
                }

                // Draw shortcut
                if (item->shortcut) {
                    vg_text_metrics_t shortcut_metrics;
                    vg_font_measure_text(
                        menu->font, menu->font_size, item->shortcut, &shortcut_metrics);
                    float shortcut_x = x + w - ITEM_PADDING_X - shortcut_metrics.width;
                    if (item->submenu)
                        shortcut_x -= SUBMENU_ARROW_WIDTH;

                    vg_font_draw_text(canvas,
                                      menu->font,
                                      menu->font_size,
                                      shortcut_x,
                                      text_y,
                                      item->shortcut,
                                      menu->disabled_color);
                }

                // Draw submenu arrow
                if (item->submenu) {
                    float arrow_x = x + w - ITEM_PADDING_X - 10;
                    vg_font_draw_text(
                        canvas, menu->font, menu->font_size, arrow_x, text_y, ">", text_color);
                }
            }
        }

        item_y += item_height;
    }
}

/// @brief vtable handle_event — routes mouse hover/click and keyboard nav (arrows, Enter, Escape).
static bool contextmenu_handle_event(vg_widget_t *widget, vg_event_t *event) {
    vg_contextmenu_t *menu = (vg_contextmenu_t *)widget;

    if (!menu->is_visible)
        return false;

    switch (event->type) {
        case VG_EVENT_MOUSE_MOVE: {
            float local_x = event->mouse.x;
            float local_y = event->mouse.y;

            // Check if inside menu
            if (local_x >= 0 && local_x < widget->width && local_y >= 0 &&
                local_y < widget->height) {
                int new_hover = get_item_at_y(menu, local_y);
                if (new_hover != menu->hovered_index) {
                    menu->hovered_index = new_hover;
                    widget->needs_paint = true;

                    // Close active submenu when moving to different item
                    if (menu->active_submenu) {
                        vg_contextmenu_dismiss(menu->active_submenu);
                        menu->active_submenu = NULL;
                    }

                    // Open submenu if hovering over submenu item
                    if (new_hover >= 0 && (size_t)new_hover < menu->item_count) {
                        vg_menu_item_t *item = menu->items[new_hover];
                        if (item->submenu && item->enabled) {
                            // Calculate submenu position
                            float item_y = ITEM_PADDING_Y;
                            for (int j = 0; j < new_hover; j++) {
                                item_y += get_item_height(menu->items[j]);
                            }
                            vg_contextmenu_t *submenu = (vg_contextmenu_t *)item->submenu;
                            vg_contextmenu_show_at(submenu,
                                                   (int)(widget->x + widget->width),
                                                   (int)(widget->y + item_y));
                            submenu->parent_menu = menu;
                            menu->active_submenu = submenu;
                        }
                    }
                }
                return true;
            } else {
                if (menu->hovered_index != -1) {
                    menu->hovered_index = -1;
                    widget->needs_paint = true;
                }
            }
            return false;
        }

        case VG_EVENT_MOUSE_DOWN: {
            float local_x = event->mouse.x;
            float local_y = event->mouse.y;

            // Check if inside menu
            if (local_x >= 0 && local_x < widget->width && local_y >= 0 &&
                local_y < widget->height) {
                int clicked = get_item_at_y(menu, local_y);
                if (clicked >= 0 && (size_t)clicked < menu->item_count) {
                    vg_menu_item_t *item = menu->items[clicked];

                    if (!item->separator && item->enabled && !item->submenu) {
                        // Record clicked index for rt_contextmenu_get_clicked_item
                        menu->clicked_index = clicked;
                        item->was_clicked = true;

                        // Invoke action
                        if (item->action) {
                            item->action(item->action_data);
                        }

                        // Invoke on_select callback
                        if (menu->on_select)
                            menu->on_select(menu, item, menu->on_select_data);

                        // Dismiss entire menu chain
                        vg_contextmenu_t *root = menu;
                        while (root->parent_menu) {
                            root = root->parent_menu;
                        }
                        vg_contextmenu_dismiss(root);

                        return true;
                    }
                }
                return true;
            } else {
                // Clicked outside - dismiss
                // Find root menu
                vg_contextmenu_t *root = menu;
                while (root->parent_menu) {
                    root = root->parent_menu;
                }
                vg_contextmenu_dismiss(root);
                return true;
            }
        }

        case VG_EVENT_KEY_DOWN: {
            if (event->key.key == VG_KEY_ESCAPE) {
                vg_contextmenu_t *root = menu;
                while (root->parent_menu) {
                    root = root->parent_menu;
                }
                vg_contextmenu_dismiss(root);
                return true;
            }

            if (event->key.key == VG_KEY_UP) {
                // Move selection up
                int new_index = menu->hovered_index - 1;
                while (new_index >= 0 && menu->items[new_index]->separator) {
                    new_index--;
                }
                if (new_index >= 0) {
                    menu->hovered_index = new_index;
                    widget->needs_paint = true;
                }
                return true;
            }

            if (event->key.key == VG_KEY_DOWN) {
                // Move selection down
                int new_index = menu->hovered_index + 1;
                while ((size_t)new_index < menu->item_count && menu->items[new_index]->separator) {
                    new_index++;
                }
                if ((size_t)new_index < menu->item_count) {
                    menu->hovered_index = new_index;
                    widget->needs_paint = true;
                }
                return true;
            }

            if (event->key.key == VG_KEY_ENTER) {
                if (menu->hovered_index >= 0 && (size_t)menu->hovered_index < menu->item_count) {
                    vg_menu_item_t *item = menu->items[menu->hovered_index];
                    if (!item->separator && item->enabled && !item->submenu) {
                        menu->clicked_index = menu->hovered_index;
                        item->was_clicked = true;
                        if (item->action) {
                            item->action(item->action_data);
                        }
                        if (menu->on_select)
                            menu->on_select(menu, item, menu->on_select_data);
                        vg_contextmenu_t *root = menu;
                        while (root->parent_menu) {
                            root = root->parent_menu;
                        }
                        vg_contextmenu_dismiss(root);
                    }
                }
                return true;
            }

            if (event->key.key == VG_KEY_RIGHT) {
                // Open submenu
                if (menu->hovered_index >= 0 && (size_t)menu->hovered_index < menu->item_count) {
                    vg_menu_item_t *item = menu->items[menu->hovered_index];
                    if (item->submenu && item->enabled) {
                        float item_y = ITEM_PADDING_Y;
                        for (int j = 0; j < menu->hovered_index; j++) {
                            item_y += get_item_height(menu->items[j]);
                        }
                        vg_contextmenu_t *submenu = (vg_contextmenu_t *)item->submenu;
                        vg_contextmenu_show_at(
                            submenu, (int)(widget->x + widget->width), (int)(widget->y + item_y));
                        submenu->parent_menu = menu;
                        menu->active_submenu = submenu;
                        // Move focus to submenu
                        submenu->hovered_index = 0;
                    }
                }
                return true;
            }

            if (event->key.key == VG_KEY_LEFT) {
                // Close submenu / return to parent
                if (menu->parent_menu) {
                    vg_contextmenu_dismiss(menu);
                }
                return true;
            }

            return false;
        }

        default:
            break;
    }

    return false;
}

//=============================================================================
// ContextMenu API
//=============================================================================

/// @brief Append a labelled item to the menu and return its handle.
///
/// @details Items are stored in a pointer array that doubles from capacity 8 on
///          overflow. Both label and shortcut are copied internally. action is
///          invoked with user_data when the item is clicked; NULL action is allowed.
///          The returned handle remains valid until the item is removed or the menu
///          is destroyed.
///
/// @param menu      The context menu to append to; may be NULL (returns NULL).
/// @param label     Display label; copied internally, may be NULL.
/// @param shortcut  Keyboard shortcut hint shown on the right (e.g., "Ctrl+C"); may be NULL.
/// @param action    Callback invoked on click, or NULL for no action.
/// @param user_data Opaque pointer forwarded to action.
/// @return          New menu item handle, or NULL on allocation failure.
vg_menu_item_t *vg_contextmenu_add_item(vg_contextmenu_t *menu,
                                        const char *label,
                                        const char *shortcut,
                                        void (*action)(void *),
                                        void *user_data) {
    if (!menu)
        return NULL;

    vg_menu_item_t *item = create_menu_item(label, shortcut, action, user_data);
    if (!item)
        return NULL;
    item->owner_contextmenu = menu;

    if (menu->item_count == SIZE_MAX ||
        !contextmenu_ensure_item_capacity(menu, menu->item_count + 1)) {
        free_menu_item(item);
        return NULL;
    }

    menu->items[menu->item_count++] = item;
    return item;
}

/// @brief Append a labelled item that opens submenu when hovered or navigated to.
///
/// @details The submenu pointer is stored in item->submenu and owned by the
///          parent menu item. Destroying the parent destroys attached submenus;
///          destroying a child directly detaches the parent item first.
///          Hovering the item triggers vg_contextmenu_show_at on the submenu
///          positioned flush to the right edge of the parent menu row.
///
/// @param menu    The parent context menu to append to; may be NULL.
/// @param label   Display label; copied internally.
/// @param submenu The nested menu to open; must outlive the parent menu.
/// @return        New menu item handle, or NULL on allocation failure.
vg_menu_item_t *vg_contextmenu_add_submenu(vg_contextmenu_t *menu,
                                           const char *label,
                                           vg_contextmenu_t *submenu) {
    if (!menu || !submenu)
        return NULL;

    vg_menu_item_t *item = create_menu_item(label, NULL, NULL, NULL);
    if (!item)
        return NULL;
    if (menu->item_count == SIZE_MAX ||
        !contextmenu_ensure_item_capacity(menu, menu->item_count + 1)) {
        free_menu_item(item);
        return NULL;
    }

    item->owner_contextmenu = menu;
    item->submenu = (struct vg_menu *)submenu;
    submenu->parent_menu = menu;
    submenu->parent_item = item;

    menu->items[menu->item_count++] = item;
    return item;
}

/// @brief Append a horizontal separator line to the menu.
///
/// @param menu The context menu to append to; may be NULL (returns NULL).
/// @return     New separator item handle, or NULL on allocation failure.
vg_menu_item_t *vg_contextmenu_add_separator(vg_contextmenu_t *menu) {
    if (!menu)
        return NULL;

    vg_menu_item_t *item = calloc(1, sizeof(vg_menu_item_t));
    if (!item)
        return NULL;

    item->magic = VG_MENU_ITEM_MAGIC;
    item->separator = true;
    item->owner_contextmenu = menu;

    if (menu->item_count == SIZE_MAX ||
        !contextmenu_ensure_item_capacity(menu, menu->item_count + 1)) {
        free(item);
        return NULL;
    }

    menu->items[menu->item_count++] = item;
    return item;
}

/// @brief Remove and free all items from the menu without destroying the menu itself.
///
/// @param menu The context menu to clear; may be NULL.
void vg_contextmenu_clear(vg_contextmenu_t *menu) {
    if (!menu)
        return;

    if (menu->active_submenu) {
        vg_contextmenu_dismiss(menu->active_submenu);
        menu->active_submenu = NULL;
    }
    menu->hovered_index = -1;
    menu->clicked_index = -1;

    for (size_t i = 0; i < menu->item_count; i++) {
        retire_menu_item(menu, menu->items[i]);
    }
    menu->item_count = 0;
    menu->base.needs_layout = true;
    menu->base.needs_paint = true;
}

/// @brief Unlink and free one exact retained context-menu item record.
/// @details Address comparison is performed against the owner's valid chain before the candidate
///          is dereferenced, so foreign pointers fail safely.
bool vg_contextmenu_reclaim_retired_item(vg_contextmenu_t *menu, vg_menu_item_t *item) {
    if (!menu || !item)
        return false;
    vg_menu_item_t **link = &menu->retired_items;
    while (*link) {
        vg_menu_item_t *candidate = *link;
        if (candidate == item) {
            *link = candidate->retired_next;
            candidate->retired_next = NULL;
            free_menu_item(candidate);
            return true;
        }
        link = &candidate->retired_next;
    }
    return false;
}

/// @brief Enable or disable a menu item, triggering a repaint without layout changes.
///
/// @param item    The item to modify; may be NULL.
/// @param enabled false to grey out and make non-interactive.
void vg_contextmenu_item_set_enabled(vg_menu_item_t *item, bool enabled) {
    if (item && item->magic == VG_MENU_ITEM_MAGIC && item->owner_contextmenu) {
        item->enabled = enabled;
        contextmenu_mark_item_changed(item, false);
    }
}

/// @brief Set the checked state of an item, showing or hiding the checkmark glyph.
///
/// @details Triggers a layout-and-paint update because the leading column may appear
///          or disappear depending on whether any item in the menu is checked.
///
/// @param item    The item to modify; may be NULL.
/// @param checked true to display a checkmark (✓) in the leading column.
void vg_contextmenu_item_set_checked(vg_menu_item_t *item, bool checked) {
    if (item && item->magic == VG_MENU_ITEM_MAGIC && item->owner_contextmenu) {
        item->checkable = true;
        item->checked = checked;
        contextmenu_mark_item_changed(item, true);
    }
}

/// @brief Replace the icon displayed in the leading column for an item.
///
/// @details Destroys the existing icon before assigning the new one, then triggers
///          layout and paint. Accepts VG_ICON_GLYPH (Unicode codepoint) or
///          VG_ICON_IMAGE (pixel data); VG_ICON_NONE removes the icon slot.
///
/// @param item The item to modify; may be NULL.
/// @param icon The new icon value; ownership of pixel data transfers to the item.
void vg_contextmenu_item_set_icon(vg_menu_item_t *item, vg_icon_t icon) {
    if (!item || item->magic != VG_MENU_ITEM_MAGIC || !item->owner_contextmenu) {
        vg_icon_destroy(&icon);
        return;
    }

    vg_icon_destroy(&item->icon);
    item->icon = icon;
    contextmenu_mark_item_changed(item, true);
}

/// @brief Show the menu anchored at screen-space coordinates (x, y).
///
/// @details Records anchor_x/anchor_y, runs measure to compute size, positions the
///          widget, and acquires input capture. Window-edge clamping is deferred to
///          contextmenu_paint where the window handle is available.
///
/// @param menu The menu to display; may be NULL.
/// @param x    Screen X of the top-left corner (pre-clamp).
/// @param y    Screen Y of the top-left corner (pre-clamp).
void vg_contextmenu_show_at(vg_contextmenu_t *menu, int x, int y) {
    if (!menu)
        return;

    menu->anchor_x = x;
    menu->anchor_y = y;
    menu->is_visible = true;
    menu->hovered_index = -1;
    menu->clicked_index = -1;

    // Calculate size
    contextmenu_measure(&menu->base, 0, 0);

    // Position menu (clamp-to-window happens in contextmenu_paint where the
    // window handle is reliably available via the canvas argument).
    menu->base.x = (float)x;
    menu->base.y = (float)y;
    menu->base.width = menu->base.measured_width;
    menu->base.height = menu->base.measured_height;

    menu->base.visible = true;
    menu->base.needs_paint = true;
    vg_widget_set_input_capture(&menu->base);
}

/// @brief Show the menu below widget's bottom-left corner with an optional pixel offset.
///
/// @details Resolves widget's screen bounds then calls vg_contextmenu_show_at with
///          (screen_x + offset_x, screen_y + screen_h + offset_y).
///
/// @param menu     The menu to display; may be NULL.
/// @param widget   The widget to anchor below; may be NULL.
/// @param offset_x Additional X offset in pixels.
/// @param offset_y Additional Y offset in pixels.
void vg_contextmenu_show_for_widget(vg_contextmenu_t *menu,
                                    vg_widget_t *widget,
                                    int offset_x,
                                    int offset_y) {
    if (!menu || !widget)
        return;

    float screen_x = 0.0f;
    float screen_y = 0.0f;
    float screen_h = 0.0f;
    vg_widget_get_screen_bounds(widget, &screen_x, &screen_y, NULL, &screen_h);

    int x = (int)screen_x + offset_x;
    int y = (int)(screen_y + screen_h) + offset_y;
    vg_contextmenu_show_at(menu, x, y);
}

/// @brief Hide the menu and its entire submenu chain, firing the on_dismiss callback.
///
/// @details Recursively dismisses active_submenu first, then hides this menu and
///          either transfers input capture back to the parent (if still visible) or
///          releases it entirely. parent_menu is cleared to prevent dangling references.
///
/// @param menu The menu to dismiss; may be NULL.
void vg_contextmenu_dismiss(vg_contextmenu_t *menu) {
    if (!menu)
        return;
    vg_contextmenu_t *parent = menu->parent_menu;

    // Dismiss active submenu first
    if (menu->active_submenu) {
        vg_contextmenu_dismiss(menu->active_submenu);
        menu->active_submenu = NULL;
    }

    menu->is_visible = false;
    menu->hovered_index = -1;
    menu->parent_menu = NULL;
    menu->base.visible = false;

    if (vg_widget_get_input_capture() == &menu->base) {
        if (parent && parent->is_visible && vg_contextmenu_is_live(parent)) {
            vg_widget_set_input_capture(&parent->base);
        } else {
            vg_widget_release_input_capture();
        }
    }

    // Invoke dismiss callback
    if (menu->on_dismiss) {
        menu->on_dismiss(menu, menu->on_dismiss_data);
    }
}

/// @brief Register the callback invoked when the user clicks a menu item.
///
/// @details Fired after the item's own action callback and before the menu chain
///          is dismissed. The callback receives the menu, the selected item, and
///          user_data. Passing NULL removes the callback.
///
/// @param menu      The context menu to configure; may be NULL.
/// @param callback  Function called on item selection, or NULL to clear.
/// @param user_data Opaque pointer forwarded to the callback.
void vg_contextmenu_set_on_select(vg_contextmenu_t *menu,
                                  void (*callback)(vg_contextmenu_t *, vg_menu_item_t *, void *),
                                  void *user_data) {
    if (!menu)
        return;
    menu->on_select = callback;
    menu->on_select_data = user_data;
    menu->user_data = user_data;
}

/// @brief Register the callback invoked when the menu is dismissed (cancel or selection).
///
/// @param menu      The context menu to configure; may be NULL.
/// @param callback  Function called on dismissal, or NULL to clear.
/// @param user_data Opaque pointer forwarded to the callback.
void vg_contextmenu_set_on_dismiss(vg_contextmenu_t *menu,
                                   void (*callback)(vg_contextmenu_t *, void *),
                                   void *user_data) {
    if (!menu)
        return;
    menu->on_dismiss = callback;
    menu->on_dismiss_data = user_data;
}

/// @brief Associate a context menu with a widget so right-clicking the widget opens it.
///
/// @details Stores a (widget, menu) pair in the global registry. If widget is
///          already registered its menu is replaced. Stale entries (dead widgets/menus) are evicted
///          inline using swap-with-last during traversal.
///
/// @param widget The widget to attach the menu to; must be live (non-NULL).
/// @param menu   The context menu to open on right-click; must be live (non-NULL).
void vg_contextmenu_register_for_widget(vg_widget_t *widget, vg_contextmenu_t *menu) {
    if (!vg_widget_is_live(widget) || !vg_contextmenu_is_live(menu))
        return;

    // Update existing entry if widget already registered
    for (size_t i = 0; i < s_registry_count;) {
        if (!vg_widget_is_live(s_registry[i].widget) ||
            !vg_contextmenu_is_live(s_registry[i].menu)) {
            s_registry[i] = s_registry[--s_registry_count];
            continue;
        }
        if (s_registry[i].widget == widget) {
            s_registry[i].menu = menu;
            return;
        }
        i++;
    }

    if (!contextmenu_ensure_registry_capacity(s_registry_count + 1))
        return;
    s_registry[s_registry_count].widget = widget;
    s_registry[s_registry_count].menu = menu;
    s_registry_count++;
}

/// @brief Remove a widget's right-click menu registration from the global registry.
///
/// @param widget The widget to detach; may be NULL (no-op).
void vg_contextmenu_unregister_for_widget(vg_widget_t *widget) {
    if (!widget)
        return;

    for (size_t i = 0; i < s_registry_count;) {
        if (s_registry[i].widget == widget || !vg_widget_is_live(s_registry[i].widget) ||
            !vg_contextmenu_is_live(s_registry[i].menu)) {
            // Swap with last entry and shrink
            s_registry[i] = s_registry[--s_registry_count];
            continue;
        }
        i++;
    }
}

/// @brief Check if a right-click event on widget should open its registered context menu.
///
/// @details Ignores all events except VG_EVENT_MOUSE_DOWN with VG_MOUSE_RIGHT.
///          Searches the registry for widget, shows the menu at the screen cursor
///          position, evicts stale entries inline, and returns true if a menu was shown.
///
/// @param widget The widget that received the event; must be live.
/// @param event  The event to process; may be NULL (returns false).
/// @return       true if a registered menu was shown, false otherwise.
bool vg_contextmenu_process_event(vg_widget_t *widget, vg_event_t *event) {
    if (!vg_widget_is_live(widget) || !event)
        return false;

    if (event->type != VG_EVENT_MOUSE_DOWN || event->mouse.button != VG_MOUSE_RIGHT)
        return false;

    for (size_t i = 0; i < s_registry_count;) {
        if (!vg_widget_is_live(s_registry[i].widget) ||
            !vg_contextmenu_is_live(s_registry[i].menu)) {
            s_registry[i] = s_registry[--s_registry_count];
            continue;
        }
        if (s_registry[i].widget == widget) {
            vg_contextmenu_show_at(
                s_registry[i].menu, (int)event->mouse.screen_x, (int)event->mouse.screen_y);
            return true;
        }
        i++;
    }
    return false;
}

/// @brief Set the font and size used to render menu item labels and shortcuts.
///
/// @param menu The context menu to configure; may be NULL.
/// @param font Font to use; NULL retains the current font.
/// @param size Font size in points; ignored (current size kept) if size <= 0.
void vg_contextmenu_set_font(vg_contextmenu_t *menu, vg_font_t *font, float size) {
    if (!menu)
        return;
    vg_font_t *new_font = font ? font : menu->font;
    float new_size = size > 0.0f ? size : menu->font_size;
    if (menu->font != new_font || menu->font_size != new_size) {
        menu->font = new_font;
        menu->font_size = new_size;
        menu->base.needs_layout = true;
        menu->base.needs_paint = true;
    }
    for (size_t i = 0; i < menu->item_count; i++) {
        vg_menu_item_t *item = menu->items[i];
        if (item && item->submenu)
            vg_contextmenu_set_font((vg_contextmenu_t *)item->submenu, font, size);
    }
}

/// @brief Apply theme colors to a context-menu subtree.
/// @details Context menus are often standalone overlays created once and shown
///          many times. Reapplying colors at show/render time keeps long-lived
///          menus synchronized with light/dark theme changes without requiring
///          scripts to rebuild their menu models.
/// @param menu Context menu root to update; may be NULL.
/// @param theme Theme whose colors should be copied; NULL uses current theme.
void vg_contextmenu_apply_theme(vg_contextmenu_t *menu, const vg_theme_t *theme) {
    if (!menu)
        return;
    if (!theme)
        theme = vg_theme_get_current();
    if (!theme)
        return;

    float font_size = menu->font_size > 0.0f ? menu->font_size : theme->typography.size_normal;
    bool changed =
        menu->bg_color != theme->colors.bg_primary || menu->hover_color != theme->colors.bg_hover ||
        menu->text_color != theme->colors.fg_primary ||
        menu->disabled_color != theme->colors.fg_secondary ||
        menu->border_color != theme->colors.border_primary ||
        menu->separator_color != theme->colors.border_secondary || menu->font_size != font_size;
    menu->bg_color = theme->colors.bg_primary;
    menu->hover_color = theme->colors.bg_hover;
    menu->text_color = theme->colors.fg_primary;
    menu->disabled_color = theme->colors.fg_secondary;
    menu->border_color = theme->colors.border_primary;
    menu->separator_color = theme->colors.border_secondary;
    menu->font_size = font_size;
    if (changed)
        menu->base.needs_paint = true;

    for (size_t i = 0; i < menu->item_count; i++) {
        vg_menu_item_t *item = menu->items[i];
        if (item && item->submenu)
            vg_contextmenu_apply_theme((vg_contextmenu_t *)item->submenu, theme);
    }
}
