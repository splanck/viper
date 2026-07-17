# GUI Modernization Public API Surface

This document fixes the additive public names for the modernization program.
Compact signatures shown in parentheses are registry signatures. Existing
members remain unchanged unless explicitly described as repaired semantics.

## 1. Capability, construction, and frame scheduling

### `Viper.GUI.System`

- `IsAvailable() -> i1`
- `GetUnavailableReason() -> str`

`IsAvailable` is true only when graphics support is compiled in and an app
backend can be attempted. `GetUnavailableReason` is empty when available and
otherwise returns the exact capability reason.

### `Viper.GUI.App`

- `TryNew(title: str, width: i64, height: i64) -> Viper.Result`
- `RunFrame() -> i1`
- `RunFrameWithDelta(deltaMs: f64) -> i1`
- `GetNextDeadlineMs() -> i64`
- `MakeCurrent() -> void`
- `GetEffectiveScale() -> f64`
- `GetLogicalFontSize() -> f64`

`RunFrame` returns false once the app should close. `RunFrameWithDelta` is the
deterministic test/automation form; negative and non-finite deltas are treated
as zero, and a single call is capped at 250 ms for animations while preserving
the caller-visible elapsed counter.

## 2. Typed constant classes

The following static classes expose read-only `i64` properties. They replace
magic integers in new documentation while legacy integer-taking methods remain
compatible:

- `Viper.GUI.Align`: `Start`, `Center`, `End`, `Stretch`.
- `Viper.GUI.Justify`: `Start`, `Center`, `End`, `SpaceBetween`, `SpaceAround`,
  `SpaceEvenly`.
- `Viper.GUI.FlexDirection`: `Row`, `Column`, `RowReverse`, `ColumnReverse`.
- `Viper.GUI.FlexWrap`: `NoWrap`, `Wrap`, `WrapReverse`.
- `Viper.GUI.Dock`: `Left`, `Top`, `Right`, `Bottom`, `Fill`.
- `Viper.GUI.ThemeMode`: `Dark`, `Light`, `System`, `Custom`.
- `Viper.GUI.AccessibleRole`: `None`, `Application`, `Window`, `Group`, `Label`,
  `Button`, `CheckBox`, `RadioButton`, `TextBox`, `SearchBox`, `ComboBox`,
  `List`, `ListItem`, `Tree`, `TreeItem`, `TabList`, `Tab`, `Table`, `Row`,
  `Cell`, `Slider`, `ProgressBar`, `Dialog`, `Alert`, `Menu`, `MenuItem`,
  `ToolBar`, `StatusBar`, `Image`, `Video`, `Link`.
- `Viper.GUI.LiveRegionMode`: `Off`, `Polite`, `Assertive`.
- `Viper.GUI.DialogButtonRole`: `Normal`, `Default`, `Cancel`, `Destructive`,
  `Accept`, `Reject`, `Help`.
- `Viper.GUI.DialogStatus`: `Idle`, `Open`, `Accepted`, `Cancelled`, `Failed`.
- `Viper.GUI.ImageFilter`: `Nearest`, `Bilinear`.
- `Viper.GUI.SortDirection`: `None`, `Ascending`, `Descending`.

## 3. Widget geometry, identity, accessibility, and events

### `Viper.GUI.Widget`

- `SetMinSize(width: f64, height: f64) -> void`
- `GetMinWidth() -> f64`; `GetMinHeight() -> f64`
- `SetPadding(all: f64) -> void`
- `SetPaddingEdges(left: f64, top: f64, right: f64, bottom: f64) -> void`
- `SetMarginEdges(left: f64, top: f64, right: f64, bottom: f64) -> void`
- `GetLogicalX/Y/Width/Height() -> f64`
- `GetScreenX/Y/Width/Height() -> f64`
- `GetParentOption() -> Viper.Option`
- `GetChildCount() -> i64`
- `GetChildAtOption(index: i64) -> Viper.Option`
- `RemoveChild(child: Widget) -> i1`
- `ClearChildren() -> void`
- `SetName(name: str) -> void`; `GetName() -> str`; `GetId() -> i64`
- `FindByIdOption(id: i64) -> Viper.Option`
- `FindByNameOption(name: str) -> Viper.Option`
- `HitTest(screenX: f64, screenY: f64) -> i1`
- `InvalidatePaint() -> void`; `InvalidateLayout() -> void`
- `SetAccessibleRole(role: i64) -> void`; `GetAccessibleRole() -> i64`
- `SetAccessibleName(name: str) -> void`; `GetAccessibleName() -> str`
- `SetAccessibleDescription(text: str) -> void`; `GetAccessibleDescription() -> str`
- `SetAccessibleValue(value: str) -> void`; `GetAccessibleValue() -> str`
- `SetAccessibleLabelFor(target: Widget) -> void`; `ClearAccessibleLabelFor() -> void`
- `SetLiveRegion(mode: i64) -> void`; `GetLiveRegion() -> i64`
- `GetRevision() -> i64`

New geometry operations use logical units. Legacy integer getters retain their
documented physical-unit behavior.

### `Viper.GUI.Accessibility`

- `Snapshot(root: Widget) -> Viper.Collections.Map`
- `SetHighContrast(enabled: i1) -> void`; `IsHighContrast() -> i1`
- `SetReducedMotion(enabled: i1) -> void`; `IsReducedMotion() -> i1`
- `GetSystemHighContrast() -> i1`; `GetSystemReducedMotion() -> i1`
- `Announce(widget: Widget, text: str, mode: i64) -> void`

The snapshot schema is versioned and contains `schemaVersion`, `id`, `role`,
`name`, `description`, `value`, state flags, logical bounds, and recursive
`children`.

## 4. Theme and typography

### `Viper.GUI.Theme`

- `SetMode(mode: i64) -> void`; `GetMode() -> i64`
- `FollowSystem() -> void`
- `SetPalette(palette: ThemePalette) -> i1`
- `GetPalette() -> ThemePalette`
- `ResetCustom() -> void`
- `WasChanged() -> i1`; `GetRevision() -> i64`

### `Viper.GUI.ThemePalette`

- `New() -> ThemePalette`; `FromDark()`, `FromLight() -> ThemePalette`
- `Clone() -> ThemePalette`
- `SetColor(token: str, rgb: i64) -> i1`; `GetColor(token: str) -> i64`
- `SetMetric(token: str, value: f64) -> i1`; `GetMetric(token: str) -> f64`
- `SetMotionEnabled(enabled: i1) -> void`
- `SetFontRoles(regular: Font, bold: Font, mono: Font) -> void`
- `Validate() -> Viper.Result`

Token names are the canonical `vg_theme.h` field names using lower camel case,
for example `bgPrimary`, `accentPrimary`, `buttonHeight`, `radiusMedium`, and
`focusGlowWidth`. Unknown tokens return false; validation reports the first
invalid token/value.

### `Viper.GUI.Font`

- `LoadSystemUi(size: f64) -> Viper.Result`
- `LoadSystemUiBold(size: f64) -> Viper.Result`
- `GetLogicalSize(font: Font) -> f64`

## 5. Text input and uniform control events

### `Viper.GUI.TextInput`

- `SetMaxLength(count: i64)`, `GetMaxLength() -> i64`
- `SetPassword(enabled: i1)`, `IsPassword() -> i1`
- `SetReadOnly(enabled: i1)`, `IsReadOnly() -> i1`
- `SetMultiline(enabled: i1)`, `IsMultiline() -> i1`
- `SetCursor(index: i64)`, `GetCursor() -> i64`
- `SelectRange(start: i64, end: i64)`, `ClearSelection()`
- `GetSelectionStart/End() -> i64`; `GetSelectedText() -> str`
- `InsertText(text: str) -> i1`; `DeleteSelection() -> i1`
- `Undo() -> i1`; `Redo() -> i1`; `CanUndo/CanRedo() -> i1`
- `WasChanged() -> i1`; `WasSubmitted() -> i1`; `GetRevision() -> i64`
- `IsComposing() -> i1`; `GetCompositionText() -> str`
- `GetCompositionStart/Length() -> i64`

Cursor and selection indices are Unicode grapheme-cluster indices. Legacy
internal codepoint offsets are converted at the API boundary.

### Other controls

`Checkbox`, `Dropdown`, `Slider`, `Spinner`, `RadioButton`, `ListBox`,
`TreeView`, `TabBar`, `Grid`, `ColorSwatch`, `ColorPalette`, and `ColorPicker`
gain `WasChanged() -> i1` and `GetRevision() -> i64`. Activatable controls gain
`WasActivated() -> i1`; text-entry controls gain `WasSubmitted() -> i1`.

## 6. Layout containers

### `Viper.GUI.VBox` and `Viper.GUI.HBox`

- `SetAlign(align: i64)`, `GetAlign() -> i64`
- `SetJustify(justify: i64)`, `GetJustify() -> i64`

### `Viper.GUI.Flex`

- `New() -> Flex`
- `SetDirection(direction: i64)`; `SetWrap(wrap: i64)`
- `SetAlign(align: i64)`; `SetJustify(justify: i64)`
- `SetGap(gap: f64)`; `SetPadding(padding: f64)`

### `Viper.GUI.LayoutGrid`

- `New() -> LayoutGrid`
- `SetRows(count: i64)`; `SetColumns(count: i64)`
- `SetRowSize(row: i64, size: f64)`; `SetColumnSize(col: i64, size: f64)`
- `SetGap(horizontal: f64, vertical: f64)`; `SetPadding(padding: f64)`
- `Place(child: Widget, row: i64, col: i64, rowSpan: i64, colSpan: i64) -> i1`

Positive sizes are fixed logical units, zero means auto/content, and negative
sizes are fractional weights whose magnitude is the weight.

### `Viper.GUI.DockPanel`

- `New() -> DockPanel`
- `SetPadding(padding: f64)`; `SetGap(gap: f64)`
- `DockChild(child: Widget, dock: i64) -> i1`

## 7. Trees, tabs, split panes, labels, and radio groups

### `TreeView` and `TreeView.Node`

- `Node.SetText(text: str)`; `SetIcon(icon: str)`; `GetIcon() -> str`
- `Node.SetHasChildren(value: i1)`; `HasChildren() -> i1`
- `Node.SetLoading(value: i1)`; `IsLoading() -> i1`
- `Node.SetStableId(id: str)`; `GetStableId() -> str`
- `Toggle(node)`; `ScrollTo(node)`; `WasActivated() -> i1`
- `WasLoadChildrenRequested() -> i1`; `GetLoadRequestedNodeOption() -> Viper.Option`
- `GetActivatedNodeOption() -> Viper.Option`; `GetRevision() -> i64`

### `TabBar` and `Tab`

- `Tab.Get/SetTitle`; `Get/SetData`; `Is/SetClosable`; `GetStableId/SetStableId`
- `SetFont(font, size)`; `WasReordered() -> i1`
- `GetReorderedFrom/To() -> i64`; `MoveTab(from, to) -> i1`
- `GetRevision() -> i64`

### `SplitPane`

- `SetMinFirst(size: f64)`; `SetMinSecond(size: f64)`
- `GetMinFirst/Second() -> f64`; `GetOrientation() -> i64`
- `CollapseFirst/Second()`; `Restore()`; `GetCollapsedSide() -> i64`

### `RadioGroup`, `RadioButton`, and `Label`

- `RadioGroup.GetSelectedIndex() -> i64`; `SetSelectedIndex(index) -> i1`
- `RadioGroup.GetCount() -> i64`; `WasChanged() -> i1`; `GetRevision() -> i64`
- `RadioButton.Get/SetText`; `Get/SetData`
- `Label.SetAlignment(align)`; `GetAlignment()`
- `Label.SetEllipsis(enabled)`; `SetMaxLines(count)`; `SetSelectable(enabled)`
- `Label.GetSelectedText() -> str`

## 8. Color controls

### `ColorSwatch`

- `New(parent, color)`, `Get/SetColor`, `SetSelected`, `IsSelected`,
  `WasChanged`, `GetRevision`.

### `ColorPalette`

- `New(parent)`, `AddColor`, `RemoveColor`, `Clear`, `GetColorCount`,
  `GetColorAt`, `SetSelectedIndex`, `GetSelectedIndex`, `WasChanged`,
  `GetRevision`.

### `ColorPicker`

- `New(parent)`, `Get/SetColor`, `SetAlphaEnabled`, `IsAlphaEnabled`,
  `GetRed/Green/Blue/Alpha`, `WasChanged`, `GetRevision`.

All colors use `0x00RRGGBB`; alpha is separately represented as `[0,255]` until
the existing graphics color contract is extended deliberately.

## 9. Interactive data grid and virtual models

### `Viper.GUI.Grid`

- `SetViewportRows(first: i64, count: i64)`
- `SetVirtualRowCount(count: i64)`; `SetVirtualCell(row, col, text)`
- `SetSelectable(enabled)`; `GetSelectedRow/Column() -> i64`
- `SelectCell(row, col) -> i1`; `ClearSelection()`
- `WasSelectionChanged() -> i1`; `WasActivated() -> i1`
- `SetSortable(col, enabled)`; `SetSort(col, direction)`
- `GetSortColumn/Direction() -> i64`; `WasSortChanged() -> i1`
- `SetColumnWidth(col, width)`; `SetColumnResizable(col, enabled)`
- `WasColumnResized() -> i1`; `GetResizedColumn() -> i64`
- `SetEditable(enabled)`; `BeginEdit(row, col) -> i1`; `CommitEdit(text) -> i1`
- `CancelEdit()`; `IsEditing() -> i1`; `WasCellEdited() -> i1`
- `ScrollToRow(row)`; `GetScrollRow() -> i64`; `GetRevision() -> i64`

### `VirtualList` and `ListBox`

- `VirtualList.Bind(listBox: ListBox) -> i1`; `Unbind()`
- `VirtualList.SetRowText(index, text)`; `InvalidateRow(index)`
- `ListBox.SetVirtualModel(model: VirtualList) -> i1`; `ClearVirtualModel()`
- `ListBox.GetVisibleFirst/Count() -> i64`

### `VirtualTree` and `TreeView`

- `VirtualTree.Bind(tree: TreeView) -> i1`; `Unbind()`
- `VirtualTree.VisibleRowsRange(first, count) -> Seq`
- `TreeView.SetVirtualModel(model: VirtualTree) -> i1`; `ClearVirtualModel()`

IDs are unique, stable strings. Duplicate IDs reject the mutation and preserve
the previous model state.

## 10. Dialogs

### `FileDialog`

- `SetShowHidden(i1)`; `SetConfirmOverwrite(i1)`
- `SetDefaultExtension(str)`; `AddBookmark(str)`; `ClearBookmarks()`
- `ShowAsync() -> i1`; `IsOpen() -> i1`; `WasCompleted() -> i1`
- `GetStatus() -> i64`; `GetError() -> str`; `GetPaths() -> Seq`
- Static `OpenOption`, `SaveOption`, `SelectFolderOption` returning `Option`.
- Static `OpenMultipleSeq` returning `Seq`.

### `MessageBox`

- `AddButtonWithRole(label, id, role)`; `SetButtonRole(id, role) -> i1`
- `SetCancelButton(id) -> i1`; `SetDefaultButton(id) -> i1`
- `ShowAsync() -> i1`; `IsOpen() -> i1`; `WasCompleted() -> i1`
- `GetStatus() -> i64`; `GetResult() -> i64`; `GetError() -> str`
- Static `PromptOption(title, message) -> Option`.

Button IDs must be unique. Roles, not translated labels, determine Enter/Escape
behavior.

## 11. Images, video, minimap, and automation

### `Image`

- `TrySetPixels(pixels, width, height) -> i1`
- `SetFilter(filter: i64)`; `GetFilter() -> i64`
- `UpdateRegion(pixels, srcX, srcY, width, height, dstX, dstY) -> i1`

Existing `SetPixels` calls `TrySetPixels` and ignores the status for compatibility.

### `VideoWidget`

- `SetAutoUpdate(i1)`; `IsAutoUpdate() -> i1`
- `WasLoaded/WasFailed/WasBufferingChanged/WasEnded/WasSeeked() -> i1`
- `GetError() -> str`; `GetRevision() -> i64`
- `SetControlsAutoHide(i1)`; `SetFullscreen(i1)`; `IsFullscreen() -> i1`

Manual `Update` remains valid and is idempotent with app auto-update in the same
frame generation.

### `Minimap`

- `GetSourceRevision() -> i64`; `InvalidateLines(first, count)`
- `SetMaximumCachedLines(count)`; `GetCachedLineCount() -> i64`

### `TestHarness`

- `BindApp(app: App) -> i1`; `UnbindApp()`
- `DispatchPending() -> i64`; `RenderFrame(deltaMs: f64) -> i1`
- `CapturePixels(x, y, width, height) -> Pixels`
- `CaptureHash(x, y, width, height) -> str`
- `CompareRegion(expected: Pixels, x, y, tolerance) -> Map`
- `GetAccessibilitySnapshot() -> Map`

Legacy manual widget registration and geometry capture remain available but are
documented as synthetic mode.

## 12. Debug and records

- `CodeEditor.GetPerfStats() -> Map` returns a schema-versioned snapshot with
  all existing counters. Existing individual getters remain callable.
- New public maps include `schemaVersion` and stable documented keys.
- Constant classes listed in section 2 are the preferred source for integer
  values; existing raw-integer parameters remain ABI-compatible.

