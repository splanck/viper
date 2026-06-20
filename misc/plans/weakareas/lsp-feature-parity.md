# LSP Feature Parity (Over-the-Wire)

**Status:** Completed
**Area:** `src/tools/lsp-common/`, `src/tools/zia-server/`
**Effort:** M
**Roadmap fit:** v0.3.x P4 (ViperIDE/editor ecosystem)

## Problem

The shared LSP handler supports full-document sync notifications, diagnostics,
completion, hover, and document symbols. It does **not** implement `definition`, `references`, `rename`,
`signatureHelp`, `workspace/symbol`, `semanticTokens`, `foldingRange`, or `inlayHint`.

The capability exists internally — ViperIDE performs go-to-definition, find-references,
and rename through a **native `ProjectIndex`** — but it isn't exposed over LSP, so
external editors (VS Code, Neovim, Emacs) get a thin experience and the rich logic is
locked to the bundled IDE.

## Current state (verified)

- `src/tools/lsp-common/LspHandler.cpp` — full-document sync, diagnostics, completion,
  hover, document symbols, UTF-16/byte-position conversion helpers.
- `src/tools/lsp-common/ICompilerBridge.hpp` — bridge surface stops at check, compile,
  completion, hover, symbols, and dumps.
- `src/tools/zia-server/` and `src/tools/vbasic-server/` both implement that bridge and
  share the handler.
- `Viper.Zia.ProjectIndex` in `runtime.def` exposes `Definition`, `References`, and
  `RenameEdits`; ViperIDE already calls it through `viperide/src/editor/project_index.zia`.

## Goal & scope

- **In:** Expose `definition`, `references`, `rename`, `signatureHelp`,
  `workspace/symbol`, and `semanticTokens` over LSP by bridging the existing Zia index
  and compiler analysis;
  advertise the new capabilities in `initialize`.
- **Stretch:** `foldingRange`, `inlayHint`, and incremental `didChange` sync (below).
- **Out:** BASIC LSP semantic services (see `basic-ide-services.md`).

## Design

Each new method is a thin adapter: parse the LSP params (position/range), call the
existing bridge/index method, map the result to the LSP response shape. No new analysis
is written — this is exposure, not invention.

**Incremental sync (stretch):** today every keystroke resends the whole buffer. Add
range-based `didChange` handling (apply `contentChanges` to a maintained buffer) to cut
overhead for large files / remote sessions. Keep full-sync as a fallback advertised via
`textDocumentSync`.

## Implementation steps

1. `LspHandler.cpp`: add handlers for definition/references/rename/signatureHelp/
   workspace-symbol/semanticTokens; extend the `initialize` capabilities object.
2. Extend `ICompilerBridge` with capability-specific optional methods (or a separate
   `ISemanticIndexBridge`) so BASIC can honestly return unsupported/null until its index
   exists.
3. `zia-server` bridge: map definition/references/rename to the existing
   `ProjectIndex` implementation instead of re-implementing indexing.
4. Reuse the existing UTF-16 LSP position helpers in `LspHandler.cpp`; add tests around
   non-ASCII fixtures.
5. (Stretch) incremental document store + range edits.

## Tests (`src/tests/zia-server/`)

- One `test_lsp_handler` case per new method: craft a request, assert the response shape
  and a known-correct location/edit for a fixture document (Given/When/Then).
- `rename` correctness: assert all reference edits returned, none missed, none spurious.
- `initialize` advertises exactly the implemented capabilities (guard against claiming
  unimplemented ones).
- (Stretch) incremental edit applied == full-resync result for the same change.

## Documentation

- Update `docs/zia-server.md` and `docs/zia-server-mcp-tools.md` with the new capability
  matrix and example requests/responses.
- Add an editor-integration note (how to point VS Code/Neovim at the server) to the
  getting-started/tools docs.
- One release-notes line.

## Completion notes (2026-06-20)

- Added optional semantic-document hooks and feature capability methods to
  `ICompilerBridge`; unsupported languages keep default null/empty behavior.
- Extended the shared LSP handler with `textDocument/definition`,
  `textDocument/references`, `textDocument/rename`, `textDocument/signatureHelp`,
  `workspace/symbol`, and `textDocument/semanticTokens/full`.
- `zia-server` now mirrors open documents into the existing native
  `Viper.Zia.ProjectIndex` and uses it for definition, references, and rename edits.
- Signature help is bridged through existing structured Zia editor services; semantic
  tokens are produced from the Zia lexer and encoded with the advertised LSP legend.
- `vbasic-server` keeps the new features unadvertised until the BASIC IDE services plan
  enables a real semantic index.
- Tests updated in `src/tests/zia-server/test_lsp_handler.cpp` and
  `src/tests/vbasic-server/test_basic_server_lsp.cpp`; docs updated in
  `docs/zia-server.md`, `docs/zia-server-mcp-tools.md`, `docs/tools.md`,
  `docs/codemap/zia-server.md`, and the 0.2.7 release notes.

Verification:

```sh
cmake --build build --target test_zia_server_lsp test_zia_server_bridge test_vbasic_server_lsp -j 8
ctest --test-dir build -R '^(test_zia_server_lsp|test_zia_server_bridge|test_vbasic_server_lsp)$' --output-on-failure
```

## Cross-platform

Protocol/string handling only; runs identically on all hosts. JSON/transport already
cross-platform per the existing server tests.

## Risks / open questions

- **Workspace symbol source:** ViperIDE has workspace symbol logic, but LSP may need a
  bridge method over either `ProjectIndex` or the completion workspace cache. Avoid a
  second index.
- **Position encoding** (UTF-16 code units per LSP spec) — ensure the offset mapping
  matches the spec, or non-ASCII files mis-locate; add a non-ASCII fixture test.
