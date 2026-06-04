# Viper Compiler Platform — Release Notes

> **Development Status:** Pre-Alpha. Viper is under active development and not ready for production use.

## Version 0.2.7 — Pre-Alpha (DRAFT — unreleased)

<!-- DRAFT: release date TBD. v0.2.6 was cut on 2026-06-01. -->

### What this release is about

A short hardening cycle continuing v0.2.6, centered on the Graphics3D runtime and reaching down into the foundation libraries, VM, codegen, the native linker, and CLI tooling. 3D animation, skinning, and asset import now fail closed on malformed input; the open-world ("3D Next Level") gaps close with cell-streaming and navmesh serialization; and the support layer, bytecode VM, IL pipeline, language servers, and packaging are hardened against bad input and untrusted paths.

- **3D animation, skinning & asset import fail closed.** Non-finite interpolation, playback, and bone inputs clamp or revert to the bind pose across Skeleton3D, AnimController3D, BlendTree3D, IKSolver3D, and CPU skinning/morphing; glTF and FBX import validate morph and accessor math, sampler and skin-weight counts, and curve times, and accept only extensions with a working parser.
- **Overflow-safe runtime & reference safety.** Scene nodes, transforms, raycasts, navigation, physics, terrain, vegetation, water, particles, audio, and Game3D plumbing reject bad handles and non-finite or overflow-prone inputs — vector lengths factor out the largest component and `Mesh3D.Transform` takes a double-precision determinant — and stored camera, node, character, navmesh, and animator references are class-checked before use, so a reused slot is nulled rather than dereferenced.
- **Graphics3D & Game3D rendering stabilized.** Culling and frame-state fixes remove flicker, `Scene3D.Save` persists `Mesh3D.Resident` across VSCN round trips, traversals skip wrong-class slots and repair visibility/portal counters before PVS, and Game3D showcase, controller, and scene runtime regressions are closed.
- **Open-world streaming & navmesh tooling (new).** `WorldStream3D` cells load a binary sidecar into their resident-byte budget; `NavMesh3D.Export`/`Import` round-trip versioned `VNAVMSH2` assets — geometry, traversal costs, areas, off-mesh links, obstacles, and agent params — while still reading legacy `VNAVMSH1`; and native compressed `TextureAsset3D` uploads drain large mips across frames under a per-mip cursor with KTX2 length validation.
- **API completeness & native asset embedding.** Material3D/Light3D scalar setters (`SetRoughness`/`SetMetallic`/`SetAO`/`SetEmissiveIntensity`/`SetNormalScale`/`SetReflectivity`, `Light3D.SetEnabled`/`SetCastsShadows`) are bound as methods alongside their property forms, and `embed`-ed assets now link into native binaries — a duplicate `viper_asset_blob` definition that had silently dropped them is fixed.
- **VM fails closed.** Loads, stores, and calls validate every memory range and indirect callee before dereference; branches and switches trap instead of asserting on malformed targets; global initializers parse range-checked; and a signal-safe interrupt plus a `BytecodeVM.setMaxInstructions` step budget bound runaway programs.
- **Support, IL, codegen & native linker hardening.** The arena and SmallVector guard size/pointer math and element lifetime, SourceManager serializes behind a mutex, and the IL serializer and verifiers reject malformed identifiers; x86-64 frame, control-flow, encoding, and register allocation tighten their invariants behind shared frame-layout utilities (closing an O1 miscompile), and the native linker hardens relocation, symbol resolution, section merging, and PE-image writing.
- **Cross-platform utilities, language servers & CLI tooling hardened.** Process, name-mangling, and integer helpers guard against overflow and untrusted input; the BASIC/Zia LSP/MCP servers compute UTF-16 ranges and reject malformed URIs and duplicate JSON keys; `il-dis` becomes a bytecode disassembler; and the ar/zip writers and project loader reject malformed entries and untrusted paths.
- **Coverage & a recorded cull baseline.** New tests close the vegetation, billboard, navmesh round-trip, reference-repair, texture-atlas, and support-library gaps, and the Scene3D spatial index records a ~1,800× indexed-vs-flat cull speedup on a 10k-node fixture.

### By the Numbers

| Metric | v0.2.6 | v0.2.7 | Delta |
|---|---|---|---|
| Commits | — | 16 | +16 |
| Source files | 3,096 | 3,103 | +7 |
| Production SLOC | 669K | 688K | +19K |
| Test SLOC | 278K | 290K | +12K |
| Demo SLOC | 192K | 193K | +1K |

Counts via `scripts/count_sloc.sh` (production 688,317 / test 290,114 / demo 193,144 / source files 3,103); commits since the `v0.2.6-dev` tag.

---

Demos and docs tracked the work: the open-world software baseline was refreshed and the largest asset-import, scene, render, and terrain routines split into focused helpers with no ABI change; the Graphics3D guides gained KTX2, glTF-extension, streaming, navmesh-export, and spatial-index entries, and a Doxygen pass covered the 3D runtime plus the tools and support layers.

<!-- END DRAFT -->
