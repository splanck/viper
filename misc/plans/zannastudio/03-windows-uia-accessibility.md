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

## 3. Threading contract (decide before code)

UIA may call providers off the UI thread while the rt_gui loop is
single-threaded. Decision to record at implementation start: either
`ProviderOptions_UseComThreading` with a marshal-to-UI-thread bridge (hidden
message window + `SendMessage`), or full snapshotting of the accessibility
tree into provider-owned immutable data updated once per frame. The snapshot
approach avoids cross-thread widget access entirely and matches the existing
per-frame accessibility sync; prefer it unless measurement says otherwise.

## 4. Zero-dependency compliance

`uiautomationcore.dll` is an OS API (link `uiautomationcore.lib` or resolve
dynamically like ConPTY in `rt_pty.c`). Hand-rolled COM vtables in C are
established house style. No new runtime-def surface — this reuses existing
accessibility metadata.

## 5. Tests / verification (exit gate)

- Headless C unit tests calling provider vtables directly (no UIA client
  runtime required): tree navigation, property values, pattern behavior —
  Windows-only compilation, registered like other platform-specific tests.
- Manual screen-reader checklist (the owner validates): menu navigation
  announces items; editor read-back by line/word; completion list announces
  selection; dialogs announce title/focus; status-bar announcements on build
  results.
- Incremental build + targeted ctest; `lint_platform_policy.sh` green (file
  is an approved adapter layer).

## 6. Risks

- UIA threading (§3) — decided up front, not discovered in debugging.
- TextPattern scope creep — staged; viewport-first is the committed scope.
- Provider lifetime vs teardown — generation-checked weak refs +
  `UiaDisconnectProvider`.
- Requires a Windows machine for validation; development can proceed
  cross-compiled/static but the gate includes a Windows run.
