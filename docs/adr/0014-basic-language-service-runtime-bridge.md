# ADR 0014: Viper BASIC Language-Service Runtime Bridge

Date: 2026-06-25

Status: Accepted

## Context

ViperIDE gives Zia files completion, diagnostics, hover, and document symbols via an
in-process runtime bridge: the Zia frontend's IDE engines are exposed as runtime functions
(`Viper.Zia.Completion.*`, `Viper.Zia.Toolchain.*`) registered in `src/il/runtime/runtime.def`,
implemented in `src/frontends/zia/rt_zia_completion.cpp` (linked via `fe_zia`/editor services),
with weak stubs in `viper_runtime` so the runtime library never depends on a frontend. The IDE
(Zia code) consumes them in `viperide/src/editor/{completion,diagnostics,hover,symbols}.zia`.

Viper BASIC has the same intelligence available — `parseAndAnalyzeBasic()` (parse + sema, no
lowering) and `BasicCompletionEngine::complete()` in `src/frontends/basic/` — but no runtime
bridge, so BASIC files are edit/build/run only in the IDE. Phase 4 of the overhaul (plan
`~/.claude/plans/viperide-needs-to-be-golden-blum.md`) adds completion, diagnostics, hover, and
symbols for BASIC. Exposing a new `Viper.Basic.*` runtime surface is a cross-layer contract
(IDE ↔ runtime), covered by the spec-currency gate (ADR 0006).

## Decision

Add a `Viper.Basic.*` runtime surface mirroring the Zia one, **synchronous and in-process**:

- `Viper.Basic.Toolchain.CheckForFile(str,str) -> obj<Seq>` — diagnostics.
- `Viper.Basic.Completion.ItemsForFile(str,str,i64,i64) -> obj<Seq>` — completion items.
- `Viper.Basic.Completion.SymbolsForFile(str,str) -> str` — document symbols.
- `Viper.Basic.Completion.HoverInfoForFile(str,str,i64,i64) -> obj<Map>` — hover.

Each emits the **same record shapes** the IDE already consumes from the Zia bridge (identical Map
keys / the `name\tkind\ttype\tline` symbol string), so the IDE controllers reuse their existing
result-application code and only branch on the active language to pick the namespace.

**Sync, not async.** The Zia bridge runs analysis on a threaded `SemanticJob` because Zia
analysis is heavy. `parseAndAnalyzeBasic()` is cheap, synchronous, error-tolerant, and holds no
global state, so the BASIC surface is sync-only — no job/thread machinery. (The unused
`vbasic-server` external-server scaffolding in `language_service.zia` is not pursued; in-process
matches Zia.)

**No VM / semantic change.** This wraps existing frontend engines. The diagnostic conversion is
frontend-agnostic — both frontends produce `il::support::DiagnosticEngine`, so the BASIC bridge
reuses the same `Diagnostic → Map` mapping. BASIC's `CompletionKind` already matches the IDE's
1:1; `Severity{Note,Warning,Error}` maps to `{2,1,0}`.

Files mirror the Zia split: strong impl `src/frontends/basic/rt_basic_completion.cpp` (in
`fe_basic`, beside `BasicCompletion.cpp`); weak stubs `src/runtime/core/rt_basic_completion_stub.c`
(in `viper_runtime`, returning empty/`unavailable` payloads so a frontend-less binary shows no
false editor warnings); header `src/runtime/graphics/common/rt_basic_completion.h`; `RT_FUNC` +
`RT_CLASS_BEGIN/END` blocks in `runtime.def`. `check_runtime_completeness.sh` must stay clean.

## Consequences

`language_service.zia` flips `basicService` `canComplete/canDiagnose/canHover/canDocumentSymbols`
on; go-to-def / find-refs / rename / signature-help stay off (no project-index / signature engine
for BASIC). Each controller gains a `LANGUAGE_BASIC` branch calling the sync `Viper.Basic.*`
function. Hover is identifier-based (extract the token at the cursor, look it up via the analyzer)
— the same surface as Zia hover; unresolved → `available=false`. The IDE-side `basicService` gate
in `phase0_phase1_probe.zia` is updated and `intellisense_probe.zia` gains BASIC cases. A future
external `vbasic-server` could supersede the in-process bridge without changing the IDE contract.
