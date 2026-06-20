# LSP Feature Parity (Over-the-Wire)

**Status:** Verified real
**Area:** `src/tools/lsp-common/`, `src/tools/zia-server/`
**Effort:** M
**Roadmap fit:** v0.3.x P4 (ViperIDE/editor ecosystem)

## Problem

The LSP server answers only **7 methods** (verified by grepping the handler):
`textDocument/completion`, `hover`, `documentSymbol`, `didOpen`/`didChange`/`didClose`,
and `publishDiagnostics`. It does **not** implement `definition`, `references`, `rename`,
`signatureHelp`, `workspace/symbol`, `semanticTokens`, `foldingRange`, or `inlayHint`.

The capability exists internally — ViperIDE performs go-to-definition, find-references,
and rename through a **native `ProjectIndex`** — but it isn't exposed over LSP, so
external editors (VS Code, Neovim, Emacs) get a thin experience and the rich logic is
locked to the bundled IDE.

## Current state (verified)

- `src/tools/lsp-common/LspHandler.cpp` — the 7-method surface; full-document sync only.
- `src/tools/zia-server/` — the Zia compiler bridge; tests in `src/tests/zia-server/`
  (~2.4K LOC: transport, JSON, handler, MCP).
- (Confirm during implementation) the native `ProjectIndex` exposes
  `findDefinition`/`findReferences`/`rename`/symbol-search — bridge these rather than
  reimplement.

## Goal & scope

- **In:** Expose `definition`, `references`, `rename`, `signatureHelp`,
  `workspace/symbol`, and `semanticTokens` over LSP by bridging the existing index;
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
2. `zia-server` bridge: add/confirm bridge methods mapping to `ProjectIndex`.
3. Define the position↔offset and range mapping helpers once; reuse across handlers.
4. (Stretch) incremental document store + range edits.

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

## Cross-platform

Protocol/string handling only; runs identically on all hosts. JSON/transport already
cross-platform per the existing server tests.

## Risks / open questions

- **Confirm the `ProjectIndex` API** before writing bridges; if a method is missing
  (e.g. workspace-wide symbol search), that becomes a small sub-task, not a blocker.
- **Position encoding** (UTF-16 code units per LSP spec) — ensure the offset mapping
  matches the spec, or non-ASCII files mis-locate; add a non-ASCII fixture test.
