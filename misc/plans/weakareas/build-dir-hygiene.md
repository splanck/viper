# Build Directory Hygiene

**Status:** Completed — canonical and secondary build directories are documented and
cleaned consistently across Unix and Windows helpers.
**Area:** repo root, `.gitignore`, `scripts/clean.sh`, `scripts/clean.ps1`
**Effort:** S
**Roadmap fit:** v0.2.x hardening / developer ergonomics

## Problem

The current checkout is clean at the root: only `build/` is present. `.gitignore` already
covers `/build/`, `/build*/`, `cmake-build*/`, `coverage/`, `*.profraw`, and
`*.profdata`, and `scripts/clean.sh` safely prompts before removing root `build*`
directories.

The remaining weakness is smaller: secondary build dirs used by sanitizer/fuzz/coverage
lanes and IDEs are not documented as a layout, and `scripts/clean.ps1` does not appear to
cover every `cmake-build*` variant that the Unix cleaner/gitignore cover. Stale binaries
remain a footgun when contributors switch between `build/`, IDE builds, and specialized
lanes.

## Goal & scope

- **In:** Document the canonical build dir and script-owned secondary dirs; keep Unix and
  Windows cleaners in sync for `build*`, `cmake-build*`, sanitizer/fuzz/coverage outputs;
  verify `.gitignore` continues to cover generated dirs.
- **Out:** Deleting the user's existing directories automatically (they may hold wanted
  state — **recommend, don't auto-destroy**; the user runs the cleanup).

## Design

- **Canonical:** `build/` (Debug) for day-to-day, configured by the build scripts.
- **Secondary on demand:** `build-release/` (release), and sanitizer/fuzz builds owned by
  their respective `scripts/` lanes (see `sanitizer-coverage.md`, `fuzzing-and-corpora.md`)
  into gitignored dirs.
- **Optional:** evaluate `Ninja Multi-Config` to collapse Debug/Release into one dir with
  multiple configs, reducing duplicate trees.
- **IDE dirs:** `cmake-build-*` (CLion) are IDE-generated — gitignore them; they need not
  be a project-blessed location.

## Implementation steps

1. Audit which dirs are genuinely needed vs incidental; confirm none are tracked in git
   (`git ls-files | grep -E '^(build|cmake-build)'` should be empty).
2. Verify `.gitignore` covers `build*/`, `cmake-build*/`, sanitizer/fuzz/coverage dirs,
   `*.profraw`, and `*.profdata`; update only for real misses.
3. Extend `scripts/clean.sh` and `scripts/clean.ps1` to remove all generated
   build/sanitizer/fuzz/coverage dirs (with a clear list of what they will delete; no
   surprise removals).
4. Document the canonical layout + how to produce each secondary build.
5. If a contributor has secondary dirs locally, recommend which are safe to delete to
   reclaim disk — do not delete them automatically.

## Tests

- Smoke: `scripts/clean.sh` followed by `./scripts/build_viper_mac.sh` (or the Unix
  script) succeeds from a clean tree — proves no build step depended on a stray dir.
- `.gitignore` check: after a full build of every variant, `git status` shows no
  build artifacts as untracked/tracked.

## Documentation

- Add a "Build directory layout" section to the build/getting-started docs: the canonical
  `build/`, the on-demand secondaries, and how each is produced.
- Note the stale-binary footgun and that the canonical dir + `which viper` are the
  trustworthy targets.

## Cross-platform

`.gitignore` + `clean.sh`/`clean.ps1` already exist for both Unix and Windows; keep them
in sync. The canonical-dir convention applies on all platforms.

## Implementation notes

- `docs/getting-started.md` now documents the canonical `build/` directory plus
  script-owned `build-coverage/`, `build-fuzz/`, sanitizer build directories,
  `cmake-build-*`, and `coverage/`.
- `scripts/clean.sh` and `scripts/clean.ps1` now remove `build*`, `cmake-build*`, and
  `coverage` directories through the existing explicit cleanup flow.
- The checkout contains no tracked build, `cmake-build`, or coverage directories.

## Verification

- `git ls-files | rg '^(build|cmake-build|coverage)'`
- Full build verification is covered by the repository build script.

## Risks / open questions

- **Do not auto-delete** — surface a recommendation and let the user act; some dirs may
  hold intentional state.
- **CLion regenerates `cmake-build-*`** — gitignoring is the right fix; don't fight the IDE.
