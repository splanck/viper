//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
///
/// @file vg_ide_widgets.h
/// @brief IDE-specific widget library -- umbrella header.
///
/// @details This header includes all IDE widget sub-headers, preserving
///          backward compatibility for code that includes vg_ide_widgets.h.
///
///          Individual sub-headers can be included directly for faster
///          compilation when only a subset of widgets is needed:
///
///          - vg_ide_widgets_common.h  -- shared types (vg_icon_t, forward decls)
///          - vg_ide_widgets_ui.h     -- StatusBar, Toolbar, CommandPalette,
///                                       Notification, Tooltip, FloatingPanel
///          - vg_ide_widgets_dialog.h -- Dialog, FileDialog
///          - vg_ide_widgets_editor.h -- CodeEditor, FindReplaceBar, Minimap
///          - vg_ide_widgets_tree.h   -- TreeView, MenuBar, ContextMenu
///          - vg_ide_widgets_panels.h -- SplitPane, TabBar, OutputPane, Breadcrumb
///
/// Key invariants:
///   - All create functions return NULL on allocation failure.
///   - String parameters are copied internally unless documented otherwise.
///   - Callbacks receive a user_data pointer that the widget never dereferences.
///
/// Ownership/Lifetime:
///   - Widgets are owned by their parent in the widget tree; destroying the
///     parent recursively destroys children.
///   - Some widgets (Dialog, ContextMenu, CommandPalette) may be created
///     without a parent and must be explicitly destroyed.
///
/// Links:
///   - vg_widget.h  -- base widget structure
///   - vg_widgets.h -- core widgets (Slider, Label, etc.) used as building blocks
///   - vg_layout.h  -- layout containers
///   - vg_font.h    -- font handles
///   - vg_event.h   -- event types and key codes
///   - vg_theme.h   -- theming system
///
//===----------------------------------------------------------------------===//
#ifndef VG_IDE_WIDGETS_H
#define VG_IDE_WIDGETS_H

#include "vg_ide_widgets_common.h"
#include "vg_ide_widgets_dialog.h"
#include "vg_ide_widgets_editor.h"
#include "vg_ide_widgets_panels.h"
#include "vg_ide_widgets_tree.h"
#include "vg_ide_widgets_ui.h"

#endif /* VG_IDE_WIDGETS_H */
