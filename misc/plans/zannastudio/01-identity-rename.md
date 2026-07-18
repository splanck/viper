# Plan 01 — Identity Rename: ZannaIDE → Zanna Studio

Date: 2026-07-17 · Track: serial root · Loop: mixed · Size: L (~200 files, mechanical)

## 1. Objective

Complete, residue-free rename of the product to **Zanna Studio** with binary
**`zannastudio`** (`zannastudio.exe` on Windows), covering source tree, build
system, installers, configuration (with migration), tests, docs, and site.
Historical documents keep "ZannaIDE" as shipped history.

## 2. Name decisions (final)

| Surface | Old | New |
|---|---|---|
| Binary | `zannaide` / `zannaide.exe` | `zannastudio` / `zannastudio.exe` |
| Source dir | `src/zannaide/` | `src/zannastudio/` (git mv) |
| AOT CMake target | `zannaide_native` | `zannastudio_native` |
| CMake option | `ZANNA_INSTALL_ZANNAIDE` (21 uses) | `ZANNA_INSTALL_ZANNASTUDIO` |
| CMake helper | `cmake/WriteZannaIDEBuildInfo.cmake` | `cmake/WriteZannaStudioBuildInfo.cmake` |
| Windows component id | `kComponentZannaIDE = "zannaide"` | `kComponentZannaStudio = "zannastudio"` |
| Windows display / shortcut / icon | "ZannaIDE", `ZannaIDE.lnk`, `bin/zannaide.ico` | "Zanna Studio", `Zanna Studio.lnk`, `bin/zannastudio.ico` |
| Linux packaging | `zannaide.desktop`, `/usr/bin/zannaide` | `zannastudio.desktop` (`Name=Zanna Studio`), `/usr/bin/zannastudio` |
| macOS | staged binary inside `Zanna Toolchain.app` | unchanged app; staged binary `zannastudio` |
| Bundle/project id | `org.zanna-lang.zannaide` | `org.zanna-lang.zannastudio` |
| Config dirs | `%APPDATA%\ZannaIDE`, `~/Library/Application Support/ZannaIDE`, `$XDG_CONFIG_HOME/zannaide` / `~/.config/zannaide`, legacy `~/.zannaide` | `ZannaStudio` / `zannastudio` equivalents + one-time copy migration |
| Trash / temp | `.zannaide-trash`, `.<name>.zannaide-tmp-<ms>` | `.zannastudio-trash` (legacy dir still restorable), `.<name>.zannastudio-tmp-<ms>` (stale legacy prefix still swept) |
| In-app consts / env | `ZANNAIDE_VERSION`, `ZANNAIDE_PERF_LOG` | `ZANNASTUDIO_VERSION`, `ZANNASTUDIO_PERF_LOG` |
| Test names / label | `zia_smoke_zannaide*`, `zia_zannaide_*` (37 names), label `zannaide` | `zia_smoke_zannastudio*`, `zia_zannastudio_*`, label `zannastudio` |
| Docs / site | `docs/internals/codemap/zannaide.md`, `misc/site/showcase/zannaide.html` | `codemap/zannastudio.md`, `showcase/zannastudio.html` + inbound links |
| ADR | — | `docs/adr/0118-rename-zannaide-to-zanna-studio.md` |

**Deliberately unchanged:** `scripts/build_ide.sh` / `build_ide_win.ps1`
filenames (generic; contents updated); the generic `ZANNA_IDE_*` build env
family (`ZANNA_IDE_ARCH`, `ZANNA_IDE_OUTPUT`, `ZANNA_IDE_SKIP_COMPAT_COPY`,
`ZANNA_IDE_BUILD_INFO`, `ZANNA_IDE_OUT_DIR`, `ZANNA_IDE_TOOL_BUILD_DIR`,
`ZANNA_IDE_BUILD_DIR`, `ZANNA_IDE_COMPAT_OUTPUT`); historical documents (§6).

## 3. Configuration migration policy

`SetupPath()` in `core/settings.zia` resolves in this order:

1. New platform dir (`ZannaStudio`/`zannastudio`) containing `settings.ini` →
   use it.
2. Otherwise probe old locations in order — platform `ZannaIDE` dir, then
   `~/.zannaide` — and **copy** every state file (settings.ini plus
   session/recent/breakpoints/recovery files present in the dir) into the new
   dir. Copy, don't move: an old install remains intact on rollback.
3. If the copy fails, adopt the old dir in place (exactly the existing legacy
   fallback behavior).

Project-local `.zannaide-trash` / `.zannaide-tmp-` artifacts need no active
handling: the explorer's dot-prefix exclusion hides both old and new names,
trash recovery is manual by design (the legacy directory stays recoverable in
place), and temp files are transient write-then-rename artifacts with no sweep
mechanism for either prefix.

## 4. Ordered execution steps

1. **ADR 0118** mirroring 0110's format (front-matter, Context, Decision =
   the table above + migration policy + keep-list, Consequences = CI waiver,
   one-time migration).
2. `git mv src/zannaide src/zannastudio`.
3. In-app sweep: window title (`src/main.zia`), About (`ui/ide_overlays.zia`),
   status bar (`ui/status_shell.zia`), welcome (`ui/welcome_view.zia`), menu
   (`ui/menu_chrome.zia`), toast (`main.zia`), consts, bundle id
   (`zanna.project`), remaining "ZannaIDE" literals (~90).
4. Settings migration per §3 (`core/settings.zia`).
5. Trash/temp renames + legacy reads (`core/project_file_ops.zia`,
   `core/document_manager.zia`, `ui/explorer_actions.zia`,
   `core/project_manager.zia`).
6. Build system: root `CMakeLists.txt` target/paths/option (all 21 option
   uses), `cmake/WriteZannaIDEBuildInfo.cmake` rename (function inside too),
   **plus the three waived CI workflow lines**
   (`.github/workflows/{windows,macos,linux}-release-installer.yml`, the
   `-DZANNA_INSTALL_ZANNAIDE=ON` line in each) in the same change.
7. Installer C++: `WindowsPackageBuilder.cpp` (component id, display, .lnk,
   .ico, file association), `LinuxPackageBuilder.cpp` (.desktop, exec path),
   `ToolchainInstallManifest.cpp` (staged binary), `cmd_install_package.cpp`,
   `windows_installer/` wizard strings, `InstallerStub.cpp` message.
8. Scripts + tests: `build_ide.sh` / `build_ide_win.ps1` contents,
   `build_installer.sh` / `.ps1`, `ci_full_sanitizer.sh`,
   `run_cross_platform_smoke.sh` regexes, `src/tests/CMakeLists.txt`
   (203 occurrences: 37 test names, label, probe paths),
   `src/tests/tools/WindowsToolchainInstallerE2E.cmake`,
   `scripts/validate-windows-toolchain-installer.ps1`.
9. Docs/site: `git mv` codemap page + index links (+ one "formerly ZannaIDE"
   line for greppable history), `git mv` showcase page + inbound links
   (`misc/site/index.html`, `features/applications.html`,
   `showcase/index.html`, sweep for stragglers), `README.md`, `.gitignore`.
10. Cleanups: delete stale committed `zannaide/bin/zannaide.buildinfo`
    artifact (and empty dirs), fix the dangling plan-path comment at
    `src/tools/zanna/DebugAdapter.hpp:20`.
11. New probe `settings_migration_probe.zia` (sandboxed HOME: seed old-path
    settings → assert copied into new dir, old untouched), registered in
    `src/tests/CMakeLists.txt`.

## 5. Verification (exit gate)

1. Incremental toolchain build (reconfigure required — CMakeLists.txt changed)
   + `./scripts/build_ide.sh` produces a launching `zannastudio` binary.
2. Targeted ctest green: `-L zannastudio` label plus the touched areas
   (packaging, install-manifest); `ctest -N` shows 37+1 `zannastudio` tests
   and zero `zannaide` tests. Full-suite validation at the owner's discretion.
3. `./scripts/check_runtime_completeness.sh` green;
   `scripts/source_health_baseline.tsv` untouched (no runtime surface change).
4. Residue grep: `grep -rniE 'zannaide' . --exclude-dir=.git
   --exclude-dir=build` — every hit on the allowlist (§6). The pattern
   deliberately does not match the kept `ZANNA_IDE_*` family. Also check for
   dynamic `"zanna" + "ide"` concatenations in Zia and shell.
5. `./scripts/lint_platform_policy.sh` and
   `./scripts/run_cross_platform_smoke.sh`.
6. Windows-only artifacts (E2E cmake, PowerShell validator) verified
   statically; execution deferred to the next Windows machine run.

## 6. Historical keep-list (never swept)

`docs/release_notes/*`, `misc/site/blog/*`,
`docs/adr/0110-project-rename-viper-to-zanna.md` and earlier ADRs, dated plan
suites under `misc/plans/`, `misc/reviews/*`. ADR 0118 and this plan suite
mention the old name as context by design.

## 7. Risks

- CI red if the three workflow lines and the option rename land separately —
  they are one change.
- Literal grep misses dynamically built names — mitigated by the
  concatenation check in §5.4.
- User data loss in migration — mitigated by copy-don't-move and the probe.
- Windows/Linux installer paths only fully provable on those OSes — deferred
  execution check, flagged in the phase gate.
