# Viper.GUI Runtime Audit Report — 2026-03-20

## Methodology
Every `.c` file in `src/runtime/graphics/rt_gui_*.c` (the Viper.GUI namespace) was read **line by line, function by function**, including all `#else` stub sections. Each function was evaluated for:
- Security vulnerabilities (memory safety, integer overflow, injection, use-after-free)
- Correctness bugs (logic errors, ownership conflicts, missing initialization)
- Unimplemented stubs / TODO placeholders
- Comment accuracy
- Optimization opportunities

**Total scope**: 7 C source files + 1 internal header = ~9,776 lines of code.

## Summary

| Severity | Count | Description |
|----------|-------|-------------|
| **P0** | 1 | Use-after-free — immediate crash risk |
| **P1** | 9 | Broken features, memory leaks, silent data corruption |
| **P2** | 35 | Stubs, partial implementations, ownership issues |
| **P3** | 21 | Minor cleanup, optimization, comment accuracy |
| **Total** | **76** | |

| Category | Count |
|----------|-------|
| Bug | 39 |
| Security | 10 |
| Stub/TODO | 15 |
| Optimization | 6 |
| Comment | 6 |

### P0/P1 Critical Issues (fix first)

| ID | File | Function | Issue |
|----|------|----------|-------|
| B-MNU-009 | rt_gui_menus.c:1095 | rt_toolbaritem_set_icon | **P0 use-after-free**: frees `cicon` after passing its pointer to `vg_toolbar_item_set_icon`, which may store the pointer |
| B-APP-001 | rt_gui_app.c:76 | rt_gui_app_new | **P1 leak**: if `vgfx_create_window` fails, GC-allocated `app` struct is never freed |
| B-WID-002 | rt_gui_widgets.c:531 | rt_treeview_was_selection_changed | **P1 broken**: global statics mean only 1 treeview's selection tracking works |
| B-CMP-002 | rt_gui_widgets_complex.c:654 | rt_listbox_was_selection_changed | **P1 broken**: always returns 0 — selection change detection completely non-functional |
| B-MNU-001 | rt_gui_menus.c:192 | rt_menu_set_title | **P1 ownership**: frees `m->title` via double-cast, potentially conflicting with vg layer |
| B-MNU-002 | rt_gui_menus.c:261 | rt_menuitem_set_text | **P1 ownership**: same pattern as B-MNU-001 |
| B-MNU-003 | rt_gui_menus.c:282 | rt_menuitem_set_shortcut | **P1 ownership**: same pattern as B-MNU-001 |
| B-SYS-005 | rt_gui_system.c:401 | rt_app_set_size | **P1 invariant violation**: calls `vg_widget_set_fixed_size` on root, contradicting the documented invariant that root must NOT have fixed size |
| B-COD-002 | rt_gui_codeeditor.c:415 | s_type_colors | **P1 rendering**: gutter icon colors use `0x00` alpha = fully transparent. Icons are invisible |
| B-COD-009 | rt_gui_codeeditor.c:1100 | rt_messagebox_show | **P1 broken**: no modal loop — returns `default_button` instead of actual user choice |
| B-COD-012 | rt_gui_codeeditor.c:1588 | rt_findbar_set_replace_text | **P1 broken**: text stored locally but never sent to the vg widget — replace operations use stale/wrong text |
| X-001 | widgets.c + codeeditor.c | (multiple) | **P1 architectural**: global static tracking for treeview selection and gutter clicks breaks with multiple widget instances |

---

## File 1: rt_gui_app.c (660 lines)

### B-APP-001 — Memory leak on window creation failure [P1]
**Location**: `rt_gui_app_new`, line 76-96
**Issue**: If `vgfx_create_window(&params)` returns NULL (line 93), the function returns NULL but the `app` struct allocated via `rt_obj_new_i64` on line 76 is never freed.
**Fix**: Return the allocated app pointer even on window failure (caller can check `app->window`), or explicitly free `app` before returning NULL. Since `app` is GC-allocated, consider whether the GC will reclaim it. If not, explicit free is needed.

### B-APP-002 — Duplicated font loading code [P2]
**Location**: `rt_gui_app_render`, lines 413-451
**Issue**: The font loading logic (try embedded font, then fallback to system font paths) is duplicated between `rt_gui_ensure_default_font()` (lines 141-177) and `rt_gui_app_render()` (lines 424-451). The render function should call `rt_gui_ensure_default_font()` instead.
**Fix**: Replace lines 422-452 with a call to `rt_gui_ensure_default_font()`.

### B-APP-003 — Unused function parameters [P3]
**Location**: `render_widget_tree`, lines 562-605
**Issue**: Parameters `font` and `font_size` are passed through recursive calls but never used — painting is delegated to `widget->vtable->paint`. These parameters are vestigial.
**Fix**: Remove `font` and `font_size` parameters, update all call sites.

### B-APP-004 — Default font never freed [P2]
**Location**: `rt_gui_app_destroy`, lines 179-198
**Issue**: `app->default_font` is loaded (via `vg_font_load` or `vg_font_load_file`) but never freed in `rt_gui_app_destroy`. This leaks the font resource.
**Fix**: Add `if (app->default_font) vg_font_destroy(app->default_font);` before destroying root/window.

### B-APP-005 — Active dialog not cleared on app destroy [P2]
**Location**: `rt_gui_app_destroy`, lines 179-198
**Issue**: If a modal dialog is open (`g_active_dialog != NULL`) when `rt_gui_app_destroy` is called, `g_active_dialog` becomes a dangling pointer because the widget tree (which may contain the dialog) is destroyed via `vg_widget_destroy(app->root)`.
**Fix**: Add `rt_gui_set_active_dialog(NULL);` at the top of `rt_gui_app_destroy`.

### S-APP-001 — Integer truncation on window dimensions [P2]
**Location**: `rt_gui_app_new`, line 81
**Issue**: `width` and `height` are `int64_t` but cast to `int32_t` via `params.width = (int32_t)width`. Values > INT32_MAX silently wrap, potentially creating negative-dimension windows.
**Fix**: Clamp to `[1, INT32_MAX]` before cast.

### S-APP-002 — HiDPI scale factor unguarded [P3]
**Location**: `rt_gui_app_new`, lines 113-134
**Issue**: `vgfx_window_get_scale` return value is used directly for multiplication without checking for ≤0. While the render function (line 430) has a `_s > 0.0f ? _s : 1.0f` guard, the app constructor does not.
**Fix**: Add `if (_s <= 0.0f) _s = 1.0f;` after line 114.

### O-APP-001 — Redundant vgfx_get_size calls [P3]
**Location**: `rt_gui_app_render`, lines 460 and 507
**Issue**: `vgfx_get_size(app->window, &win_w, &win_h)` is called once for layout (line 460) and again for dialog centering (line 507). The window size doesn't change between these calls.
**Fix**: Cache `win_w`/`win_h` from the first call and reuse for the dialog.

### C-APP-001 — Empty comment block [P3]
**Location**: Lines 218-219
**Issue**: Comment `// Declared in rt_gui_internal.h, defined in rt_gui_system.c` followed by blank line — refers to `rt_shortcuts_clear_triggered` and `rt_shortcuts_check_key` but the text is orphaned.
**Fix**: Remove the comment or move it closer to where these functions are called.

### C-APP-002 — Redundant forward declaration [P3]
**Location**: Line 216
**Issue**: `void rt_gui_set_last_clicked(void *widget);` is a forward declaration, but this function is already declared in `rt_gui_internal.h` which is included.
**Fix**: Remove the redundant forward declaration.

---

## File 2: rt_gui_widgets.c (944 lines)

### B-WID-001 — NULL ctext passed to vg_label_create [P3]
**Location**: `rt_label_new`, lines 204-211
**Issue**: If `rt_string_to_cstr(text)` returns NULL (allocation failure), `ctext` is passed as NULL to `vg_label_create`. The behavior depends on whether `vg_label_create` handles NULL text gracefully.
**Fix**: Add `if (!ctext) ctext = "";` fallback, or guard and return NULL early.

### B-WID-002 — TreeView selection tracking uses global statics [P1]
**Location**: `rt_treeview_was_selection_changed`, lines 531-555
**Issue**: Uses `g_last_treeview_selected` and `g_last_treeview_checked` (static globals) to track selection changes. Only ONE treeview can be correctly tracked. Checking a second treeview resets the tracking state for the first.
**Fix**: Store tracking state per-widget (in the treeview's `user_data` or a hash map keyed by widget pointer) instead of using globals.

### B-WID-003 — Non-owning string pointer used with strdup [P2]
**Location**: `rt_treeview_node_set_data`, line 578
**Issue**: Uses `rt_string_cstr(data)` which returns a non-owning pointer into the runtime string. The pointer is then `strdup`'d, which is fine if the runtime string is still alive. However, `rt_string_to_cstr` (which allocates a copy) would be safer and more consistent with the rest of the codebase.
**Fix**: Replace `rt_string_cstr(data)` with `rt_string_to_cstr(data)` and free the intermediate copy.

### S-WID-001 — strdup NULL return unchecked [P3]
**Location**: `rt_treeview_node_set_data`, line 579
**Issue**: `strdup(cstr)` can return NULL on allocation failure. The result is assigned to `n->user_data` without checking. The old `user_data` was already freed on line 576, so data is lost if strdup fails.
**Fix**: Check strdup return and log/handle allocation failure.

### O-WID-001 — Redundant scroll position queries [P3]
**Location**: `rt_scrollview_get_scroll_x/y`, lines 418-436
**Issue**: Each function calls `vg_scrollview_get_scroll(scroll, &x, &y)` to retrieve both coordinates but only returns one. Two calls per frame for independent x/y queries.
**Fix**: Minor — could expose a combined getter, but this is not performance-critical.

---

## File 3: rt_gui_widgets_complex.c (1,459 lines)

### B-CMP-001 — Font check before ensure_default_font [P2]
**Location**: `rt_tabbar_new`, lines 49-58
**Issue**: Line 52 checks `s_current_app->default_font` BEFORE calling `rt_gui_ensure_default_font()` on line 54. If the font hasn't been loaded yet, the outer condition is false and ensure is never called.
**Fix**: Call `rt_gui_ensure_default_font()` FIRST, then check `s_current_app->default_font`.
```c
rt_gui_ensure_default_font();
if (tabbar && s_current_app && s_current_app->default_font)
    vg_tabbar_set_font(tabbar, ...);
```

### B-CMP-002 — ListBox selection change detection broken [P1]
**Location**: `rt_listbox_was_selection_changed`, lines 654-664
**Issue**: Function always returns 0. The comment says "For now, return 0 as a stub - would need tracking state". This means `ListBox.WasSelectionChanged()` never fires in Zia code.
**Fix**: Implement using per-widget tracking (store previous selected index in the listbox or in user_data, compare on each call).

### B-CMP-003 — Layout struct assumption [P3]
**Location**: `rt_container_set_spacing`, lines 381-385
**Issue**: Comment claims "Both vg_vbox_layout_t and vg_hbox_layout_t have spacing as their first field, so vg_vbox_set_spacing works for either type." This relies on struct layout that could break if either struct is modified.
**Fix**: Use a generic `vg_container_set_spacing` if available, or type-check before dispatching.

### B-CMP-004 — Theme state tracked independently [P3]
**Location**: `s_theme_is_dark`, line 343
**Issue**: Static variable `s_theme_is_dark` tracks theme state separately from `vg_theme_get_current()`. If the theme is changed via the vg layer directly (not through `rt_theme_set_dark/light`), `rt_theme_get_name()` returns stale data.
**Fix**: Query the vg theme system directly instead of maintaining a shadow variable.

### T-CMP-001 — rt_codeeditor_set_token_color not implemented [P2]
**Location**: Lines 299-305
**Comment**: `"No token_colors array yet — no-op for now"`
**Fix**: Add a `token_colors[TOKEN_TYPE_MAX]` array to `vg_codeeditor_t` and use it in the syntax highlighting callback.

### T-CMP-002 — rt_codeeditor_set_custom_keywords not implemented [P2]
**Location**: Lines 307-313
**Comment**: `"No custom_keywords field yet — no-op for now"`
**Fix**: Add a dynamic keyword list to `vg_codeeditor_t` and integrate with the syntax highlighting callback's keyword matching.

### T-CMP-003 — rt_codeeditor_set_cursor_selection not implemented [P2]
**Location**: Lines 744-761
**Issue**: Stub that discards all parameters. Selection cannot be set programmatically.
**Fix**: Implement via `vg_codeeditor_set_selection()`.

### S-CMP-001 — Image pixel buffer unvalidated [P2]
**Location**: `rt_image_set_pixels`, lines 815-821
**Issue**: Casts `void *pixels` to `const uint8_t *` and passes `width`/`height` to `vg_image_set_pixels` without validating the buffer size. Caller could pass a truncated buffer, leading to out-of-bounds reads.
**Fix**: Validate at the vg layer, or add explicit bounds checking here.

---

## File 4: rt_gui_menus.c (1,796 lines)

### B-MNU-001 — Menu title ownership conflict [P1]
**Location**: `rt_menu_set_title`, lines 192-200
**Issue**: Frees `m->title` via `free((void *)(uintptr_t)m->title)` then replaces it with `rt_string_to_cstr(title)`. The double-cast `(void *)(uintptr_t)` strips const from what may be a const pointer managed by the vg layer. If the vg layer allocated this memory differently (e.g., as part of a pool or struct), this is undefined behavior.
**Fix**: Use `vg_menu_set_title()` if available, or verify that `m->title` is always a standalone `malloc`/`strdup` allocation.

### B-MNU-002 — MenuItem text ownership conflict [P1]
**Location**: `rt_menuitem_set_text`, lines 261-269
**Issue**: Same pattern as B-MNU-001 — frees `mi->text` directly via double-cast, bypassing any vg layer ownership model.
**Fix**: Use `vg_menu_item_set_text()` if available.

### B-MNU-003 — MenuItem shortcut ownership conflict [P1]
**Location**: `rt_menuitem_set_shortcut`, lines 282-290
**Issue**: Same pattern — frees `mi->shortcut` directly.
**Fix**: Use a vg setter function.

### B-MNU-004 — Menu enabled state not implemented [P2]
**Location**: `rt_menu_set_enabled` / `rt_menu_is_enabled`, lines 238-255
**Issue**: `set_enabled` is a no-op with comment "Stub for future implementation". `is_enabled` always returns 1. Menu disabled state cannot be set from Zia.
**Fix**: Add an `enabled` field to `vg_menu_t` and check it during rendering/event dispatch.

### B-MNU-005 — MenuItem icon not implemented [P2]
**Location**: `rt_menuitem_set_icon`, lines 303-310
**Issue**: Stub — ignores the `pixels` parameter. Menu items cannot have icons.
**Fix**: Extend `vg_menu_item_t` to support icon rendering.

### B-MNU-006 — ContextMenu submenu not implemented [P2]
**Location**: `rt_contextmenu_add_submenu`, lines 448-458
**Issue**: Always returns NULL. Comment: "Context menu submenu support would need vg_contextmenu_add_submenu".
**Fix**: Implement in the vg layer.

### B-MNU-007 — ContextMenu returns hovered, not clicked item [P2]
**Location**: `rt_contextmenu_get_clicked_item`, lines 495-506
**Issue**: Returns `cm->items[cm->hovered_index]` — the currently hovered item, not the one that was clicked. If the mouse moved between click and query, the wrong item is returned.
**Fix**: Track `clicked_index` separately from `hovered_index`, set it on mouse-up.

### B-MNU-008 — Vertical toolbar missing font initialization [P2]
**Location**: `rt_toolbar_new_vertical`, lines 846-849
**Issue**: Unlike `rt_toolbar_new` (lines 833-843), the vertical variant doesn't call `rt_gui_ensure_default_font()` or set the font on the toolbar. Text labels will render with wrong/missing font.
**Fix**: Add the same font initialization block as `rt_toolbar_new`.

### B-MNU-009 — Use-after-free in toolbar icon setting [P0]
**Location**: `rt_toolbaritem_set_icon`, lines 1095-1106
**Issue**: `cicon` is allocated via `rt_string_to_cstr`, its pointer is stored in `icon.data.path`, then `vg_toolbar_item_set_icon` is called. After the call, `free(cicon)` is executed on line 1105. If `vg_toolbar_item_set_icon` stores the pointer (doesn't make a copy), this is a **use-after-free** — the icon path string is freed while the toolbar item still references it.
**Fix**: Either don't free `cicon` (transfer ownership), or verify that `vg_toolbar_item_set_icon` copies the string. Compare with `rt_toolbar_add_button` (lines 861-897) which explicitly comments "Ownership transferred to item on success".

### T-MNU-001 — Toolbar item pixel icon not implemented [P2]
**Location**: `rt_toolbaritem_set_icon_pixels`, lines 1108-1116
**Comment**: `"Would need to convert pixels to vg_icon_t"`
**Fix**: Implement pixel-to-icon conversion.

### C-MNU-001 — Incomplete file header [P3]
**Location**: Lines 12-13
**Issue**: File header Purpose section mentions "MenuBar, Menu, StatusBar" but doesn't explicitly list ContextMenu, Toolbar, and ToolbarItem, which are also implemented in this file.
**Fix**: Update the Purpose line.

---

## File 5: rt_gui_system.c (931 lines)

### B-SYS-001 — NULL shortcut keys/description stored [P3]
**Location**: `rt_shortcuts_register`, lines 162-196
**Issue**: If `rt_string_to_cstr(keys)` or `rt_string_to_cstr(description)` returns NULL (allocation failure), the NULL is stored in the shortcuts table. `parse_shortcut_keys` handles NULL `keys` (returns 0), so shortcuts with failed allocs are silently non-functional.
**Fix**: Check `ckeys` for NULL and skip registration, or log a warning.

### B-SYS-002 — Function key constant approximation [P2]
**Location**: `parse_shortcut_keys`, line 152
**Issue**: Comment says `VGFX_KEY_F1 = 290 is approximated`, but the code uses `*key = 289 + fnum` (so F1 = 290). If the actual `VGFX_KEY_F1` constant differs, ALL function key shortcuts will silently fail.
**Fix**: Use the actual `VGFX_KEY_F1` constant: `*key = VGFX_KEY_F1 + (fnum - 1)`.

### B-SYS-003 — Shortcut keys re-parsed every frame [P2]
**Location**: `rt_shortcuts_check_key`, lines 275-311
**Issue**: On every key-down event, the function iterates all registered shortcuts and calls `parse_shortcut_keys` to re-parse the string representation (e.g., "Ctrl+Shift+S"). This is O(n) string parsing per keypress.
**Fix**: Pre-parse and cache the modifier/key values in `rt_shortcut_t` at registration time.

### B-SYS-004 — rt_app_get_title always returns empty [P2]
**Location**: Lines 394-399
**Issue**: `rt_app_get_title` always returns `rt_str_empty()` with no implementation and no TODO comment. The title IS set via `rt_app_set_title`, but can never be read back.
**Fix**: Call `vgfx_get_title(gui_app->window)` if available, or cache the title string.

### B-SYS-005 — rt_app_set_size contradicts root invariant [P1]
**Location**: `rt_app_set_size`, lines 401-413
**Issue**: Calls `vg_widget_set_fixed_size(gui_app->root, ...)` and directly sets `gui_app->root->width/height`. This directly contradicts the key invariant documented in `rt_gui_app.c` line 19-21 and lines 103-107: "The root is sized dynamically every frame... Do NOT pin it with vg_widget_set_fixed_size".
**Fix**: Remove `vg_widget_set_fixed_size` call. Either resize the window via `vgfx_set_window_size` (already handled by `rt_app_set_window_size` at line 608), or document that `SetSize` is for the window, not the root.

### B-SYS-006 — No Windows font paths in fallback [P2]
**Location**: `rt_gui_ensure_default_font`, lines 160-165 (in rt_gui_app.c, referenced from system)
**Issue**: The system font fallback list only contains macOS and Linux paths. No Windows paths like `C:\Windows\Fonts\consola.ttf` or similar.
**Fix**: Add Windows font paths guarded by `#ifdef _WIN32`.

### S-SYS-001 — Negative delay wraps to huge value [P3]
**Location**: `rt_tooltip_set_delay`, line 301
**Issue**: `(uint32_t)delay_ms` — if `delay_ms` is negative, it wraps to a very large uint32_t value (e.g., -1 → 4294967295 ms ≈ 49 days).
**Fix**: Clamp `delay_ms` to `[0, UINT32_MAX]` before cast.

### O-SYS-001 — Pre-parse shortcut keys at registration [P2]
**Location**: Lines 162-196 (register) and 275-311 (check)
**Issue**: Shortcut key strings are stored as text and re-parsed on every keypress. This is both wasteful and error-prone (parse failures are silent).
**Fix**: Parse the key string at registration time, store `(ctrl, shift, alt, key)` tuple in `rt_shortcut_t`, and compare directly in `rt_shortcuts_check_key`.

---

## File 6: rt_gui_features.c (1,462 lines)

### B-FEA-001 — Rich tooltip truncation [P2]
**Location**: `rt_tooltip_show_rich`, line 277
**Issue**: Uses `char combined[1024]` stack buffer and `snprintf(combined, sizeof(combined), "%s\n%s", ...)`. If title + body exceeds 1024 chars, the tooltip text is silently truncated.
**Fix**: Dynamically allocate: `size_t len = strlen(ctitle) + strlen(cbody) + 2; char *combined = malloc(len); snprintf(combined, len, ...);`

### B-FEA-002 — Widget tooltip truncation [P2]
**Location**: `rt_widget_set_tooltip_rich`, line 329
**Issue**: Same 1024-byte stack buffer truncation as B-FEA-001.
**Fix**: Same dynamic allocation fix.

### B-FEA-003 — Tooltip never freed [P3]
**Location**: `g_active_tooltip`, line 238
**Issue**: `g_active_tooltip` is allocated by `vg_tooltip_create()` on first use but never freed. Memory leak on program exit.
**Fix**: Add a cleanup function called from `rt_gui_app_destroy` or program shutdown.

### B-FEA-004 — Notification manager never freed [P3]
**Location**: `g_notification_manager`, line 351
**Issue**: Allocated lazily by `rt_get_notification_manager()` but never freed. Memory leak on program exit.
**Fix**: Add cleanup in `rt_gui_app_destroy`.

### B-FEA-005 — Toast auto-dismissal not detected [P2]
**Location**: `rt_toast_was_dismissed`, lines 503-512
**Issue**: Comment says "For now, return stored state". `data->was_dismissed` is only set by `rt_toast_dismiss()` (manual dismissal). Auto-timeout dismissal by the notification manager is never reflected.
**Fix**: Query the notification manager for whether the notification is still active: `data->was_dismissed = !vg_notification_is_active(mgr, data->id)`.

### B-FEA-006 — Breadcrumb path leaks strdup'd strings [P2]
**Location**: `rt_breadcrumb_set_path`, lines 651-656
**Issue**: `strdup(token)` is called and passed as `user_data` to `vg_breadcrumb_push`. If `vg_breadcrumb_push` does NOT take ownership, the strdup'd string leaks. If it DOES take ownership, this is fine — but the ownership contract is unclear.
**Fix**: Verify vg_breadcrumb_push ownership semantics. If it copies, use a plain pointer and free after.

### B-FEA-007 — Breadcrumb items leak strdup'd strings [P2]
**Location**: `rt_breadcrumb_set_items`, lines 680-697
**Issue**: Same strdup leak pattern as B-FEA-006.
**Fix**: Same verification needed.

### T-FEA-001 — CommandPalette category ignored [P3]
**Location**: `rt_commandpalette_add_command`, line 113
**Issue**: The `category` parameter is accepted but cast to `(void)`. Commands cannot be organized by category.
**Fix**: Pass category to `vg_commandpalette_add_command` if the vg layer supports it, or prepend to label.

### T-FEA-002 — CommandPalette placeholder not implemented [P2]
**Location**: `rt_commandpalette_set_placeholder`, lines 196-207
**Issue**: Stub — comment says "Would need placeholder support in vg_commandpalette".
**Fix**: Add placeholder support to `vg_commandpalette_t`.

### T-FEA-003 — All drag-and-drop functions are stubs [P1]
**Location**: Lines 954-1035 (9 functions)
**Functions affected**: `rt_widget_set_draggable`, `rt_widget_set_drag_data`, `rt_widget_is_being_dragged`, `rt_widget_set_drop_target`, `rt_widget_set_accepted_drop_types`, `rt_widget_is_drag_over`, `rt_widget_was_dropped`, `rt_widget_get_drop_type`, `rt_widget_get_drop_data`
**Issue**: All are no-ops. The entire drag-and-drop API is non-functional.
**Fix**: Extend `vg_widget_t` with drag/drop state and implement event handling.

### S-FEA-001 — Toast max_visible negative wraps [P3]
**Location**: `rt_toast_set_max_visible`, line 544
**Issue**: `(uint32_t)count` — negative count wraps to very large value.
**Fix**: Clamp to `[1, 100]` or similar reasonable range.

### X-FEA-001 — File drop never populated [P3]
**Location**: Lines 1037-1076
**Issue**: `g_file_drop` struct is defined and queried by `rt_app_was_file_dropped` / `rt_app_get_dropped_file`, but nothing ever populates `g_file_drop.files` or sets `g_file_drop.was_dropped = 1`. The file drop feature is completely non-functional.
**Fix**: Hook into VGFX file drop events in `rt_gui_app_poll` and populate `g_file_drop`.

### C-FEA-001 — "Rich tooltip" is just string concatenation [P3]
**Location**: Lines 277 and 329
**Issue**: Functions named "show_rich" and "set_tooltip_rich" suggest rich formatting, but the implementation just concatenates title+body with a newline. The comment says "rich tooltip would need more widget support."
**Fix**: Update function doc comment to clarify the limitation.

---

## File 7: rt_gui_codeeditor.c (2,524 lines)

### B-COD-001 — Line number width approximation [P3]
**Location**: `rt_codeeditor_set_line_number_width`, line 386
**Issue**: `ce->gutter_width = (float)((int)width * 8)` uses a hardcoded `8` for character width. Actual char width depends on font and font size.
**Fix**: Use `ce->char_width` (if available) instead of hardcoded 8.

### B-COD-002 — Gutter icon colors fully transparent [P1]
**Location**: `s_type_colors` array, line 415
**Issue**: Colors are `0x00E81123, 0x00FFB900, 0x00E81123, 0x000078D4`. The high byte `0x00` is the alpha channel = fully transparent. These icons are **invisible**.
**Fix**: Change to `0xFFE81123, 0xFFFFB900, 0xFFE81123, 0xFF0078D4`.

### B-COD-003 — Gutter click tracking uses global statics [P2]
**Location**: Lines 459-496
**Issue**: `g_gutter_clicked`, `g_gutter_clicked_line`, `g_gutter_clicked_slot` are global statics. With multiple CodeEditor instances, gutter clicks from one editor are incorrectly attributed to queries about another.
**Fix**: Store gutter click state per-editor (in `vg_codeeditor_t` or per-widget user_data).

### B-COD-004 — Show fold gutter not implemented [P2]
**Location**: `rt_codeeditor_set_show_fold_gutter`, lines 498-505
**Issue**: Stub — comment says "Would enable/disable fold gutter column".
**Fix**: Add `show_fold_gutter` boolean to `vg_codeeditor_t` and use in paint.

### B-COD-005 — Auto fold detection not implemented [P2]
**Location**: `rt_codeeditor_set_auto_fold_detection`, lines 641-646
**Issue**: Stub — "Auto fold detection requires language-specific parsing".
**Fix**: Implement indent-based fold detection as a fallback.

### B-COD-006 — Extra cursor positions not queryable [P2]
**Location**: `rt_codeeditor_get_cursor_line_at` / `get_cursor_col_at`, lines 707-723
**Issue**: Functions only return data for the primary cursor (index 0). For any other index, they return 0. Despite `add_cursor` / `remove_cursor` working for extra cursors, their positions cannot be queried.
**Fix**:
```c
if (index == 0) return ce->cursor_line;
int extra_idx = (int)index - 1;
if (extra_idx >= 0 && extra_idx < ce->extra_cursor_count)
    return ce->extra_cursors[extra_idx].line;
return 0;
```

### B-COD-007 — Cursor selection setting not implemented [P2]
**Location**: `rt_codeeditor_set_cursor_selection`, lines 744-761
**Issue**: Stub that discards all selection parameters.
**Fix**: Call `vg_codeeditor_set_selection()` for the primary cursor.

### B-COD-008 — Custom messagebox buttons not implemented [P2]
**Location**: `rt_messagebox_add_button`, lines 1080-1090
**Issue**: Stub — custom buttons cannot be added to `MessageBox.New()` dialogs.
**Fix**: Implement button tracking in `rt_messagebox_data_t` and pass to dialog creation.

### B-COD-009 — MessageBox.Show() doesn't run modal loop [P1]
**Location**: `rt_messagebox_show`, lines 1100-1108
**Issue**: Calls `vg_dialog_show(data->dialog)` but does NOT enter a modal event loop (unlike `rt_messagebox_question` and `rt_messagebox_confirm` which do). Returns `data->default_button` immediately, which is always 0 unless explicitly set. The user's actual button choice is never captured.
**Fix**: Add a modal loop like `rt_messagebox_question` (lines 894-903), capture the dialog result, and return it.

### B-COD-010 — Zia keyword list missing `match` and `enum` [P2]
**Location**: `zia_keywords`, lines 113-116
**Issue**: The keyword list was written before enum support was added (Phase 4 of enum support is complete per memory). Missing keywords include `match`, `enum`, and potentially `import`. These words won't be syntax-highlighted.
**Fix**: Add `"match"`, `"enum"` to `zia_keywords`.

### B-COD-011 — FindBar visibility not implemented [P2]
**Location**: `rt_findbar_set_visible` / `rt_findbar_is_visible`, lines 1726-1743
**Issue**: Both are stubs. `set_visible` discards the parameter, `is_visible` always returns 0.
**Fix**: Implement via the findbar widget's visibility control.

### B-COD-012 — FindBar replace text never sent to widget [P1]
**Location**: `rt_findbar_set_replace_text`, lines 1588-1597
**Issue**: The replace text is stored in `data->replace_text` but the comment says "vg_findreplacebar doesn't have a set_replace_text". This means when `rt_findbar_replace()` or `rt_findbar_replace_all()` are called, they use whatever text the vg widget has internally (likely empty or stale), NOT the text the user set via `SetReplaceText()`.
**Fix**: Add `vg_findreplacebar_set_replace_text()` to the vg layer and call it here.

### S-COD-001 — syn_fill no bounds check [P2]
**Location**: `syn_fill`, lines 56-60
**Issue**: `colors[pos + i] = color` writes without checking that `pos + i` is within the colors array bounds. If the tokenizer calculates incorrect boundaries (e.g., UTF-8 multi-byte sequences where strlen != character count), this could write past the array end.
**Fix**: Pass `colors_len` and add `if (pos + i >= colors_len) return;` guard.

### S-COD-002 — Type colors array access pattern [P3]
**Location**: Line 419
**Issue**: `s_type_colors[type]` where `type = (int)(slot & 3)`. The `& 3` guarantees index 0-3, and the array has exactly 4 elements. Safe but fragile — adding a 5th slot type without expanding the array would cause OOB.
**Fix**: Add `_Static_assert(sizeof(s_type_colors)/sizeof(s_type_colors[0]) >= 4, "...");` or use `type % ARRAY_SIZE`.

### O-COD-001 — Linear keyword scanning [P3]
**Location**: `syn_is_keyword` / `syn_is_keyword_ci`, lines 88-109
**Issue**: O(n × m) where n = number of keywords, m = average keyword length. Called for every identifier in every line of code. For Zia (~26 keywords), this is acceptable but would scale poorly.
**Fix**: Could use a perfect hash or sorted array with binary search. Low priority given current keyword counts.

### T-COD-001 — Token color customization not implemented [P2]
**Location**: Lines 299-305
**Duplicate of**: T-CMP-001. Listed in this file because the function definition is here.

### T-COD-002 — Custom keywords not implemented [P2]
**Location**: Lines 307-313
**Duplicate of**: T-CMP-002.

---

## Cross-File Issues

### X-001 — Global static tracking breaks multi-instance widgets [P1]
**Files**: `rt_gui_widgets.c:531-555`, `rt_gui_codeeditor.c:459-496`
**Issue**: TreeView selection change detection and CodeEditor gutter click detection both use module-scoped static variables for state tracking. This means:
- Only 1 TreeView can correctly report selection changes
- Only 1 CodeEditor can correctly report gutter clicks
- Any application with multiple instances of these widgets will see incorrect/missed events
**Fix**: Store tracking state per-widget instance (either in the widget's user_data or in a lightweight hash map).

### X-002 — Dead global g_clicked_menuitem [P3]
**File**: `rt_gui_menus.c:372`
**Issue**: `g_clicked_menuitem` is set by `rt_gui_set_clicked_menuitem()` but never read. `rt_menuitem_was_clicked()` (line 380) checks `mi->was_clicked` on the menu item struct itself, not the global. The global variable and its setter function are dead code.
**Fix**: Remove `g_clicked_menuitem` and `rt_gui_set_clicked_menuitem()`.

### X-003 — Missing RT_ASSERT_MAIN_THREAD in complex widgets [P2]
**File**: `rt_gui_widgets_complex.c`
**Issue**: Several functions are missing the `RT_ASSERT_MAIN_THREAD()` call that all other GUI functions use:
- `rt_tabbar_new` (line 49)
- `rt_tabbar_add_tab` (line 60)
- `rt_tabbar_remove_tab` (line 70)
- `rt_tabbar_set_active` (line 78)
- `rt_tab_set_title` (line 86)
- `rt_tab_set_modified` (line 95)
- And many more throughout the file
**Fix**: Add `RT_ASSERT_MAIN_THREAD();` as the first line of every public function.

### X-004 — Inconsistent empty string returns [P3]
**Files**: All 7 files
**Issue**: Functions returning empty strings use three different patterns:
1. `rt_str_empty()` — used in rt_gui_widgets.c
2. `rt_string_from_bytes("", 0)` — used in rt_gui_features.c, rt_gui_codeeditor.c
3. `rt_const_cstr("")` — used in rt_gui_widgets_complex.c (listbox functions)
All three may be semantically equivalent but the inconsistency makes the codebase harder to audit and could mask subtle bugs if their semantics differ.
**Fix**: Standardize on `rt_str_empty()` across all files.

---

## Recommendations — Priority Order

### Immediate (P0)
1. **B-MNU-009**: Fix `rt_toolbaritem_set_icon` use-after-free — either don't free cicon or verify vg layer copies the string

### High Priority (P1)
2. **B-COD-002**: Fix transparent gutter icon colors (0x00 → 0xFF alpha)
3. **B-CMP-002**: Implement `rt_listbox_was_selection_changed` — broken API
4. **B-COD-012**: Wire `rt_findbar_set_replace_text` through to vg widget
5. **B-COD-009**: Add modal loop to `rt_messagebox_show`
6. **B-SYS-005**: Remove `vg_widget_set_fixed_size` from `rt_app_set_size` — violates root invariant
7. **B-APP-001**: Free `app` on window creation failure
8. **B-WID-002**: Replace global statics with per-widget tracking for treeview selection
9. **X-001**: Same for gutter clicks — per-widget tracking
10. **T-FEA-003**: Implement drag-and-drop (9 stub functions)

### Medium Priority (P2) — 35 items
- Fix all ownership conflicts in menu/menuitem setters (B-MNU-001/002/003)
- Implement remaining stubs (token colors, custom keywords, fold gutter, etc.)
- Add Windows font paths (B-SYS-006)
- Pre-parse shortcut keys (O-SYS-001)
- Fix tabbar font init order (B-CMP-001)
- Add missing RT_ASSERT_MAIN_THREAD calls (X-003)
- Update Zia keyword list with `match`, `enum` (B-COD-010)
- Implement extra cursor position queries (B-COD-006)
- Fix rich tooltip buffer truncation (B-FEA-001/002)

### Low Priority (P3) — 21 items
- Clean up dead code (X-002, C-APP-001/002)
- Standardize empty string returns (X-004)
- Minor optimizations (O-APP-001, O-WID-001, O-COD-001)
- Integer truncation guards (S-APP-001, S-SYS-001, S-FEA-001)
