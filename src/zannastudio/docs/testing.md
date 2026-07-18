# Zanna Studio Testing And Verification

This document lists the current automated probes, useful CTest invocations, and
manual checks for Zanna Studio.

## Build First

For repository changes, use the repository build scripts rather than raw CMake
full-build commands:

```sh
./scripts/build_zanna_mac.sh
./scripts/build_zanna_linux.sh
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build_zanna_win.ps1
```

For IDE-only native binary checks:

```sh
./scripts/build_ide.sh
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build_ide_win.ps1
```

Targeted `ctest` commands assume the `build/` tree exists.

## Main CTest Entries

Current Zanna Studio-related CTest entries are registered in
`src/tests/CMakeLists.txt`.

| Test | Purpose | Labels |
| --- | --- | --- |
| `zia_smoke_zannastudio` | Compile/run smoke for `src/probes/smoke_probe.zia`. | `zia;zannastudio;smoke;requires_display` |
| `zia_smoke_zannastudio_project_compile` | Build the `zannastudio/` project to IL. | `zia;zannastudio` |
| `zia_zannastudio_phase0_phase1` | Core document/project/command/location/session/language-service regression coverage. | `zia;zannastudio;phase0;phase1` |
| `zia_zannastudio_phase2_phase3` | Build/run jobs, project entry execution, breakpoints, debug boundary, scene data contracts. | `zia;zannastudio;phase2;phase3` |
| `zia_zannastudio_editor_hot_path` | Editor revision/performance hot-path guard. | `zia;zannastudio;editor;requires_display;perf` |
| `zia_zannastudio_intellisense` | Completion, diagnostics, hover, signature, and IntelliSense UI behavior. | `zia;zannastudio;intellisense;requires_display` |
| `zia_zannastudio_file_tree` | Explorer interactions and file-tree workflows. | `zia;zannastudio;file-tree;requires_display` |
| `zia_zannastudio_activity_bar` | Activity bar and workbench visibility behavior. | `zia;zannastudio;activity-bar;requires_display` |
| `zia_zannastudio_multi_root` | Multi-root workspace behavior. | `zia;zannastudio;multi-root;requires_display` |
| `zia_zannastudio_scm` | Git Source Control command layer, async job pump, spaces/rename/conflict parsing, and view model smoke. | `zia;zannastudio;scm` |
| `zia_zannastudio_terminal` | Terminal session/controller non-display behavior. | `zia;zannastudio;terminal` |
| `zia_zannastudio_terminal_open` | Terminal panel open/start behavior. | `zia;zannastudio;terminal;requires_display` |
| `zia_zannastudio_terminal_hidden_start` | Terminal hidden/open lifecycle and hidden-output draining behavior. | `zia;zannastudio;terminal;requires_display` |
| `zia_zannastudio_terminal_render` | Terminal rendering path. | `zia;zannastudio;terminal;requires_display` |
| `zia_zannastudio_context_menu` | Context menu routing and enabled state. | `zia;zannastudio;context-menu;requires_display` |
| `zia_zannastudio_syntax_render` | Syntax rendering path. | `zia;zannastudio;syntax;requires_display` |
| `zia_zannastudio_formatting` | Formatting commands and helpers. | `zia;zannastudio;format` |
| `zia_zannastudio_debug` | VM-backed debug adapter integration. | `zia;zannastudio;debug` |
| `zia_zannastudio_semantic_tokens` | Semantic token rendering behavior. | `zia;zannastudio;semantic;requires_display` |
| `zia_zannastudio_console_search` | Output panel helpers, console behavior, docked search panel, workspace-symbol discovery, and Quick Open ranking. | `zia;zannastudio;console;search;requires_display` |
| `native_smoke_zannastudio_completion_arm64` | Native completion link/e2e smoke on arm64 when enabled. | native/e2e labels from CMake |
| `native_smoke_zannastudio_completion_x64` | Native completion link/e2e smoke on x64 when enabled. | native/e2e labels from CMake |

Some labels and availability depend on the configured platform, graphics
backend, and native-link settings.

## Useful Test Commands

Compile the Zanna Studio project:

```sh
ctest --test-dir build -R zia_smoke_zannastudio_project_compile --output-on-failure
```

Run all Zanna Studio-labelled tests:

```sh
ctest --test-dir build -L zannastudio --output-on-failure
```

Run non-display Zanna Studio tests in a headless environment:

```sh
ctest --test-dir build -L zannastudio -LE requires_display --output-on-failure
```

Run display-dependent editor and UI probes:

```sh
ctest --test-dir build -R 'zia_zannastudio_(editor_hot_path|intellisense|file_tree|activity_bar|console_search)' --output-on-failure
```

Run terminal-specific probes:

```sh
ctest --test-dir build -R 'zia_zannastudio_terminal' --output-on-failure
```

Run debugger probe:

```sh
ctest --test-dir build -R zia_zannastudio_debug --output-on-failure
```

Run Source Control probe:

```sh
ctest --test-dir build -R zia_zannastudio_scm --output-on-failure
```

Run OutputPane low-level regressions:

```sh
ctest --test-dir build -R test_vg_audit_fixes --output-on-failure
```

## Probe Source Map

Zanna Studio probes live in `zannastudio/src/probes/`.

| Probe file | Area |
| --- | --- |
| `smoke_probe.zia` | Basic app compile/runtime smoke. |
| `phase0_phase1_probe.zia` | Documents, commands, sessions, language gates, workspace edits. |
| `phase2_phase3_probe.zia` | Build/run/debug boundary and scene data contracts. |
| `editor_hot_path_probe.zia` | Editor copy/layout/index performance hot paths. |
| `intellisense_probe.zia` | Completion and language-service UI behavior. |
| `file_tree_probe.zia` | Explorer behavior. |
| `activity_bar_probe.zia` | Activity bar/workbench view toggles. |
| `multi_root_file_tree_probe.zia` | Multi-root workspace behavior. |
| `scm_probe.zia` | Git command layer, async status jobs, paths with spaces, staged renames, and unmerged conflict rows. |
| `terminal_probe.zia` | Terminal session/controller core behavior. |
| `terminal_open_probe.zia` | Terminal panel start/open behavior. |
| `terminal_hidden_start_probe.zia` | Hidden terminal lifecycle and output replay. |
| `terminal_render_probe.zia` | Terminal render behavior. |
| `context_menu_probe.zia` | Context menu state and dispatch. |
| `syntax_render_probe.zia` | Syntax rendering path. |
| `formatting_probe.zia` | Formatting helpers and commands. |
| `debug_probe.zia` | VM debug adapter session integration. |
| `semantic_tokens_probe.zia` | Semantic tokens. |
| `console_search_probe.zia` | Output/console/docked-search/Quick Open helpers. |

## Performance Logging

Set `ZANNASTUDIO_PERF_LOG` before launching the IDE to write frame/controller and
editor performance counters:

```sh
ZANNASTUDIO_PERF_LOG=/tmp/zannastudio-perf.log ./src/zannastudio/bin/zannastudio
```

Perf output is intended for dogfood sessions and hot-path regressions. It is
especially useful for checking:

- Full-text copy counts and bytes.
- Layout scan counts.
- Project index updates and bytes.
- Controller timing spikes.
- Worst-frame windows during large-file editing.

## Manual Verification Checklist

Use this when changing user-facing IDE behavior:

- Launch with a temporary settings directory or isolated profile.
- Open a real project with at least one large Zia file.
- Restore session and verify active tab/cursor/scroll.
- Type in a large file and verify no obvious frame stalls.
- Trigger completion and accept an item.
- Trigger diagnostics and navigate from Problems.
- Use hover and signature help.
- Use Quick Open and workspace search.
- Rename or create a file from the explorer.
- Save, Save As, Save All, and close modified tabs.
- Build and run the project.
- Start debugging, hit a breakpoint, step, inspect locals/call stack, evaluate,
  and stop.
- Open the integrated terminal, type a shell command, resize, stop, restart.
- Use Source Control status, stage/unstage, diff, and commit in a throwaway repo.
- Switch dark/light theme and check contrast.
- Restart the IDE and verify session/recovery behavior.

## Coverage Gaps

Known areas needing stronger tests:

- Full visual scene editor behavior does not exist yet and therefore has no app
  tests.
- Source Control push/pull, credential prompts, complex conflict recovery, and
  exotic path bytes are not deeply covered.
- Terminal row-addressing redraws are covered by probes; full-screen TUI behavior
  remains out of scope and is not tested.
- Cross-platform PTY/ConPTY behavior needs regular Windows/macOS/Linux smoke.
- Tool-panel virtualization and huge-output behavior need stronger UI stress
  coverage.
- Rich debugger object expansion and a dedicated watch-management panel are not
  present or covered.
- Accessibility and keyboard-focus behavior need more systematic checks.
