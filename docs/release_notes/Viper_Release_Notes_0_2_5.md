# Viper Compiler Platform — Release Notes

> **Development Status:** Pre-Alpha. Viper is under active development and not ready for production use.

## Version 0.2.5 — Pre-Alpha (2026-05-07)

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
| Commits | — | 140 | +140 |
| Source files | 2,869 | 2,996 | +127 |
| Production SLOC | 450K | 552K | +102K |
| Test SLOC | 183K | 228K | +45K |
| Demo SLOC | 177K | 188K | +11K |

Counts via `scripts/count_sloc.sh`.

---

### Audio

- WAV decoder expanded to 8/16/24/32-bit PCM and float32; all resample-and-alloc sites guard against overflow and zero/negative frame counts.
- ALSA write loop handles partial writes and underrun recovery; Linux shutdown no longer hangs waiting on a blocking write; mixer callbacks use a blocking mutex lock (was trylock, causing audible silence during seek/load).
- Platform audio clocks use widened arithmetic to prevent long-uptime overflow; streaming buffers preserve the old allocation before growing; `Music.Seek` stops and clears the stream on source failure instead of leaving stale audio buffered.
- The music-scan mutex is now held across the entire per-frame scan rather than acquired and released per candidate, closing a TOCTOU window where a concurrent `Music.Free` could free a track between selection and finalization.
- `Music.Free` now waits for any in-progress buffer refill to complete before releasing the music handle, closing a race where the streaming thread could write into freed memory after the free returned.
- `vaud_context_is_destroying()` guard added to `play_sound` and `play_sound_ex`; a secondary check confirms the sound's owning context matches before the mutex is released, preventing use-after-free when a sound is played on a concurrently-shutting-down context.
- The `buffer_refilling[]` slot array is zeroed on music reuse so stale refill flags from a previous playback cycle cannot suppress legitimate fills on the next play.
- The streaming fill loop skips any slot that is already being refilled or already has frames, preventing duplicate fill scheduling on the same buffer slot.

### GUI Library

Seven rounds of widget audits plus an app-registry overhaul.

- **New capabilities** — BMP image loading (pure-C, no deps); macOS PNG/JPEG/TIFF via CoreGraphics/ImageIO; POSIX regex search in FindReplaceBar; keyboard nav, undo/redo, multi-select, and typeahead across all interactive widgets.
- **Widget lifetime** — TreeView nodes, ListBox items, and all widget handles are now tombstoned on removal; accessing a stale handle is detected and treated as a no-op rather than a crash. Focused, captured, and hovered widget pointers are cleared when a subtree is detached. Drag-and-drop strings are freed on destroy. Widget IDs are now 64-bit, preventing ID wrap in long-running sessions.
- **Event correctness** — `CLICK`/`DOUBLE_CLICK` fires after the widget's own `MOUSE_UP` handler; `WasClicked()` reports the click-receiving widget, not the hit-test target; modal input capture releases when a captured widget is outside the active modal; dynamic focus list replaces the 512-entry fixed cap. The internal event root-state slot table now grows dynamically, removing the previous hard cap of 16 simultaneous root states.
- **TrueType rendering** — All table reads bounds-checked; composite glyph point-index alignment implemented; per-contour edge wrapping fixed (was producing phantom fill regions in glyphs like 'O' and '8'); UTF-8 decoder rejects overlong, surrogate, and out-of-range sequences.
- **Layout & constraints** — NaN/inf rejected on all constraint, margin, padding, flex, spacing, and grid-track setters. Grid and Dock containers purge stale placement metadata when a child is removed, so re-adding a widget does not inherit its old position. Constraint application is now unified across all widget measure functions.
- **Widget state fixes** — Checkbox `set_indeterminate()` getter+setter; RadioButton group `selected_index` accuracy and deselect callbacks; ProgressBar `set_style()`/`show_percentage()` new APIs and NaN guards; TextInput paste emits a single `on_change`; strdup OOM guards (copy-then-free) across all text-setting paths; Spinner font-size NaN guard.
- **Font validation** — `vg_font_valid_size()` rejects non-finite, zero, negative, and absurdly large (>1,000,000) font sizes before passing them to the layout engine. `vg_font_metric_to_int()` converts double metrics to `int` with overflow clamping (replaces direct casts that were UB on overflow). `vg_font_load()` now checks `fseek` return values and rejects negative file sizes before allocating, and closes the file handle on both error paths.
- **FileDialog cross-platform guards** — Path join helper guards against `SIZE_MAX` overflow in `dir_len + sep_len + file_len + 1` before calling `malloc`. `strtok_r`/`strtok_s` and `strdup`/`_strdup` are conditionally selected per platform; OOM on `strdup` is checked and handled before the old path is freed. `S_ISREG` is defined for MSVC builds that do not provide it via `<sys/stat.h>`.

### Graphics runtime (2D)

- **2D graphics class expansion** — CPU-backed rendering, effects, tilemap/layer, shape/text/UI, viewport, animation, collision, and import-helper classes including RenderTarget2D, Texture2D, Renderer2D, Shader2D, PostProcess2D, TileSet2D/TileLayer2D, Path2D, TextRenderer2D, NineSlice2D, Viewport2D, and Aseprite/TexturePacker/TiledMap helpers.
- **Renderer2D texture APIs** — `DrawTextureScaled(texture, x, y, w, h)` and `DrawTextureRegion(texture, srcX, srcY, srcW, srcH, dstX, dstY, dstW, dstH)` added with a CPU software sampler (nearest-neighbour and bilinear filter paths sharing a shared `sample_pixels` dispatcher). These provide GPU-independent texture rendering for the software 2D path.
- **BitmapFont PSF Unicode tables** — PSF1 and PSF2 Unicode table sections are now parsed and applied, so non-Latin characters in PSF bitmap fonts render correctly instead of falling back to the placeholder glyph.
- **Pixels.DrawLine** — Integer Bresenham line drawing added to the `Pixels` runtime API.
- **PNG validation** — ZLIB header and Adler-32 checksum validated on decode; malformed deflate streams caught before the pixel buffer is touched.
- **2D graphics correctness pass** — Sprite, SpriteSheet, SpriteBatch, Camera, Scene, and SceneNode now carry runtime class IDs and reject wrong-class handles before casting. Scene transform composition and camera-scaled scene draws use saturating arithmetic at int64 limits. PNG loading now enforces IHDR/chunk-order/palette invariants, BMP and tilemap loaders use checked file offsets, GIF-backed sprites reject successful decodes with no frames, partial autotile rules fall back to the base tile, duplicate tile-animation JSON entries update the matching base tile, and alpha-aware resize preserves exact source endpoints.
- Overflow hardening across pixel/tile allocation, PNG/BMP stride math, blit/draw coordinates, flood-fill, and blend compositing.

### Graphics3D

- **glTF/FBX asset pipeline** — Import with real skeletons, per-vertex skinning, sparse-accessor morph deltas, and the full KHR extension suite (texture transform, emissive strength, unlit, punctual lights).
- **Material3D** — 6 independent texture slots with UV-set and UV-transform per slot; HDR render targets; dynamic-light cap raised to 16; bone limit raised to 256. UV transform scale/offset/rotation validated and clamped so extreme values can no longer corrupt shader coordinates.
- **Physics3D** — Bodies, contacts, and joints grow on demand; CCD substep budget raised from 8 to 64; duplicate body insertion and self-collision are now caught and rejected. `OverlapSphere` / `OverlapAABB` validate all inputs before querying.
- **Handle safety** — All public 3D APIs now validate incoming handles against their expected class before casting. Passing a NULL, wrong-type, or stack-local handle to any function returns a safe default or no-op instead of crashing. This covers every class across the 3D subsystem.
- **Asset loader hardening** — The STL loader (binary and ASCII paths), OBJ loader, and NavMesh builder all reject NaN/inf vertex data, out-of-range float attributes, and partial or truncated input. The STL binary loader guards against overflow in the triangle-count size calculation.
- **Geometry deduplication** — Static meshes that do not change between frames now share GPU geometry, reducing redundant uploads on the software backend. Animated meshes continue to re-upload each frame as before.
- **Software shadow clipper** — Shadow polygon projection now uses a proper Sutherland-Hodgman clipper in homogeneous clip space (6 planes, no per-frame allocation) rather than the previous approximate path.
- **NavMesh3D** — `SetMaxSlope` applies retroactively without requiring a full rebuild; height projection from mesh triangles is now correctly calculated.
- **Particle and water stability** — `Particles3D.Burst` clamps to available pool capacity; particle `Update` clamps delta time to 1 second to prevent physics blow-up on long hitches. `Water3D` setters reject wrong-type texture handles without silently dropping the previous valid binding.
- **Camera, scene, and canvas correctness** — Camera3D shake now derives the up-vector from the actual view matrix rather than a hard-coded axis, so rolled cameras shake in the right direction. `SceneNode.SetMesh/SetMaterial/SetLight` and `SceneGraph.Remove` validate their arguments. `Canvas3D` delta-time cap is now correctly initialised from the first frame.
- **Earlier correctness fixes** — Animation keyframes are sorted on load; spot-light inner/outer cones are enforced; mid-frame render-target rebinding is rejected; NaN/inf is sanitized on Camera3D, Light3D, and PostFX3D setters.

### Game runtime

- Entity tile sweep preserves sub-pixel remainder correctly. Physics2D exposes `CircleBody` and per-step contact data. Pathfinder rejects grids that exceed the maximum supported size rather than producing garbage paths.
- Fixed a use-after-free where worker VM threads could access a `Future` payload after the originating VM had already torn down its stack.
- Dialogue system rewritten against real font measurement with proper UTF-8 wrap and codepoint boundaries.
- Random number generation now uses rejection sampling to eliminate modulo bias in `rand.int` and `rand.range`.
- **2D game runtime handle safety** — All 16 remaining 2D game modules (Behavior, Collision, Config, DebugOverlay, Dialogue, LevelDocument, Lighting2D, ObjPool, Pathfinder, Raycast2D, SceneManager, ScreenFX, StateMachine, Timer, Typewriter, AnimState) received the same handle-guard treatment previously applied to the 2D graphics and 3D subsystems. Every incoming handle is validated before use; NULL, wrong-type, and stack-local handles return safe defaults or no-ops instead of crashing.
- **Physics2D robustness** — Physics2D and joint objects received input validation and an expanded API surface.

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

- Number formatting is now locale-isolated — float, currency, and percent output is deterministic regardless of the process's `setlocale` state, fixing subtle formatting differences between platforms.
- GC shutdown fixed: tracking and weak-reference tables detach under the GC lock before being freed; shutdown no longer allocates when no weak refs exist.
- Time runtime: overflow-checked arithmetic via shared helpers across all DateTime/Duration/Stopwatch/Countdown modules; `Duration.Abs(INT64_MIN)` and `Neg(INT64_MIN)` now trap.
- Text runtime: JSON streaming adds a per-depth state machine; `Version.Parse` is strict SemVer 2.0.0; StringBuilder/TOML/CSV/Scanner/Template/Codec/GUID all received length/depth guards.
- Threads runtime: timeout/deadline math clamps throughout (fixes a ~49-day Win32 hang where an overflow produced an immediate timeout); Thread and SafeThread objects carry runtime type tags for validation; Gate/Barrier/RwLock gain `closing`/`cancelled` flags; VM traps inside async execution now surface correctly as `Future` errors rather than silently swallowing the failure.

### Network

- **New** — TLS-backed `HttpsServer` + `WssServer` with EC/RSA cert loading, SNI, and ALPN; HTTP/2 transport with in-tree HPACK + Huffman; native RSA; in-tree X.509 chain validator with EKU (drops `Security.framework`).
- HTTP client: RFC cookie jar, transparent gzip, streaming download, keep-alive + connection pooling, CRLF injection guards.
- TLS/Crypto: PBKDF2 iterations 100K→300K; P-384 ECDSA removed from ClientHello (was advertised but never implemented, creating a silent verification gap).

### BASIC frontend

- `DIM arr(N) AS ClassName` now produces a properly typed object array; method dispatch selects overloads by argument score and falls back to case-insensitive class lookup.
- Return-flow analysis is now a full data-flow pass through `IF`/`SELECT`/`TRY`/`CATCH`/`FINALLY`, and loops; partial branches are rejected and a top-level `RETURN` outside a `GOSUB` is an error (B1008).
- `USING` blocks now correctly destroy the acquired object and rethrow when an exception escapes the block body.
- Class declarations are analysed with constructors, destructors, and method bodies in proper member scope; member-assignment type errors report structured diagnostics B2001/B2002.

### Zia frontend

- **New** — flow-narrowing on dotted field-access paths (`self.child`); `Optional` postfix-try (`?`); `async Future[T]` payload typing; SSA temporaries carry source-level names in IR dumps.
- Sema: `Map.get` returns `Optional<V>`; `finalFields_` writable only during `init()`; multi-arg generics; collection literal element-type checking.
- Lowerer: range `.rev()`/`.step(n)` chains; overflow-checking opcodes unconditionally; typed EH catch rethrow preserves original error token.
- Safety warnings (missing return, division-by-zero, uninitialized variable, optional-without-check, non-exhaustive match) promoted to errors by default; `--no-strict-diagnostics` restores warning mode.

### Diagnostics & tooling

- `Diagnostic` gains `range`, `stage`, `help`, and fix-it fields; source-snippet printing with caret/tilde underlines; JSON formatter via `--diagnostic-format=json`.
- The verifier now reports all violations in a single pass (was stop-at-first-error); the bytecode compiler runs a verifier preflight before emitting code and surfaces failures as structured errors.
- VM debugger: step-over and step-out via call-stack depth tracking; trap messages enriched with file/line/column.
- Zia undefined-identifier errors include Levenshtein-distance typo suggestions with fix-it ranges; stable `V-ZIA-*` diagnostic codes across lex/parse/sema/lowerer.

### Compiler, IL & codegen

**IL surface and VM**
- Variadic functions are now supported end-to-end (`...` syntax with arity enforcement at call sites).
- BytecodeVM gains global-variable address loads, six array fast-path opcodes (bounds-check-free after static elimination), and a trusted dispatch mode for verified modules. `call.indirect` now requires an explicit `[ret(params)]` signature annotation.
- Stack allocations and string constants are now correctly attributed with memory/side-effect metadata, closing optimizer holes where LICM or DCE could incorrectly eliminate them.

**IL verifier**
- The verifier now collects all violations before reporting (was stop-at-first-error); GEP bounds, array retain/release balance, and global type/linkage/initializer syntax are all checked.
- Stack-derived temporaries (values flowing through `alloca` → GEP chains → block arguments) are now correctly exempted from pure/readonly/nothrow effect violations.
- Parallel-edge correctness: CFG predecessor/successor caches preserve duplicate edges when a `cbr` or `switch` targets the same block via two different arms.
- **`ExternVerifier`** (new pass) — dedicated pass that validates linkage, type consistency, and calling-convention annotations on all extern declarations.
- **`EffectAttrs`** (new) — structured call-effect attribute infrastructure; optimizer passes now query and set memory/side-effect contracts on call instructions through a shared API rather than encoding rules redundantly in each pass.
- Instruction-level checks expanded to cover additional classes; load-safety analysis tightened for alias cases; runtime signature surface validated to stay in sync with `runtime.def`.

**IL optimizer**
- Four new O2 passes: `Devirtualize` (indirect→direct call via vtable/alloca tracing), `OwnershipOpt` (removes provably redundant retain/release pairs), `ArrayFastPathOpt` (replaces bounds-checked array ops after static elimination), `RuntimeFastPathOpt` (bypasses dynamic kind dispatch for proven heap objects).
- Thirteen function passes run concurrently by default via atomic work-stealing thread pools; CFG successor/predecessor accessors return cached references (eliminates per-call vector copies across all passes).
- `Mem2Reg`, full LICM, and full peephole promoted to canonical O1/O2 pipelines; CheckOpt demotes checked div/rem when the divisor is statically nonzero; MemorySSA DFS rewritten as memoized recursive with cycle detection.

**Linker hardening**
- All four object-file readers (ELF, COFF, Mach-O, Archive) and all three writers received a bounds-checking and correctness pass.
- COFF ARM64 addend decoding corrected for branch and page-relative instruction encodings; ELF extended section counts handled; `.init_array`/`.fini_array` sections are now sorted by priority before merging.
- Branch trampolines are now reused by address with collision-free naming; weak-external symbols are correctly seeded as live during dead-strip root analysis.

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
- Zero-copy codegen: the verified IL module is now passed directly to the backend, eliminating the previous serialize → temp file → re-parse → re-verify round-trip.
- Cleanup passes (DCE, CFG simplification, late-cleanup) are skipped when no upstream pass has mutated IR, reducing compile time for already-clean modules.
- New CLI flags: `--time-compile` (per-phase timing), `--pass-stats` (per-pass statistics), `--fast-link` (skip non-essential link-time reductions; auto-enabled at -O0), `--paranoid-verify` (restore all verifier checkpoints), `--debug-lines`/`--no-debug-lines` (control debug-line emission; default off).
- Build profiles (`debug`/`balanced`/`release`) map to -O0/-O1/-O2; project manifests accept a `profile` directive; `viper run` defaults to debug profile for fast script invocations while respecting project-level overrides.

### Platform

- macOS: premultiplied RGBA from CoreGraphics correctly unpremultiplied before storage; bare arrow keys no longer map to PageUp/Down/Home/End; `mach_absolute_time` conversion widened to prevent long-uptime overflow. Keyboard input state is now cleared when the window loses focus, preventing stuck keys after Cmd+Tab. Text-input events correctly emit one event per Unicode codepoint, including characters that require UTF-16 surrogate pairs.
- Linux: X11 clipboard upgraded to full ICCCM CLIPBOARD selection protocol; UTF-8 text input validates every codepoint; XDND URI parsing enforces byte-length bounds; RGBA swizzle reads channel masks from the active X11 Visual instead of assuming layout. XDND file-drop URIs are now percent-decoded (`%XX` sequences resolved) before the path is passed to the application. `XLookupString` handles `XBufferOverflow` by allocating a heap buffer of the reported size and retrying; if the retry also overflows the event is discarded rather than truncated. `ConfigureNotify` resize events validate `new_w`/`new_h` against `VGFX_MAX_WIDTH`, `VGFX_MAX_HEIGHT`, and `INT32_MAX/4` before updating the framebuffer; out-of-range sizes are rejected. Cursor shape mapping extended: `pointer→XC_hand2`, `text→XC_xterm`, `resize-h→XC_sb_h_double_arrow`, `resize-v→XC_sb_v_double_arrow`.
- All platforms: `vgfx_cls()` fill and framebuffer presentation rewritten as byte-wise writes (eliminates strict-aliasing UB); Cohen-Sutherland line clipper pre-clips extreme off-screen coordinates; Bresenham error accumulator widened to int64; event queue protected by atomic spinlock.

### Packaging & distribution

Six-pass hardening of the `viper install-package` and `viper package` subsystems.

- **Cross-platform installer correctness** — Windows uninstaller removes all registry entries it created. Existing `Content Type` values for extensions are preserved; only VAPS-owned MIME values are tagged on install and removed on uninstall. ProgID trees are tagged with a VAPS owner marker and only deleted when that marker is present, preventing `viper install` from silently overwriting another application's file associations. Duplicate `file-assoc` extensions in a project manifest are now rejected at validation time.
- **Windows long-path support** — Installer paths are prefixed with `\\?\` before all `CreateFile`/`CopyFile`/`DeleteFile` calls so installations into deep directory trees succeed on Windows without requiring the system-wide `LongPathsEnabled` registry key.
- **Windows toolchain PATH and file-association management** — The toolchain installer adds the Viper `bin` directory to the system `Path` environment variable as a `REG_EXPAND_SZ` entry; on reinstall, existing Viper-owned PATH entries are deduplicated rather than appended. `SendMessageTimeoutW(WM_SETTINGCHANGE)` is broadcast after each PATH modification so running processes pick up the change without a reboot. On uninstall, only the Viper-owned PATH token is removed. Source (`.zia`) and IL (`.il`) file associations are registered during toolchain install with `viper run` and `viper -run` open commands respectively.
- **PE hardening** — Emitted Windows executables now have ASLR, NX (DEP), Control Flow Guard, and high-entropy VA enabled; the application manifest is embedded with `asInvoker` execution level. The linker validates that all required security flags are present in the output before reporting success.
- **Manifest validation** — The embedded application manifest is compared against the source manifest after each build to catch silent embedding failures.
- **PNG full color-type support in package assets** — Asset packager now handles PNG color types 0 (greyscale), 2 (RGB), 3 (indexed), 4 (greyscale+alpha), and 6 (RGBA); previously only type 6 was decoded correctly for VPA asset packs.
- **ZIP header cross-check** — `viper package` verifies that the central-directory entry for each asset matches the local-file header (compression method, CRC, sizes) and rejects the archive if any field diverges — catches truncated or partially-written asset packs before distribution.
- **Executable format validation** — Packaged native executables validated post-link: ELF magic + class + machine checked for the target architecture; PE `MachineType` and `Subsystem` validated against the build profile; Mach-O `cputype`/`cpusubtype` pair validated per platform. Portable tarballs accept Mach-O, ELF, or PE payloads but reject unknown formats and architecture mismatches.
- **macOS signed app packaging** — macOS app packages are now staged as real `.app` bundles before ZIP emission. The default signing mode is ad-hoc on macOS hosts (sealing `Info.plist` and bundled resources with `codesign`) and `preserve` on non-macOS hosts where local signing tools are unavailable. Developer ID signing, hardened runtime, notarization profile, entitlements, and stapling are supported for production distribution flows. `macos-staple` requires `developer-id` mode and a notary profile.
- **macOS toolchain package hardening** — `viper install-package --target macos` validates that the staged tree's platform matches before invoking `pkgbuild`; `--macos-pkg-version` accepts a dotted-numeric override for cases where the Viper version string has pre-release suffixes that `pkgbuild` cannot accept. `/usr/local/bin` symlinks are now embedded directly in the package root so the postinstall script no longer needs to create them.
- **Mach-O `__LINKEDIT` correctness** — Native Mach-O output no longer pads the final `__LINKEDIT` segment past `LC_CODE_SIGNATURE`, allowing Apple `strip` and `codesign --force` to process Viper-generated arm64 binaries and enabling bundle-level signing.
- **Linux toolchain package improvements** — Staged Unix permission bits are preserved in `.deb` and tarball payloads. Toolchain packages include a `NoDisplay` desktop entry and shared-MIME XML for `.zia` and `.il` file types so file associations work system-wide after `dpkg -i`.
- **Platform and architecture auto-detection** — `viper install-package` now inspects the staged `viper` binary's ELF, PE, or Mach-O header to derive platform and architecture automatically. A `--arch` override that contradicts the detected binary architecture is rejected. `--target all` includes RPM only when `rpmbuild` is available on the host.
- **Package asset root clarification** — `asset <source> <target>` targets are relative to each platform's resource root. Asset directory symlinks are followed when their resolved targets remain inside the project root; their packaged paths preserve the symlink path.
- **Deep installer verification** — Post-build verification for Windows `.exe`, `.deb`, and `.tar.gz` toolchain artifacts checks every path listed in the staged manifest against the emitted payload. `.pkg` verification now inflates and validates the XAR TOC and requires a payload entry. `.rpm` verification confirms a non-empty CPIO payload follows the headers. Installer stub overlay extraction streams in 1 MiB fixed-size chunks, preventing large-payload OOM on the target machine.

### Tests

~23K new lines of test coverage added this cycle.

- **3D handle-safety contracts** — Every major 3D class (Mesh3D, SceneGraph, Canvas3D, Camera3D, Collider3D, Light3D, Material3D, Particles3D, Physics3D, Water3D) has contract tests confirming that NULL, wrong-type, and out-of-range handles return safe defaults rather than crashing. STL loader tests verify that NaN/inf vertices and out-of-range floats are rejected in both binary and ASCII formats.
- **2D graphics regression suite** — New tests cover wrong-class handle contracts for Sprite/SpriteSheet/SpriteBatch/Camera/Scene, PNG chunk-order and palette validation, sub-byte greyscale transparency, zero-frame GIF rejection, scene transform saturation at int64 limits, tilemap autotile fallback, and tile-animation JSON deduplication.
- **Game runtime contracts** — All 16 2D game modules have dedicated wrong-class-handle tests aligned with the class-ID guard pass; new suites cover Lighting2D, Typewriter, Physics2D, Collision, ObjPool, ScreenFX, Behavior, Raycast2D, SceneManager, and Pathfinder.
- **GUI** — 40+ new regression tests covering layout constraints, image loading, widget lifecycle, regex search, modal input routing, TextInput undo, TrueType composite glyph bounds, Grid/Dock metadata cleanup on child removal, RadioButton deselect callbacks, Checkbox indeterminate state, and TextInput single-change paste.
- **Packaging** — New unit and CLI smoke tests covering duplicate file-association extension rejection, `validatePackageFileAssociations`, `resolveMacOSSignModeForHost`, `validateMacOSSigningConfig`, macOS toolchain platform guard, `--macos-pkg-version` override validation, Windows PATH token dedup logic, file-association ProgID generation, Linux `permissionBitsFor` mode selection, and deep archive verification (`.pkg` XAR TOC, `.rpm` CPIO, `.deb` payload paths, tarball entry completeness).
- **IL, compiler, and audio** — New tests for linker correctness, opcode metadata consistency, optimizer passes (DSE, LICM, LoopRotate, LoopUnroll, IndVarSimplify, MemorySSA, ValueKey), verifier escape-analysis golden fixtures, and IL transform edge cases. Audio tests cover PCM overflow, ALSA write simulation, and WAV format variants. Localization fuzzing harnesses added (opt-in via `VIPER_ENABLE_FUZZ`).

### Demos & docs

- **Demos** — New text-mode baseball franchise simulator; Crackman rewrite; two new 3D demos; Paint gains layers and undo; ViperIDE file-watcher and context-menu fixes; XENOSCAPE boss/player fixes and macOS package; Chess drag-vs-click detection fix; three localization examples; Windows ARM64 builds for all 3D demos.
- **Docs** — `viperlib/` coverage extended across all subsystems: 2D graphics split into rendering/effects, tilemaps/layers, shapes/text/UI, and animation/collision/camera; new `viperlib/localization/`; IL Optimizer Correctness Contract; GUI viperlib updated through round-7 APIs. Doxygen/`@brief` coverage pass across all 42 `lib/gui` source files, ~100 runtime files, and every Crypto/Network/Text/Packaging runtime module; all source files received canonical Viper two-separator file headers.

---

### Commits

See `git log a91d388db..HEAD -- .` for the full 140-commit history.
