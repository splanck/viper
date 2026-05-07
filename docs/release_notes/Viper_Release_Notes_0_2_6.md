# Viper Compiler Platform — Release Notes

> **Development Status:** Pre-Alpha. Viper is under active development and not ready for production use.

## Version 0.2.6 — Pre-Alpha (DRAFT — unreleased)

<!-- DRAFT: release date TBD. v0.2.5 was cut on 2026-05-07. -->

### What this release is about

Early fixes cycle opening the v0.2.6 development window. The primary change is a correctness fix to the `Viper.Audio.*` compatibility namespace introduced in v0.2.5, along with OGG test infrastructure hardening.

### Runtime

**Viper.Audio.* compatibility surface reworked** — The `Viper.Audio.*` compat layer was converted from `RT_ALIAS` entries (which delegated to `Viper.Sound.*` handlers) to full `RT_CLASS_BEGIN` / `RT_FUNC` / `RT_METHOD` / `RT_PROP` registrations with dedicated `AudioCompat*` handler names. `RT_ALIAS` cannot express typed class return values correctly: methods that return `obj<Viper.Audio.Sound>` or `obj<Viper.Audio.Music>` require a registered `Viper.Audio.*` class entry in the runtime type registry, otherwise handle type resolution fails at the call site. All eight promoted classes: `Audio`, `Sound`, `Voice`, `Music`, `Playlist`, `SoundBank`, `Synth`, `MusicGen`.

**RuntimeSurfacePolicy** — `rt_audio3d_register_voice` added to the internal symbol whitelist; its absence caused policy validation failures when the 3D audio voice registration path was exercised.

### Tests

`TestOggVorbis` page builder now computes correct OGG CRC-32 checksums (polynomial `0x04C11DB7`) for every synthesized test page, replacing the previous zero placeholder. Required since `rt_ogg.c` re-enabled strict CRC validation in v0.2.5; all synthetic test streams were previously being rejected before the test logic could run.

`TestMethodIndex` — assertion corrected: `Viper.Audio.Synth.Tone` target is now `"Viper.Audio.Synth.Tone"` (its own `RT_CLASS_BEGIN` entry) rather than the former alias redirect `"Viper.Sound.Synth.Tone"`. Return class confirmed as `Viper.Audio.Sound`.

### By the Numbers

| Metric | v0.2.5 | v0.2.6 | Delta |
|---|---|---|---|
| Commits | — | 1 | +1 |
| Source files | 2,996 | 2,999 | +3 |
| Production SLOC | 552K | 553,774 | +~2K |
| Test SLOC | 228K | 229,151 | +~1K |

---

### Commits

See `git log f5c25c231 -- .` for the full commit history since v0.2.5.

<!-- END DRAFT -->
