# Viper.GUI & Viper.Graphics — Deep Audit Report
*Files audited: ~55 .c/.m/.h source files across `src/lib/graphics/`, `src/lib/gui/`, `src/runtime/graphics/`, `src/il/runtime/runtime.def`*

---

## 1. CONFIRMED BUGS (Code Verified)

### BUG-GUI-001 — Label `word_wrap` / `max_lines` Silently Ignored
**File:** `src/lib/gui/src/widgets/vg_label.c:109–172`
`vg_label_t` declares `bool word_wrap` and `int max_lines` (initialized at creation). The paint function calls `vg_font_measure_text()` and `vg_font_draw_text()` exactly once with the full text string — never inspects `word_wrap` or `max_lines`. Setting either field does nothing.
**Fix:** In `label_measure()`, split on width limit and accumulate line heights when `word_wrap` is set. In `label_paint()`, call `vg_font_draw_text` once per wrapped line, truncating at `max_lines`.

### BUG-GUI-002 — Dialog Modal Overlay Is Visual-Only; Events Still Route Behind It
**File:** `src/lib/gui/src/widgets/vg_dialog.c:359–366`
The modal dim overlay is painted correctly (`vgfx_fill_rect` over the full window). However, the event dispatcher has no modal check — events are delivered to all widgets including those visually behind the dialog. A modal dialog does not prevent background interaction.
**Fix:** Add a `is_modal_active()` check in the parent event dispatch loop; route all events exclusively to the topmost modal dialog while it is open.

### BUG-GUI-003 — Button Cannot Be Activated via Keyboard
**File:** `src/lib/gui/src/widgets/vg_button.c:186–210`
`handle_event` only handles `VG_EVENT_CLICK` (mouse). There is no `VG_EVENT_KEY_DOWN` case for Space or Enter. A Button can receive focus but has no keyboard activation path.
**Fix:**
```c
case VG_EVENT_KEY_DOWN:
    if (event->key.key == VG_KEY_SPACE || event->key.key == VG_KEY_ENTER) {
        if (button->on_click) button->on_click(widget, button->user_data);
        event->handled = true; return true;
    }
```

### BUG-GUI-004 — Slider Has No Keyboard Handler
**File:** `src/lib/gui/src/widgets/vg_slider.c:115–192`
Only mouse events are handled. Arrow keys do nothing. A focused slider cannot be adjusted without a mouse.
**Fix:** Add key handler: Up/Right → `value + step`; Down/Left → `value - step`; Home → `min_value`; End → `max_value`. Step-snap is already implemented (`vg_slider_set_value` at line 226).

### BUG-GUI-005 — ListBox Cannot Receive Focus or Keyboard Input
**File:** `src/lib/gui/src/widgets/vg_listbox.c:29`
`g_listbox_vtable.can_focus = NULL`. The widget is permanently non-focusable. Arrow key navigation, Home/End, Page Up/Down are entirely absent.
**Fix:** Set `can_focus` to return `true`; implement `VG_EVENT_KEY_DOWN` with Up/Down/Home/End/PageUp/PageDown selection logic, scrolling the view to keep the selection visible.

### BUG-GUI-006 — TreeView Has No Keyboard Navigation
**File:** `src/lib/gui/src/widgets/vg_treeview.c`
Expand/collapse, selection, hover, and scrolling are all implemented. Keyboard events are not handled at all.
**Fix:** Same pattern as ListBox: enable focus, add arrow-key handler (Up/Down to move, Right to expand, Left to collapse/jump to parent, Enter to activate).

### BUG-GUI-007 — TextInput Missing Shift+Arrow Selection and Ctrl+Word-Jump
**File:** `src/lib/gui/src/widgets/vg_textinput.c`
Standard bindings confirmed present: Ctrl+A/C/X/V, Home, End, Backspace, Delete, Left, Right. Missing: Shift+Left/Right/Home/End (extend selection), Ctrl+Left/Right (word jump), double-click word selection, Ctrl+Z/Y undo/redo.
**Fix:** Track `shift_held` state in event handler; on Shift+arrow, extend `selection_end` rather than moving `cursor_pos`. For Ctrl+Left/Right, scan for next word boundary.

### BUG-GUI-008 — TextInput Has No Undo/Redo
**File:** `src/lib/gui/src/widgets/vg_textinput.c`
No edit history anywhere in the file. The CodeEditor has a full `vg_edit_history_t` command stack (vg_codeeditor.c:199–336) — the same pattern could be applied to TextInput.
**Fix:** Reuse `vg_edit_history_t` or a simpler string-snapshot ring buffer. Bind Ctrl+Z/Ctrl+Y.

### BUG-GUI-009 — macOS Framebuffer Resize Loses Alignment
**File:** `src/lib/graphics/src/vgfx_platform_macos.m:383`
Initial allocation uses `aligned_alloc_wrapper(VGFX_FRAMEBUFFER_ALIGNMENT, fb_size)` for cache-line alignment. The `windowDidResize` path uses plain `malloc()`, silently breaking the alignment guarantee after the first resize.
**Fix:** Replace `malloc()` in the resize path with `aligned_alloc_wrapper()`; free with `aligned_free_wrapper()`.

---

## 2. PARTIALLY IMPLEMENTED FEATURES IN CODEEDITOR (C layer done, not rendered/bound)

These all have `vg_codeeditor_t` struct fields AND C API functions but are neither rendered nor accessible from Zia. They are the highest-ROI wins for ViperIDE completeness.

### PARTIAL-001 — Gutter Icons (Breakpoints / Diagnostics)
**File:** `src/lib/gui/include/vg_ide_widgets.h:2072–2080`
Fields: `gutter_icons`, `gutter_icon_count`, `gutter_icon_cap`. C API: `vg_codeeditor_set_gutter_icon()`, `vg_codeeditor_clear_gutter_icons()`. Type enum: 0=breakpoint, 1=warning, 2=error, 3=info.
**Not rendered** — no paint code handles these arrays.
**Fix:** In `vg_codeeditor_paint`, iterate `gutter_icons` and render colored dots/squares in the gutter column at the correct line Y offset. Expose via Zia: `SetGutterIcon(line, type)`, `ClearGutterIcons()`.

### PARTIAL-002 — Highlight Spans (Semantic/Search Highlighting)
**File:** `src/lib/gui/include/vg_ide_widgets.h:2062–2070`
Fields: `highlight_spans`, `highlight_span_count`, `highlight_span_cap`. C API: `vg_codeeditor_set_highlight_span()`, `vg_codeeditor_clear_highlights()`.
**Not rendered** — paint never iterates these arrays.
**Fix:** In the paint loop, for each visible line draw background rects for matching spans before drawing text. Expose via Zia: `AddHighlight(startLine, startCol, endLine, endCol, color)`, `ClearHighlights()`. Critical for find-match highlighting and linting underlines.

### PARTIAL-003 — Code Folding
**File:** `src/lib/gui/include/vg_ide_widgets.h:2082–2090`
Fields: `fold_regions`, `fold_region_count`, `fold_region_cap`. Each has `start_line`, `end_line`, `folded` (bool — **never checked**). C API: `vg_codeeditor_set_fold_region()`, `vg_codeeditor_get_fold_region()`.
**Not functional** — paint doesn't skip folded lines; input doesn't handle fold toggle clicks.
**Fix (larger):** In the paint and layout loop, skip lines belonging to a folded region; render a `···` placeholder. Add click handling on the fold triangle in the gutter.

### PARTIAL-004 — Multi-Cursor (Display Infrastructure Only)
**File:** `src/lib/gui/include/vg_ide_widgets.h:2092–2099`
Fields: `extra_cursors`, `extra_cursor_count`, `extra_cursor_cap`. C API exists. Comment: *"input still uses primary cursor"*.
**Not rendered, not functional for input.** Display-only scaffolding.
**Fix (large):** Rendering is straightforward (draw cursor line at each extra position). Input handling requires broadcasting keystrokes to all cursors — more complex.

### PARTIAL-005 — Horizontal Scroll
**File:** `src/lib/gui/include/vg_ide_widgets.h:2014`
`scroll_x` field exists and is computed in layout, but there is no horizontal scrollbar rendered and navigation doesn't consistently update it.
**Fix:** Add horizontal scrollbar rendering (same pattern as vertical); update `scroll_x` on Right arrow past visible area and on cursor moves that go off-screen horizontally.

### PARTIAL-006 — FindBar Bindings Incomplete
**File:** `src/runtime/graphics/rt_gui_codeeditor.c:1454–1843` + `runtime.def:5668–5692`
`FindBar` widget exists in C and has a Zia class, but `FindNext`, `ReplaceCurrent`, `ReplaceAll`, and `FindPrev` are not bound to Zia.
**Fix:** Add 4 RT_METHOD entries and corresponding `rt_findbar_*` C wrappers (the underlying `vg_findreplacebar_t` already has this logic).

### PARTIAL-007 — CodeEditor `GetSelectedText` Not Bound
**File:** `src/runtime/graphics/rt_gui_codeeditor.c`
C-level `vg_codeeditor_get_selection()` exists and returns a `char*`. No Zia binding.
**Fix:** Add `RT_METHOD("GetSelectedText", "str()", rt_codeeditor_get_selected_text)` in runtime.def and a trivial wrapper.

### PARTIAL-008 — CodeEditor `SetCursor` Not Bound
**File:** `src/lib/gui/src/widgets/vg_codeeditor.c`
C API `vg_codeeditor_set_cursor(editor, line, col)` exists. Zia only has read-only `CursorLine` and `CursorCol` properties. Can read cursor position but cannot set it from Zia.
**Fix:** Add `RT_METHOD("SetCursor", "void(i64,i64)", rt_codeeditor_set_cursor)`.

---

## 3. PERFORMANCE ISSUES

### PERF-001 — `vgfx_cls()` Per-Pixel Byte Loop
**File:** `src/lib/graphics/src/vgfx.c:1073–1090`
Writes 4 individual bytes per pixel in a loop. On a 2560×1600 framebuffer = ~16M byte writes per clear. Modern CPUs fill memory via 32/128-bit writes 4–16× faster.
```c
// CURRENT (slow):
for (size_t i = 0; i < n; i++) { px[i*4]=r; px[i*4+1]=g; px[i*4+2]=b; px[i*4+3]=0xFF; }

// FAST:
uint32_t c = ((uint32_t)r<<24)|((uint32_t)g<<16)|((uint32_t)b<<8)|0xFF;
uint32_t *p = (uint32_t*)window->pixels;
for (size_t i = 0; i < n; i++) p[i] = c;
// Or on Apple: memset_pattern4(p, &c, n * 4);
```

### PERF-002 — Non-LRU Glyph Cache Eviction
**File:** `src/lib/gui/src/font/vg_cache.c:150–171`
Eviction removes 25% of entries from the first non-empty buckets (bucket-order). Frequently-used glyphs can be evicted while rarely-used ones survive.
**Fix:** Add `uint32_t access_tick` to `vg_cache_entry_t`; increment global tick on each cache hit; evict entries with smallest tick value.

### PERF-003 — No Dirty-Region Tracking
**File:** `src/lib/gui/src/core/vg_widget.c`
`needs_paint` flags exist per-widget but no dirty bounding box is accumulated. Every repaint re-traverses and re-renders the entire widget tree.
**Fix (medium effort):** Accumulate a dirty union-rect as widgets set `needs_paint`; during paint, skip widgets whose bounds don't intersect the dirty rect.

---

## 4. MISSING ZIA BINDINGS (C functions exist, no Zia access)

### BINDING-001 — Canvas Window Management Gaps
The following `vgfx_*` C functions have no Canvas Zia binding:
| Missing Method | C Function |
|----------------|-----------|
| `GetMonitorSize()` | `vgfx_get_monitor_size` |
| `GetPosition()` | `vgfx_get_position` |
| `SetPosition(x,y)` | `vgfx_set_position` |
| `GetSize()` | `vgfx_get_size` |
| `GetFps()` | `vgfx_get_fps` |
| `SetFps(n)` | `vgfx_set_fps` |
| `IsMaximized()` | `vgfx_is_maximized` |
| `Maximize()` | `vgfx_maximize` |
| `IsMinimized()` | `vgfx_is_minimized` |
| `Minimize()` | `vgfx_minimize` |
| `Restore()` | `vgfx_restore` |
| `IsFocused()` | `vgfx_is_focused` |
| `Focus()` | `vgfx_focus` |
| `PreventClose(i64)` | `vgfx_set_prevent_close` |

**Fix:** Add `RT_METHOD` entries in runtime.def and trivial `rt_canvas_*` wrappers.

### BINDING-002 — Canvas HiDPI Scale Not Exposed
The Canvas API has no `GetScale()`. Zia code rendering at physical pixel coordinates has no way to query the display scale factor (2.0 on Retina).
**Fix:** Add `RT_METHOD("GetScale", "f64()", rt_canvas_get_scale)` backed by `vgfx_window_get_scale(canvas->gfx_win)`.

### BINDING-003 — GuiWidget Base Missing All Read Accessors
Setters `SetVisible`, `SetEnabled`, `SetSize`, `SetPosition`, `SetFlex` exist. Zero corresponding getters.
**Fix:** Add to GuiWidget: `IsVisible()->i1`, `IsEnabled()->i1`, `GetWidth()->i64`, `GetHeight()->i64`, `GetX()->i64`, `GetY()->i64`, `GetFlex()->f64`.

### BINDING-004 — ScrollView Missing Scroll Position Query
`SetScroll(x,y)` exists. `GetScrollX()->f64` and `GetScrollY()->f64` are absent.

### BINDING-005 — Dropdown Missing Programmatic Selection
`GetSelected()->i64` and `GetSelectedText()->str` exist. `SetSelected(index:i64)` does not.

### BINDING-006 — SplitPane `GetPosition()` Missing
`SetPosition(f64)` is bound. `GetPosition()->f64` is not.

### BINDING-007 — Checkbox Missing `SetChecked()` Binding
C widget has `vg_checkbox_set_checked(checkbox, bool)`. Zia exposes `IsChecked()->i1` but no setter.
**Fix:** Add `RT_METHOD("SetChecked", "void(i64)", rt_checkbox_set_checked)`.

### BINDING-008 — CodeEditor Extra Cursor C API Not Bound
C functions `vg_codeeditor_set_extra_cursor()` and `vg_codeeditor_clear_extra_cursors()` exist. No Zia binding.

---

## 5. API INCONSISTENCIES & RUNTIME.DEF QUALITY

### API-001 — 194 Lines of Duplicate Method Declarations
Base widget methods (`SetVisible`×25, `SetSize`×22, `Destroy`×30, etc.) are copy-pasted into every derived widget class in runtime.def. Verified count: **194 duplicate lines**.
**Fix:** Remove from derived classes; GuiWidget base declarations are sufficient.

### API-002 — Opacity/Alpha Type Inconsistency
`GuiImage.SetOpacity(f64)` uses 0.0–1.0. `SpriteBatch.SetAlpha(i64)` uses an unspecified integer range.
**Fix:** Standardize all opacity/alpha parameters to `f64` (0.0–1.0).

### API-003 — Canvas Color Format Undocumented
`Canvas.Clear(i64)`, `Canvas.Plot(i64)`, `Canvas.Box(...)` color format (`0x00RRGGBB`) is never stated. `Pixels` uses a different format (`0xRRGGBBAA`). This difference is invisible in Zia code.
**Fix:** Add doc comment to every Canvas color param in runtime.def.

### API-004 — Mouse-Wheel Delta Units Undefined
`vg_event_t.wheel.delta_y` units are undocumented in `vg_event.h`. Different widgets multiply by different factors, causing inconsistent scroll speeds.
**Fix:** Document delta_y as "lines-per-notch"; or normalize to pixels at the platform layer.

### API-005 — Missing `SetMargin` Anywhere
`SetPadding` exists for containers. `SetMargin` appears nowhere in the codebase.
**Fix:** Add `margin` field to `vg_widget_t` base; apply during arrange.

---

## 6. MISSING FEATURES (No C implementation)

### FEAT-001 — No Gradient Fill Primitive
Only solid-color fills. No `vgfx_fill_rect_gradient_h/v`.
**Add:** `vgfx_fill_rect_gradient_h(win, x, y, w, h, color_left, color_right)` + `_v` variant; bind to Canvas.

### FEAT-002 — No Anti-Aliased Line Drawing
`vgfx_line()` uses integer Bresenham. Diagonal lines are jagged.
**Add:** Wu's line algorithm as `vgfx_line_aa()`.

### FEAT-003 — ComboBox (Editable Dropdown) Missing
No widget combines typed text input with a filtered suggestion list.
**Add:** `vg_combobox_t` — TextInput + filtered popup ListBox. Expose as `GUI.ComboBox`.

### FEAT-004 — Focus Visible Ring Missing
`VG_STATE_FOCUSED` is set correctly, but no widget draws a visual indicator.
**Fix:** Draw a 1–2px themed border when `widget->state & VG_STATE_FOCUSED` in each focusable widget's paint.

### FEAT-005 — Button Icon Support Missing
`vg_button_t` has no icon field. Toolbar-style icon+text buttons are not possible.
**Add:** `vg_font_t *icon_font; char icon_codepoint[8]; enum icon_position` to button struct.

### FEAT-006 — Tab Order (`tab_index`) Not Implemented
`vg_widget_focus_next()` does depth-first traversal with no override.
**Add:** `int tab_index` to `vg_widget_t`; sort by `tab_index` in `focus_next`.

### FEAT-007 — TextInput Undo/Redo
CodeEditor has full `vg_edit_history_t` command stack. TextInput has nothing.
**Add:** Port the same history infrastructure; bind Ctrl+Z/Ctrl+Y.

---

## 7. PRIORITIZED WORK PLAN

### Tier 1 — Verified Bugs (1–3h each)
| ID | Task | File(s) |
|----|------|---------|
| BUG-GUI-001 | Implement Label word_wrap / max_lines | `vg_label.c` |
| BUG-GUI-003 | Button: Space/Enter keyboard activation | `vg_button.c` |
| BUG-GUI-004 | Slider: arrow key handler | `vg_slider.c` |
| BUG-GUI-005 | ListBox: focus + keyboard navigation | `vg_listbox.c` |
| BUG-GUI-006 | TreeView: keyboard navigation | `vg_treeview.c` |
| BUG-GUI-007 | TextInput: Shift+select, Ctrl+word-jump | `vg_textinput.c` |
| BUG-GUI-009 | macOS resize: use aligned_alloc_wrapper | `vgfx_platform_macos.m` |
| PERF-001 | vgfx_cls() 32-bit write loop | `vgfx.c` |
| FEAT-004 | Focus visible ring on all focusable widgets | widget paint fns |

### Tier 2 — Complete Partial Implementations (2–6h each, very high ROI)
| ID | Task | File(s) |
|----|------|---------|
| PARTIAL-001 | Render gutter icons (breakpoints/errors/warnings) | `vg_codeeditor.c` + `runtime.def` |
| PARTIAL-002 | Render highlight spans (search, linting) | `vg_codeeditor.c` + `runtime.def` |
| PARTIAL-006 | FindBar: add FindNext/ReplaceCurrent/ReplaceAll/FindPrev | `runtime.def` + `rt_gui_codeeditor.c` |
| PARTIAL-007 | CodeEditor: bind GetSelectedText | `runtime.def` |
| PARTIAL-008 | CodeEditor: bind SetCursor(line, col) | `runtime.def` |
| BINDING-001 | Canvas: add 14 missing window management bindings | `runtime.def` + `rt_graphics.c` |
| BINDING-002 | Canvas: add GetScale() HiDPI binding | `runtime.def` + `rt_graphics.c` |
| BINDING-003 | GuiWidget: add 7 read accessors | `runtime.def` + `rt_gui_system.c` |
| BINDING-004 | ScrollView: GetScrollX/Y | `runtime.def` |
| BINDING-005 | Dropdown: SetSelected(i64) | `runtime.def` + widget |
| BINDING-006 | SplitPane: GetPosition() | `runtime.def` |
| BINDING-007 | Checkbox: SetChecked(i64) binding | `runtime.def` |
| API-001 | Remove 194 duplicate method decls in runtime.def | `runtime.def` |
| API-005 | Add SetMargin to widget base | `vg_widget.h` + layout |

### Tier 3 — New Features (4–16h each)
| ID | Task | Notes |
|----|------|-------|
| BUG-GUI-002 | Dialog modal event blocking | Requires event dispatch change |
| BUG-GUI-008 | TextInput undo/redo | Port `vg_edit_history_t` |
| FEAT-001 | Gradient fill primitive | `vgfx.c` + Canvas binding |
| FEAT-002 | Anti-aliased line drawing (Wu's algorithm) | `vgfx.c` |
| FEAT-003 | ComboBox widget | New `vg_combobox_t` |
| FEAT-005 | Button icon support | Field + paint change |
| FEAT-006 | Tab order via tab_index | Widget base + focus logic |
| PARTIAL-003 | Code folding (render + click input) | Complex |
| PARTIAL-004 | Multi-cursor input handling | Complex |
| PARTIAL-005 | Code editor horizontal scroll | Scrollbar + nav |
| PERF-002 | LRU glyph cache eviction | `vg_cache.c` |
| PERF-003 | Dirty-region tracking | Core widget system |

---

## 8. FILES WITH THE MOST ISSUES

| File | Issues |
|------|--------|
| `src/il/runtime/runtime.def` | 194 dup lines; 14 missing Canvas bindings; 7 missing GuiWidget getters; missing Checkbox setter, Dropdown setter, SplitPane getter, ScrollView getters; FindBar incomplete |
| `src/lib/gui/src/widgets/vg_codeeditor.c` | Gutter icons / highlight spans / fold regions: struct+API exist, not rendered |
| `src/lib/gui/src/widgets/vg_label.c` | word_wrap entirely unimplemented |
| `src/lib/gui/src/widgets/vg_listbox.c` | No focus, no keyboard |
| `src/lib/gui/src/widgets/vg_treeview.c` | No keyboard navigation |
| `src/lib/gui/src/widgets/vg_button.c` | No keyboard activation |
| `src/lib/gui/src/widgets/vg_slider.c` | No keyboard handler |
| `src/lib/gui/src/widgets/vg_dialog.c` | Modal doesn't block events |
| `src/lib/gui/src/widgets/vg_textinput.c` | No undo/redo; no Shift+select; no Ctrl+word-jump |
| `src/lib/graphics/src/vgfx.c` | cls() unoptimized; no gradient; no AA lines |
| `src/lib/graphics/src/vgfx_platform_macos.m` | Resize uses unaligned malloc |
| `src/runtime/graphics/rt_gui_codeeditor.c` | GetSelectedText, SetCursor, gutter/highlight/fold/extra-cursor Zia bindings absent |
