---
status: active
audience: contributors
last-verified: 2026-07-16
---

# CODEMAP: ViperIDE

ViperIDE (`src/viperide/`) is the repository IDE application for editing,
building, running, and debugging Viper projects. It is written in Zia and runs
on the `Viper.GUI.*`, `Viper.System.*`, `Viper.Workspace.*`, and
language-service runtime surfaces.

ViperIDE maintains its own detailed documentation set inside the application
tree — this page is a locator, not a duplicate:

| Document | Purpose |
|----------|---------|
| [`src/viperide/README.md`](../../../src/viperide/README.md) | Scope, current capabilities, and entry points |
| [`src/viperide/docs/architecture.md`](../../../src/viperide/docs/architecture.md) | Source layout, ownership, and layering rules |
| [`src/viperide/docs/source-map.md`](../../../src/viperide/docs/source-map.md) | Detailed module-by-module source guide |
| [`src/viperide/docs/status.md`](../../../src/viperide/docs/status.md) | Feature status and known gaps |
| [`src/viperide/docs/workflows.md`](../../../src/viperide/docs/workflows.md) | User and developer workflows |
| [`src/viperide/docs/runtime-integration.md`](../../../src/viperide/docs/runtime-integration.md) | Runtime APIs used by the IDE |
| [`src/viperide/docs/maintenance.md`](../../../src/viperide/docs/maintenance.md) | How to change ViperIDE safely |
| [`src/viperide/docs/testing.md`](../../../src/viperide/docs/testing.md) | Probes, CTest entries, manual checks |

## Top-Level Layout (`src/viperide/`)

| Directory | Purpose |
|-----------|---------|
| `src/app/` | Application shell, startup, session restore (12 files) |
| `src/basic/` | BASIC language-service integration |
| `src/build/` | Build and run job management via `Viper.System.Process` (8 files) |
| `src/commands/` | Command registry and palette actions (16 files) |
| `src/core/` | Shared core utilities and state (9 files) |
| `src/editor/` | Multi-tab editing on `Viper.GUI.CodeEditor` (20 files) |
| `src/probes/` | Headless CTest probes (36 files) |
| `src/scm/` | Git source-control view (4 files) |
| `src/services/` | Workspace indexing, settings, external-change detection (8 files) |
| `src/terminal/` | Integrated PTY terminal via `Viper.System.Pty` (2 files) |
| `src/tests/` | In-tree test helpers (4 files) |
| `src/ui/` | Panels, dialogs, explorer, status bar (15 files) |
| `src/zia/` | Zia language-service integration (10 files) |
| `bin/`, `scripts/` | Launch and packaging helpers |
