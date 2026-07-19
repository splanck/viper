//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/gui/rt_gui_linux_portal.h
// Purpose: Optional, dynamically loaded Linux desktop Settings portal queries.
// Key invariants:
//   - No GLib/GIO headers or link-time dependency are required.
//   - Unavailable buses, portals, keys, and unexpected variants return not-found.
// Ownership/Lifetime: Every temporary GIO object is released before a query returns.
// Links: org.freedesktop.portal.Settings, docs/adr/0139-native-wayland-backend-and-linux-runtime-selection.md
//
//===----------------------------------------------------------------------===//

#pragma once

#include <stdint.h>

/// @brief Read one unsigned or Boolean Settings portal value as a signed integer.
/// @return One when decoded into @p out_value, otherwise zero.
int rt_gui_linux_portal_read(const char *name_space, const char *key, int32_t *out_value);
