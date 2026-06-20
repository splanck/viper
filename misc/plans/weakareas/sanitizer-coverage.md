# Sanitizer Coverage Breadth

**Status:** Verified-ish (ASan/UBSan wired; run only on a namespace-test subset; TSan
build exists but that lane isn't exercised)
**Area:** `scripts/`, `CMakeLists.txt`
**Effort:** S–M
**Roadmap fit:** v0.2.x hardening

## Problem

ASan/UBSan are wired in CMake but the local sanitizer script
(`scripts/ci_sanitizer_tests.sh`) runs them over **only the namespace tests** — a small
slice. TSan is wired and there's a `cmake-build-tsan-g3d/` directory, but the TSan lane
isn't run as part of any regular flow. So large areas (codegen, runtime alloc paths,
game/audio mixing, networking) never see a sanitizer, and the concurrency hardening work
(`CONC-001..010`) has no standing TSan guard.

## Goal & scope

- **In:** Broaden ASan/UBSan to a much larger test subset via the **local** scripts; add
  a runnable **TSan lane** focused on threads/runtime/concurrency; document cadence.
- **Out:** MSan (needs instrumented libc — large effort; note as future); GitHub Actions
  changes (prohibited — these stay local `scripts/` lanes like the existing ones).

## Design

Three local lanes (mirroring the existing `scripts/ci_*.sh` locals):
1. **ASan+UBSan, broad:** run most ctest labels (exclude only the genuinely
   incompatible). These catch memory errors and UB across the codebase.
2. **TSan, targeted:** build the relevant libraries with `-fsanitize=thread` and run the
   threads/runtime/game-audio-mixing tests. TSan can't combine with ASan, hence a
   separate build.
3. Keep the fast default build sanitizer-free.

## Implementation steps

1. Audit `scripts/ci_sanitizer_tests.sh` scope; extend its ctest label set from
   namespace-only to the broad subset (exclude device/display-gated tests).
2. Add `scripts/tsan_tests.sh`: configure a TSan build, run the concurrency-relevant
   labels (`runtime`, threads, the audio mixer, the game scheduler). Reuse the existing
   `cmake-build-tsan-g3d`-style layout but make it script-driven and gitignored.
3. Triage + fix findings — sanitizers surface **real** bugs; budget remediation time.
   (The prior concurrency audit should keep TSan relatively clean, but expect a few.)
4. Record the recommended cadence (e.g. before each release, after concurrency changes).

## Tests

- The sanitizer lanes **are** the tests; success = clean runs (zero ASan/UBSan/TSan
  reports) over their label sets.
- Add a deliberately-faulty unit (behind a disabled flag) to confirm each sanitizer lane
  actually trips on a planted defect — proves the lane is wired, not silently passing.

## Documentation

- Update `docs/testing.md` with the three lanes, what each catches, the cadence, and how
  to reproduce + interpret a finding.
- Cross-reference the concurrency hardening notes so TSan becomes the standing guard for
  that work.

## Cross-platform

ASan/UBSan/TSan are Clang features (canonical). Run on the Unix dev platforms; note that
TSan is unavailable/limited on some Windows toolchains — scope TSan to macOS/Linux and say
so.

## Risks / open questions

- **Real findings will appear** — that's the point; the plan must budget for fixes, not
  just enabling.
- **Build/runtime cost** — keep lanes opt-in and separate from the default build; ASan and
  TSan need distinct builds (can't share).
