# ViperIDE Runtime & Platform Bugs

Discovered during ViperIDE functional testing (Phase 2 — making the IDE actually work).
All bugs below were found and fixed unless marked otherwise.

---

## VIDE-BUG-001: Hit testing broken after coordinate conversion

**Severity:** Blocker (editor completely non-interactive)
**Status:** Fixed

**Symptom:** The code editor area could not be clicked or typed into. Pulldown menus were interactive but nothing else responded.

**Root cause:** `convert_to_screen_coords()` in `rt_gui.c` permanently modified widget `x/y` from relative to absolute coordinates during the render pass. After rendering, `vg_widget_get_screen_bounds()` (used by hit testing during `poll()`) assumed coordinates were relative and walked the parent chain — double-counting parent offsets.

**Fix:** Removed `convert_to_screen_coords()` entirely. Modified `render_widget_tree()` to pass parent absolute offsets as parameters, temporarily set absolute coords for paint calls, and restore relative coords immediately after.

**Files:** `src/runtime/rt_gui.c`

---

## VIDE-BUG-002: Exit crash (SIGSEGV at 0x10)

**Severity:** High
**Status:** Fixed

**Symptom:** Application crashed on exit with SIGSEGV accessing address near 0x10.

**Root cause:** `rt_gui_app_destroy()` freed the app struct but left the global `s_current_app` pointer dangling. Subsequent code accessed members of the freed struct.

**Fix:** Added `if (s_current_app == app) s_current_app = NULL;` in `rt_gui_app_destroy()`.

**Files:** `src/runtime/rt_gui.c`

---

## VIDE-BUG-003: MenuItem.WasClicked() always returns 0

**Severity:** Blocker (menus non-functional)
**Status:** Fixed

**Symptom:** Menu items could be clicked but `WasClicked()` always returned 0 in Zia polling code.

**Root cause:** The runtime function `rt_menuitem_was_clicked()` checked a global `g_clicked_menuitem` pointer that was never set. The menubar widget used a callback pattern (`item->action()`) but the runtime expected a polling pattern.

**Fix:** Added `bool was_clicked` field to `vg_menu_item_t` struct. Set `item->was_clicked = true` in the menubar's VG_EVENT_CLICK and VG_KEY_ENTER handlers. Updated `rt_menuitem_was_clicked()` to check and clear the per-item flag.

**Files:** `src/lib/gui/include/vg_ide_widgets.h`, `src/lib/gui/src/widgets/vg_menubar.c`, `src/runtime/rt_gui.c`

---

## VIDE-BUG-004: ToolbarItem.WasClicked() always returns 0

**Severity:** Blocker (toolbar non-functional)
**Status:** Fixed

**Symptom:** Same as BUG-003 but for toolbar buttons.

**Root cause:** Same pattern — global `g_clicked_toolbaritem` never set.

**Fix:** Added `bool was_clicked` to `vg_toolbar_item_t`. Set in toolbar's MOUSE_UP handler. Updated `rt_toolbaritem_was_clicked()` to check per-item flag.

**Files:** `src/lib/gui/include/vg_ide_widgets.h`, `src/lib/gui/src/widgets/vg_toolbar.c`, `src/runtime/rt_gui.c`

---

## VIDE-BUG-005: Keyboard shortcuts never trigger

**Severity:** High
**Status:** Fixed

**Symptom:** Ctrl+N, Ctrl+O, Ctrl+S etc. had no effect.

**Root cause (multi-part):**
1. `g_shortcuts[i].triggered` was never set to 1 — no code matched key events against registered shortcuts.
2. Platform key events (`vgfx_event_t`) did not include modifier flags — the `modifiers` field was always 0.
3. KEY_CHAR was synthesized for Ctrl+key combos, inserting characters into the editor.

**Fix:**
1. Added `rt_shortcuts_check_key()` function to match KEY_DOWN events against registered shortcuts.
2. Added `int modifiers` to vgfx key event struct. Populated from `NSEventModifierFlags` on macOS.
3. Integrated shortcut matching into `rt_gui_app_poll()`: clear triggered flags at start of frame, match KEY_DOWN against shortcuts, suppress KEY_CHAR synthesis when a shortcut matches or Ctrl/Cmd is held.

**Files:** `src/lib/graphics/include/vgfx.h`, `src/lib/graphics/src/vgfx_platform_macos.m`, `src/runtime/rt_gui.c`

---

## VIDE-BUG-006: FileDialog parameter order mismatch

**Severity:** Medium
**Status:** Fixed

**Symptom:** File dialogs showed wrong filter or didn't work correctly.

**Root cause:** Zia code passes `(title, filter, path)` but the C function expected `(title, path, filter)`.

**Fix:** Reordered C function parameters to match Zia calling convention: `rt_filedialog_open(title, filter, default_path)`.

**Files:** `src/runtime/rt_gui.c`

---

## VIDE-BUG-007: Menu dropdown clicks don't reach menubar widget

**Severity:** Blocker (menus non-functional even after BUG-003 fix)
**Status:** Fixed

**Symptom:** Clicking menu items in the dropdown had no effect. The `was_clicked` fix (BUG-003) was correct but unreachable for mouse clicks.

**Root cause:** MenuBar widget is 28px tall. Dropdown menus render below it (starting at y=28). When user clicks a menu item in the dropdown (e.g., at y=50), `vg_widget_hit_test()` rejects the menubar because the click point is outside its 28px bounds. The event goes to whatever widget is behind the dropdown (or nothing).

Additionally, `vg_event_send()` generates CLICK events from MOUSE_UP only when `vg_widget_contains_point()` passes — which also fails for out-of-bounds dropdown clicks.

**Fix:** Implemented input capture mechanism for popups:
1. Added `vg_widget_set_input_capture()` / `vg_widget_release_input_capture()` / `vg_widget_get_input_capture()` API in `vg_widget.h/c`.
2. Modified `vg_event_dispatch()`: when input capture is active, all mouse events route to the captured widget regardless of hit testing. For MOUSE_UP, a CLICK event is synthesized directly (bypassing the `contains_point` check).
3. Keyboard events also route to captured widget first (for arrow key navigation in open menus).
4. MenuBar sets input capture when opening a menu, releases when closing (via click, Escape, Enter, or clicking outside).

**Files:** `src/lib/gui/include/vg_widget.h`, `src/lib/gui/src/core/vg_widget.c`, `src/lib/gui/src/core/vg_event.c`, `src/lib/gui/src/widgets/vg_menubar.c`

---

## VIDE-BUG-008: CodeEditor cursor not rendered

**Severity:** Medium (cosmetic but impacts usability)
**Status:** Fixed

**Symptom:** No visible cursor in the code editor. User couldn't see insertion point.

**Root cause:** Cursor drawing in `vg_codeeditor.c` was a TODO stub.

**Fix:** Implemented cursor rendering with `vgfx_fill_rect()` — 2px wide vertical bar at computed cursor position.

**Files:** `src/lib/gui/src/widgets/vg_codeeditor.c`

---

## VIDE-BUG-009: Exit crash (SIGABRT in vg_icon_destroy)

**Severity:** High
**Status:** Fixed

**Symptom:** Application crashed on exit with SIGABRT in `vg_icon_destroy()` during `toolbar_destroy()`. The malloc debugger reported "pointer being freed was not allocated".

**Root cause:** Three functions in `rt_gui.c` had an ownership bug with toolbar icon paths:
- `rt_toolbar_add_button()`
- `rt_toolbar_add_button_with_text()`
- `rt_toolbar_add_toggle()`

These functions:
1. Created `cicon` from `rt_string_to_cstr()` (malloc)
2. Set `icon.data.path = cicon`
3. Passed icon struct to `vg_toolbar_add_button()` which stores it by shallow copy
4. Called `free(cicon)` — leaving item with dangling pointer
5. On destroy, `vg_icon_destroy()` tried to free the already-freed pointer

**Fix:** Transfer ownership of `cicon` to the toolbar item instead of freeing it after the call:
1. Only set `VG_ICON_PATH` if icon string is non-empty
2. Don't free `cicon` after passing to toolbar (ownership transferred)
3. Only free `cicon` if item creation fails (ownership not transferred)

**Files:** `src/runtime/rt_gui.c`

---

## Known Remaining Issues

### VIDE-TODO-001: Dropdown menu has no visible background
**Status:** Fixed

Implemented proper rendering in `menubar_paint()`:
- Dark background panel for dropdown
- Border around dropdown
- Highlight on hover/selection
- Separator lines between sections
- Checkmark and submenu arrow indicators

### VIDE-TODO-002: Toolbar has no visible background
**Status:** Fixed

Implemented proper rendering in `toolbar_paint()`:
- Background for toolbar
- Button backgrounds on hover/active states
- Separator lines between button groups
- Dropdown arrow indicators
- Overflow "..." indicator

### VIDE-TODO-003: Edit menu items not wired up
Edit menu items (Undo, Redo, Cut, Copy, Paste, Select All, Find) are defined in `app_shell.zia` but the main event loop doesn't poll them. The CodeEditor widget would need clipboard and selection APIs to support these.

### VIDE-TODO-004: Sidebar TreeView is empty
The sidebar TreeView is created but no file tree is populated. This requires a file system browsing API.

### VIDE-TODO-005: Shift+key doesn't produce correct characters
The KEY_CHAR synthesis in `rt_gui_app_poll()` handles Shift+A-Z (uppercase) but doesn't handle shift variants of other keys (e.g., Shift+1 should produce '!', Shift+; should produce ':'). Full keymap translation would require platform-specific logic.
