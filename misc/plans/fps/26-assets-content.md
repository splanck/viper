# 26 — Game: Assets & Content Pipeline — Packs, GLB Conventions, VPA, Fallbacks, Credits

> **STATUS: PLANNED (2026-07-07)** · Baseline `3166d1dc2` · Track G · 1 session (+ ongoing
> content passes in P5/P18). Prereqs: 08 (E27 alias maps), 09 (E30 GenerateLODs, E31 ASCII
> FBX). **Iron rule (ridgebound lineage): every imported asset is optional** — the repo with
> an empty `assets_src/` builds, runs, and passes every probe on procedural fallbacks.

## 1. Placeholder pack shortlist (all CC0 unless noted; verify URLs + license text at download
time and record exact versions in CREDITS.md — packs evolve)

| Need | Pack | Format notes |
|---|---|---|
| Humanoid frames + full anim sets | **Quaternius Ultimate Animated Character Pack** | GLB, rigged+animated; alias table `quat_human` |
| Robots/mechs/drones/turrets | **Quaternius Ultimate Space Kit / RObit robots / Mech pack** | GLB; some static (node-anim in 18 §1) |
| Character variety (skeleton-crew style frames) | **KayKit Character packs** (CC0) | GLB, Viper-loader friendly |
| Modular sci-fi interiors (L2/L4/L7/L9 kits) | **Quaternius Ultimate Modular Sci-Fi** | GLB kit pieces; manifest `scene` prefabs |
| Weapons (view + world models) | **Kenney Blaster Kit** + Quaternius weapons | GLB/OBJ; muzzle socket nodes added (§3) |
| Props/pickups/crates/barrels | **Kenney Space Kit, Sci-Fi RTS** | GLB/OBJ |
| Prototype textures (arena + fallback materials) | **Kenney Prototype Textures** | PNG |
| SFX polish layer | **Kenney Sci-Fi Sounds, Impact Sounds, Interface Sounds** | OGG/WAV over the synth bank |
Sources: quaternius.com, kenney.nl, kaylousberg.com (KayKit) — CC0 (Kenney/Quaternius/KayKit
publish CC0; re-verify per pack page at download). If a needed pack turns CC-BY: acceptable
(attribution in CREDITS.md + credits screen), **no NC/ND licenses ever**. Downloads are
data-only; nothing links into the build (zero-dependency rule intact). User note honored:
**all placeholders — final art swaps later**, so §3 conventions keep swaps mechanical.

## 2. Directory & load flow

```
assets_src/            # raw downloads (git-ignored? NO — committed for reproducibility, they're CC0)
  characters/ enemies/ weapons/ kits/ props/ textures/ audio/
assets_gen/            # LOD-generated + converted outputs (committed; deterministic)
```
`assets/registry.zia` load flow per asset id: `SceneAsset.LoadAsset` (packed VPA) →
`LoadResult` candidate paths (CWD + ExeDir probing — landmine rule) → **procedural fallback**
(§4). On load: alias table (E27) → `GenerateAllLODs(3, 0.4)` (E30, skipped if `_lod` variants
exist) → material fixups (§5) → cached. Async: hub/level preloads via `Assets3D` handles
feeding the loading screen; residency budget per tier.

## 3. GLB conventions (make the later art-swap mechanical)

Naming contract (applies to placeholders AND future final art): socket nodes `socket_muzzle`,
`socket_lhand`, `socket_back`, `socket_pin_*`; hit-region collider meshes `col_head`,
`col_core`, `col_tank`, `col_plate0..5`, `col_cell`, `col_wing_l/r` (loader builds compound
colliders + reduced hulls E14 from `col_*`, never renders them); LOD variants `<name>_lod1..3`
honored over generation; forward = −Z, up = +Y, 1 unit = 1 m (validated at import: bounds
sanity + axis heuristic warning). Per-pack conversion notes (ASCII FBX → E31 handles; OBJ
props fine; any Draco/meshopt-compressed glTF → re-export uncompressed until E32) recorded in
`assets_src/README.md`.

## 4. Procedural fallbacks (`assets/fallbacks.zia`)

Per-archetype capsule/box frames with emissive eye strips + silhouette blockers (16 table);
weapon blocks (box+cylinder assemblies per silhouette); kit pieces (parametric corridor/room
boxes with trim insets); prototype-grid textures (Kenney-style generated `Pixels`: grid +
hue per family); terrain/sky/particles/decals always procedural (ridgebound modules
generalized). Fallback quality bar: **playable and readable, not pretty** — probes and CI-free
verification always run on fallbacks (no downloads on the verification path).

## 5. Materials (`assets/materials.zia`)

PBR library: archetype palettes (per 27 §3 bible), emissive maps for eyes/status lights
(pulse-animated via `set_EmissiveIntensity` curves), Wraith cloak (alpha-blend + scrolling
noise normal + fresnel-ish rim via env map — material anim recipe), Vanguard shield
(additive double-sided), glass, molten (scrolling emissive), monitor surfaces (E20 RT bind),
decal set (scorch/pit/rivet-glow). All via `set_*` property setters (landmine rule).

## 6. VPA packing & shipping (lands P19)

`viper.project` gains: `pack meshes assets_gen/meshes` + `pack-compressed audio assets_src/audio`
+ `pack textures assets_gen/textures` (pre-compressed formats auto-skip). **Build gotcha
(landmine)**: pack directives require single-step `viper build` — `build_demos_*.sh` auto-
detects via the `^(embed|pack|pack-compressed)` grep (`build_demos_mac.sh:193-201` precedent)
— verify ashfall's entry hits that path. `.vpa` beside the binary automounts; loose
`assets_src/` remains the dev-mode source (CWD fallback). Package metadata (icon, name)
per xenoscape precedent.

## 7. CREDITS.md + credits screen

Per-pack rows: name, author, URL, license, version/date downloaded, files used. Mirrored
into the credits scroll (23 §3). Template committed with the first download.

## 8. Probes

Empty-`assets_src` run: full campaign-smoke on fallbacks (THE asset-optional gate);
naming-contract linter probe (walk loaded scenes: sockets/col_* present per archetype table,
axis/scale sanity); alias-table zero-warnings per pack (18 §5); LOD presence post-registry
(every spawned mesh ≥ 3 LODs or explicit static-prop exemption list); VPA round-trip
(packed == loose behavior, from any CWD); CREDITS completeness scan (every loaded non-
procedural path has a row). VM==native.

## 9. Verification gate

Probes green in both modes (empty + downloaded); pack downloads committed with CREDITS;
one dressed archetype (Ranger) swapped end-to-end (download → alias → LODs → sockets →
colliders → in-game) as the pipeline proof before P5 mass-dressing. Full build green.
