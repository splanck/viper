# Viper Compiler Platform - Release Notes

> **Development Status**: Pre-Alpha
> These are early development releases. Viper is under active development and not ready for production use.
> Future milestones will define supported releases when appropriate.

> **DRAFT** — This document is a preliminary draft for v0.2.4. Content is subject to change before
> the official release.

## Version 0.2.4 - Pre-Alpha (TBD) — DRAFT

### Release Overview

Version 0.2.4 is a showcase and infrastructure release: a flagship demo game, new game runtime classes, a critical compiler fix for runtime property setters, AArch64 codegen correctness improvements, and packaging/documentation polish.

#### By the Numbers

| Metric | v0.2.3 | v0.2.4 | Delta |
|--------|--------|--------|-------|
| Source files | 2,671 | 2,699 | +28 |
| Production SLOC | ~348K | ~380K | +32K |
| Test count | 1,351 | 1,358 | +7 |

---

### Compiler & Codegen Fixes

- **Zia sema: runtime property setter resolution** — Property assignments on runtime class instances (e.g., `ctrl.VY = value`) now correctly call the setter function via symbol lookup. Previously, only user-defined class property setters were resolved; runtime class setters silently fell through to invalid direct memory writes. This fix also registers the property type from the setter's function signature, enabling automatic `Number → Integer` coercion for write-only properties.
- **AArch64: i1 parameter masking at function entry** — Boolean (`i1`) parameters received by Viper-generated functions are now masked with `AND 1` on load, matching the existing return-value masking. Prevents upper-bit garbage from corrupting boolean values.
- **Native linker: `RtComponent::Game`** — Added the `Game` runtime component to the selective linking infrastructure. Game runtime classes moved from `collections/` to their own `game/` directory and static archive (`libviper_rt_game.a`) are now correctly linked by the AArch64 native linker.

---

### New Game Runtime Classes

Five additions to the `Viper.Game` namespace:

- **Timer ms-mode** — `StartMs(durationMs)`, `UpdateMs(dt)`, `ElapsedMs`, `RemainingMs` added to the existing `Timer` class for delta-time-independent cooldowns.
- **Lighting2D** — 2D darkness overlay with pulsing player light and pooled dynamic point lights. Configurable darkness alpha, tint color, and per-biome atmosphere.
- **PlatformerController** — Jump buffering, coyote time, variable jump height, ground/air acceleration curves, and apex gravity bonus. All ms-based for dt-independence.
- **AchievementTracker** — Up to 64 achievements with bitmask unlock tracking, 32 stat counters, and animated slide-in notification popups. Mask is save/load compatible.
- **Typewriter** — Character-by-character text reveal with configurable ms-per-character rate. For dialogue, lore terminals, tutorials.

### Runtime Directory Reorganization

Game engine classes moved from `src/runtime/collections/` to `src/runtime/game/` — 36 files (18 class pairs). Collections now contains only data structures. No API changes.

---

### VAPS Packaging System

Comprehensive overhaul of the Viper Application Packaging System with 10 improvements to package validation, metadata handling, and archive structure. 57 new tests. Windows installer stub in progress.

---

### Documentation

Comprehensive documentation pass across all 2,668 source files with standardized Viper file headers (purpose, invariants, ownership/lifetime, links).

---

### Demo Games

- **XENOSCAPE** (sidescroller): Expanded into a Metroid-style action exploration game — 10 themed levels (jungle, caves, volcano, sky fortress, frozen abyss, corrupted ruins, the core), 30+ enemy types, boss fights, unlockable abilities (dash, charge shot, ground pound), procedural music via MusicGen, dynamic lighting, achievement system, lore terminals, save stations, and Perlin noise cloud rendering. Migrated to runtime APIs (PlatformerController, CollisionRect, Tilemap, ObjectPool, Quadtree, Camera, Lighting2D, AchievementTracker, Typewriter) with smooth biome crossfade transitions via Color.Lerp.
- **Platformer Reference**: New 10-file teaching codebase demonstrating 25+ runtime APIs.

---

### Bug Fixes

- Particle emitter renders with zero alpha from `Color.RGB()` values
- Runtime property setter calls on runtime classes (see Compiler section)
- `i1` boolean parameter corruption in AArch64 native codegen
- Native linker missing `libviper_rt_game.a` archive (symbol-not-found crash)
- PlatformerController velocity desync after damage knockback, death bounce, enemy stomp
- Sidescroller expansion systems (`updateNewSystems`) not wired into game loop
- Sprite3D use-after-free: per-frame mesh/material allocation replaced with cached instances + GC temp buffer registration
- Added `Mesh3D.Clear()` — reset vertex/index counts without freeing backing arrays (enables mesh reuse)
- Physics3D sphere-sphere and AABB-sphere narrow-phase collision (replaces AABB-only approximation)
- Physics3D character controller with slide-and-step movement (replaces trivial velocity-set)
- Physics3D collision event queue: `CollisionCount`, `GetCollisionBodyA/B`, `GetCollisionNormal/Depth`
- Canvas3D software framebuffer clear optimized: uint32 writes instead of per-byte loop (~4x faster at 1080p)
- Animation crossfade rewritten: TRS decomposition + quaternion SLERP replaces raw matrix lerp (eliminates shear artifacts)
- `Light3D.NewSpot` — spot light with position, direction, cone angles, and smoothstep attenuation
- `Camera3D.NewOrtho` — orthographic camera for isometric/strategy games (no perspective foreshortening)
- `DistanceJoint3D` and `SpringJoint3D` — physics joint constraints with 6-iteration sequential impulse solver
- Audio3D per-voice max_distance tracking (replaces shared global that caused cross-voice attenuation bugs)
- NavMesh3D adjacency build optimized from O(n²) to O(n) via edge hash map
- Terrain3D texture splatting: `SetSplatMap` + 4 layer textures with per-layer UV tiling, baked blend
