# Example / Demo Build-Smoke Automation

**Status:** Verified real (demos have build scripts but no test-suite gate)
**Area:** `examples/`, `src/tests/`, `scripts/`
**Effort:** S
**Roadmap fit:** v0.2.x hardening

## Problem

`examples/` holds substantial programs (games, apps, IL/BASIC/Zia samples) and there are
`build_demos*.sh` scripts to compile them, but **nothing in the test suite gates on them**
— a grep for `examples/` in `src/tests/` finds only incidental references, not an
example-building test. So an example can bit-rot (break against a language/runtime change)
and no test fails. A few e2e native-run probes exist (e.g. `test_xenoscape_native_start.sh`,
`test_zia_native_run.cmake`) but they cover only a handful of demos.

## Goal & scope

- **In:** An automated build-smoke that compiles every example/demo via `viper` and, where
  feasible, runs a headless probe — wired into `ctest` under an `examples` label so a
  broken example fails the suite.
- **Out:** Full functional testing of each demo; graphical/interactive runs (gate those
  behind labels or skip on headless). Hosted CI (local ctest/script, per the CI constraint).

## Design

A smoke **driver** enumerates `examples/` programs (by extension/manifest), compiles each
with the freshly-built `viper`, and asserts success. Runnable, non-graphical demos get a
headless run probe reusing the existing native-run patterns in `src/tests/e2e/`. Long or
graphical demos are labelled `slow`/skipped-on-headless so the default run stays fast.

## Implementation steps

1. Enumerate `examples/` programs and classify: compile-only vs headless-runnable vs
   graphical/interactive.
2. Add a ctest driver (or extend `scripts/build_demos.sh` and wrap it in ctest) that
   compiles each via `viper check`/`viper build`; assert exit 0.
3. For headless-runnable demos, add a run probe asserting expected stdout (reuse the
   `test_*_native_*.sh` / `*.cmake` patterns already in `src/tests/e2e/`).
4. Label appropriately (`examples`, plus `slow` for heavy ones); keep graphical demos out
   of the default run.
5. **Fix any examples that are already broken** — surfacing those is the immediate payoff.

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

## Risks / open questions

- **Build-time cost** — compiling all examples adds time; keep heavy demos in a `slow`
  label out of the default `ctest` run, run them in a broader lane.
- **Graphical demos** can't assert output headlessly — limit them to compile-only in the
  gate.
