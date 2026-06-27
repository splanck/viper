# ADR 0007: CodeEditor Syntax Surface Expansion Uses Registry-Only Semantics

Date: 2026-06-24

Status: Accepted

## Context

Phase 2 of the ViperIDE overhaul (plan
`~/.claude/plans/viperide-needs-to-be-sharded-puppy.md`) makes code rendering
richer for all three languages. That work touches the GUI runtime C surface:

- The CodeEditor syntax highlighter exposed only six overridable token types
  (`token_colors[6]`, bounds-checked at `< 6` in both `syn_color` and
  `rt_codeeditor_set_token_color`), with `FUNCTION` hardcoded and operators /
  brackets falling through to the default color
  (`src/runtime/graphics/gui/rt_gui_codeeditor_syntax.c`).
- `Viper.GUI.CodeEditor.SetLanguage` recognized only `"zia"` and `"basic"`, so
  Viper (IL) source rendered as plain text.
- The editor had no indent-guide toggle, and `Viper.GUI.TabBar` had no
  point→index hit-test.

Public GUI APIs are registered through `src/il/runtime/runtime.def` and
classified by `src/il/runtime/RuntimeSurfacePolicy.inc`. Per the spec-currency
gate (ADR 0006) and CLAUDE.md Core Principle 1, runtime C ABI surface changes
require ADR coverage even when they do not change IL instructions or VM
execution.

## Decision

Treat the Phase 2 CodeEditor / TabBar changes as **registry-only GUI
runtime-surface additions**, the same class established for Graphics3D in
ADR 0004. Specifically the following are permitted under this note:

- A stable `vg_syntax_token_type` enum (DEFAULT, KEYWORD, TYPE, STRING, COMMENT,
  NUMBER, FUNCTION, OPERATOR, BRACKET, PARAMETER, PROPERTY, CONSTANT, DECORATOR)
  defined in `vg_ide_widgets_editor.h`, widening `token_colors[]` to
  `VG_SYN_TOKEN_COUNT` and the `syn_color` / `SetTokenColor` bound to the same
  count. The first six indices keep their existing meaning, so prior
  `SetTokenColor(0..5, …)` calls are unaffected; `FUNCTION` and the new types
  become overridable. This enum is the single classification contract shared by
  the lexical tokenizers now and the Phase 5 semantic-token overlay later.
- A `rt_viper_syntax_cb` tokenizer for Viper/IL and `"viper"`/`"il"` mappings in
  `rt_codeeditor_set_language` (no new `runtime.def` entry — `SetLanguage`
  already exists).
- New editor display flags + methods (`SetShowIndentGuides`, and a reserved
  `SetRenderWhitespace`) and a `Viper.GUI.TabBar.GetTabIndexAt(x, y)` hit-test,
  added as `RT_FUNC`/`RT_METHOD` pairs for new GUI implementation functions.

These additions:

- add or extend extern names, classes, signatures, and a per-editor color
  contract for GUI implementation functions only;
- do **not** add or reinterpret IL opcodes, IL types, linkage rules, verifier
  rules, VM call/heap/exception behavior, numeric semantics, or native codegen
  lowering;
- are covered by runtime completeness, ABI/surface, and focused IDE probe tests
  (`viperide/src/probes/syntax_render_probe.zia`).

Compiler intelligence still reaches the runtime only through the existing
strong/weak `rt_zia_*` bridge — the GUI runtime never links the compiler.

## Consequences

Phase 2 closes its ADR gate by pointing to this note, the runtime completeness
check, and the syntax probe. The richer token palette, the Viper/IL highlighter,
indent guides, and the TabBar hit-test are visible to interpreted and native
execution through the same runtime registry, while IL and VM semantics remain
unchanged. Any later change that alters IL, VM, or native codegen semantics
still requires its own ADR.

## Spec Impact

No IL language semantics changed. The impact is limited to additional GUI
runtime catalog entries, a widened CodeEditor token-color contract, and policy
classification for the new public CodeEditor/TabBar APIs.
