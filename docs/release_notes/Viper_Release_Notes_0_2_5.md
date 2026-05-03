# Viper Compiler Platform — Release Notes

> **Development Status:** Pre-Alpha. Viper is under active development and not ready for production use.

## Version 0.2.5 — Pre-Alpha (DRAFT — unreleased)

<!-- DRAFT: release date TBD. v0.2.4 was cut on 2026-04-13. -->

### What this release is about

A hardening-and-polish cycle with several notable new capabilities.

- **Hardening across every runtime subsystem.** Integer-overflow, handle-validation, timeout-clamping, and lifetime-correctness passes applied to graphics, text, threads, audio, I/O, and network.
- **2D graphics class expansion.** ~40 classes on top of `Pixels` / `Canvas`: offscreen surfaces, materials, shaders, post-effects, viewport scaling, tile layers, path drawing, text layout, nine-slice UI, animation, and a render-pass graph.
- **Full 3D asset pipeline.** glTF and FBX import real skeletons, per-vertex skinning, and animations including sparse-accessor morph deltas and the full KHR extension suite.
- **Network stack became a platform.** TLS-backed `HttpsServer` + `WssServer`, from-scratch HTTP/2, native RSA, in-tree X.509 validator, cookie jar, streaming downloads, and connection pooling.
- **Viper.Localization.* (new).** Eleven-class namespace for locale-aware number/date/time/list formatting, translation catalogs, CLDR plural selection, and text-direction utilities. Zero external dependencies.
- **Structured diagnostics & tooling.** Source-location snippet printing with caret underlines, JSON diagnostic output, `--diagnostic-format`, `--strict-diagnostics`, `--pass-stats`, and `--fast-link` CLI flags; VM step-over/step-out debugger; typo suggestions on undefined identifiers.
- **Compiler throughput.** Parallel IL optimizer and codegen (hardware-concurrency thread pool), zero-copy codegen pipeline, analytical AArch64 instruction sizing, bitset-backed liveness, fast-link mode. `viper run` skips optimization by default for fast script invocations.

The biggest user-visible new thing is a text-mode baseball-franchise simulator.

### By the Numbers

| Metric | v0.2.4 | v0.2.5 | Delta |
|---|---|---|---|
| Commits | — | 118 | +118 |
| Source files | 2,869 | 2,989 | +120 |
| Production SLOC | 450K | 537K | +87K |
| Test SLOC | 183K | 222K | +39K |
| Demo SLOC | 177K | 188K | +11K |

Counts via `scripts/count_sloc.sh`.

---

### Audio

- WAV decoder expanded to 8/16/24/32-bit PCM and float32; all resample-and-alloc sites guard against overflow and zero/negative frame counts.
- ALSA write loop handles partial writes and underrun recovery; Linux shutdown no longer hangs waiting on a blocking write; mixer callbacks use a blocking mutex lock (was trylock, causing audible silence during seek/load).
- Platform audio clocks use widened arithmetic to prevent long-uptime overflow; streaming buffers preserve the old allocation before growing; `Music.Seek` stops and clears the stream on source failure instead of leaving stale audio buffered.

### GUI Library

Seven rounds of widget audits plus an app-registry overhaul.

- **New capabilities** — BMP image loading (pure-C, no deps); macOS PNG/JPEG/TIFF via CoreGraphics/ImageIO; POSIX regex search in FindReplaceBar; keyboard nav, undo/redo, multi-select, and typeahead across all interactive widgets.
- **Widget lifetime** — Tombstone/retire pattern (magic tags + live predicates) for TreeView nodes, ListBox items, and all widget handles prevents use-after-remove; focused/captured/hovered widget pointers cleared when a subtree is detached; drag-and-drop strings freed on destroy; 64-bit widget IDs prevent wrap in long sessions.
- **Event correctness** — `CLICK`/`DOUBLE_CLICK` fires after the widget's own `MOUSE_UP` handler; `WasClicked()` reports the click-receiving widget, not the hit-test target; modal input capture releases when a captured widget is outside the active modal; dynamic focus list replaces the 512-entry fixed cap.
- **TrueType rendering** — All table reads bounds-checked; composite glyph point-index alignment implemented; per-contour edge wrapping fixed (was producing phantom fill regions in glyphs like 'O' and '8'); UTF-8 decoder rejects overlong, surrogate, and out-of-range sequences.
- **Layout & constraints** — NaN/inf rejected on all constraint, margin, padding, flex, spacing, and grid-track setters; Grid/Dock purge stale placement metadata when a child is removed so re-adding does not inherit stale position; `vg_widget_apply_constraints()` unified across all 12 widget measure functions.
- **Widget state fixes** — Checkbox `set_indeterminate()` getter+setter; RadioButton group `selected_index` accuracy and deselect callbacks; ProgressBar `set_style()`/`show_percentage()` new APIs and NaN guards; TextInput paste emits a single `on_change`; strdup OOM guards (copy-then-free) across all text-setting paths; Spinner font-size NaN guard.

### Graphics runtime (2D)

- **2D graphics class expansion** — CPU-backed rendering, effects, tilemap/layer, shape/text/UI, viewport, animation, collision, and import-helper classes including RenderTarget2D, Texture2D, Renderer2D, Shader2D, PostProcess2D, TileSet2D/TileLayer2D, Path2D, TextRenderer2D, NineSlice2D, Viewport2D, and Aseprite/TexturePacker/TiledMap helpers.
- Overflow hardening across pixel/tile allocation, PNG/BMP stride math, blit/draw coordinates, flood-fill, and blend compositing.

### Graphics3D

- glTF/FBX import with real skeletons, per-vertex skinning, sparse-accessor morph deltas, and full KHR extension suite (texture transform, emissive strength, unlit, punctual lights).
- Material3D: 6 independent texture slots with UV-set/UV-transform; TEXCOORD_1 end-to-end; HDR `RenderTarget3D`; backend capability introspection; dynamic-light cap 8→16; bone limit 128→256.
- Physics3D: bodies/contacts/joints grow on demand; CCD substeps 8→64; broadphase moves toward O(N log N).
- Correctness: animation keyframes sorted; spot-light inner/outer cones enforced; mid-frame render-target rebinding rejected; NaN/inf sanitized on Camera3D/Light3D/PostFX3D setters.

### Game runtime

- Entity tile sweep preserves centipixel remainders; Physics2D exposes `CircleBody` and per-step contacts; Pathfinder rejects oversized grids.
- Async UAF fix: worker VMs retain the Future payload past worker unwind, pinned by a 25× regression loop.
- Dialogue rewritten against real BitmapFont measurement with proper UTF-8 wrap and codepoint boundaries.
- RNG debiasing via rejection sampling eliminates modulo bias in `rt_rand_int`/`rt_rand_range`.

### Collections runtime

- All string-keyed collections compare full byte-length keys; embedded NULs no longer truncate identity or lookups (Map, MultiMap, CountMap, LruCache, TreeMap, FrozenMap/Set, SortedSet, WeakMap).
- Ownership discipline: retained values released on overwrite, removal, clear, eviction, and finalization across Seq, Deque, List, MultiMap, SparseArray, OrderedMap, and TreeMap.
- WeakMap stores zeroing handles; `Compact` removes stale slots; overflow hardening on all growth paths, hash-table resize, and BloomFilter/BinaryBuffer sizing.

### IO runtime

- All destructive write paths (`WriteAllText`, `WriteAllBytes`, Archive, SaveData) write to temp sidecars and atomically replace the destination; failed writes clean up.
- Archive: CRC/size/bounds validation; symlink traversal and embedded-NUL names rejected; `Create` no longer truncates before `Finish`.
- GZIP/DEFLATE reject malformed headers, bad checksums, and trailing data after the final block.
- `SaveData.Load` treats missing files as empty-success; `Config.Load` soft-fails on missing configs.

### Localization runtime (new)

Eleven-class `Viper.Localization.*` namespace. Zero external dependencies; en-US built in, all other locales load from JSON at runtime.

- **Locale / LocaleInfo / LocaleManager** — BCP-47 parsing, fallback chains, rwlock-guarded process-global registry, system-locale detection on all three platforms.
- **NumberFormat / DateFormat** — CLDR-compliant format + parse with six rounding modes; locale digits (Arabic-Indic, Devanagari, Thai…); canonical Short/Medium/Long/Full/DateTime styles.
- **RelativeTimeFormat / MessageBundle / PluralRules** — CLDR cardinal + ordinal rules, auto unit selection, named/positional placeholders.
- **Collator / ListFormat / TextDirection** — DUCET-lite collation with sv-SE tailoring; strong-RTL classifier for Hebrew/Arabic/Syriac/Thaana/N'Ko.

### Core runtime

- `rt_fmt` rewritten for locale isolation — float/currency/percent route through a C-locale-scoped `snprintf` so output is deterministic regardless of process `setlocale` state.
- GC shutdown fixed: tracking and weak-reference tables detach under the GC lock before being freed; shutdown no longer allocates when no weak refs exist.
- Time runtime: overflow-checked arithmetic via shared helpers across all DateTime/Duration/Stopwatch/Countdown modules; `Duration.Abs(INT64_MIN)` and `Neg(INT64_MIN)` now trap.
- Text runtime: JSON streaming adds a per-depth state machine; `Version.Parse` is strict SemVer 2.0.0; StringBuilder/TOML/CSV/Scanner/Template/Codec/GUID all received length/depth guards.
- Threads runtime: timeout/deadline math clamps throughout (fixes a ~49-day Win32 hang); Thread/SafeThread carry magic tags; Gate/Barrier/RwLock gain `closing`/`cancelled` flags; VM traps in async trampolines surface as Future errors.

### Network

- **New** — TLS-backed `HttpsServer` + `WssServer` with EC/RSA cert loading, SNI, and ALPN; HTTP/2 transport with in-tree HPACK + Huffman; native RSA; in-tree X.509 chain validator with EKU (drops `Security.framework`).
- HTTP client: RFC cookie jar, transparent gzip, streaming download, keep-alive + connection pooling, CRLF injection guards.
- TLS/Crypto: PBKDF2 iterations 100K→300K; P-384 ECDSA removed from ClientHello (was advertised but never implemented, creating a silent verification gap).

### BASIC frontend

- `SemanticAnalyzer::Type::ArrayObject` for `DIM arr(N) AS ClassName`; typed method dispatch via `scoreArgMatch`; case-insensitive OopIndex fallback.
- `mustReturn` rewritten as data-flow through IF/SELECT/TRY/CATCH/FINALLY/loops; partial branches rejected; top-level `RETURN` without GOSUB emits B1008.
- `lowerUsingStmt` rewritten with an EH handler block for correct DESTROY + rethrow on the exception exit path.
- `analyzeClassDecl` walks constructor, destructor, and method bodies under proper class-member scope; `analyzeMemberAssignment` emits typed diagnostics B2001/B2002.

### Zia frontend

- **New** — flow-narrowing on dotted field-access paths (`self.child`); `Optional` postfix-try (`?`); `async Future[T]` payload typing; SSA temporaries carry source-level names in IR dumps.
- Sema: `Map.get` returns `Optional<V>`; `finalFields_` writable only during `init()`; multi-arg generics; collection literal element-type checking.
- Lowerer: range `.rev()`/`.step(n)` chains; overflow-checking opcodes unconditionally; typed EH catch rethrow preserves original error token.
- Safety warnings (missing return, division-by-zero, uninitialized variable, optional-without-check, non-exhaustive match) promoted to errors by default; `--no-strict-diagnostics` restores warning mode.

### Diagnostics & tooling

- `Diagnostic` gains `range`, `stage`, `help`, and fix-it fields; source-snippet printing with caret/tilde underlines; JSON formatter via `--diagnostic-format=json`.
- `Verifier::verifyAll` collects all violations with deduplication instead of stopping at the first error; `BytecodeCompiler::compileChecked` returns `Expected<BytecodeModule>` with verifier preflight.
- VM debugger: step-over and step-out via call-stack depth tracking; trap messages enriched with file/line/column.
- Zia undefined-identifier errors include Levenshtein-distance typo suggestions with fix-it ranges; stable `V-ZIA-*` diagnostic codes across lex/parse/sema/lowerer.

### Compiler, IL & codegen

**IL surface and VM**
- Variadic functions end-to-end (`Function::isVarArg`, `...` syntax, arity enforcement).
- BytecodeVM: `LOAD_GLOBAL_ADDR` for global variables; six array fast-path opcodes (bounds-check-free after static elimination); trusted dispatch mode for verified modules; `call.indirect` now requires an explicit `[ret(params)]` signature annotation.
- `Alloca` reclassified as `MemoryEffects::Write`; `ConstStr` reclassified with observable side effects — both close optimizer holes where LICM/DCE could incorrectly eliminate stack allocations and string constants.

**IL verifier**
- `computeStackDerivedTemps` fixpoint traces alloca → GEP chains → brArgs to find all stack-derived temporaries and correctly exempt them from pure/readonly/nothrow violations.
- `Verifier::verifyAll` replaces single-error-stop; GEP bounds-checking; array retain/release tracking; `GlobalVerifier` validates type/linkage/initializer syntax.
- Parallel-edge correctness: CFG predecessor/successor caches preserve duplicate edges for `cbr`/`switch` targeting the same block twice.

**IL optimizer**
- Four new O2 passes: `Devirtualize` (indirect→direct call via vtable/alloca tracing), `OwnershipOpt` (removes provably redundant retain/release pairs), `ArrayFastPathOpt` (replaces bounds-checked array ops after static elimination), `RuntimeFastPathOpt` (bypasses dynamic kind dispatch for proven heap objects).
- Thirteen function passes run concurrently by default via atomic work-stealing thread pools; CFG successor/predecessor accessors return cached references (eliminates per-call vector copies across all passes).
- `Mem2Reg`, full LICM, and full peephole promoted to canonical O1/O2 pipelines; CheckOpt demotes checked div/rem when the divisor is statically nonzero; MemorySSA DFS rewritten as memoized recursive with cycle detection.

**Linker hardening**
- All four object-file readers (ELF, COFF, Mach-O, Archive) and all three writers received a bounds-checking and correctness pass; every buffer access goes through `checkedRange`.
- COFF ARM64 addend decoding fixed for instruction bit-fields (Branch26/19, PageRel21, PageOff12); ELF extended section counts from SH0; `SectionMerger` stable sort by priority for `.init_array`/`.fini_array`.
- `BranchTrampoline` reuse keyed by address with collision-free name generation; `DeadStripPass` seeds weak-external fallback targets live during initial root seeding.

**AArch64 codegen**
- New: `LegalizePass` (overflow pseudo expansion, `@main` runtime init insertion, leaf-flag refresh from post-legalized MIR); `PreRegAllocOpt` (copy propagation and redundant-move elimination before register pressure is frozen); parallel lowering and regalloc.
- Loop optimizer: full iterative dominator set prevents hoisting across backward-join edges; call-clobber guard; STP pair-store recognition; iterates to fixed point.
- Phi-spill cross-block fix (dominated successors see the promoted value); CFG-aware DCE with full function-wide liveness convergence; analytical instruction sizing replaces dry-run encoder.
- Windows ARM64: COFF ARM64 relocation constants, SEH stubs, `ExitProcess` startup import; build scripts default to host architecture with `--arch`/`VIPER_DEMO_ARCH` override.

**x86-64 codegen**
- Post-RA list scheduler with latency table; `MemoryOpt` peephole: frame-slot store forwarding and dead store elimination before DCE.
- `OperandRoles` unified across DCE, allocator, and liveness (replaces three diverged switch tables); XMM-select fallthrough fix; DCE seeds callee-saved registers as live at exit.
- Checked unsigned div/rem opcodes added; parallel regalloc and peephole via atomic work-stealing.

**Compiler pipeline & build**
- Zero-copy codegen: verified `il::core::Module` passed directly to the backend, skipping the IL serialize → temp file → re-parse → re-verify round-trip.
- `PipelineExecutor` skips cleanup passes (DCE, SimplifyCFG, late-cleanup) when no upstream pass has mutated IR; `PassManager` constructor no longer forces `verifyBetweenPasses`.
- `--time-compile` instruments all 9 compilation phases; `--pass-stats` reports per-pass statistics; `--fast-link` skips non-essential link-time reductions (auto-enabled for -O0); `--paranoid-verify` restores all frontend verifier checkpoints; `--debug-lines`/`--no-debug-lines` control debug-line emission (default off).
- Build profiles (`debug`/`balanced`/`release`) map to -O0/-O1/-O2; project manifests accept a `profile` directive; `viper run` defaults to debug/O0 for fast script invocations while respecting explicit project profiles.

### Platform

- macOS: premultiplied RGBA from CoreGraphics correctly unpremultiplied before storage; bare arrow keys no longer map to PageUp/Down/Home/End; `mach_absolute_time` conversion widened to prevent long-uptime overflow.
- Linux: X11 clipboard upgraded to full ICCCM CLIPBOARD selection protocol; UTF-8 text input validates every codepoint; XDND URI parsing enforces byte-length bounds; RGBA swizzle reads channel masks from the active X11 Visual instead of assuming layout.
- All platforms: `vgfx_cls()` fill and framebuffer presentation rewritten as byte-wise writes (eliminates strict-aliasing UB); Cohen-Sutherland line clipper pre-clips extreme off-screen coordinates; Bresenham error accumulator widened to int64; event queue protected by atomic spinlock.

### Tests

- ~20K new lines of test coverage: Canvas3D production harness, async 25-iteration race loop, 2D-graphics contract suites, overflow-boundary tests for every hardened parser, and human-manager baseball probes.
- GUI: 40+ new regression tests in `test_vg_audit_fixes.c` covering rounds 3–7 (layout constraints, image loading, widget lifecycle, regex search, modal routing, textinput undo, font bounds, composite glyphs, grid/dock metadata cleanup, radiobutton callbacks, checkbox indeterminate state, textinput single-change paste).
- IL, compiler, and audio: optimizer unit tests for DSE/LICM/LoopRotate/LoopUnroll/IndVarSimplify/MemorySSA/ValueKey; verifier golden fixtures for alloca-load/store escape; PCM overflow, ALSA simulation, and WAV format tests; Localization fuzzing harnesses (gated on `VIPER_ENABLE_FUZZ`).

### Demos & docs

Demos: human-manager baseball franchise simulator (new), Crackman rewrite, two new 3D demos, Paint gains layers + undo, ViperIDE file-watcher and context-menu fixes, XENOSCAPE boss/player fixes, Chess drag-vs-click detection fix, three localization examples, Windows ARM64 coverage for 3D demos. Docs: `viperlib/` sweep across all subsystems, split 2D graphics docs for rendering/effects, tilemaps/layers, shapes/text/UI, and animation/collision/camera helpers, plus new `viperlib/localization/` docs, IL Optimizer Correctness Contract, GUI viperlib updated through round-7 APIs, Doxygen pass across ~100 runtime files.

---

### Commits

See `git log a91d388db..HEAD -- .` for the full 118-commit history.

<!-- END DRAFT -->
