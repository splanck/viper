# Migration Plan: Viper → Zanna

**Status:** Draft — awaiting decision sign-off (§2)
**Scope:** Full repository rename. One standalone change on a clean tree.
**Baseline:** Tree clean as of 2026-07-17; all prior work committed.

---

## 1. Measured scope (surveyed 2026-07-17)

| Surface | Size |
|---|---|
| Files whose *content* mentions viper (any case) | ~5,930 of 7,315 tracked |
| Files with viper in their *path* | 565 (`src/tools/viper/`, `include/viper/`, `docs/viperlib/`, `viperide/`, `cmake/ViperConfig.cmake.in`, `scripts/build_viper_*`, …) |
| `Viper.*` runtime-namespace bind sites in `.zia` | 1,150 files |
| `VIPER_*` identifiers (CMake vars, env vars, defines) | ~60 distinct, thousands of uses (`VIPER_TESTS_DIR` ×644, `VIPER_ENABLE_GRAPHICS` ×436, …) |
| Packaging | `VAPS`, `.vap` extension, `text/x-vaps` MIME, `org.viper.*` registry keys, `ViperMaintenanceCache` |
| CI workflows referencing viper | 3 (`{linux,macos,windows}-release-installer.yml`) — **requires waiver of the no-CI-edits rule** |
| Pre-existing `zanna` strings | Only `misc/site/zanna-index.html` (no conflicts) |

## 2. Decisions

### Confirmed defaults (flip any before execution)
| Item | Old | New |
|---|---|---|
| Project / repo / binary | viper | **zanna** |
| Runtime namespace root | `Viper.*` | **`Zanna.*`** |
| Env/CMake/defines | `VIPER_*` | **`ZANNA_*`** (no compat shims — pre-launch) |
| Packaging (project) | VAPS / `.vap` / `text/x-vaps` / `org.viper.*` | **ZAPS / `.zap` / `text/x-zaps` / `org.zanna.*`** |
| Asset archive | VPA "Viper Pack Archive" / `.vpa` / magic `'V','P','A','1'` / `VpaWriter` / `rt_vpa_reader` / `vpa_*_t` | **ZPAK "Zanna Pack Archive" / `.zpak` / magic `'Z','P','A','K'` (separate `kVersion` field retained) / `ZpakWriter` / `rt_zpak_reader` / `zpak_*_t`** — magic-byte char literals in writer + reader are HAND EDITS, the sweep cannot rewrite them |
| GitHub URLs | `github.com/splanck/viper` (×243) | **`github.com/zannagames/zanna`** — dedicated rewrite BEFORE the generic sweep (generic would yield dead `splanck/zanna` links) |
| IDE | ViperIDE | **ZannaIDE** (mechanical case-map; "Zanna Studio" display-branding can come later) |
| Build scripts | `build_viper_*.sh` | `build_zanna_*.sh` |
| CMake package | `find_package(Viper)` / `ViperConfig.cmake.in` | `Zanna` / `ZannaConfig.cmake.in` |
| Test helpers | `viper_add_test` / `viper_add_ctest` | `zanna_add_test` / `zanna_add_ctest` |
| C runtime ABI `rt_*`, GUI `vg_*` prefixes | — | **Unchanged** (never encode the name) |
| Historical docs, ADRs, release notes, plans | — | **Full sweep** — the only permitted stale mentions are the rename ADR and one release-note line ("formerly Viper") |
| `.zia` extension, Zia language | — | **Unchanged** |

### Resolved 2026-07-17
1. **CI-workflow waiver: GRANTED** — the 3 release-installer workflows are edited as part of the rename.
2. **ViperDOS: REMOVE ALL REFERENCES** (not rename). This is real code excision, executed as its own commit *before* the rename sweep (Step A below): 67 files reference viperdos, including live `RT_PLATFORM_VIPERDOS` / `__viperdos__` conditional branches in ~25 runtime C files (`rt_context.c` ×9, `rt_platform.h` ×4, `rt_pool.c` ×4, sockets, crypto, heap, msgbus, output, datetime, countdown, box, string_ops), the dedicated test `src/tests/runtime/RTViperDOSPlatformTests.cpp` + its `viper_add_test` registration in `src/tests/unit/CMakeLists.txt:1884`, a `platform_policy_migration_baseline.txt` entry, and doc/site mentions. The branches are dead in-tree (ViperDOS always 0 since the 2026-03 extraction); each conditional collapses to its non-viperdos arm, read individually — this is surgery, not sed.
3. **Local directory rename:** deferred, Stephen's call post-migration.

### Commit structure (small-increments principle)
- **Commit A:** `chore(runtime): remove ViperDOS platform support` — excision + build/test green.
- **Commit B:** `chore: rename project Viper → Zanna` — the full mechanical sweep + path renames + goldens.
Both commits authored by the sweep, committed by Stephen only.

## 3. Execution phases

### Phase 0 — Preflight
- Confirm clean tree; run one full build + test suite (no skip flags) to establish a green baseline.
- `git tag viper-final` on the baseline commit (rollback anchor).

### Phase 1a (Commit A) — ViperDOS excision
1. Delete `src/tests/runtime/RTViperDOSPlatformTests.cpp`; remove its registration from `src/tests/unit/CMakeLists.txt` and its row from `scripts/platform_policy_migration_baseline.txt`.
2. Collapse every `RT_PLATFORM_VIPERDOS` / `__viperdos__` conditional in `src/runtime/**` to its surviving arm, reading each site (comments referencing ViperDOS semantics go too); remove the macro from `rt_platform.h`.
3. Purge prose mentions from docs/site/CLAUDE.md/AGENTS.md (67 files total).
4. Gate: incremental build + runtime/unit test labels green, then hand to Stephen for Commit A.

### Phase 1b (Commit B) — Content sweep (scripted, case-aware)
Perl in-place replacements (identical behavior macOS/Linux), applied to all tracked text files, in this order:
1. `github.com/splanck/viper` → `github.com/zannagames/zanna` (×243, before everything)
2. `VPA`/`Vpa`/`vpa` → `ZPAK`/`Zpak`/`zpak` (covers `.vpa`→`.zpak`, `VpaWriter`→`ZpakWriter`, `vpa_entry_t`→`zpak_entry_t`, `rt_vpa_reader`→`rt_zpak_reader`)
3. `VAPS`/`Vaps`/`vaps` → `ZAPS`/`Zaps`/`zaps`; `.vap` (word-boundary) → `.zap`
4. `VIPER` → `ZANNA`, `Viper` → `Zanna`, `viper` → `zanna` (ViperIDE→ZannaIDE falls out automatically)
5. Hand edits the sweep cannot do: magic char literals `{'V','P','A','1'}` → `{'Z','P','A','K'}` in `VpaWriter.cpp` and the matching check in `rt_vpa_reader.c`.

Exclusions: `.git/`, binary files, `misc/site/zanna-index.html`, this plan file (documents old names intentionally), and the rename ADR once written.

### Phase 2 — Path renames (565 files, `git mv` scripted from `git ls-files`)
Highlights: `src/tools/viper/` → `src/tools/zanna/`, `include/viper/` → `include/zanna/`, `docs/viperlib/` → `docs/zannalib/`, `viperide/` → `zannaide/`, `cmake/ViperConfig.cmake.in` → `ZannaConfig.cmake.in`, `cmake/WriteViperIDEBuildInfo.cmake` → `WriteZannaIDEBuildInfo.cmake`, `scripts/build_viper_*` → `build_zanna_*`, plus docs/man pages, fixtures, plan files, images.

### Phase 3 — Special surfaces checklist
- Runtime defs: every `Viper.X.Y` class in `src/il/runtime/defs/**` → `Zanna.X.Y`; rerun `./scripts/check_runtime_completeness.sh`.
- `RuntimeSurfacePolicy.inc` and surface-pin tests (procedure in the hardening-migration memo).
- Diagnostic/version strings (`viper:` prefixes, `--version` output) — goldens will shift.
- Man pages (`docs/man`), README, CLAUDE.md, AGENTS.md, presentation deck content.
- Installer smoke tests: registry keys `org.viper.smoke.install` → `org.zanna.smoke.install`, `ViperMaintenanceCache` → `ZannaMaintenanceCache`, product names.
- `.gitignore` entries containing viper paths.
- The 3 release-installer workflows (post-waiver).
- New ADR: "Project renamed Viper → Zanna" (motivation: name collisions — ETH Viper IL, Vyper homophone, ViperIDE, 5+ viper game engines; clearance summary). Next free ADR number.
- One release-notes line: platform renamed Zanna (formerly Viper).

### Phase 4 — Regenerate and verify (gates, in order)
1. Full clean configure + build (`./scripts/build_zanna_unix.sh` — no skip flags; SKIP_CLEAN masks configure errors).
2. `./scripts/update_goldens.sh` for renamed namespace/diagnostic output; re-run suite.
3. `ctest -L slow` explicitly (build script excludes it).
4. `./scripts/lint_platform_policy.sh` and `./scripts/run_cross_platform_smoke.sh`.
5. `./scripts/build_demos.sh` (examples/games all use `bind Zanna.*` now).
6. Staleness audit: `git grep -Ii viper` → only ADR + release-note whitelist; `git ls-files | grep -i viper` → empty.
7. Toolchain sanity: run the freshly built `build/src/tools/zanna/zanna` (never a stale stage binary), `zanna --version`, `zanna check` on an example, one VM-vs-native probe.

### Phase 5 — Remote choreography (Stephen-only actions)
1. Stephen reviews the diff and commits (single commit: `chore: rename project Viper → Zanna`).
2. GitHub: transfer `splanck/viper` to the `zannagames` org, rename repo to `zanna` (old URLs auto-redirect).
3. `git remote set-url origin git@github.com:zannagames/zanna.git` (or https), push.
4. Optionally rename the local directory (open decision #3).
5. Later, before public launch: attorney trademark knockout search on ZANNA (Class 9/42).

## 4. Rollback
Everything before Phase 5 is a working-tree change on top of `viper-final`: `git reset --hard viper-final` restores the world. After push, revert commit.

## 5. Known risks
- **Goldens churn**: any golden embedding class names, paths, or CLI strings shifts; `update_goldens.sh` handles it, and the E2E VM==native gate catches semantic drift.
- **Path-pinning docs test** (gotcha from the docs overhaul): docs paths change again; the sweep renames both content and paths together, but rerun `check_docs.sh`.
- **Windows validation**: run `.\scripts\build_zanna_win.cmd` on the Windows box after the Unix suite is green (installer smoke + registry keys changed).
- **Case-insensitive filesystem** (macOS): `git mv viper zanna` differs enough to be safe; no same-name-different-case renames exist.
