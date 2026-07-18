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
