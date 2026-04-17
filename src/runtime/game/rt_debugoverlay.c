//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/game/rt_debugoverlay.c
// Purpose: Debug overlay rendering FPS, delta time, and custom watched
//   variables as a semi-transparent panel in the top-right canvas corner.
//   Designed for use during game development; can be toggled with a single
//   key binding (e.g., F3).
//
// Key invariants:
//   - FPS is a rolling average over RT_DEBUG_FPS_HISTORY (16) frame deltas.
//   - Watch entries are stored in a flat array with linear scan (max 16).
//   - Drawing uses public rt_canvas_* APIs — no internal struct access.
//   - Disabled by default; no rendering cost when off.
//
// Ownership/Lifetime:
//   - GC-managed via rt_obj_new_i64; watch name strings are not retained
//     (they are copied into fixed-length buffers).
//
// Links: src/runtime/game/rt_debugoverlay.h (public API)
//
//===----------------------------------------------------------------------===//

#include "rt_debugoverlay.h"
#include "rt_graphics.h"
#include "rt_internal.h"
#include "rt_object.h"
#include "rt_string.h"

#include <string.h>

// Helper: create a runtime string from a C string
static rt_string make_string(const char *s) {
    return rt_string_from_bytes(s, (int64_t)strlen(s));
}

/// Maximum watch name length (including null terminator).
#define WATCH_NAME_MAX 32

/// A single watch entry: name + integer value.
typedef struct {
    char name[WATCH_NAME_MAX];
    int64_t value;
    int8_t active;
} watch_entry_t;

/// Internal DebugOverlay state.
struct rt_debugoverlay_impl {
    int8_t enabled;
    int64_t frame_times[RT_DEBUG_FPS_HISTORY]; ///< Ring buffer of dt values (ms).
    int64_t frame_index;                       ///< Current ring buffer write position.
    int64_t frame_count;                       ///< Number of frames recorded (up to HISTORY).
    int64_t total_frames;                      ///< Total frames since creation.
    watch_entry_t watches[RT_DEBUG_MAX_WATCHES];
    int64_t watch_count;
};

/// @brief Create a new debugoverlay object.
rt_debugoverlay rt_debugoverlay_new(void) {
    struct rt_debugoverlay_impl *dbg = rt_obj_new_i64(0, sizeof(struct rt_debugoverlay_impl));
    if (!dbg)
        return NULL;

    dbg->enabled = 0;
    dbg->frame_index = 0;
    dbg->frame_count = 0;
    dbg->total_frames = 0;
    dbg->watch_count = 0;
    memset(dbg->frame_times, 0, sizeof(dbg->frame_times));
    memset(dbg->watches, 0, sizeof(dbg->watches));

    return dbg;
}

/// @brief Release resources and destroy the debugoverlay.
void rt_debugoverlay_destroy(rt_debugoverlay dbg) {
    // GC-managed; no manual free needed.
    (void)dbg;
}

/// @brief Enable the debugoverlay.
void rt_debugoverlay_enable(rt_debugoverlay dbg) {
    if (!dbg)
        return;
    dbg->enabled = 1;
}

/// @brief Disable the debugoverlay.
void rt_debugoverlay_disable(rt_debugoverlay dbg) {
    if (!dbg)
        return;
    dbg->enabled = 0;
}

/// @brief Toggle the debugoverlay.
void rt_debugoverlay_toggle(rt_debugoverlay dbg) {
    if (!dbg)
        return;
    dbg->enabled = dbg->enabled ? 0 : 1;
}

/// @brief Check whether the debug overlay is currently visible.
int8_t rt_debugoverlay_is_enabled(rt_debugoverlay dbg) {
    if (!dbg)
        return 0;
    return dbg->enabled;
}

/// @brief Update the debugoverlay state (called per frame/tick).
void rt_debugoverlay_update(rt_debugoverlay dbg, int64_t dt_ms) {
    if (!dbg)
        return;

    dbg->frame_times[dbg->frame_index] = dt_ms;
    dbg->frame_index = (dbg->frame_index + 1) % RT_DEBUG_FPS_HISTORY;
    if (dbg->frame_count < RT_DEBUG_FPS_HISTORY)
        dbg->frame_count++;
    dbg->total_frames++;
}

/// Find a watch entry by name.
/// Returns index, or -1 if not found.
static int64_t find_watch(rt_debugoverlay dbg, const char *name) {
    for (int64_t i = 0; i < RT_DEBUG_MAX_WATCHES; i++) {
        if (dbg->watches[i].active && strcmp(dbg->watches[i].name, name) == 0)
            return i;
    }
    return -1;
}

/// @brief Watch the debugoverlay.
void rt_debugoverlay_watch(rt_debugoverlay dbg, rt_string name, int64_t value) {
    if (!dbg || !name)
        return;

    const char *cname = rt_string_cstr(name);
    if (!cname)
        return;

    // Check if already exists — update value
    int64_t idx = find_watch(dbg, cname);
    if (idx >= 0) {
        dbg->watches[idx].value = value;
        return;
    }

    // Find an empty slot
    for (int64_t i = 0; i < RT_DEBUG_MAX_WATCHES; i++) {
        if (!dbg->watches[i].active) {
            size_t len = strlen(cname);
            if (len >= WATCH_NAME_MAX)
                len = WATCH_NAME_MAX - 1;
            memcpy(dbg->watches[i].name, cname, len);
            dbg->watches[i].name[len] = '\0';
            dbg->watches[i].value = value;
            dbg->watches[i].active = 1;
            dbg->watch_count++;
            return;
        }
    }
    // Silently ignore if all slots are full.
}

/// @brief Unwatch the debugoverlay.
int8_t rt_debugoverlay_unwatch(rt_debugoverlay dbg, rt_string name) {
    if (!dbg || !name)
        return 0;

    const char *cname = rt_string_cstr(name);
    if (!cname)
        return 0;

    int64_t idx = find_watch(dbg, cname);
    if (idx < 0)
        return 0;

    dbg->watches[idx].active = 0;
    dbg->watches[idx].name[0] = '\0';
    dbg->watch_count--;
    return 1;
}

/// @brief Remove all entries from the debugoverlay.
void rt_debugoverlay_clear(rt_debugoverlay dbg) {
    if (!dbg)
        return;
    for (int64_t i = 0; i < RT_DEBUG_MAX_WATCHES; i++) {
        dbg->watches[i].active = 0;
        dbg->watches[i].name[0] = '\0';
    }
    dbg->watch_count = 0;
}

/// @brief Return the most recently computed frames-per-second value.
int64_t rt_debugoverlay_get_fps(rt_debugoverlay dbg) {
    if (!dbg || dbg->frame_count == 0)
        return 0;

    int64_t sum = 0;
    for (int64_t i = 0; i < dbg->frame_count; i++)
        sum += dbg->frame_times[i];

    if (sum <= 0)
        return 0;

    // FPS = 1000 * frame_count / sum_of_dt_ms
    return (1000 * dbg->frame_count) / sum;
}

// --- Drawing ---

/// Int-to-string helper for rendering. Writes into a caller-provided buffer.
/// Returns pointer to the start of the number string.
static char *i64_to_str(int64_t val, char *buf, size_t bufsize) {
    if (bufsize == 0)
        return buf;

    int negative = 0;
    uint64_t uval;
    if (val < 0) {
        negative = 1;
        uval = (uint64_t)(-(val + 1)) + 1;
    } else {
        uval = (uint64_t)val;
    }

    buf[bufsize - 1] = '\0';
    size_t pos = bufsize - 1;

    if (uval == 0) {
        if (pos > 0)
            buf[--pos] = '0';
    } else {
        while (uval > 0 && pos > 0) {
            buf[--pos] = '0' + (char)(uval % 10);
            uval /= 10;
        }
    }

    if (negative && pos > 0)
        buf[--pos] = '-';

    return &buf[pos];
}

/// @brief Draw the debugoverlay.
void rt_debugoverlay_draw(rt_debugoverlay dbg, void *canvas_ptr) {
    if (!dbg || !canvas_ptr || !dbg->enabled)
        return;

    // Layout constants
    const int64_t SCALE = 1;
    const int64_t LINE_H = 12;
    const int64_t PAD = 6;
    const int64_t COL_BG = 0x000000;
    const int64_t COL_LABEL = 0x888888;
    const int64_t COL_VALUE = 0x44FF44;
    const int64_t COL_FPS_GOOD = 0x44FF44;
    const int64_t COL_FPS_WARN = 0xFFDD44;
    const int64_t COL_FPS_BAD = 0xFF4444;
    const int64_t ALPHA = 180;

    // Count lines: FPS + DT + blank + watches
    int64_t num_lines = 2; // FPS + DT
    int64_t num_watches = 0;
    for (int64_t i = 0; i < RT_DEBUG_MAX_WATCHES; i++) {
        if (dbg->watches[i].active)
            num_watches++;
    }
    if (num_watches > 0)
        num_lines += 1 + num_watches; // blank line + watches

    // Panel dimensions
    int64_t panel_w = 160;
    int64_t panel_h = PAD * 2 + num_lines * LINE_H;
    int64_t canvas_w = rt_canvas_width(canvas_ptr);
    int64_t panel_x = canvas_w - panel_w - 4;
    int64_t panel_y = 4;

    // Background
    rt_canvas_box_alpha(canvas_ptr, panel_x, panel_y, panel_w, panel_h, COL_BG, ALPHA);

    int64_t tx = panel_x + PAD;
    int64_t ty = panel_y + PAD;
    char numbuf[24];

    // FPS line
    int64_t fps = rt_debugoverlay_get_fps(dbg);
    int64_t fps_col = COL_FPS_GOOD;
    if (fps < 30)
        fps_col = COL_FPS_BAD;
    else if (fps < 55)
        fps_col = COL_FPS_WARN;

    {
        char line[48];
        const char *fpsstr = i64_to_str(fps, numbuf, sizeof(numbuf));
        size_t flen = strlen(fpsstr);
        memcpy(line, "FPS: ", 5);
        if (flen > 40)
            flen = 40;
        memcpy(line + 5, fpsstr, flen);
        line[5 + flen] = '\0';
        rt_string s = make_string(line);
        rt_canvas_text_scaled(canvas_ptr, tx, ty, s, SCALE, fps_col);
        rt_string_unref(s);
        ty += LINE_H;
    }

    // DT line
    {
        char line[48];
        int64_t dt = 0;
        if (dbg->frame_count > 0) {
            int64_t prev = (dbg->frame_index - 1 + RT_DEBUG_FPS_HISTORY) % RT_DEBUG_FPS_HISTORY;
            dt = dbg->frame_times[prev];
        }
        const char *dtstr = i64_to_str(dt, numbuf, sizeof(numbuf));
        size_t dlen = strlen(dtstr);
        memcpy(line, "DT:  ", 5);
        if (dlen > 38)
            dlen = 38;
        memcpy(line + 5, dtstr, dlen);
        size_t pos = 5 + dlen;
        memcpy(line + pos, " ms", 3);
        line[pos + 3] = '\0';
        rt_string s = make_string(line);
        rt_canvas_text_scaled(canvas_ptr, tx, ty, s, SCALE, COL_LABEL);
        rt_string_unref(s);
        ty += LINE_H;
    }

    // Watch variables
    if (num_watches > 0) {
        ty += LINE_H; // blank separator

        for (int64_t i = 0; i < RT_DEBUG_MAX_WATCHES; i++) {
            if (!dbg->watches[i].active)
                continue;

            // Build "name: value" string
            char line[80];
            size_t nlen = strlen(dbg->watches[i].name);
            if (nlen > 28)
                nlen = 28;
            memcpy(line, dbg->watches[i].name, nlen);
            line[nlen] = ':';
            line[nlen + 1] = ' ';

            const char *valstr = i64_to_str(dbg->watches[i].value, numbuf, sizeof(numbuf));
            size_t vlen = strlen(valstr);
            if (vlen > 40)
                vlen = 40;
            memcpy(line + nlen + 2, valstr, vlen);
            line[nlen + 2 + vlen] = '\0';

            rt_string s = make_string(line);
            rt_canvas_text_scaled(canvas_ptr, tx, ty, s, SCALE, COL_VALUE);
            rt_string_unref(s);
            ty += LINE_H;
        }
    }
}
