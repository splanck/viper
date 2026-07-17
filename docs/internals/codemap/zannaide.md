---
status: active
audience: contributors
last-verified: 2026-07-16
---

# CODEMAP: ZannaIDE

ZannaIDE (`src/zannaide/`) is the repository IDE application for editing,
building, running, and debugging Zanna projects. It is written in Zia and runs
on the `Zanna.GUI.*`, `Zanna.System.*`, `Zanna.Workspace.*`, and
language-service runtime surfaces.

ZannaIDE maintains its own detailed documentation set inside the application
tree — this page is a locator, not a duplicate:

| Document | Purpose |
|----------|---------|
| [`src/zannaide/README.md`](../../../src/zannaide/README.md) | Scope, current capabilities, and entry points |
| [`src/zannaide/docs/architecture.md`](../../../src/zannaide/docs/architecture.md) | Source layout, ownership, and layering rules |
| [`src/zannaide/docs/source-map.md`](../../../src/zannaide/docs/source-map.md) | Detailed module-by-module source guide |
| [`src/zannaide/docs/status.md`](../../../src/zannaide/docs/status.md) | Feature status and known gaps |
| [`src/zannaide/docs/workflows.md`](../../../src/zannaide/docs/workflows.md) | User and developer workflows |
| [`src/zannaide/docs/runtime-integration.md`](../../../src/zannaide/docs/runtime-integration.md) | Runtime APIs used by the IDE |
| [`src/zannaide/docs/maintenance.md`](../../../src/zannaide/docs/maintenance.md) | How to change ZannaIDE safely |
| [`src/zannaide/docs/testing.md`](../../../src/zannaide/docs/testing.md) | Probes, CTest entries, manual checks |

## Top-Level Layout (`src/zannaide/`)

| Directory | Purpose |
|-----------|---------|
| `src/app/` | Application shell, startup, session restore (12 files) |
| `src/basic/` | BASIC language-service integration |
| `src/build/` | Build and run job management via `Zanna.System.Process` (8 files) |
| `src/commands/` | Command registry and palette actions (16 files) |
| `src/core/` | Shared core utilities and state (9 files) |
| `src/editor/` | Multi-tab editing on `Zanna.GUI.CodeEditor` (20 files) |
| `src/probes/` | Headless CTest probes (36 files) |
| `src/scm/` | Git source-control view (4 files) |
| `src/services/` | Workspace indexing, settings, external-change detection (8 files) |
| `src/terminal/` | Integrated PTY terminal via `Zanna.System.Pty` (2 files) |
| `src/tests/` | In-tree test helpers (4 files) |
| `src/ui/` | Panels, dialogs, explorer, status bar (15 files) |
| `src/zia/` | Zia language-service integration (10 files) |
| `bin/`, `scripts/` | Launch and packaging helpers |
