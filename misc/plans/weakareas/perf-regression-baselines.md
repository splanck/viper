# Performance Regression Baselines

**Status:** Completed — the benchmark baseline lane has a reproducible Viper-only mode,
comparison self-test coverage, and documented cadence.
**Area:** `src/tests/perf/`, `scripts/`
**Effort:** S–M
**Roadmap fit:** v0.3.x P1 (engine performance)

## Problem

`src/tests/perf/` already does relative regression checks — e.g. `vm_switch_bench.cpp`
fails if dispatch ratio regresses, and `native_arm64_bench.cpp` times codegen.
`scripts/benchmark.sh` also writes JSONL results/baselines under `misc/benchmarks/`, and
`scripts/benchmark_compare.sh` compares latest results against the baseline and exits
non-zero on Viper-mode regressions over 5%.

The remaining weakness is that the benchmark lane is not the documented, reproducible
performance gate: baselines are machine-sensitive, external-language reference modes are
optional, and hot paths such as IL optimization/codegen throughput/game-frame probes need
a clearer curated set.

## Goal & scope

- **In:** Improve the existing `benchmark.sh` / `benchmark_compare.sh` baseline flow:
  make the curated Viper-only lane easy to reproduce, document baseline refresh, and add
  missing high-signal benchmarks for compile/codegen/game hot paths.
- **Out:** Micro-optimizing anything (this is detection infra); hosted CI dashboards
  (local script only, per the CI constraint).

## Design — lean on existing JSONL, add normalization where needed

Keep `misc/benchmarks/results.jsonl` and `baseline.jsonl` as the storage format. Add a
curated Viper-only mode to avoid failures caused by missing Rust/Lua/Java/.NET toolchains
when the goal is Viper regression detection. Absolute wall-time varies by machine, so add
machine-normalized ratios for metrics that need cross-host comparison; keep raw medians
for local before/after work.

```
results.jsonl:  { "metric": "vm_dispatch", "median": 1.00, "normalized": 1.00, ... }
baseline.jsonl: checked-in reference medians/normalized ratios
compare:        fail if normalized metric regresses beyond threshold
```

## Implementation steps

1. Add `scripts/benchmark.sh --viper-only` (or equivalent option set) for the canonical
   regression lane: bytecode VM, native backends available on the host, and Viper source
   benchmarks only.
2. Teach `benchmark_compare.sh` to compare only common modes/programs by default and to
   report missing baseline entries clearly.
3. Add missing benchmarks: O0/O1/O2 compile time on a fixed corpus, codegen throughput
   (KLOC/s) per backend, and a representative headless game-frame/update probe.
4. Add a reviewed baseline refresh command or documented wrapper around
   `benchmark.sh --set-baseline` that records host/platform metadata and prints the delta
   that justified the refresh.

## Tests

- Keep the existing in-test ratio assertions (they stay valuable as fast guards).
- A self-test: feed `benchmark_compare.sh` a synthetic "10% slower" result and assert it trips;
  feed an in-threshold result and assert it passes.
- The game-frame bench runs headless and deterministically (fixed timestep, seeded RNG).

## Documentation

- Add a "Performance baselines" section to `docs/testing.md`: how to run
  `benchmark.sh`, `benchmark_compare.sh`, the Viper-only lane, and the **reviewed**
  baseline-update procedure.
- Document the normalized-ratio rationale so future authors don't add raw-ms gates.

## Cross-platform

Ratio normalization is the cross-platform strategy — it tolerates different host speeds.
Run on all canonical platforms; record per-platform baselines if variance warrants.

## Implementation notes

- `scripts/benchmark.sh` includes a Viper-only regression lane and continues to record
  JSONL results/baselines with host metadata.
- `scripts/benchmark_compare.sh` compares common entries, reports missing baselines
  clearly, and has `--self-test` coverage for pass/fail regression thresholds.
- `benchmark_compare_self_test` is registered in CTest under the `perf`/`tools` labels.
- `docs/testing.md` documents running benchmarks, comparing results, and reviewing
  baseline refreshes.

## Verification

- `./scripts/benchmark_compare.sh --self-test`
- `ctest --test-dir build -R '^benchmark_compare_self_test$' --output-on-failure`

## Risks / open questions

- **Noise / thermal throttling** — use medians of repeated runs and generous thresholds;
  the goal is catching real regressions, not 2% jitter.
- **Baseline drift** — only `perf_update_baseline.sh` may change the baseline, and changes
  should be reviewed with the perf delta that motivated them.
