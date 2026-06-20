# Example / Demo Build-Smoke Automation

**Status:** Completed — examples are classified by a checked-in smoke manifest with
manifest-audit and fast-smoke CTest coverage.
**Area:** `examples/`, `src/tests/`, `scripts/`
**Effort:** S
**Roadmap fit:** v0.2.x hardening

## Problem

`examples/` holds substantial programs (games, apps, IL/BASIC/Zia samples). The repo
already has `build_demos*.sh` scripts, IL/BASIC example tests, many app/game smoke
probes, and arm64 native demo tests. The gap is not "nothing gates examples"; it is that
there is no single manifest showing which examples are compile-only, VM-run, native-run,
graphical, slow, or intentionally excluded. New examples can still be missed.

## Goal & scope

- **In:** A manifest-driven example smoke layer that reuses existing tests and
  `build_demos*.sh`, compiles every eligible example/demo via `viper`, and runs headless
  probes where feasible. Wire it into `ctest` under an `examples` label.
- **Out:** Full functional testing of each demo; graphical/interactive runs (gate those
  behind labels or skip on headless). Hosted CI (local ctest/script, per the CI constraint).

## Design

A checked-in manifest classifies examples by expected lane: compile-only, VM-run,
native-build, native-run, graphical/display-required, slow, or excluded-with-reason. A
smoke driver reads the manifest, calls the freshly built `viper`, and reuses existing
probe scripts where they already exist. Long or graphical demos are labelled
`slow`/`requires_display` so the default run stays fast.

## Implementation steps

1. Create an example-smoke manifest by auditing `examples/` and current `src/tests/e2e`
   probes. Each entry has path, language/project kind, lane, labels, and exclusion reason
   if any.
2. Add a ctest driver (or wrap `scripts/build_demos.sh --skip-run` where appropriate)
   that compiles each eligible entry via `viper check`/`viper build`; assert exit 0.
3. For headless-runnable demos, add or link a run probe asserting expected stdout (reuse the
   `test_*_native_*.sh` / `*.cmake` patterns already in `src/tests/e2e/`).
4. Add a manifest-audit test: every `.zia`, `.bas`, and project under `examples/` is
   either listed or intentionally excluded.
5. Label appropriately (`examples`, plus `slow` for heavy ones); keep graphical demos out
   of the default run.
6. **Fix any examples that are already broken** — surfacing those is the immediate payoff.

## Tests

- The smoke driver **is** the test; it must be green over the current `examples/` set
  (after fixing any pre-existing breakage).
- Negative self-check: a deliberately-broken throwaway example makes the driver fail
  (proves the gate works), then remove it.

## Documentation

- Add an `examples`-label section to `docs/testing.md` (what it covers, how to run, how to
  add a demo to the smoke).
- Note in `examples/README.md` that examples are build-smoked locally so contributors keep
  them compiling.

## Cross-platform

Compile-only smoke runs everywhere. Gate native-run and graphical probes by platform/host
capability exactly as the existing e2e probes do (Windows dialog suppression, headless
display handling).

## Implementation notes

- `examples/smoke_manifest.tsv` classifies checked-in examples by lane, labels, and
  exclusion reason where needed.
- `scripts/example_smoke.sh` provides `--audit`, `--fast`, and `--all` modes over the
  freshly built `viper`.
- `example_smoke_manifest_audit` and `example_smoke_fast` are registered in CTest with
  the `examples` label.
- `docs/testing.md` and `examples/README.md` document how contributors add or classify
  examples.

## Verification

- `ctest --test-dir build -R '^(example_smoke_manifest_audit|example_smoke_fast)$' --output-on-failure`

## Risks / open questions

- **Build-time cost** — compiling all examples adds time; keep heavy demos in a `slow`
  label out of the default `ctest` run, run them in a broader lane.
- **Graphical demos** can't assert output headlessly — limit them to compile-only in the
  gate.
