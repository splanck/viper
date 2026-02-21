# GUI Runtime — Complete Stub Elimination

## Background

Two user-reported bugs ("About goes black", "dialog_paint renders nothing") both
traced to the same root cause: `(void)param;` no-ops replacing actual `vgfx_fill_rect`
calls. This tracking document covers the full audit of ALL stub implementations
found across the GUI runtime, with progress tracking and ctest requirements.

## Implementation Status

### Group A — Visual Paint Stubs

| Task | File | Status | CTest |
|------|------|--------|-------|
| A1 | `src/lib/gui/src/widgets/vg_statusbar.c` | ✅ Done | N/A (visual) |
| A2 | `src/lib/gui/src/widgets/vg_minimap.c` | ✅ Done | N/A (visual) |
| A3 | `src/lib/gui/src/widgets/vg_colorswatch.c` | ✅ Done | N/A (visual) |

### Group B — Logic Correctness Stubs

| Task | File | Status | CTest |
|------|------|--------|-------|
| B1 | `src/runtime/graphics/rt_gui_codeeditor.c` — `rt_messagebox_question` | ✅ Done | — |
| B2 | `src/runtime/graphics/rt_gui_codeeditor.c` — `rt_messagebox_confirm` | ✅ Done | — |

### Group C — Platform API Extensions

| Task | File | Status | CTest |
|------|------|--------|-------|
| C1 | `src/lib/graphics/include/vgfx.h` — 12 new public declarations | ✅ Done | — |
| C2 | `src/lib/graphics/src/vgfx_internal.h` — `prevent_close`, `is_focused` fields + 12 platform decls | ✅ Done | — |
| C3 | `src/lib/graphics/src/vgfx.c` — public wrapper implementations | ✅ Done | — |
| C4 | `src/lib/graphics/src/vgfx_platform_macos.m` — 12 Cocoa platform functions + cursor | ✅ Done | — |
| C5 | `src/lib/graphics/src/vgfx_platform_linux.c` — 12 X11/EWMH platform functions | ✅ Done | — |
| C6 | `src/lib/graphics/src/vgfx_platform_win32.c` — 12 Win32 platform functions | ✅ Done | — |
| C7 | `src/runtime/graphics/rt_gui_system.c` — wire up all stubs to new vgfx calls | ✅ Done | — |

### Group E — No-Vtable Widgets

| Task | File | Status | CTest |
|------|------|--------|-------|
| E1 | `src/lib/gui/src/widgets/vg_slider.c` — full vtable + measure/arrange/paint/handle_event | ✅ Done | `test_vg_widgets_new` ✅ |
| E2 | `src/lib/gui/src/widgets/vg_progressbar.c` — full vtable + measure/arrange/paint | ✅ Done | `test_vg_widgets_new` ✅ |
| E3 | `src/lib/gui/src/widgets/vg_listbox.c` — full vtable + measure/arrange/paint/handle_event | ✅ Done | `test_vg_widgets_new` ✅ |

### Group D-menu — Menu Management

| Task | File | Status | CTest |
|------|------|--------|-------|
| D-m1 | `src/lib/gui/src/widgets/vg_menubar.c` — add remove_item/clear/remove_menu/enabled | ✅ Done | `test_vg_widgets_new` ✅ |
| D-m2 | `src/runtime/graphics/rt_gui_menus.c` — fix stubs to call new vg_menubar functions | ✅ Done | — |

### Group D-editor — CodeEditor Advanced Features

| Task | File | Status | CTest |
|------|------|--------|-------|
| D-e1 | `src/lib/gui/include/vg_ide_widgets.h` — new fields in vg_codeeditor_t | ✅ Done | `test_vg_widgets_new` ✅ |
| D-e2 | `src/lib/gui/src/widgets/vg_codeeditor.c` — integrate new fields into paint/events | ✅ Done | — |
| D-e3 | `src/runtime/graphics/rt_gui_codeeditor.c` — implement all editor stub rt_* functions | ✅ Done | — |

### Group D-other — Remaining Feature Stubs

| Task | File | Status | CTest |
|------|------|--------|-------|
| D-o1 | Toast set_action — `rt_gui_features.c` | ✅ Done | — |
| D-o2 | CommandPalette clear — `rt_gui_features.c` + `vg_commandpalette.c` | ✅ Done | `test_vg_widgets_new` ✅ |
| D-o3 | Breadcrumb max_items — `vg_breadcrumb.c` | ✅ Done | `test_vg_widgets_new` ✅ |
| D-o4 | Minimap runtime set_markers, set_viewport, get_scroll_position | ✅ Done | — |
| D-o5 | Rich tooltip title+body | ✅ Done | — |
| D-o6 | Context menu submenu | ✅ Done | — |

---

## CTests Delivered

`src/lib/gui/tests/test_vg_widgets_new.c` — 36 tests, registered in `src/lib/gui/CMakeLists.txt`, label `tui`.

| Test Group | # Tests | Covers |
|------------|---------|--------|
| Slider vtable | 6 | E1: create, orientation, value, clamp×2, step |
| ProgressBar vtable | 6 | E2: create, default, value, clamp×2, style |
| ListBox vtable | 7 | E3: create, add, selection, remove, clear |
| Breadcrumb max_items | 5 | D-o3: push/pop, clear, sliding window, trim, separator |
| CommandPalette clear | 5 | D-o2: create, add/find, remove, clear, show/hide |
| Menu management | 3 | D-m1: remove_item, clear, remove_menu |
| CodeEditor fields | 4 | D-e1: highlight_spans, gutter_icons, fold_regions, extra_cursors |

---

## Manual Verification Checklist

- [ ] ViperIDE status bar shows background color + top separator (not transparent)
- [ ] Minimap shows pixel-art code preview (characters as single pixels)
- [ ] ColorSwatch shows fill color + checkerboard for transparent swatches
- [ ] MessageBox.YesNo blocks and returns 1 for Yes, 0 for No
- [ ] MessageBox.Confirm blocks and returns 1 for OK, 0 for Cancel
- [ ] `App.Minimize()` / `App.Maximize()` works on macOS
- [ ] `Cursor.Set(1)` changes cursor to hand on macOS
- [ ] Slider renders track + thumb, responds to mouse drag
- [ ] ProgressBar renders filled portion based on value
- [ ] ListBox renders items, click selects, scroll works
- [ ] Menu.RemoveItem removes menu item at runtime
- [ ] CodeEditor gutter icon visible on specified line
- [ ] CodeEditor fold toggle hides lines
- [ ] CodeEditor multi-cursor shows additional cursor lines

---

## Root Cause Analysis

The recurring stub pattern has a clear signature:
1. Widget file lacks `#include "../../../graphics/include/vgfx.h"`
2. Paint callbacks contain `(void)sb->color_field;` instead of `vgfx_fill_rect()`
3. No-vtable widgets set `base.type` directly, bypassing `vg_widget_init()`

Prevention: The completeness guard (`scripts/check_runtime_completeness.sh`) catches
runtime.def gaps. A similar lint pass checking for missing vgfx includes in widget
files would prevent recurrence.
