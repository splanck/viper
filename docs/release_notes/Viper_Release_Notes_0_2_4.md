# Viper Compiler Platform - Release Notes

> **Development Status**: Pre-Alpha
> These are early development releases. Viper is under active development and not ready for production use.
> Future milestones will define supported releases when appropriate.

> **DRAFT** — This document is a preliminary draft for v0.2.4. Content is subject to change before
> the official release.

## Version 0.2.4 - Pre-Alpha (TBD) — DRAFT

### Release Overview

Version 0.2.4 focuses on game engine showcase quality, demonstrating Viper's 2D game development capabilities through XENOSCAPE — a complete, visually polished 4-level sci-fi platformer.

---

### XENOSCAPE: Flagship Game Demo

The sidescroller demo has been transformed into **XENOSCAPE: The Descent** — a complete 4-level alien planet exploration game serving as Viper's flagship 2D game development showcase.

#### Visual Overhaul
- **Realistic astronaut character**: properly proportioned with articulated limbs, detailed spacesuit panels, jetpack hardware, reflective visor. All sprites drawn at small size then upscaled via `Pixels.Scale()` for visible detail at 1280x720.
- **Alien enemies**: translucent slime blobs with inner structure, angular membrane-winged bats, military sentry turrets with sensor domes, heavy mech boss with reactor core and hydraulic limbs.
- **64px tiles** (up from 48px): larger, more detailed tile sprites with biome-specific art.
- **Bright saturated color palette**: replaced dark moody colors with vivid, high-contrast palette.
- **Tile edge bevels**: every solid tile gets 1px light/dark edge overlays for 3D depth.
- **Biome ambient effects**: bioluminescent root glow, crystal shimmer, mushroom pulse, magma crack embers, electric sparks, coolant glow, console flicker, holographic shimmer.

#### Color.RGB() Palette System
All 160+ color constants migrated from hex `final` values to a `Palette` entity using `Color.RGB(r, g, b)` for self-documenting, readable color definitions. The Palette is passed via constructor injection to all subsystems — enables future theme-swapping.

#### Four Levels

| Level | Theme | Biomes |
|-------|-------|--------|
| 1: The Descent | Alien planet | Jungle → Crystal Caverns → Thermal Vents |
| 2: Salvage | Crashed station | Outer Hull → Reactor Core → Command Bridge |
| 3: Frozen Abyss | Ice caverns | Frozen Surface → Crystal Depths → Frozen Core |
| 4: Overgrowth | Ancient ruins | Overgrown Surface → Deep Ruins → Heart of Ruins |

Each level is 150 tiles wide with 3 seamless biome transitions, unique tile types, enemy placement, and a boss arena at the end.

#### Level Select
New menu option allows starting at any level — useful for testing and replayability.

#### 11 New Station Tile Types
Hull plate, girder, electric hazard, airlock, pipe, catwalk, coolant (animated), conduit, console, holographic bridge, viewport — all with detailed sprites and ambient effects.

#### Reusable Level Helpers
Extracted common level-building patterns:
- `buildPlatformRun()`: platform + coins in one call
- `buildGapWithHazard()`: ground clear + hazard fill
- `buildArena()`: flat boss arena with containment walls
- `getTile()` refactored to list-based lookup (zero code changes when adding tiles)

#### 3 Station Background Renderers
- **Outer Hull**: dark starfield with floating debris and emergency light strips
- **Reactor Core**: deep red emergency gradient with pipe silhouettes and pulsing reactor glow
- **Command Bridge**: holographic screen glow with data stream particles

---

### Game Engine Test Coverage

4 new unit test suites covering Canvas frame helpers (14 tests), SaveData persistence (17 tests), Canvas text layout (14 tests), and DebugOverlay (20 tests). Total: 1,355 tests passing.

---

### Bug Fixes
- **Sidescroller boss reset**: `bossMaxHp` now properly reset between levels
- **Boss arena containment**: walls prevent boss from falling into adjacent sections
- **Victory flow**: works correctly for multi-level progression

---

### Files Changed

| Category | Files | Net LOC |
|----------|-------|---------|
| Demo overhaul | 17 sidescroller files | +4,000 |
| New palette system | palette.zia (new) | +341 |
| Test suites | 4 new test files | +700 |
| Documentation | release notes, docs | +200 |
