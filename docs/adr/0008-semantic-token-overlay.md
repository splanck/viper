# ADR 0008: Semantic Token Overlay Uses Registry-Only Semantics

Date: 2026-06-25

Status: Accepted

## Context

Phase 5 of the ViperIDE overhaul (plan
`~/.claude/plans/viperide-needs-to-be-sharded-puppy.md`) adds compiler-classified
("semantic") highlighting on top of the lexical highlighter introduced in
Phase 2 (ADR 0007). The lexical tokenizers cannot tell a parameter from a local,
a field from a free identifier, or a lowercase type alias from a value; the Zia
`Sema` pass can. Two new public runtime surfaces deliver this:

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
GUI editor C ABI, so per the spec-currency gate (ADR 0006) and CLAUDE.md Core
Principle 1 they require ADR coverage even though IL/VM execution is unchanged.

## Decision

Treat the Phase 5 additions as **registry-only runtime-surface additions**, the
class established for Graphics3D (ADR 0004) and the CodeEditor lexical surface
(ADR 0007). Specifically permitted under this note:

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

## Consequences

Phase 5 closes its ADR gate via this note, the runtime completeness check, and
the semantic-token probe. Parameter/field/type/function classification becomes
visible in interpreted and native execution through the same runtime registry,
while IL and VM semantics remain unchanged. Any later change that alters IL, VM,
or native codegen semantics still requires its own ADR.

## Spec Impact

No IL language semantics changed. The impact is limited to additional GUI /
frontend runtime catalog entries and a per-editor semantic-token overlay store.
