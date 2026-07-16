---
status: active
audience: contributors
last-verified: 2026-06-27
---

# ADR 0008: Semantic Token Overlay Uses Registry-Only Semantics

Date: 2026-06-25

Status: Accepted; implemented and verified against source, runtime registry, and
focused tests on 2026-06-27

## Context

The ViperIDE semantic-token overlay adds compiler-classified ("semantic")
highlighting on top of the lexical highlighter introduced in ADR 0007. The
lexical tokenizers cannot tell a parameter from a local, a field from a free
identifier, or a lowercase type alias from a value; the Zia `Sema` pass can. Two
public runtime surfaces deliver this:

1. **A Zia `Tokens` semantic job.** `Viper.Zia.Completion.BeginTokensForFile`
   starts a background `SemanticJobKind::Tokens` worker
   (`src/frontends/zia/rt_zia_completion.cpp`) that parses + analyzes the source
   and, for each identifier token, resolves its symbol via
   `Sema::findSymbolAtPosition` and emits a `line<TAB>start<TAB>end<TAB>kind`
   row (0-based editor coordinates). `Viper.Zia.SemanticJob.Tokens` returns the
   serialized rows. This reuses the existing async semantic-job pool, the
   strong/weak `rt_zia_*` bridge, and the `symbolsForSource` analysis pattern —
   the worker runs pure compiler code off-thread (no runtime/GC).
2. **A CodeEditor semantic-token overlay.**
   `Viper.GUI.CodeEditor.AddSemanticToken(line, start, end, tokenType)` and
   `ClearSemanticTokens()` store resolved foreground colors on the editor
   (`vg_semantic_token` array) that `highlight_line()` applies on top of the
   lexical colors. The `tokenType` is the same `vg_syntax_token_type` enum ADR
   0007 established, so `SetTokenColor` overrides apply uniformly.

These touch the public runtime registry (`src/il/runtime/runtime.def`) and the
GUI editor C ABI, so per ADR 0006 and CLAUDE.md Core Principle 1 they require
ADR coverage even though IL/VM execution is unchanged.

## Decision

Treat the semantic-token additions as **registry-only runtime-surface
additions**, the class established for Graphics3D (ADR 0004) and the CodeEditor
lexical surface (ADR 0007). Specifically permitted under this note:

- `rt_zia_completion_begin_tokens_for_file` / `rt_zia_semantic_job_tokens`
  registered as `Viper.Zia.Completion.BeginTokensForFile` /
  `Viper.Zia.SemanticJob.Tokens` (frontend extern-C in `zia_editor_services`,
  with weak stubs in `rt_zia_completion_stub.c` for non-frontend builds).
- `rt_codeeditor_add_semantic_token` / `rt_codeeditor_clear_semantic_tokens`
  registered as `Viper.GUI.CodeEditor.AddSemanticToken` / `ClearSemanticTokens`,
  with headless stubs.

These additions:

- add extern names, classes, and signatures for new GUI / frontend
  implementation functions, plus a per-editor overlay store;
- do **not** add or reinterpret IL opcodes, IL types, linkage rules, verifier
  rules, VM call/heap/exception behavior, numeric semantics, or native codegen
  lowering;
- keep the strict layering: compiler intelligence reaches the editor only
  through the async `SemanticJob` (frontend layer); the GUI runtime never links
  the compiler, and the lib layer (`vg_codeeditor`) only stores/applies resolved
  colors;
- are covered by runtime completeness, ABI/surface, and a focused IDE probe
  (`viperide/src/probes/semantic_tokens_probe.zia`).

The overlay is advisory: unresolved identifiers are omitted so the lexical color
stands, and the editor remains fully functional with no semantic tokens (offline
or non-Zia files).

## Implementation Status

Verified on 2026-06-27:

- `src/frontends/zia/rt_zia_completion.cpp` implements `tokensForSource`,
  resolves identifier occurrences with `Sema::findSymbolAtPosition`, emits
  `line<TAB>start<TAB>end<TAB>kind` rows, starts
  `SemanticJobKind::Tokens` through `rt_zia_completion_begin_tokens_for_file`,
  and returns rows through `rt_zia_semantic_job_tokens`.
- `src/runtime/core/rt_zia_completion_stub.c` provides weak no-op stubs for
  non-frontend builds.
- `src/runtime/graphics/gui/rt_gui_codeeditor_syntax.c` implements
  `rt_codeeditor_add_semantic_token` and
  `rt_codeeditor_clear_semantic_tokens`; `src/lib/gui/src/widgets/vg_codeeditor_core.inc`
  sorts and applies semantic-token colors on top of lexical colors.
- `src/il/runtime/runtime.def` registers
  `Viper.Zia.Completion.BeginTokensForFile`, `Viper.Zia.SemanticJob.Tokens`,
  `Viper.GUI.CodeEditor.AddSemanticToken`, and
  `Viper.GUI.CodeEditor.ClearSemanticTokens`.
- The current built CLI (`build/src/tools/viper/viper --dump-runtime-api`)
  exposes those four methods.
- Focused checks pass: `test_rt_gui_runtime`, `test_rt_gui_ide`,
  `zia_rt_api_test_viperide_primitives`, and `zia_viperide_semantic_tokens`.

## Consequences

Parameter/field/type/function classification is visible in interpreted and
native execution through the same runtime registry, while IL and VM semantics
remain unchanged. Changes that alter IL, VM, or native codegen semantics still
require their own ADR.

## Spec Impact

No IL language semantics changed. The impact is limited to additional GUI /
frontend runtime catalog entries and a per-editor semantic-token overlay store.
