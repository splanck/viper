# BASIC IDE Language Services (Decision + Enablement)

**Status:** Completed — decision recorded; `vbasic-server` has basic IDE services
while ViperIDE disables BASIC semantic commands by design.
**Area:** `viperide/`, `src/tools/vbasic-server/`, `src/frontends/basic/`
**Effort:** M
**Roadmap fit:** v0.3.x P4 (ViperIDE)

## Problem

BASIC editing in ViperIDE is treated as mostly plain text: a probe
(`viperide/src/phase0_phase1_probe.zia`) asserts `basicService.canGoToDefinition ==
false` and that `rename` is not executable. `language_service.zia` leaves all BASIC
semantic capability flags false. Meanwhile `vbasic-server` already implements the shared
compiler bridge for diagnostics/check, completion, hover, document symbols, dumps, and
MCP/LSP tests.

So the gap is not missing BASIC compiler services; it is that ViperIDE does not consume
them, and BASIC still lacks the project-index layer required for definition/references/
rename parity.

This is a **deliberate scoping choice**, not a defect. This plan records the
decision, documents the server/IDE split, and preserves the incremental path to
enable services when ViperIDE has a real BASIC adapter boundary.

## Current state (verified)

- ViperIDE language-service registry with per-language capability flags; BASIC flags off.
- `BasicCompilerBridge` implements check/compile/completions/hover/symbols and is covered
  by `src/tests/vbasic-server/` LSP/MCP tests.
- Zia-only `Viper.Zia.ProjectIndex` powers ViperIDE definition/references/rename; there
  is no equivalent BASIC project index yet.

## Decision

ViperIDE keeps BASIC semantic commands disabled for now. The project should not
flip BASIC capability flags until ViperIDE can call a real BASIC backend without
blocking the editor and without falling through to Zia-only semantic APIs.

`vbasic-server` is the supported BASIC language-server surface for diagnostics,
completion, hover, document symbols, dumps, and MCP/LSP structured requests.
Definition/references/rename/signature-help/workspace-symbol/semantic-token
parity remains unsupported until a BASIC project index or equivalent server-side
semantic layer exists.

If BASIC IDE services are later enabled inside ViperIDE, do it
**incrementally**, cheapest-first, because the server analysis already exists:

1. **Diagnostics** (cheapest — `BasicCompilerBridge::check` already produces them): surface compile
   diagnostics in the editor.
2. **Hover** (already implemented in `BasicCompilerBridge::hover`): reuse bridge output.
3. **Completion** (already implemented in `BasicCompilerBridge::completions`).
4. **Document symbols** (already implemented in `BasicCompilerBridge::symbols`).
5. **Definition / references / rename** (needs a BASIC symbol index — most work; do last).

Each step flips one capability flag and wires one bridge call. Capability honesty
is the invariant: never advertise a flag before its feature actually works.

## Completed work

- Kept ViperIDE BASIC semantic flags disabled and covered by
  `zia_viperide_phase0_phase1`.
- Kept `vbasic-server` LSP capabilities honest: completion, hover, document
  symbols, and diagnostics are exposed; ProjectIndex-dependent features and
  semantic tokens are not advertised.
- Documented the split in `docs/feature-parity.md`,
  `viperide/plans/roadmap.md`, `viperide/plans/editor-first-class-plan.md`, and
  `viperide/README.md`.
- Recorded the release-note behavior as an intentional BASIC capability boundary.

## Deferred extension path

1. Add a small language-server client boundary used by ViperIDE language services. Prefer
   talking to `vbasic-server` through its existing LSP/MCP surface first, because that path
   already has tests and keeps compiler state out of the editor process.
2. Enable diagnostics: flip `canDiagnose`, route server diagnostics to existing editor
   squiggles, and keep the Zia diagnostics path unchanged.
3. Enable hover: flip `canHover`, map cursor positions to server hover requests.
4. Enable completion: flip `canComplete`, map server completion items to the existing
   completion UI.
5. Enable document symbols: flip `canDocumentSymbols` and populate the existing symbol UI.
6. (Last) build a BASIC symbol index for definition/references/rename, or expose one
   through the server; only then flip `canGoToDefinition` / `canRename`.
7. Update the probe expectations at each step (they currently assert *disabled*).

## Tests

- `src/tests/vbasic-server/test_basic_server_lsp.cpp` verifies BASIC LSP
  initialization advertises completion, hover, document sync, and document
  symbols while omitting definition/references/rename/signature-help/workspace
  symbols/semantic tokens.
- `src/tests/vbasic-server/test_basic_compiler_bridge.cpp` and
  `src/tests/vbasic-server/test_basic_server_mcp.cpp` cover the bridge/MCP
  services that remain available outside ViperIDE.
- `viperide/src/phase0_phase1_probe.zia` verifies BASIC semantic commands are
  disabled through the IDE language-service capability gate.
- Later flag flips must update the ViperIDE probe expectations in the same
  patch.

## Documentation

- `docs/feature-parity.md` records the server/IDE split.
- ViperIDE docs record BASIC semantic commands as deliberately disabled until an
  adapter and BASIC project index exist.
- Release notes record the capability-honesty behavior.

## Cross-platform

IDE/tooling only; no platform concerns beyond the existing IDE build.

## Risks / open questions

- **Symbol index scope:** definition/rename need a BASIC project index; gauge whether the
  existing bridge can supply ranges or whether a new index is required (the costly part).

## Verification

Completed on 2026-06-20:

```sh
cmake --build build --target test_vbasic_server_lsp test_zia_server_lsp test_zia_server_bridge -j 8
ctest --test-dir build -R '^(test_zia_server_lsp|test_zia_server_bridge|test_vbasic_server_lsp)$' --output-on-failure
```

Post-documentation focused verification is:

```sh
cmake --build build --target test_vbasic_server_lsp test_vbasic_server_bridge zia -j 8
ctest --test-dir build -R '^(test_vbasic_server_lsp|test_vbasic_server_bridge|zia_viperide_phase0_phase1)$' --output-on-failure
```
