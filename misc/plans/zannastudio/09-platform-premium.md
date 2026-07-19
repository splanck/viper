# Plan 09 — Platform Premium

Date: 2026-07-17 · Track: P (platform) · Loop: C · Size: M

## 1. Objective

Finish per-OS nativeness where it clearly helps; keep drawn UI where brand
and accessibility consistency win. Explicit non-goals recorded.

## 2. Scope

1. **Native Windows file dialogs.** New
   `src/lib/gui/src/dialogs/vg_filedialog_native_win32.c` using raw COM
   `IFileOpenDialog`/`IFileSaveDialog` (OS API — zero-dep compliant;
   hand-written vtable calls, CoCreateInstance, filters, default paths),
   mirroring the macOS `vg_filedialog_native.m` selection logic through the
   existing `vg_filedialog_platform_win32.c` glue. The drawn dialog remains
   the fallback (and the Linux path).
2. **Cursor set expansion.** `vgfx.h` cursor enum (currently 6 shapes) gains
   diagonal resizes (NWSE/NESW), grab/grabbing, crosshair, help,
   not-allowed; mapped per platform (NSCursor / `IDC_*` /
   `XCreateFontCursor`); wire SplitPane dividers (diagonal/axis resize),
   drag operations (grab/grabbing), and disabled drop targets (not-allowed).
3. **Linux AT-SPI parity.** Complete `rt_gui_accessibility_linux.c` to the
   macOS bridge's role/state/event coverage (it currently has a headless
   fallback and partial mapping).

## 2a. As-built record (2026-07-18)

- **Native Windows file dialogs**: `src/lib/gui/src/dialogs/
  vg_filedialog_native_win32.c` — raw COM `IFileOpenDialog`/`IFileSaveDialog`
  (open, multi-open, save, pick-folder), UTF-8↔wide conversion, filters,
  default folders, all interfaces released on every path. New
  `vg_native_dialogs_available()` (COM probe; macOS always 1) routes the four
  `rt_gui_filedialog.c` entry points: native on macOS/Windows, **drawn
  fallback preserved** when the Windows COM probe fails, drawn on Linux.
  `zannagui` links `ole32 shell32` on Windows.
- **Cursor set**: `VGFX_CURSOR_*` grew from 6 to 13 shapes (diagonal NWSE/
  NESW resize, grab/grabbing, crosshair, help, not-allowed) mapped on all
  three backends (NSCursor / `IDC_*` / `XC_*`; X11 cache widened). The
  runtime `Cursor.Set` clamp widened to the new range. macOS lacks public
  diagonal-resize cursors, so 6/7 use the closest public shape (documented).
- **Linux AT-SPI: REMAINING SCOPE.** A true AT-SPI2 provider means speaking
  the accessibility D-Bus protocol (bus discovery via
  `org.a11y.Bus.GetAddress`, auth handshake, marshaling, and the
  `org.a11y.atspi.{Accessible,Component,Action,Value,EventListener}`
  interfaces) with a hand-rolled zero-dependency D-Bus client — a
  subsystem comparable to the whole UIA provider. Shipping a partial stub
  would be worse than the honest headless fallback, so this is carved out
  as the program's one explicit follow-up (design notes above); the
  preference queries and headless snapshot remain in place.
- Windows validation of dialogs/cursors rides the same owner Windows-machine
  pass as plan 03.

## 3. Non-goals (recorded)

- Native Windows/Linux menu bars — the drawn menu keeps theme/brand
  consistency and uniform accessibility semantics (the UIA/AT-SPI providers
  expose menus regardless).
- Rich (image) clipboard — future work.
- Wayland backend — out of this program's scope; X11 remains the Linux
  backend (XWayland covers Wayland desktops).

## 4. Runtime surface

None expected. If cursor constants are exposed to Zia
(`Zanna.GUI.App.SetCursor(kind)`), they take the full runtime checklist and
join the Plan 04 ADR.

## 5. Tests / verification (exit gate)

C tests for dialog selection/fallback logic and cursor mapping tables;
Windows dialogs manually verified on a Windows machine; Linux AT-SPI checked
against an accerciser-style manual checklist; incremental build + targeted
ctest; `lint_platform_policy.sh` green.

## 6. Risks

- COM error paths (device with no dialog service, RDP sessions) — fallback to
  the drawn dialog is always available and tested.
- AT-SPI transport variance across distros — the headless fallback stays; the
  gate is a named-distro manual check.
