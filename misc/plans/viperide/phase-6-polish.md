# Phase 6 - Hardening and Dogfood

## 1. Summary and Objective

Finish the IDE by proving it on real projects and removing rough edges. This phase is not where accessibility, keyboard access, cross-platform support, or data safety first appear; those are required throughout. Phase 6 audits the whole product after Phases 0-5 land.

## 2. Scope

In:

- Full dogfood pass on at least one real Viper game.
- Accessibility and high-contrast audit.
- Shortcut and command-palette completeness audit.
- Cross-platform build/smoke verification.
- Documentation and showcase updates.
- Performance and large-project sanity checks.
- Crash/data-loss recovery audit.

Out:

- New feature areas.
- New runtime APIs except for small fixes found by the audit.

## 3. Checklist

### 3.1 Data Safety

- Dirty code docs, scene docs, and untitled docs block close.
- Save failures never clear dirty state.
- External changes are detected before overwrite.
- Session restore never loses unsaved work.
- Autosave/recovery decision is documented. If not implemented, docs must not claim it.

### 3.2 Keyboard and Command Palette

- Every command has a registry entry.
- Every command has a palette label unless intentionally hidden.
- Common commands have shortcuts.
- Active-surface commands are disabled or hidden when unsupported.
- No command id drift like the old sidebar mismatch.

### 3.3 Accessibility and Theme

- Dark theme remains high contrast.
- Light theme is usable.
- Focus indicators are visible.
- Tool panels work by keyboard where reasonable.
- Text fits in buttons, tabs, panels, and status areas.
- Scene editor conveys selected tool/layer/object without color alone.

### 3.4 Cross-Platform

- macOS, Linux, and Windows builds pass.
- No macOS-only shell commands remain in core workflows without platform guards.
- Path handling works for spaces, colons, backslashes, and case differences.
- Graphics and non-graphics builds link after runtime GUI additions.

### 3.5 Performance

- Large project tree open does not freeze unacceptably.
- Search, indexing, build, and run jobs report progress or remain cancellable.
- SceneView remains interactive on representative maps.
- Console scrollback has a cap.

### 3.6 Documentation and Showcase

- ViperIDE docs describe actual shipped features only.
- Showcase page does not claim multi-cursor, debugging, scene editing, or recovery until those features pass acceptance tests.
- User guide covers:
  - projects and run configs
  - code navigation
  - scene editing
  - build/run/debug
  - recovery limitations

## 4. Tests and Verification

- Full build and `ctest --test-dir build --output-on-failure`.
- Runtime completeness checks.
- Graphics-on and graphics-off builds.
- Platform policy lint if available.
- Manual end-to-end script:
  - open project
  - edit code
  - navigate definition/reference
  - rename symbol
  - build and run
  - open scene
  - paint and place object
  - save/reload
  - play scene
  - debug sample
  - close with dirty docs and cancel/save/discard

## 5. Exit Criteria

Phase 6 is done only when the IDE can be used for a small real Viper project without switching to another editor for ordinary code editing, running, and scene authoring tasks.
