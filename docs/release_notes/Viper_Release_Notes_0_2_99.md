# Viper Compiler Platform — Release Notes

> **Development Status:** Pre-Alpha. Viper is under active development and not ready for production use.

## Version 0.2.99 — Pre-Alpha (DRAFT — unreleased)

<!-- DRAFT: release date TBD. v0.2.7 was cut on 2026-06-30. -->

### What this release is about

A focused runtime-API canonicalization pass on top of the v0.2.7 hardening cycle. The pre-alpha runtime surface is pruned to the canonical public names it will carry forward: duplicate and alias entries are removed, the generator refuses to mint new aliases, and the whole tree — both frontends, ViperIDE, demos, and the generated docs — rebinds to that one set of names. Line counts stay flat because this is a rename-and-prune, not a feature release.

- **Runtime API surface canonicalized.** Names settle on the canonical forms — `Core.Box` option conversions, `Core.Parse` `Try*` helpers, collection `To*` conversion direction, `Length` cardinality properties, `Path`-suffixed canvas path operations, and corrected `Game3D`/`Graphics3D` class leaves — with the duplicate and alias entries removed rather than kept.
- **`rtgen` rejects public aliases.** `RT_ALIAS` is now a generator error and no longer feeds the name map, signatures, or frontend names; functions in descendant namespaces are no longer synthesized as methods on their parent runtime classes.
- **`--dump-runtime-api` reports public signatures.** The inventory prints public signature text instead of the lowered descriptor form, so typed object signatures stay visible in the API dump.
- **Tree migrated to the canonical names.** BASIC and Zia examples, ViperIDE, graphics3d and app demos, conformance probes, golden IL, and the generated docs/site pages all rebind to the smaller surface (e.g. `MusicGen.Length`, `Viper.Text.Fmt.Int`); tests now guard the canonical names instead of alias-heavy behavior.
- **macOS `VIPER_GFX_NO_ACTIVATE` knob (new).** Graphics CTest cases can run without repeatedly activating their windows, with the knob documented alongside the display-requiring tests.

### By the Numbers

| Metric | v0.2.7 | v0.2.99 | Delta |
|---|---|---|---|
| Commits | — | 2 | +2 |
| Source files | 3,402 | 3,402 | 0 |
| Production SLOC | 762K | 762K | flat |
| Test SLOC | 304K | 304K | flat |
| ViperIDE SLOC | 28K | 28K | flat |
| Demo SLOC | 197K | 197K | flat |

Counts via `scripts/count_sloc.sh` (production 761,751 / test 304,367 / demo 196,786 / viperide 28,210 / source files 3,402); commits since the `v0.2.7-dev` tag (2026-06-30). Line counts are effectively flat by design: the canonicalization landed +4,974 / −4,808 across 440 files — a net-neutral prune, not new surface.

<!-- END DRAFT -->
