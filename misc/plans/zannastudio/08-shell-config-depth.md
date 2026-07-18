# Plan 08 — Shell and Config Depth

Date: 2026-07-17 · Track: A (Zia app) · Loop: Zia · Size: M

## 1. Objective

First-run-to-daily-use polish: rebindable keyboard shortcuts, a new-project
wizard with templates, and a findable settings panel.

## 2. Scope

1. **Keybindings editor.** New settings tab listing the
   `commands/command_registry.zia` shortcut table (command, category, current
   binding, source default/user); click-to-rebind key-capture modal;
   conflict detection reusing `ShortcutConflictSummary`; overrides persisted
   as a `[keybindings]` section in `settings.ini`; registry loads overrides
   at startup; per-row and global reset-to-default. The existing read-only
   Help ▸ Keyboard Shortcuts report remains (accessibility: menu beats
   chords) and gains an "Edit…" affordance.
2. **New-project wizard.** Overlay flow: name → location → template →
   options (git init). Embedded templates under
   `src/zannastudio/src/templates/` (console app, GUI app, library — each a
   `zanna.project` + starter sources). Creates, opens as project, offers
   first build. Welcome page gains a "New Project" quick action (hooks the
   Plan 02 welcome design).
3. **Settings polish.** Search/filter box over settings cards; category
   navigation; live theme preview on the Appearance card (apply-on-change
   with revert).

## 3. Runtime surface

None.

## 4. Tests / verification (exit gate)

Probes: `keybinding_editor_probe.zia` (rebind, conflict, persist, reset),
`new_project_probe.zia` (wizard creates a project that `zanna build` compiles
— reuse the `--zanna-bin` harness pattern from `phase2_phase3_probe`),
`settings_search_probe.zia`. Incremental build + targeted ctest;
`build_ide.sh` iteration loop; manual pass of key capture on all three
platforms (modifier quirks).

## 5. Risks

- Key-capture vs IME/platform modifiers — capture uses existing rt key
  events; the cross-platform manual pass is the gate.
- Template drift vs toolchain project format — the compile-the-result probe
  covers it permanently.
- Settings INI growth — the `[keybindings]` section only stores deltas from
  defaults.
