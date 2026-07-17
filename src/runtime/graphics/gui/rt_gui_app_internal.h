//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/gui/rt_gui_app_internal.h
// Purpose: Internal contract between the GUI app core (rt_gui_app.c) and the
//          font-lifetime management split (rt_gui_app_font.c) — the process
//          app registry and the font retire/apply/inherit helpers.
//
// Key invariants:
//   - Engine-internal; included only by the graphics/gui/ app translation units.
//   - All symbols are graphics-only (ZANNA_ENABLE_GRAPHICS); no stub variants.
//   - The app registry tracks every live rt_gui_app_t so font retirement can
//     check usage across apps before freeing a font.
//
// Ownership/Lifetime:
//   - The registry holds weak references; apps unregister on destroy.
//
// Links: src/runtime/graphics/gui/rt_gui_app.c (app core + registry),
//        src/runtime/graphics/gui/rt_gui_app_font.c (font lifetime)
//
//===----------------------------------------------------------------------===//
#pragma once

#include "rt_gui_internal.h"

#ifdef ZANNA_ENABLE_GRAPHICS

// Process-wide GUI app registry (defined in rt_gui_app.c).
extern rt_gui_app_t *s_active_app;
extern rt_gui_app_t **s_registered_apps;
extern int s_registered_app_count;

/// @brief Register a newly-created app in the process registry. Defined in rt_gui_app.c.
int rt_gui_register_app(rt_gui_app_t *app);

// Font helpers consumed by the app core (defined in rt_gui_app_font.c).
void rt_gui_apply_font_to_widget(vg_widget_t *widget, vg_font_t *font, float size);
int rt_gui_retire_font_in_other_apps(rt_gui_app_t *skip, vg_font_t *font);

/// @brief Collect an app's generation-retired fonts after a presentation or idle boundary.
/// @details Fonts retired in the current generation or still referenced anywhere remain queued;
///          dead entries are removed and older unreferenced fonts are destroyed. The pass is
///          allocation-free and compacts the queue in place.
/// @param app App whose retirement queue should be collected; NULL is ignored.
void rt_gui_collect_retired_fonts(rt_gui_app_t *app);

#endif // ZANNA_ENABLE_GRAPHICS
