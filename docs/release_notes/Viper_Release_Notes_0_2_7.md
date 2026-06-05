# Viper Compiler Platform — Release Notes

> **Development Status:** Pre-Alpha. Viper is under active development and not ready for production use.

## Version 0.2.7 — Pre-Alpha (DRAFT — unreleased)

<!-- DRAFT: release date TBD. v0.2.6 was cut on 2026-06-01. -->

### What this release is about

A short hardening cycle continuing v0.2.6, centered on the Graphics3D and Game3D runtime and reaching down into the VM, codegen, native linker, and CLI tooling. The headline is a sustained pass that ends per-frame rendering flicker and closes the open-world ("3D Next Level") streaming gaps, joined by node-animation playback and a substantially upgraded glTF/FBX model importer. Alongside the new capability, 3D animation, skinning, and import fail closed on malformed input, the foundation libraries and bytecode VM harden against untrusted data, and the Windows/MSVC build returns to green.

- **Graphics3D rendering flicker stabilized.** A sustained pass ends per-frame visible-triangle flicker: queue-order-independent occlusion history with covered-streak gating, camera-depth-fitted shadow cascades, per-camera LOD/impostor hysteresis against a union AABB, and conservative terrain-horizon culling with a two-way far-plane fit. Every backend (software/OpenGL/Metal/D3D11) validates draw-command index ranges and shares one depth-bias scale.
- **glTF/FBX import & node animation (new).** Imported models expose node, object, camera, and morph-weight clips through new `NodeAnimation3D`/`NodeAnimator3D` types, and `Model3D`/`Assets3D` can pull a single clip from an external file (`LoadAnimation`/`LoadNodeAnimation`). The FBX importer gains full model transforms (rotation order, pivots, Z-up), cubic animation curves, PBR texture-slot routing, and async loading, while glTF accessor and skin-weight validation tightens.
- **New Canvas3D authoring helpers.** `DrawMeshWind` sways foliage with height-weighted per-vertex deformation on every backend, `DrawImage2D` blits a `Pixels` image into the 2D overlay (HUD minimaps via `RenderTarget3D.AsPixels`), and `SetFullscreen`/`ToggleFullscreen`/`IsFullscreen` plus `Game3D.Keys.F11` drive native fullscreen. `DrawMeshSkinned` accepts an `AnimController3D` pose, and `Material3D` gains depth-bias and `ShadowMode` (auto/none/cast) controls.
- **Open-world streaming & navmesh tooling (new).** `WorldStream3D` cells load a binary sidecar into their resident-byte budget, and `NavMesh3D.Export`/`Import` round-trip versioned `VNAVMSH2` assets — geometry, traversal costs, areas, off-mesh links, obstacles, and agent params — while still reading legacy `VNAVMSH1`. Native compressed `TextureAsset3D` uploads drain large mips across frames with KTX2 length validation, and stream manifests resolve `asset://` sidecars through the asset manager.
- **3D runtime fails closed.** Non-finite interpolation, playback, and bone inputs across Skeleton3D, AnimController3D, BlendTree3D, IKSolver3D, and CPU skinning clamp or revert to the bind pose, and scene, transform, raycast, navigation, physics, terrain, water, particle, and Game3D entry points reject bad handles and overflow-prone inputs. Stored camera, node, character, navmesh, and animator references are class-checked before use, so a reused slot is nulled rather than dereferenced.
- **Game3D hardened across threads and capture.** Async model loads read worker-safe request snapshots (never touching runtime strings off-thread) and free staged payloads on shutdown; the world's body/name indices rebuild from the entity registry on detected corruption; tree spawn/despawn are roll-back-exact; and `ScreenshotFinal`/render-target capture finalize a frame with no present side effect, leaked post-FX state, or stale-identity cache hit, with targets reserved against a process-wide budget.
- **Foundation hardening (support, VM, IL, codegen, linker).** The bytecode VM validates memory ranges and indirect callees before dereference, traps instead of asserting on bad branch/switch targets, and bounds runaway programs via `BytecodeVM.setMaxInstructions` and a signal-safe interrupt. Runtime error paths release temporaries on trap, the arena and SmallVector guard size/pointer math, x86-64/AArch64 register allocation and encoding tighten behind shared frame layout (closing O1 miscompiles), and the native linker hardens relocation, symbol resolution, and PE-image writing.
- **Cross-platform utilities, language servers & CLI tooling.** Process, name-mangling, and integer helpers guard against overflow and untrusted input; the BASIC/Zia LSP/MCP servers compute UTF-16 ranges and reject malformed URIs and duplicate JSON keys; `il-dis` becomes a bytecode disassembler; and the ar/zip writers and project loader reject malformed entries and untrusted paths.
- **API completeness & native asset embedding.** Material3D/Light3D scalar setters (`SetRoughness`/`SetMetallic`/`SetAO`/`SetEmissiveIntensity`/`SetNormalScale`/`SetReflectivity`, `Light3D.SetEnabled`/`SetCastsShadows`) bind as methods alongside their property forms, and `embed`-ed assets now link into native binaries — fixing a duplicate `viper_asset_blob` definition that had silently dropped them.
- **Windows/MSVC build back to green.** NOMINMAX, extended MSVC atomic shims, a simplified CreateProcess stdio path, and a standard-VM bridge that no longer double-releases borrowed strings restore the BASIC-VM, installer, ViperIDE, and x86-64 codegen suites. The D3D11 backend closes its sign-off gaps, validating native texture metadata before resource creation, clearing stale cache slots on replacement, and rebinding the swapchain immediately after resize.
- **Coverage & a recorded cull baseline.** New tests close the vegetation, billboard, navmesh round-trip, reference-repair, texture-atlas, FBX/node-animation import, D3D11 mip-validation, and support-library gaps, and the Scene3D spatial index records a ~1,800× indexed-vs-flat cull speedup on a 10k-node fixture.

### By the Numbers

| Metric | v0.2.6 | v0.2.7 | Delta |
|---|---|---|---|
| Commits | — | 27 | +27 |
| Source files | 3,096 | 3,103 | +7 |
| Production SLOC | 669K | 695K | +26K |
| Test SLOC | 278K | 292K | +14K |
| Demo SLOC | 192K | 193K | +1K |

Counts via `scripts/count_sloc.sh` (production 695,031 / test 291,921 / demo 193,434 / source files 3,103); commits since the v0.2.6 release (2026-06-01).

---

Demos and docs tracked the work: the `game3d-showcase` gained an F11 fullscreen toggle, wind-swayed foliage, a render-to-image minimap, physics props, and wandering sentinels; the open-world software baseline was refreshed; and the Graphics3D guides picked up KTX2, glTF-extension, streaming, navmesh-export, spatial-index, and window/image/foliage/skinned-draw entries.

<!-- END DRAFT -->
