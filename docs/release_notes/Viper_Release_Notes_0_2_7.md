# Viper Compiler Platform — Release Notes

> **Development Status:** Pre-Alpha. Viper is under active development and not ready for production use.

## Version 0.2.7 — Pre-Alpha (DRAFT — unreleased)

<!-- DRAFT: release date TBD. v0.2.6 was cut on 2026-06-01. -->

### What this release is about

A short hardening cycle continuing v0.2.6, centered on the Graphics3D runtime and reaching down into the foundation libraries, VM, codegen, the native linker, and CLI tooling. 3D animation, skinning, and asset import now fail closed on malformed input; the open-world ("3D Next Level") gaps close with cell-streaming and navmesh serialization; and the support layer, bytecode VM, IL pipeline, language servers, and packaging are hardened against bad input and untrusted paths.

- **3D animation & skinning fail closed.** Non-finite interpolation and playback inputs clamp across Skeleton3D, AnimController3D, BlendTree3D, IKSolver3D, and the node animator; AnimBlend3D reuses scratch buffers; CPU skinning and morphing skip invalid bone influences and revert bad output to the source vertex; the glTF cubic-spline sampler coerces non-finite curve time.
- **Asset import is contract-checked.** glTF validates morph-target accessors, CUBICSPLINE index math, sampler counts, and skin weights; FBX samples through a clamped, monotonic binary search; Model3D guards scene names and allocations against overflow; `extensionsRequired` accepts only extensions with a working parser.
- **Overflow-safe robustness sweep.** Scene nodes, transforms, raycasts, navigation, physics, terrain, vegetation, water, particles, audio, and Game3D plumbing reject bad handles and non-finite or overflow-prone inputs; vector lengths factor out the largest component so finite coordinates never square to infinity, and `Mesh3D.Transform` takes its determinant in double precision.
- **Reference safety across private slots.** Stored camera, node, character, navmesh, and animator references are class-checked before use or release, so a slot whose memory was reused for another class is nulled rather than dereferenced; the audio-source, texture-atlas, and node-animator tables compact corrupt entries before any read.
- **Scene & streaming residency.** `Scene3D.Save` persists `Mesh3D.Resident` across VSCN round trips, traversals skip wrong-class root/child slots, visibility and portal counters repair before PVS traversal, and `WorldStream3D` rechecks payload sizes against the residency budget.
- **Open-world streaming & navmesh tooling (new).** `WorldStream3D` cells load a binary sidecar into their resident-byte budget; `NavMesh3D.Export`/`Import` round-trip versioned `VNAVMSH2` assets — geometry, traversal costs, areas, off-mesh links, obstacles, and agent params — and still read legacy `VNAVMSH1` geometry, rebuilding query grids on load.
- **Texture-upload budget closure.** Native compressed `TextureAsset3D` uploads in Metal, D3D11, and OpenGL drain a large mip across frames via a per-mip block-row cursor, validate KTX2 mip lengths, and reject unsupported supercompression.
- **Targeted 3D runtime fixes.** Cached Physics3D primitives stay queryable; `World3D.LastCCDRequestedSubsteps` and `Body3D.GetRestitution` honor their documented unclamped / `[0,1]` contracts; Water3D rewrites retained mesh buffers in place and bounds wave phase; Vegetation3D draws double-sided; `Assets3D.ClearCache` leaves the cache intact when publishers don't quiesce in time.
- **API completeness & native asset embedding.** Material3D/Light3D scalar setters (`SetRoughness`/`SetMetallic`/`SetAO`/`SetEmissiveIntensity`/`SetNormalScale`/`SetReflectivity`, `Light3D.SetEnabled`/`SetCastsShadows`) are now bound as methods alongside their property forms; `embed`-ed assets link cleanly into native binaries — a duplicate `viper_asset_blob` definition that had silently dropped them is fixed — and Game3D showcase, controller, and scene runtime regressions are closed.
- **VM fails closed.** Loads, stores, and calls validate every memory range and indirect callee before dereference; branches and switches trap instead of asserting on malformed targets; global initializers parse range-checked; `main`'s return becomes the real process exit code; and a signal-safe interrupt plus a `BytecodeVM.setMaxInstructions` step budget bound runaway programs.
- **Support & IL hardening.** The arena guards size and pointer math against overflow, SmallVector drives element lifetime through the allocator so non-trivial types are safe, SourceManager serializes access behind a mutex with stable disk-path tracking, and diagnostics validate UTF-8; the IL serializer and verifiers reject malformed identifiers, and ModuleLinker intersects extern effect attributes.
- **Codegen & native linker hardening.** x86-64 frame lowering, control-flow lowering, operand/encoding, and register allocation tighten their invariants behind shared frame-layout utilities; the native linker hardens relocation application, symbol resolution, section merging, and PE-image writing.
- **Language servers & CLI tooling hardened.** The BASIC/Zia LSP/MCP servers compute UTF-16 ranges, separate notifications from null-id requests, reject malformed URIs and duplicate JSON keys, and exit non-zero on bad framing; `il-dis` becomes a bytecode disassembler, native-compile temporaries are reserved atomically, source/IL loaders cap at 256 MB, and the ar/zip writers and project loader reject malformed entries and untrusted paths.
- **Coverage & a recorded cull baseline.** New tests close the vegetation, billboard, navmesh round-trip, reference-repair, texture-atlas, and support-library gaps, and the Scene3D spatial index records a ~1,800× indexed-vs-flat cull speedup on a 10k-node fixture.

### By the Numbers

| Metric | v0.2.6 | v0.2.7 | Delta |
|---|---|---|---|
| Commits | — | 15 | +15 |
| Source files | 3,096 | 3,103 | +7 |
| Production SLOC | 669K | 687K | +18K |
| Test SLOC | 278K | 290K | +12K |
| Demo SLOC | 192K | 193K | +1K |

Counts via `scripts/count_sloc.sh` (production 686,645 / test 290,051 / demo 193,137 / source files 3,103); commits since the `v0.2.6-dev` tag.

---

Demos and docs tracked the work: the open-world software baseline was refreshed; the largest asset-import, scene, render, and terrain routines were split into focused in-file helpers with no ABI change; the Graphics3D guides gained the KTX2, glTF-extension, streaming, navmesh-export, and spatial-index entries; and a Doxygen pass covered the Graphics3D/Game3D runtime plus every source file under the tools and support layers.

<!-- END DRAFT -->
