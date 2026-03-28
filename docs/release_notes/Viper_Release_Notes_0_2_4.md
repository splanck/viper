# Viper Compiler Platform - Release Notes

> **Development Status**: Pre-Alpha
> These are early development releases. Viper is under active development and not ready for production use.
> Future milestones will define supported releases when appropriate.

> **DRAFT** — This document is a preliminary draft for v0.2.4. Content is subject to change before
> the official release.

## Version 0.2.4 - Pre-Alpha (TBD) — DRAFT

### Release Overview

Version 0.2.4 is a showcase and polish release focused on demonstrating Viper's 2D game development capabilities, packaging infrastructure, and documentation quality.

#### By the Numbers

| Metric | Value |
|--------|-------|
| Commits since v0.2.3 | 96 |
| Files changed | 616 |
| Lines added / removed | ~30K / ~3K |
| Source files | 2,685 |
| Test count | 1,358 (up from 1,351) |

---

### VAPS Packaging System

Comprehensive overhaul of the Viper Application Packaging System with 10 improvements to package validation, metadata handling, and archive structure. 57 new tests. Windows installer stub in progress.

---

### Documentation

Comprehensive documentation pass across all 2,668 source files with standardized Viper file headers (purpose, invariants, ownership/lifetime, links).

---

### Game Engine Test Coverage

4 new unit test suites: Canvas frame helpers (14 tests), SaveData persistence (17 tests), Canvas text layout (14 tests), DebugOverlay (20 tests).

---

### New Game Runtime Classes

Five additions to the `Viper.Game` namespace:

- **Timer ms-mode** — `StartMs(durationMs)`, `UpdateMs(dt)`, `ElapsedMs`, `RemainingMs` added to the existing `Timer` class for delta-time-independent cooldowns.
- **Lighting2D** — 2D darkness overlay with pulsing player light and pooled dynamic point lights. Configurable darkness alpha, tint color, and per-biome atmosphere.
- **PlatformerController** — Jump buffering, coyote time, variable jump height, ground/air acceleration curves, and apex gravity bonus. All ms-based.
- **AchievementTracker** — Up to 64 achievements with bitmask unlock tracking, 32 stat counters, and animated slide-in notification popups. Mask is save/load compatible.
- **Typewriter** — Character-by-character text reveal with configurable ms-per-character rate. For dialogue, lore terminals, tutorials.

### Runtime Directory Reorganization

Game engine classes moved from `src/runtime/collections/` to `src/runtime/game/` — 36 files (18 class pairs). Collections now contains only data structures. No API changes.

---

### Demo Games

- **XENOSCAPE**: Sidescroller demo expanded into a Metroid-style action exploration game — 5 themed levels, 30 enemy types, boss fights, unlockable abilities, procedural music, dynamic lighting, and save system. 17K LOC across 26 files. Migrated to use runtime APIs (CollisionRect, Tilemap, ObjectPool, Quadtree, Camera) and added smooth biome crossfade transitions via Color.Lerp.
- **Platformer Reference**: New 10-file teaching codebase demonstrating 25+ runtime APIs (GameBase, Tilemap, Camera, ObjectPool, Quadtree, StateMachine, ParticleEmitter, SoundBank, ScreenFX, UI widgets, etc.).

---

### Bug Fixes

- Particle emitter renders with zero alpha from `Color.RGB()` values
- Sidescroller boss HP reset between levels
- Dynamic light pool swap-remove bug (replaced with ObjectPool)
