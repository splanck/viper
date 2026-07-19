//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/gui/rt_gui_atspi_linux.h
// Purpose: Optional native Linux AT-SPI projection entry points.
// Key invariants:
//   - A missing accessibility bus or GIO runtime silently preserves the in-process tree.
//   - Every successfully attached application is unregistered and joined before destruction.
// Ownership/Lifetime: Attach borrows the window/root; detach ends all native bridge activity.
// Links: org.a11y.atspi.Socket, rt_gui_accessibility_platform.h
//
//===----------------------------------------------------------------------===//

#pragma once

#include "rt_gui_accessibility_platform.h"

#include <stddef.h>
#include <stdint.h>

void rt_gui_atspi_linux_attach(vgfx_window_t window, vg_widget_t *root);
void rt_gui_atspi_linux_detach(vgfx_window_t window);
void rt_gui_atspi_linux_notify(vgfx_window_t window, vg_widget_t *widget);
void rt_gui_atspi_linux_sync(vgfx_window_t window, vg_widget_t *root);

/// @brief Return the number of registered snapshot nodes for internal Linux tests/diagnostics.
size_t rt_gui_atspi_linux_snapshot_count(vgfx_window_t window);

/// @brief Materialize the AT-SPI Cache payload and return its item count for internal tests.
size_t rt_gui_atspi_linux_cache_item_count(vgfx_window_t window);

enum {
    RT_GUI_ATSPI_TEST_ACTION = 1,
    RT_GUI_ATSPI_TEST_CARET = 2,
    RT_GUI_ATSPI_TEST_VALUE = 3,
};

/// @brief Exercise the same bounded worker-to-GUI mutation queue used by D-Bus callbacks.
int rt_gui_atspi_linux_test_request(vgfx_window_t window,
                                    uint64_t widget_id,
                                    int kind,
                                    double value);
void rt_gui_atspi_linux_announce(vgfx_window_t window,
                                 vg_widget_t *widget,
                                 const char *text,
                                 vg_live_region_mode_t mode);
