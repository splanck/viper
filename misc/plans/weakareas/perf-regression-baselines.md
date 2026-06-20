# Performance Regression Baselines

**Status:** Reframed — perf tests are **not** pure smoke (`vm_switch_bench.cpp` already
asserts a regression ratio); the gap is **baseline-tracked history** and broader coverage.
**Area:** `src/tests/perf/`, `scripts/`
**Effort:** M
**Roadmap fit:** v0.3.x P1 (engine performance)

## Problem

`src/tests/perf/` already does *relative* regression checks — e.g.
`vm_switch_bench.cpp:278` fails if a dispatch ratio regresses, and `native_arm64_bench.cpp`
times codegen. But there is **no baseline tracked across commits**: a gradual, uniform
slowdown that preserves internal ratios slips through, and most hot paths (IL optimization
time, codegen throughput, game frame) aren't measured at all.

## Goal & scope

- **In:** Extend the existing approach to **baseline-tracked** perf: a versioned baseline
  file, a comparator that flags regressions beyond a threshold, and benchmarks covering VM
  dispatch, O0/O1/O2 compile time, codegen throughput, and a game hot-path frame.
- **Out:** Micro-optimizing anything (this is detection infra); hosted CI dashboards
  (local script only, per the CI constraint).

## Design — lean on ratios for portability

Absolute wall-time varies by machine, so prefer **machine-normalized** metrics: measure
each benchmark *and* a fixed calibration loop, and track the ratio (this is why the
existing `vm_switch_bench` ratio check is robust). The baseline stores ratios; the
comparator flags a regression when a ratio worsens beyond a generous threshold (e.g.
>10–15%) to avoid noise.

```
result.json:  { "vm_dispatch": 1.00, "compile_o2_per_kloc": 1.00, "codegen_kloc_s": 1.00, ... }
baseline.json: checked-in reference ratios
compare:       fail if metric / baseline > 1 + threshold (or < 1 - threshold for throughput)
```

## Implementation steps

1. Standardize perf-test output to a structured `result.json` (extend the existing
   benches to emit normalized ratios alongside their current assertions).
2. Add missing benchmarks: O0/O1/O2 compile time on a fixed corpus; codegen throughput
   (KLOC/s) per backend; a representative game frame (scene update + physics + draw call
   counting, headless).
3. `scripts/perf_compare.sh`: run benches → emit `result.json` → diff vs `baseline.json`
   → report deltas, exit non-zero on regression.
4. `scripts/perf_update_baseline.sh`: the single reviewed path to refresh `baseline.json`.

## Tests

- Keep the existing in-test ratio assertions (they stay valuable as fast guards).
- A self-test: feed the comparator a synthetic "10% slower" result and assert it trips;
  feed an in-threshold result and assert it passes.
- The game-frame bench runs headless and deterministically (fixed timestep, seeded RNG).

## Documentation

- Add a "Performance baselines" section to `docs/testing.md`: how to run `perf_compare`,
  interpret deltas, and the **reviewed** baseline-update procedure.
- Document the normalized-ratio rationale so future authors don't add raw-ms gates.

## Cross-platform

Ratio normalization is the cross-platform strategy — it tolerates different host speeds.
Run on all canonical platforms; record per-platform baselines if variance warrants.

## Risks / open questions

- **Noise / thermal throttling** — use medians of repeated runs and generous thresholds;
  the goal is catching real regressions, not 2% jitter.
- **Baseline drift** — only `perf_update_baseline.sh` may change the baseline, and changes
  should be reviewed with the perf delta that motivated them.
