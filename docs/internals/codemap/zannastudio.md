---
status: active
audience: contributors
last-verified: 2026-07-16
---

# CODEMAP: Zanna Studio

Zanna Studio (`src/zannastudio/`; formerly ZannaIDE, renamed in ADR 0118) is
the repository IDE application for editing,
building, running, and debugging Zanna projects. It is written in Zia and runs
on the `Zanna.GUI.*`, `Zanna.System.*`, `Zanna.Workspace.*`, and
language-service runtime surfaces.

Zanna Studio maintains its own detailed documentation set inside the application
tree — this page is a locator, not a duplicate:

| Document | Purpose |
|----------|---------|
| [`src/zannastudio/README.md`](../../../src/zannastudio/README.md) | Scope, current capabilities, and entry points |
| [`src/zannastudio/docs/architecture.md`](../../../src/zannastudio/docs/architecture.md) | Source layout, ownership, and layering rules |
| [`src/zannastudio/docs/source-map.md`](../../../src/zannastudio/docs/source-map.md) | Detailed module-by-module source guide |
| [`src/zannastudio/docs/status.md`](../../../src/zannastudio/docs/status.md) | Feature status and known gaps |
| [`src/zannastudio/docs/workflows.md`](../../../src/zannastudio/docs/workflows.md) | User and developer workflows |
| [`src/zannastudio/docs/runtime-integration.md`](../../../src/zannastudio/docs/runtime-integration.md) | Runtime APIs used by the IDE |
| [`src/zannastudio/docs/maintenance.md`](../../../src/zannastudio/docs/maintenance.md) | How to change Zanna Studio safely |
| [`src/zannastudio/docs/testing.md`](../../../src/zannastudio/docs/testing.md) | Probes, CTest entries, manual checks |

## Top-Level Layout (`src/zannastudio/`)

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
