# Viper Weak-Area Remediation Plans

This directory holds detailed, technically-accurate plans for resolving the weak
spots identified in the June 2026 state-of-project review. Each plan is standalone
and executable on its own.

**Provenance note.** The original review fanned out across the codebase and several
"weak spots" turned out, on direct verification against source, to be **already
implemented or mis-scoped**. Only verified-real or accurately-reframed issues get a
plan here. The retractions are recorded below so the accounting is complete — they
are *not* work items.

## Shared conventions (read once)

- **Runtime API additions** are registered in `src/il/runtime/runtime.def`:
  - `RT_FUNC(Id, c_symbol, "Viper.Ns.Name", "ret(args)")` — a free function.
  - `RT_CLASS_BEGIN("Viper.Ns.Type", Id, "obj", CtorId)` … `RT_METHOD("Name","sig",Id)` /
    `RT_PROP("Name","type",GetterId,SetterId)` … `RT_CLASS_END`.
  - Static-only classes use `"none"` instance type and `none` ctor.
  - Every `RT_METHOD`/`RT_PROP` handler must also have a matching `RT_FUNC`.
  - After any change: **`./scripts/check_runtime_completeness.sh`**.
  - Because dispatch goes through the registry, the `RT_FUNC` line *is* the VM/codegen
    wiring — there is no separate per-function VM plumbing.
- **CI constraint:** Do **not** add/modify `.github/workflows/*`. Test-infra plans use
  CMake options + `scripts/` lanes (mirroring the existing `scripts/ci_*.sh` locals).
- **Build/test:** `./scripts/build_viper_mac.sh` (full) or the fast-iteration env vars
  for inner loops; finish with a clean full `ctest --test-dir build`.
- **Headers:** every new `.c/.h/.cpp/.hpp` gets the full Viper file header.
- **Effort** is rated S/M/L **relative to solo AI-assisted velocity**, not team-weeks.

## Plans (verified-real or accurately reframed)

| # | Plan | Area | Effort | Status |
|---|------|------|--------|--------|
| 1 | [audio-effects-dsp.md](audio-effects-dsp.md) | Runtime/audio (+ decouple spatial audio from graphics) | M | Completed |
| 2 | [typed-numeric-buffers.md](typed-numeric-buffers.md) | Runtime/collections | M | Completed |
| 3 | [system-signals.md](system-signals.md) | VM/runtime system | S | Completed |
| 4 | [localization-timezones.md](localization-timezones.md) | Runtime/time + localization data | M | Completed |
| 5 | [runtime-caps-configurable.md](runtime-caps-configurable.md) | Runtime/game | M | Completed |
| 6 | [aarch64-test-parity.md](aarch64-test-parity.md) | Codegen | M | Completed |
| 7 | [bytecode-vm-parity.md](bytecode-vm-parity.md) | Bytecode VM | M | Completed |
| 8 | [zia-constrained-generics.md](zia-constrained-generics.md) | Frontend/Zia | M | Completed |
| 9 | [lsp-feature-parity.md](lsp-feature-parity.md) | Tooling/LSP | M | Completed |
| 10 | [basic-ide-services.md](basic-ide-services.md) | Tooling/IDE | M | Completed |
| 11 | [code-coverage.md](code-coverage.md) | Test infra | S | Completed |
| 12 | [fuzzing-and-corpora.md](fuzzing-and-corpora.md) | Test infra | M | Completed |
| 13 | [perf-regression-baselines.md](perf-regression-baselines.md) | Test infra | S-M | Completed |
| 14 | [sanitizer-coverage.md](sanitizer-coverage.md) | Test infra | S | Completed |
| 15 | [il-docs-consolidation.md](il-docs-consolidation.md) | Docs | S | Completed |
| 16 | [spec-currency-and-adr.md](spec-currency-and-adr.md) | Docs/process | S | Completed |
| 17 | [build-dir-hygiene.md](build-dir-hygiene.md) | Repo hygiene | S | Completed |
| 18 | [example-build-smoke.md](example-build-smoke.md) | Test infra | S | Completed |

## Retracted on verification (NOT work items)

| Original claim | Why retracted |
|---|---|
| "No Environment/env-var API" | `Viper.System.Environment.GetVariable/SetVariable/HasVariable` already exist (impl in `core/rt_args.c`). |
| "No file/dir watch" | `Viper.IO.Watcher` + `Viper.Workspace.WorkspaceWatcher` already exist. |
| "No monotonic/perf timer" | `Viper.Time.Stopwatch` (Ms/Us/Ns) already exists. |
| "Arrays: no slices/generics" | `arrays/` is the BASIC `DIM` primitive; `Collections.List`/`Seq` already have `Slice`/`Sort`/etc. (Real gap reframed → plan #2.) |
| "AArch64: zero codegen tests" | 90+ AArch64-specific unit/codegen tests, e2e/native probes, and differential tests exist; the remaining gap is measurable parity/organization (→ plan #6). |
| "Coalescer only on ARM" | Both backends have one (`x86_64/ra/Coalescer.cpp`, `aarch64/Coalescer.cpp`). |
| "No VM-vs-native differential test" | Exists for **both** backends. |
| "No spatial/3D audio" | `graphics/3d/audio/rt_sound3d.c` exists (the *effects/DSP* gap is real → plan #1). |
| "TUI is orphaned dead code" | Consumed by the REPL line editor + shared text buffer. |
| "Bytecode VM is experimental/unreachable" | Wired into `cmd_run`/`cmd_bench`/`vm_executor`/REPL, with scalar IL-vs-bytecode parity already present; the remaining gap is broader observable parity (→ plan #7). |
| "Localization is really thin" | 130 runtime functions incl. script detection; only **IANA timezones** are missing (→ plan #4). |
| "ViperIDE debugger is a non-executing placeholder" | `debug_session.zia` spawns `viper run --debug-adapter` and speaks the real `@@VDBG@@` protocol. Already wired. |
| "BASIC diagnostics trail Zia" | Not substantiated. `vbasic-server` already exposes diagnostics, completion, hover, symbols, and MCP/LSP tests; ViperIDE simply keeps BASIC semantic flags off (→ plan #10). |
| "Perf tests are pure smoke" | `vm_switch_bench.cpp` already asserts a regression ratio and `scripts/benchmark*.sh` already track JSONL baselines/results; the gap is reproducibility and documented cadence (→ plan #13). |
| "No Ctrl-C interrupt support" | The VM already handles SIGINT/console Ctrl-C and exposes `VM::requestInterrupt`; the remaining gap is a script-visible graceful-shutdown polling API (→ plan #3). |

## Suggested order

1. **Cheap visibility/correctness:** 11 (coverage), 14 (sanitizer consolidation), 15–17 (docs/hygiene/spec currency), 3 (script-visible shutdown).
2. **Reuse existing infrastructure:** 9 (LSP over `ProjectIndex`), 10 (BASIC IDE over `vbasic-server`), 13 (benchmark cadence), 18 (example smoke manifest).
3. **Runtime depth:** 1 (audio DSP/decoupling), 2 (numeric buffers), 4 (timezones), 5 (growable caps), 12 (fuzz corpora).
4. **Cross-engine confidence:** 6 (AArch64/x86 parity organization), 7 (bytecode parity breadth), 8 (constrained-generics hardening).
