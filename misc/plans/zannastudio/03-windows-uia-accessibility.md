# Plan 03 — Windows UIA Accessibility Provider

Date: 2026-07-17 · Track: P (platform) · Loop: C (Windows) · Size: L (~2-3K LOC)

## 1. Objective

Narrator and NVDA can read Zanna Studio. Windows accessibility today is
preferences-only (`src/runtime/graphics/gui/rt_gui_accessibility_win32.c`
reads high-contrast/reduced-motion/dark-mode); there is **no** UI Automation
provider, so screen readers see nothing. macOS has a complete VoiceOver bridge
(`rt_gui_accessibility_macos.m`) over the shared cross-platform accessibility
tree (`rt_gui_accessibility.c`) — this plan brings Windows to parity.

## 2. Scope

- Implement UIA providers in `rt_gui_accessibility_win32.c` as hand-written
  COM vtables in C (house style; no ATL/WRL):
  `IRawElementProviderSimple`, `IRawElementProviderFragment`,
  `IRawElementProviderFragmentRoot`, mapping the existing accessibility tree
  (mirror the macOS bridge's role/state/property mapping).
- Control patterns: Invoke (buttons/menu items), Toggle (checkboxes), Value
  (inputs/sliders), Selection/SelectionItem (lists/tabs/trees), ScrollItem;
  **TextPattern for the code editor scoped to viewport-range granularity
  first** (full document ranges as a stretch goal, staged explicitly).
- `WM_GETOBJECT` handling in
  `src/lib/graphics/src/vgfx_platform_win32.c` via
  `UiaReturnRawElementProvider`.
- Events: `UiaRaiseAutomationEvent` / `UiaRaiseAutomationPropertyChangedEvent`
  on focus change, value change, structure change (hook the existing
  accessibility-tree notification points that feed the macOS bridge).
- Lifetime: `UiaDisconnectProvider` on widget destroy; providers hold weak
  references into the widget tree with generation checks.

## 3. Threading contract (DECIDED, as-built 2026-07-18)

**Live-access on the HWND thread.** HWND-based server-side providers
(`ProviderOptions_ServerSideProvider`, no `UseComThreading`) have their calls
marshalled by UIA to the thread that owns the HWND, delivered while the
platform pumps messages — the same thread that owns the widget tree. This
matches the macOS bridge exactly (live widgets guarded by
`vg_widget_is_live` + immutable widget ID), so no snapshotting is needed.

## 3a. As-built record (2026-07-18)

- `vgfx` gained a generic native-message hook
  (`vgfx_set_native_msg_hook`, stored on the shared `struct vgfx_window`;
  the Win32 wndproc consults it for `WM_GETOBJECT` only).
- `rt_gui_accessibility_win32.c` implements the full provider: hand-rolled
  C COM (Simple/Fragment/FragmentRoot + Invoke/Toggle/Value/RangeValue/
  SelectionItem), role→control-type mapping mirroring the macOS bridge,
  bounds via `vg_widget_get_screen_bounds` + `ClientToScreen`, hit-testing,
  focus tracking, property-changed + focus events, live-region announcements
  via `UiaRaiseNotificationEvent` (dynamically resolved, tolerant of older
  Windows). `uiautomationcore.dll` is loaded dynamically (ConPTY precedent);
  `oleaut32` linked for BSTR/VARIANT/SAFEARRAY.
- **Editor content is surfaced through the Value pattern (read-only,
  `Document` control type) in this revision; the full TextPattern
  (ITextProvider/ITextRangeProvider, viewport ranges) is the staged
  follow-up** — tracked as the remaining scope item of this plan.
- Sliders/progress/spinners expose read-only RangeValue; adjustment is
  focus + arrow keys. Selection surfaces are single-select
  (AddToSelection/RemoveFromSelection intentionally fail).
- Headless vtable test: `src/tests/runtime/RTUiaProviderTests.c`
  (`test_rt_uia_provider`, WIN32-only, labels runtime/gui/accessibility) via
  the `rt_gui_accessibility_win32_test_root/_teardown` seams — no UIA
  client, HWND, or uiautomationcore.dll needed.

## 4. Zero-dependency compliance

`uiautomationcore.dll` is an OS API (link `uiautomationcore.lib` or resolve
dynamically like ConPTY in `rt_pty.c`). Hand-rolled COM vtables in C are
established house style. No new runtime-def surface — this reuses existing
accessibility metadata.

## 5. Tests / verification (exit gate)

- Headless C unit tests calling provider vtables directly (no UIA client
  runtime required): tree navigation, property values, pattern behavior —
  Windows-only compilation, registered like other platform-specific tests.
- Manual screen-reader checklist (the owner validates on Windows, Narrator
  or NVDA):
  1. Launch `zannastudio.exe`; Narrator announces the window.
  2. Tab through the toolbar — each button announces its name and "button".
  3. Open the File menu — menu items announce as "menu item" with names.
  4. Focus the editor — announced as a document; content is read back
     (whole-buffer Value in this revision).
  5. Open the command palette — the input announces; typed text reads back.
  6. Trigger a build — the status-bar diagnostics change is announced
     (live-region notification).
  7. Open Settings — checkboxes announce name + checked state; toggling
     with Space announces the new state.
  8. Arrow through the file tree — items announce selection.
  9. Run `test_rt_uia_provider` (ctest) — all assertions pass.
- Incremental build + targeted ctest; `lint_platform_policy.sh` green (file
  is an approved adapter layer).

## 6. Risks

- UIA threading (§3) — decided up front, not discovered in debugging.
- TextPattern scope creep — staged; viewport-first is the committed scope.
- Provider lifetime vs teardown — generation-checked weak refs +
  `UiaDisconnectProvider`.
- Requires a Windows machine for validation; development can proceed
  cross-compiled/static but the gate includes a Windows run.
