# Build Directory Hygiene

**Status:** Verified real (6 build directories, large disk, no documented canonical)
**Area:** repo root, `.gitignore`, `scripts/clean.sh`
**Effort:** S
**Roadmap fit:** v0.2.x hardening / developer ergonomics

## Problem

Six build directories coexist at the repo root: `build/`, `build-release/`,
`build-fuzz/`, `build-fuzz-llvm/`, `cmake-build-debug/` (CLion-generated), and
`cmake-build-tsan-g3d/`. They consume substantial disk, there's no documented canonical
dir, and stale binaries in secondary dirs are a known footgun (cf. the memory note about
verifying the toolchain binary, never trusting a stale fixture build).

## Goal & scope

- **In:** Declare one canonical build dir; make secondary builds (release/fuzz/tsan)
  script-driven into well-known, gitignored locations; ensure `.gitignore` covers all
  variants; provide a clean script; document the layout.
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
2. Update `.gitignore` to cover `build*/`, `cmake-build*/`, `*.profraw/.profdata`, fuzz
   artifacts — verify the patterns actually match all six current dirs.
3. Extend `scripts/clean.sh` to remove all generated build/sanitizer/fuzz/coverage dirs
   (with a clear list of what it will delete; no surprise removals).
4. Document the canonical layout + how to produce each secondary build.
5. **Recommend** to the user which existing dirs are safe to delete to reclaim disk
   (e.g. duplicate `cmake-build-debug/`, stale `cmake-build-tsan-g3d/`) — do not delete
   them as part of this plan.

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

## Risks / open questions

- **Do not auto-delete** — surface a recommendation and let the user act; some dirs may
  hold intentional state.
- **CLion regenerates `cmake-build-*`** — gitignoring is the right fix; don't fight the IDE.
