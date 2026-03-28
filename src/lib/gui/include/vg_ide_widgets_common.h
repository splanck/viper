//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/lib/gui/include/vg_ide_widgets_common.h
// Purpose: Shared types and forward declarations for IDE widget sub-headers.
//          Extracted from vg_ide_widgets.h to avoid circular dependencies
//          between the split headers.
// Key invariants:
//   - vg_icon_t is the canonical icon type used across all IDE widgets.
//   - String parameters are copied internally unless documented otherwise.
// Ownership/Lifetime:
//   - vg_icon_destroy frees any resources owned by an icon.
// Links: vg_ide_widgets.h (umbrella), vg_widget.h (base)
//
//===----------------------------------------------------------------------===//
#pragma once

#include "vg_font.h"
#include "vg_layout.h"
#include "vg_widget.h"

#ifdef __cplusplus
extern "C" {
#endif

// Forward declarations used across IDE widget headers
typedef struct vg_menu_item vg_menu_item_t;
typedef struct vg_menu vg_menu_t;
struct vg_treeview;
struct vg_codeeditor;
struct vg_textinput;
struct vg_button;
struct vg_checkbox;

//=========================================================================
// Icon — shared icon type used by Toolbar, TreeView, Dialog, Breadcrumb,
//        CommandPalette, and others.
//=========================================================================

/// @brief Icon specification
typedef struct vg_icon {
    enum {
        VG_ICON_NONE,  ///< No icon
        VG_ICON_GLYPH, ///< Unicode character
        VG_ICON_IMAGE, ///< Pixel data
        VG_ICON_PATH   ///< File path
    } type;

    union {
        uint32_t glyph; ///< Unicode codepoint

        struct {
            uint8_t *pixels; ///< RGBA pixel data
            uint32_t width;
            uint32_t height;
        } image;

        char *path; ///< File path
    } data;
} vg_icon_t;

/// @brief Create icon from Unicode glyph
/// @param codepoint Unicode codepoint
/// @return Icon specification
vg_icon_t vg_icon_from_glyph(uint32_t codepoint);

/// @brief Create icon from pixel data
/// @param rgba RGBA pixel data
/// @param w Image width
/// @param h Image height
/// @return Icon specification
vg_icon_t vg_icon_from_pixels(uint8_t *rgba, uint32_t w, uint32_t h);

/// @brief Create icon from file path
/// @param path File path
/// @return Icon specification
vg_icon_t vg_icon_from_file(const char *path);

/// @brief Destroy icon and free resources
/// @param icon Icon to destroy
void vg_icon_destroy(vg_icon_t *icon);

#ifdef __cplusplus
}
#endif
