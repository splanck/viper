# Sanitizer Coverage Breadth

**Status:** Completed — sanitizer entry points are consolidated, documented, and covered
by a CTest self-check.
**Area:** `scripts/`, `CMakeLists.txt`
**Effort:** S–M
**Roadmap fit:** v0.2.x hardening

## Problem

ASan/UBSan/TSan are wired in CMake (`IL_SANITIZE_ADDRESS`,
`IL_SANITIZE_UNDEFINED`, `IL_SANITIZE_THREAD`). The old
`scripts/ci_sanitizer_tests.sh` still covers only the namespace subset, but it is no
longer the whole sanitizer story:

- `scripts/ci_full_sanitizer.sh` already builds ASan and UBSan and runs a broad ctest
  subset, excluding diff/perf/stress tests; `--tsan` runs a focused VM/runtime lane.
- `scripts/g3d_tsan_concurrency_lane.sh` already provides a targeted graphics3d
  concurrency TSan lane.

The real weakness is that these lanes are split, under-documented, and easy to miss.
The plan is to make the broad lane the canonical entry point and either retire or
redirect the namespace-only script.

## Goal & scope

- **In:** Consolidate sanitizer scripts around the existing broad lanes, document the
  supported ASan/UBSan/TSan entry points, tune exclusions/labels, and add self-checks.
- **Out:** MSan (needs instrumented libc — large effort; note as future); GitHub Actions
  changes (prohibited — these stay local `scripts/` lanes like the existing ones).

## Design

Canonical local lanes:

1. **ASan+UBSan broad:** `scripts/ci_full_sanitizer.sh` remains the main entry point and
   owns the broad exclusion list.
2. **TSan VM/runtime:** `scripts/ci_full_sanitizer.sh --tsan` remains the generic
   concurrency lane.
3. **TSan graphics3d:** `scripts/g3d_tsan_concurrency_lane.sh` stays as a specialized
   high-signal lane for graphics3d concurrency.
4. Keep the fast default build sanitizer-free.

## Implementation steps

1. Update `scripts/ci_sanitizer_tests.sh` to delegate to `ci_full_sanitizer.sh` or rename
   it as a compatibility wrapper so namespace-only coverage is not mistaken for the
   sanitizer lane.
2. Audit `ci_full_sanitizer.sh` exclusions and labels; keep diff/perf/stress out, but
   ensure runtime alloc paths, parsers, networking, audio, and bytecode tests are covered
   where sanitizer-compatible.
3. Document `ci_full_sanitizer.sh --tsan` and `g3d_tsan_concurrency_lane.sh` together,
   including expected build directories and common false-positive triage.
4. Triage + fix findings — sanitizers surface **real** bugs; budget remediation time.
5. Record the recommended cadence (e.g. before each release, after concurrency changes).

## Tests

- The sanitizer lanes **are** the tests; success = clean runs (zero ASan/UBSan/TSan
  reports) over their label sets.
- Add a tiny disabled/self-check mode to the scripts, or a documented local recipe, that
  proves ASan/UBSan/TSan instrumentation is active without checking a crashing test into
  the normal suite.

## Documentation

- Update `docs/testing.md` with the lanes, what each catches, the cadence, and how
  to reproduce + interpret a finding.
- Cross-reference the concurrency hardening notes so TSan becomes the standing guard for
  that work.

## Cross-platform

ASan/UBSan/TSan are Clang features (canonical). Run on the Unix dev platforms; note that
TSan is unavailable/limited on some Windows toolchains — scope TSan to macOS/Linux and say
so.

## Implementation notes

- `scripts/ci_sanitizer_tests.sh` is now a compatibility wrapper that delegates to the
  canonical `scripts/ci_full_sanitizer.sh` lane.
- `scripts/ci_full_sanitizer.sh --self-test` validates ASan/UBSan toolchain support and
  optionally TSan support when requested.
- `sanitizer_script_self_test` is registered in CTest.
- `docs/testing.md` documents ASan/UBSan, TSan VM/runtime, and graphics3d TSan lanes.

## Verification

- `./scripts/ci_full_sanitizer.sh --self-test`
- `ctest --test-dir build -R '^sanitizer_script_self_test$' --output-on-failure`

## Risks / open questions

- **Real findings will appear** — that's the point; the plan must budget for fixes, not
  just enabling.
- **Script drift** — keeping both a legacy namespace script and the full sanitizer script
  invites confusion; prefer one canonical wrapper.
- **Build/runtime cost** — keep lanes opt-in and separate from the default build; ASan and
  TSan need distinct builds (can't share).
