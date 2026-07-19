# Zanna Studio Flagship Program

Date: 2026-07-17 · **All 11 phases implemented 2026-07-17..18** (as-built
records in each plan; P4's staged glyph adoption closed with the program).
Owner validation remaining: Windows machine pass (P1 packaging artifacts, P3
UIA screen-reader checklist, P9 native dialogs/cursors), vim/less/htop in the
built terminal per platform, a real credentialed push, and the Linux AT-SPI
provider recorded as the program's one carve-out (plan 09).

## 1. Summary and objective

This program elevates the platform IDE from ZannaIDE to **Zanna Studio**: the
flagship product of the Zanna platform and the first impression most users
will have of it. The program has three goals:

1. **Identity** — a complete, residue-free rename to Zanna Studio (binary
   `zannastudio`), including installer identity, configuration migration, and
   documentation.
2. **Appearance** — a total reface to the Zanna brand: green/steel/teal on a
   dark charcoal-green field, a scalable vector icon library, premium motion,
   pacing, and typography.
3. **Capability** — closing the functional gaps between the current editor and
   a premium IDE: project-wide replace, side-by-side diff, deeper splits,
   rebindable keys, a new-project wizard, structured debugger inspection, a
   real terminal emulator, richer source control, and screen-reader coverage
   on every platform.

The game scene/level editor is explicitly **out of scope** for this program.
It remains a separate future initiative; nothing in this program may block it.

## 2. Phase index

| Plan | Phase | Track | Loop | Size |
|---|---|---|---|---|
| [01-identity-rename.md](01-identity-rename.md) | P1 ZannaIDE → Zanna Studio full rename | serial root | mixed | L |
| [02-brand-reface.md](02-brand-reface.md) | P2 brand palettes + chrome | serial root | C + Zia | M |
| [03-windows-uia-accessibility.md](03-windows-uia-accessibility.md) | P3 Windows UIA provider | P | C (Windows) | L |
| [04-iconography-and-assets.md](04-iconography-and-assets.md) | P4 vector icon library + brand marks | R | C | L |
| [05-motion-and-present-pacing.md](05-motion-and-present-pacing.md) | P5 real-dt animation, smooth scroll, vsync, minimap | R | C | M-L |
| [06-typography-premium.md](06-typography-premium.md) | P6 gamma-correct AA, ligatures, glyph fallback | R | C | L |
| [07-editor-search-depth.md](07-editor-search-depth.md) | P7 project replace, diff view, splits, tabs, breadcrumbs | A | Zia + small C | L |
| [08-shell-config-depth.md](08-shell-config-depth.md) | P8 keybindings editor, project wizard, settings polish | A | Zia | M |
| [09-platform-premium.md](09-platform-premium.md) | P9 native Windows dialogs, cursors, Linux AT-SPI | P | C | M |
| [10-debugger-depth.md](10-debugger-depth.md) | P10 debug reflection + watch panel (ADR-gated) | D | C++ + Zia | L |
| [11-terminal-scm-depth.md](11-terminal-scm-depth.md) | P11 terminal alt-screen, SCM history/multi-job | D | Zia | M-L |

## 3. Dependency order and parallelization

```
P1 Identity rename ──► P2 Brand reface ──┬─► Track R (C rendering):  P4 Icons, P5 Motion, P6 Typography
        │                                ├─► Track A (Zia app):      P7 Editor/Search, P8 Shell/Config
        └───────► P3 Windows UIA ────────┴─► Track P (platform):     P9 Platform premium
P10 Debugger depth and P11 Terminal/SCM depth: any time after P1/P2
```

- **P1 first, serial.** Every later phase creates files under
  `src/zannastudio/`, registers `zia_zannastudio_*` tests, and writes docs
  saying "Zanna Studio". Renaming first means nothing is touched twice and the
  residue grep stays meaningful.
- **P2 second.** The palette is the visual vocabulary every later phase
  consumes (icons tint from theme tokens; new UI is styled by it).
- **P3 is priority-hoisted.** Windows screen readers currently receive
  nothing; macOS VoiceOver is complete. P3 shares no files with P2 and can run
  in parallel with it.
- Tracks R, A, P, D are file-disjoint by construction (R: `src/lib/gui` +
  `src/lib/graphics` + `src/runtime/graphics`; A: `src/zannastudio`; P:
  platform adapter files; D: `src/tools/zanna` + the IDE `terminal/`/`scm/`
  subdirectories). Shared chokepoints — `src/tests/CMakeLists.txt`, the
  runtime def files, `rt_gui.h`, `scripts/source_health_baseline.tsv` — are
  small and mergeable; phases rebase before landing.
- P5 and P6 are mutually independent. P8 depends only softly on P7.

## 4. ADRs

- **ADR 0118** (P1): the rename — identity, configuration migration policy,
  installer component contract, historical keep-list. Precedent: ADR 0110.
- **Consolidated premium-rendering ADR** (start of P4): the additive
  `Zanna.GUI.*` surface for P4-P6 (icon names, smooth-scroll toggle, ligature
  toggle) plus P7's shared-buffer method if required. Number checked against
  `docs/adr/` at execution time (the directory has live numbering collisions —
  always re-verify; P1's rename ADR landed as 0114 for exactly this reason).
- **Debug-reflection ADR** (start of P10): structured variable protocol.

## 5. House-rule compliance checklist (applies to every phase)

- Zero external dependencies. OS APIs are permitted; libraries are not.
- 100% cross-platform: macOS, Windows, Linux — no partial features.
- `vg_draw` determinism contract: no floating point in per-pixel loops.
- New runtime surface requires, per item: entries in the modular runtime defs
  (`src/il/runtime/defs/classes/*.def`), a hand-added `RTCLS_*` member in
  `src/il/runtime/classes/RuntimeClasses.hpp` for any new class, declarations
  in `rt_gui.h`, `RuntimeSurfacePolicy.inc` coverage, a
  `scripts/source_health_baseline.tsv` bump for new `rt_*` files, graphics-off
  stub variants, and a green `./scripts/check_runtime_completeness.sh`.
  New class leaf names must be globally unique across `Zanna.*`.
- Raw `_WIN32`/`__APPLE__`/`__linux__` checks only in approved adapter layers
  (`./scripts/lint_platform_policy.sh` green).
- Every feature lands with tests: IDE probes under
  `src/zannastudio/src/probes/` registered in `src/tests/CMakeLists.txt`
  (label `zannastudio`), C tests under `src/lib/gui/tests/`.
- The default theme remains **Dark**.
- No version bumps; version decisions belong to the project owner.
- No CI workflow edits beyond the explicitly waived three lines in P1.
- Historical documents (release notes, blog posts, ADR 0110, dated plan
  suites) are never renamed or swept.

## 6. Program-wide exit gate

Every phase ends with: incremental toolchain build green (full rebuilds only
at the owner's discretion), `./scripts/build_ide.sh` green, targeted ctest
green (`zannastudio` label + touched areas; full-suite runs are the owner's
call), `./scripts/check_runtime_completeness.sh` green (baseline bumped iff
surface was added), new behavior covered by probes or C tests, and — for
phases that touch rendering — a manual visual pass on at least the host
platform with the other platforms validated at the next opportunity.
