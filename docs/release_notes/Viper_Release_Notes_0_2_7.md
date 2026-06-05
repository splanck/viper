# Viper Compiler Platform — Release Notes

> **Development Status:** Pre-Alpha. Viper is under active development and not ready for production use.

## Version 0.2.7 — Pre-Alpha (DRAFT — unreleased)

<!-- DRAFT: release date TBD. v0.2.6 was cut on 2026-06-01. -->

### What this release is about

A short hardening cycle continuing v0.2.6, centered on the Graphics3D and Game3D runtime and reaching down into the VM, codegen, native linker, and CLI tooling. The headline is a sustained pass that ends per-frame rendering flicker and closes the open-world ("3D Next Level") streaming gaps. Alongside it, 3D animation, skinning, and asset import now fail closed on malformed input, the foundation libraries and bytecode VM harden against untrusted data, and the Windows/MSVC build returns to green.

- **3D animation, skinning & asset import fail closed.** Non-finite interpolation, playback, and bone inputs across Skeleton3D, AnimController3D, BlendTree3D, IKSolver3D, and CPU skinning clamp or revert to the bind pose. glTF and FBX import validate morph, accessor, sampler, and skin-weight counts, and accept only extensions with a working parser.
- **Overflow-safe runtime & reference safety.** Scene, transform, raycast, navigation, physics, terrain, water, particle, and Game3D entry points reject bad handles and non-finite or overflow-prone inputs. Stored camera, node, character, navmesh, and animator references are class-checked before use, so a reused slot is nulled rather than dereferenced.
- **Graphics3D rendering flicker stabilized.** A sustained pass ends per-frame visible-triangle flicker: queue-order-independent occlusion history with covered-streak gating, camera-depth-fitted shadow cascades, per-camera LOD/impostor hysteresis against a union AABB, and conservative terrain-horizon culling with a two-way far-plane fit. Every backend (software/OpenGL/Metal/D3D11) now validates draw-command index ranges and shares one depth-bias scale.
- **New Canvas3D authoring helpers.** `DrawMeshWind` sways foliage with height-weighted per-vertex deformation on every backend, `DrawImage2D` blits a `Pixels` image into the 2D overlay (HUD minimaps via `RenderTarget3D.AsPixels`), and `SetFullscreen`/`ToggleFullscreen`/`IsFullscreen` plus `Game3D.Keys.F11` drive native fullscreen. `DrawMeshSkinned` accepts an `AnimController3D` pose, and `Material3D` gains depth-bias and `ShadowMode` (auto/none/cast) controls.
- **Open-world streaming & navmesh tooling (new).** `WorldStream3D` cells load a binary sidecar into their resident-byte budget, and `NavMesh3D.Export`/`Import` round-trip versioned `VNAVMSH2` assets — geometry, traversal costs, areas, off-mesh links, obstacles, and agent params — while still reading legacy `VNAVMSH1`. Native compressed `TextureAsset3D` uploads drain large mips across frames with KTX2 length validation, and stream manifests resolve `asset://` sidecars through the asset manager.
- **Game3D hardened across threads and capture.** Async model loads snapshot their request into worker-safe fields so the loader never touches runtime strings off-thread, and a shutdown commit queue releases its handle and staged glTF instead of leaking. The world's body and name indices rebuild from the authoritative entity registry on any detected corruption, tree spawn/despawn became roll-back-exact transactions, and `ScreenshotFinal` and render-target capture finalize a frame with no present side effect or leaked post-FX state — with targets reserved against a process-wide budget and keyed on a stable cache identity.
- **API completeness & native asset embedding.** Material3D/Light3D scalar setters (`SetRoughness`/`SetMetallic`/`SetAO`/`SetEmissiveIntensity`/`SetNormalScale`/`SetReflectivity`, `Light3D.SetEnabled`/`SetCastsShadows`) are bound as methods alongside their property forms, and `embed`-ed assets now link into native binaries — fixing a duplicate `viper_asset_blob` definition that had silently dropped them.
- **VM fails closed.** Loads, stores, and calls validate every memory range and indirect callee before dereference, branches and switches trap instead of asserting on malformed targets, and a signal-safe interrupt plus a `BytecodeVM.setMaxInstructions` step budget bound runaway programs.
- **Runtime cleans up on the error path.** Archive, compression, glob, file-extension, and memstream paths release every temporary `Bytes`, string, and sequence when a step traps, and the snapshot helpers behind sorted sets, tree maps, tries, and LRU caches unwind partially built state. Async, concurrent-map, and scheduler completions resolve their futures with an error and drop retained references exactly once under refcount overflow.
- **Support, IL, codegen & native linker hardening.** The arena and SmallVector guard size and pointer math, SourceManager serializes behind a mutex, and the IL serializer and verifiers reject malformed identifiers. x86-64 and AArch64 frame, encoding, and register allocation tighten behind shared frame-layout utilities, closing O1 miscompiles on both targets, and the native linker hardens relocation, symbol resolution, and PE-image writing.
- **Cross-platform utilities, language servers & CLI tooling.** Process, name-mangling, and integer helpers guard against overflow and untrusted input; the BASIC/Zia LSP/MCP servers compute UTF-16 ranges and reject malformed URIs and duplicate JSON keys; `il-dis` becomes a bytecode disassembler; and the ar/zip writers and project loader reject malformed entries and untrusted paths.
- **Windows/MSVC build back to green.** NOMINMAX and extended MSVC atomic shims unblock the 3D runtime under warning-as-error, a simplified CreateProcess stdio path fixes child launches, and the standard-VM bridge stops double-releasing borrowed string temporaries. The D3D11 backend closes its sign-off gaps and now validates native texture metadata before resource creation, clears stale cache slots and unbinds shader resources on replacement, and rebinds the swapchain immediately after resize — restoring the BASIC-VM, installer, ViperIDE, and x86-64 codegen suites.
- **Coverage & a recorded cull baseline.** New tests close the vegetation, billboard, navmesh round-trip, reference-repair, texture-atlas, D3D11 mip-validation, and support-library gaps, and the Scene3D spatial index records a ~1,800× indexed-vs-flat cull speedup on a 10k-node fixture.

### By the Numbers

| Metric | v0.2.6 | v0.2.7 | Delta |
|---|---|---|---|
| Commits | — | 26 | +26 |
| Source files | 3,096 | 3,103 | +7 |
| Production SLOC | 669K | 695K | +26K |
| Test SLOC | 278K | 292K | +14K |
| Demo SLOC | 192K | 193K | +1K |

Counts via `scripts/count_sloc.sh` (production 694,583 / test 291,691 / demo 193,434 / source files 3,103); commits since the `v0.2.6-dev` tag.

---

Demos and docs tracked the work: the `game3d-showcase` gained an F11 fullscreen toggle, wind-swayed foliage, a render-to-image minimap, physics props, and wandering sentinels; the open-world software baseline was refreshed; and the Graphics3D guides picked up KTX2, glTF-extension, streaming, navmesh-export, spatial-index, and window/image/foliage/skinned-draw entries.

<!-- END DRAFT -->
