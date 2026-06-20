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

## Plans (verified-real)

| # | Plan | Area | Effort | Status |
|---|------|------|--------|--------|
| 1 | [audio-effects-dsp.md](audio-effects-dsp.md) | Runtime/audio (+ move audio out of graphics) | M | Verified |
| 2 | [typed-numeric-buffers.md](typed-numeric-buffers.md) | Runtime/collections | M | Reframed (real gap) |
| 3 | [system-signals.md](system-signals.md) | Runtime/system | S | Verified |
| 4 | [localization-timezones.md](localization-timezones.md) | Runtime/localization | M | Reframed (narrow gap) |
| 5 | [runtime-caps-configurable.md](runtime-caps-configurable.md) | Runtime/game | S–M | Verified (scope carefully) |
| 6 | [aarch64-test-parity.md](aarch64-test-parity.md) | Codegen | M | Reframed (org, not absence) |
| 7 | [bytecode-vm-parity.md](bytecode-vm-parity.md) | Bytecode VM | M | Verified |
| 8 | [zia-constrained-generics.md](zia-constrained-generics.md) | Frontend/Zia | M–L | Doc-sourced (deferred feature) |
| 9 | [lsp-feature-parity.md](lsp-feature-parity.md) | Tooling/LSP | M | Verified |
| 10 | [basic-ide-services.md](basic-ide-services.md) | Tooling/IDE | M | Verified (by-design decision) |
| 11 | [code-coverage.md](code-coverage.md) | Test infra | S | Verified |
| 12 | [fuzzing-and-corpora.md](fuzzing-and-corpora.md) | Test infra | M | Verified |
| 13 | [perf-regression-baselines.md](perf-regression-baselines.md) | Test infra | M | Reframed (extend existing) |
| 14 | [sanitizer-coverage.md](sanitizer-coverage.md) | Test infra | S–M | Verified |
| 15 | [il-docs-consolidation.md](il-docs-consolidation.md) | Docs | S | Verified |
| 16 | [spec-currency-and-adr.md](spec-currency-and-adr.md) | Docs/process | S | Verified |
| 17 | [build-dir-hygiene.md](build-dir-hygiene.md) | Repo hygiene | S | Verified |
| 18 | [example-build-smoke.md](example-build-smoke.md) | Test infra | S | Verified |

## Retracted on verification (NOT work items)

| Original claim | Why retracted |
|---|---|
| "No Environment/env-var API" | `Viper.System.Environment.GetVariable/SetVariable/HasVariable` already exist (impl in `core/rt_args.c`). |
| "No file/dir watch" | `Viper.IO.Watcher` + `Viper.Workspace.WorkspaceWatcher` already exist. |
| "No monotonic/perf timer" | `Viper.Time.Stopwatch` (Ms/Us/Ns) already exists. |
| "Arrays: no slices/generics" | `arrays/` is the BASIC `DIM` primitive; `Collections.List`/`Seq` already have `Slice`/`Sort`/etc. (Real gap reframed → plan #2.) |
| "AArch64: zero codegen tests" | ~15+ ARM unit tests + e2e + differential exist; only the directory organization is thin (→ plan #6). |
| "Coalescer only on ARM" | Both backends have one (`x86_64/ra/Coalescer.cpp`, `aarch64/Coalescer.cpp`). |
| "No VM-vs-native differential test" | Exists for **both** backends. |
| "No spatial/3D audio" | `graphics/3d/audio/rt_sound3d.c` exists (the *effects/DSP* gap is real → plan #1). |
| "TUI is orphaned dead code" | Consumed by the REPL line editor + shared text buffer. |
| "Bytecode VM is experimental/unreachable" | Wired into `cmd_run`/`cmd_bench`/`vm_executor`/REPL (test gap is real → plan #7). |
| "Localization is really thin" | 130 runtime functions incl. script detection; only **IANA timezones** are missing (→ plan #4). |
| "ViperIDE debugger is a non-executing placeholder" | `debug_session.zia` spawns `viper run --debug-adapter` and speaks the real `@@VDBG@@` protocol. Already wired. |
| "BASIC diagnostics trail Zia" | BASIC has *more* diagnostic code (645 vs 340 LOC); not substantiated. |
| "Perf tests are pure smoke" | `vm_switch_bench.cpp` already asserts a regression ratio (gap is *baseline tracking* → plan #13). |

## Suggested order

1. **Cheap visibility/correctness:** 11 (coverage), 15–17 (docs/hygiene), 16 (spec currency), 3 (signals).
2. **Genuine depth:** 1 (audio DSP), 2 (numeric buffers), 9 (LSP), 12–14 (test infra), 6–7 (codegen tests).
3. **Larger/feature:** 8 (constrained generics), 10 (BASIC IDE decision), 4 (timezones), 5 (caps).
