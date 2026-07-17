---
status: active
audience: contributors
last-verified: 2026-06-27
---

# ADR 0007: CodeEditor Syntax Surface Expansion Uses Registry-Only Semantics

Date: 2026-06-24

Status: Accepted; implemented and verified against source, runtime registry, and
focused tests on 2026-06-27

## Context

The ZannaIDE syntax-rendering work makes code rendering richer for all three
languages. That work touches the GUI runtime C surface:

- The CodeEditor syntax highlighter exposed only six overridable token types
  (`token_colors[6]`, bounds-checked at `< 6` in both `syn_color` and
  `rt_codeeditor_set_token_color`), with `FUNCTION` hardcoded and operators /
  brackets falling through to the default color
  (`src/runtime/graphics/gui/rt_gui_codeeditor_syntax.c`).
- `Zanna.GUI.CodeEditor.SetLanguage` recognized only `"zia"` and `"basic"`, so
  Zanna (IL) source rendered as plain text.
- The editor had no indent-guide toggle, and `Zanna.GUI.TabBar` had no
  point→index hit-test.

Public GUI APIs are registered through `src/il/runtime/runtime.def` and
classified by `src/il/runtime/RuntimeSurfacePolicy.inc`. Per ADR 0006 and
CLAUDE.md Core Principle 1, runtime C ABI surface changes require ADR coverage
even when they do not change IL instructions or VM execution.

## Decision

Treat the CodeEditor / TabBar changes as **registry-only GUI runtime-surface
additions**, the same class established for Graphics3D in ADR 0004.
Specifically the following are permitted under this note:

- A stable `vg_syntax_token_type` enum (DEFAULT, KEYWORD, TYPE, STRING, COMMENT,
  NUMBER, FUNCTION, OPERATOR, BRACKET, PARAMETER, PROPERTY, CONSTANT, DECORATOR)
  defined in `src/lib/gui/include/vg_ide_widgets_editor.h`, widening
  `token_colors[]` to
  `VG_SYN_TOKEN_COUNT` and the `syn_color` / `SetTokenColor` bound to the same
  count. The first six indices keep their existing meaning, so prior
  `SetTokenColor(0..5, …)` calls are unaffected; `FUNCTION` and the new types
  become overridable. This enum is the single classification contract shared by
  the lexical tokenizers and ADR 0008 semantic-token overlay.
- A `rt_zanna_syntax_cb` tokenizer for Zanna/IL and `"zanna"`/`"il"` mappings in
  `rt_codeeditor_set_language` (no new `runtime.def` entry — `SetLanguage`
  already exists).
- New editor display methods (`SetShowIndentGuides`,
  `GetShowIndentGuides`) and a `Zanna.GUI.TabBar.GetTabIndexAt(x, y)` hit-test,
  added as `RT_FUNC`/`RT_METHOD` pairs for GUI implementation functions. The
  internal `render_whitespace` field remains reserved and is not a public runtime
  API.

These additions:

- add or extend extern names, classes, signatures, and a per-editor color
  contract for GUI implementation functions only;
- do **not** add or reinterpret IL opcodes, IL types, linkage rules, verifier
  rules, VM call/heap/exception behavior, numeric semantics, or native codegen
  lowering;
- are covered by runtime completeness, ABI/surface, and focused IDE probe tests
  (`zannaide/src/probes/syntax_render_probe.zia`).

Compiler intelligence still reaches the runtime only through the existing
strong/weak `rt_zia_*` bridge — the GUI runtime never links the compiler.

## Implementation Status

Verified on 2026-06-27:

- `src/lib/gui/include/vg_ide_widgets_editor.h` defines
  `vg_syntax_token_type` with 13 token slots and `token_colors[VG_SYN_TOKEN_COUNT]`.
- `src/runtime/graphics/gui/rt_gui_codeeditor_syntax.c` implements
  `rt_zanna_syntax_cb`, maps `"zanna"`/`"il"` in
  `rt_codeeditor_set_language`, and bounds token-color overrides by
  `VG_SYN_TOKEN_COUNT`.
- `src/runtime/graphics/gui/rt_gui_codeeditor.c` implements
  `rt_codeeditor_set_show_indent_guides` and
  `rt_codeeditor_get_show_indent_guides`; `src/runtime/graphics/gui/rt_gui_widgets_complex.c`
  implements `rt_tabbar_get_tab_index_at`.
- `src/il/runtime/runtime.def` registers `SetShowIndentGuides`,
  `GetShowIndentGuides`, and `TabBar.GetTabIndexAt`.
- The current built CLI (`build/src/tools/zanna/zanna --dump-runtime-api`)
  exposes those three methods and does not expose `SetRenderWhitespace`.
- Focused checks pass: `test_rt_gui_runtime`, `test_rt_gui_ide`,
  `zia_rt_api_test_zannaide_primitives`, and `zia_zannaide_syntax_render`.

## Consequences

The richer token palette, the Zanna/IL highlighter, indent guides, and the
TabBar hit-test are visible to interpreted and native execution through the same
runtime registry, while IL and VM semantics remain unchanged. Changes that alter
IL, VM, or native codegen semantics still require their own ADR.

## Spec Impact

No IL language semantics changed. The impact is limited to additional GUI
runtime catalog entries, a widened CodeEditor token-color contract, and policy
classification for the new public CodeEditor/TabBar APIs.
