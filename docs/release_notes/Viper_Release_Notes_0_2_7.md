# Viper Compiler Platform — Release Notes

> **Development Status:** Pre-Alpha. Viper is under active development and not ready for production use.

## Version 0.2.7 — Pre-Alpha (DRAFT — unreleased)

<!-- DRAFT: release date TBD. v0.2.6 was cut on 2026-06-01. -->

### What this release is about

A short hardening cycle continuing v0.2.6, centered on the Graphics3D and Game3D runtime and reaching down into the foundation libraries, VM, codegen, the native linker, and CLI tooling. 3D animation, skinning, and asset import now fail closed on malformed input; a sustained pass removes per-frame rendering flicker and adds new Canvas3D authoring helpers; the open-world ("3D Next Level") gaps close with cell streaming and navmesh serialization; the support layer, bytecode VM, IL pipeline, language servers, and packaging are hardened against bad input and untrusted paths; and the Windows/MSVC build returns to green.

- **3D animation, skinning & asset import fail closed.** Non-finite interpolation, playback, and bone inputs clamp or revert to the bind pose across Skeleton3D, AnimController3D, BlendTree3D, IKSolver3D, and CPU skinning/morphing; glTF and FBX import validate morph, accessor, sampler, and skin-weight counts and accept only extensions with a working parser.
- **Overflow-safe runtime & reference safety.** Scene, transform, raycast, navigation, physics, terrain, vegetation, water, particle, audio, and Game3D entry points reject bad handles and non-finite or overflow-prone inputs, and stored camera, node, character, navmesh, and animator references are class-checked before use — a reused slot is nulled rather than dereferenced.
- **Graphics3D rendering flicker stabilized.** A sustained pass ends per-frame visible-triangle flicker: queue-order-independent occlusion history with covered-streak gating that invalidates on camera cuts, camera-depth-fitted shadow cascades, stable deferred sort identities, per-camera LOD/impostor hysteresis against a union AABB, and conservative terrain-horizon culling; every backend (software/OpenGL/Metal/D3D11) now validates draw-command index ranges and shares one depth-bias scale.
- **New Canvas3D authoring helpers.** `DrawMeshWind` sways foliage with a height-weighted per-vertex deformation on every backend; `DrawImage2D` blits a `Pixels` image into the 2D overlay (HUD minimaps via `RenderTarget3D.AsPixels`); `SetFullscreen`/`ToggleFullscreen`/`IsFullscreen` and `Game3D.Keys.F11` drive native fullscreen; `DrawMeshSkinned` accepts an `AnimController3D` pose; and `Material3D` gains depth-bias and `ShadowMode` (auto/none/cast) controls.
- **Open-world streaming & navmesh tooling (new).** `WorldStream3D` cells load a binary sidecar into their resident-byte budget; `NavMesh3D.Export`/`Import` round-trip versioned `VNAVMSH2` assets — geometry, traversal costs, areas, off-mesh links, obstacles, and agent params — while still reading legacy `VNAVMSH1`; and native compressed `TextureAsset3D` uploads drain large mips across frames with KTX2 length validation.
- **API completeness & native asset embedding.** Material3D/Light3D scalar setters (`SetRoughness`/`SetMetallic`/`SetAO`/`SetEmissiveIntensity`/`SetNormalScale`/`SetReflectivity`, `Light3D.SetEnabled`/`SetCastsShadows`) are bound as methods alongside their property forms, and `embed`-ed assets now link into native binaries — a duplicate `viper_asset_blob` definition that had silently dropped them is fixed.
- **VM fails closed.** Loads, stores, and calls validate every memory range and indirect callee before dereference; branches and switches trap instead of asserting on malformed targets; global initializers parse range-checked; and a signal-safe interrupt plus a `BytecodeVM.setMaxInstructions` step budget bound runaway programs.
- **Runtime cleans up on the error path.** Archive, compression, glob, file-extension, binary-buffer, and memstream paths release every temporary `Bytes`, string, sequence, and normalized path when a step traps, and the snapshot helpers behind sorted sets, tree maps, tries, maps, sets, LRU caches, and bags unwind partially built state; async, concurrent-map, and scheduler completions resolve their futures with an error and drop retained references exactly once under refcount overflow.
- **Support, IL, codegen & native linker hardening.** The arena and SmallVector guard size/pointer math and element lifetime, SourceManager serializes behind a mutex, and the IL serializer and verifiers reject malformed identifiers; x86-64 and AArch64 frame, encoding, and register allocation tighten their invariants behind shared frame-layout utilities, closing O1 miscompiles on both targets; and the native linker hardens relocation, symbol resolution, section merging, and PE-image writing.
- **Cross-platform utilities, language servers & CLI tooling.** Process, name-mangling, and integer helpers guard against overflow and untrusted input; the BASIC/Zia LSP/MCP servers compute UTF-16 ranges and reject malformed URIs and duplicate JSON keys; `il-dis` becomes a bytecode disassembler; and the ar/zip writers and project loader reject malformed entries and untrusted paths.
- **Windows/MSVC build back to green.** NOMINMAX and extended MSVC atomic shims unblock the 3D runtime under warning-as-error, a simplified CreateProcess stdio path fixes child launches, the D3D11 backend sign-off gaps close, and the standard-VM bridge stops double-releasing borrowed string temporaries — restoring the BASIC-VM, installer, ViperIDE, and x86-64 codegen suites.
- **Coverage & a recorded cull baseline.** New tests close the vegetation, billboard, navmesh round-trip, reference-repair, texture-atlas, and support-library gaps, and the Scene3D spatial index records a ~1,800× indexed-vs-flat cull speedup on a 10k-node fixture.

### By the Numbers

| Metric | v0.2.6 | v0.2.7 | Delta |
|---|---|---|---|
| Commits | — | 24 | +24 |
| Source files | 3,096 | 3,103 | +7 |
| Production SLOC | 669K | 692K | +23K |
| Test SLOC | 278K | 291K | +13K |
| Demo SLOC | 192K | 193K | +1K |

Counts via `scripts/count_sloc.sh` (production 692,362 / test 290,622 / demo 193,434 / source files 3,103); commits since the `v0.2.6-dev` tag.

---

Demos and docs tracked the work: the `game3d-showcase` gained an F11 fullscreen toggle, wind-swayed foliage, a render-to-image minimap, physics props, and wandering sentinels; the open-world software baseline was refreshed; and the Graphics3D guides picked up KTX2, glTF-extension, streaming, navmesh-export, spatial-index, and window/image/foliage/skinned-draw entries.

<!-- END DRAFT -->
