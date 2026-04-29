# Viper Compiler Platform — Release Notes

> **Development Status:** Pre-Alpha. Viper is under active development and not ready for production use.

## Version 0.2.5 — Pre-Alpha (DRAFT — unreleased)

<!-- DRAFT: release date TBD. v0.2.4 was cut on 2026-04-13. -->

### What this release is about

A polish-and-hardening cycle with several notable new capabilities.

- **Hardening across every runtime subsystem.** Integer-overflow, handle-validation, timeout-clamping, and lifetime-correctness passes applied to graphics, text, threads, audio, I/O, and network.
- **Production 2D Graphics module (new).** ~40 classes on top of `Pixels` / `Canvas` — offscreen surfaces, materials, shaders, post-effects, viewport scaling, tile layers, path drawing, text layout, nine-slice UI, animation, and a render-pass graph.
- **Full 3D asset pipeline.** glTF and FBX import real skeletons, per-vertex skinning, and animations including sparse-accessor morph deltas and the full KHR extension suite. HDR RenderTarget3D and backend-capability introspection complete the picture.
- **Network stack became a platform.** TLS-backed `HttpsServer` + `WssServer`, from-scratch HTTP/2, native RSA, in-tree X.509 validator, cookie jar, streaming downloads, and connection pooling.
- **Viper.Localization.* (new).** Eleven-class namespace for locale-aware number/date/time/list formatting, translation catalogs, CLDR plural selection, and text-direction utilities. Zero external dependencies; en-US baked in.
- **Native codegen correctness pass.** Variadic IL end-to-end, checked FP casts with distinct error kinds, sub-width overflow arithmetic, `fptosi` NaN traps, Windows ARM64 native build, and two rounds of IL optimizer/verifier hardening.
- **Structured diagnostics & developer tooling.** Source-location snippet printing with caret underlines, JSON output mode (`--diagnostic-format=json`), strict diagnostics and bounds checks on by default, `Verifier::verifyAll` multi-diagnostic collection, and `BytecodeCompiler::compileChecked` returning typed `Expected<BytecodeModule>` with verifier preflight instead of throwing.

The biggest user-visible new thing is a text-mode baseball-franchise simulator.

### By the Numbers

| Metric | v0.2.4 | v0.2.5 | Delta |
|---|---|---|---|
| Commits | — | 92 | +92 |
| Source files | 2,869 | 2,953 | +84 |
| Production SLOC | 450K | 522K | +72K |
| Test SLOC | 183K | 216K | +33K |
| Demo SLOC | 177K | 188K | +11K |

Counts via `scripts/count_sloc.sh`.

---

### Audio

- WAV / OGG / MP3 share one decoder dispatch; float32 WAV, large headers, incremental MP3, crossfade pause/resume, ADSR short-note release, and silent-channel skip all fixed.
- Sound/Music handles carry magic tags (`VSND` / `VMUS`) and validate before use; decoded sound capped at 100 MiB, sample rate at 384 kHz.
- vaud mixer: `source_channels` tracking, stream-eof/loop-pending flags, seek clamping, and voice-ID wrap recovery.
- `Music.SetLoop` is IL-bound; `Audio.Update()` drives crossfade from outside `Playlist.Update`; MusicGen rejects beats past the song span; Playlist shuffle uses `rt_rand_int`.

### GUI Library

Four rounds of widget audit plus an app-registry + widget-family overhaul.

- **Interactive widgets** — keyboard nav, undo/redo, multi-select (Ctrl/Shift), press-release coupling, typeahead, and desktop-convention behavior across CodeEditor, TabBar, Toolbar, Dropdown, TextInput, TreeView, ListBox, Spinner, SplitPane, FileDialog, and CommandPalette.
- **Overlay widgets** — Dialog, Tooltip, Notification, Breadcrumb, ContextMenu, and FloatingPanel rewritten with rounded-card paint, scaled metrics, fade+slide animation, and screen-bounds anchoring.
- **Layout and framework** — flex non-stretch alignment, VBox/HBox margin budgets, synthesized double-click, Tab/Shift+Tab focus traversal. Lifetime crashes closed across Tooltip, Dialog, Notification, CodeEditor, and VideoWidget.
- **App-registry refactor** — `rt_gui_app.c` gains handle-registry arrays for safe liveness queries; handle-validation pass on every public widget entry.
- **Widget tombstone/retire pattern** — TreeView nodes and ListBox items carry magic tags (`VG_TREE_NODE_MAGIC`, `VG_LISTBOX_ITEM_MAGIC`) and retire on removal; public API entries check the live predicate so handle use-after-remove becomes a silent no-op instead of heap corruption. Widget-base sentinels (`VG_WIDGET_MAGIC`), `vg_widget_is_live()`, and `rt_gui_is_widget_handle()` extend the pattern to all runtime widget handles.
- **Scroll and click precision** — Wheel events store hit-test coordinates in dedicated `wheel.screen_x/y` fields separate from scroll deltas, preventing delta corruption during coordinate localization. `WasClicked()` now reports the widget that received `VG_EVENT_CLICK` (via `vg_widget_note_click`) rather than the pointer-hit target at mouse-up.
- **Layout constraint propagation** — `layout_nonnegative()` prevents negative computed dimensions from propagating when padding exceeds container size; `layout_apply_constraints()` enforces preferred/min/max constraints uniformly across VBox, HBox, Flex, Grid, and Dock.
- **Round-3 widget hardening** — Spinner uses a dynamic `realloc`-based text buffer (was 64 bytes fixed) and rejects NaN/inf; Slider rejects NaN/inf and auto-swaps inverted range bounds; RadioButton/RadioGroup cross-reference lifecycle correctly unregisters on destroy; ColorPicker `syncing_children` reentrancy flag prevents N `on_change` firings during programmatic color set; ColorPalette paint implemented (`vgfx_fill_rect` for swatches, `vgfx_rect` for borders); Label and Checkbox initialize to theme fonts; ListBox ctrl-toggle-off correctly moves `selected` and `anchor_selected`; ScrollView `clamp_scroll` re-clamps on direction change; TextInput returns `false` for unhandled keys so focus events bubble; Dropdown fires `on_change` on item remove/clear and uses `vg_utf8_decode` for Unicode typeahead.
- **Image loading** — `vg_image_load_file()` implemented: pure-C BMP decoder (24/32bpp, top/bottom-up, overflow-safe stride math) plus macOS CoreGraphics/ImageIO for PNG/JPEG/TIFF. Zero external dependencies maintained.
- **Round-4 widget hardening** — `event_modal_safe_capture()` releases input capture when a modal root is active and the captured widget lives outside the modal subtree, applied to both mouse and keyboard dispatch. FindReplaceBar gains POSIX `<regex.h>` search (variable-length patterns, `REG_EXTENDED`/`REG_ICASE`, whole-word post-check; Windows path returns "Regex unavailable"); `regcomp`/`regexec`/`regfree`/`regerror` added to `DynamicSymbolPolicy`. MenuBar `remove_item`/`remove_menu` validate `parent_menu`/`owner_menubar` before unlinking and roll back cleanly on OOM. TextInput `ensure_capacity` bootstraps from zero; `push_undo` pre-allocates snapshot before truncating redo history; surrogate codepoints (U+D800–U+DFFF) and scalars above U+10FFFF silently rejected on character input. CodeEditor `SetCustomKeywords` builds the new array atomically before freeing the old one. CommandPalette, Breadcrumb, and Minimap wrappers install GC finalizers and vtable `destroy` intercepts so explicit `Destroy()` and runtime collection share the same idempotent path; all public methods null-guard the backing widget after destroy. Keyboard shortcut evaluation deferred until after modal overlays dispatch; migrated from `VGFX_MOD_*` to `VG_MOD_*` constants. `Viper.GUI.Theme` instance type corrected to `"none"` in `runtime.def` — it is a static class with no constructor.

### Graphics runtime (2D)

- `RT_CANVAS_MAGIC` guard on every public entry; `CopyRect` / `Screenshot` carry `Pixels` return types through IL; shared HiDPI helpers and explicit-alpha blend flag.
- **Production 2D Graphics module (new)** — ~40 CPU-backed classes: core rendering (RenderTarget2D, Texture2D, Renderer2D, Material2D, Shader2D, PostProcess2D), tiles (TileSet2D, TileLayer2D, AutoTile2D, TilemapRenderer2D), animation (AnimationClip2D, AnimatedSprite2D, CameraRig2D), vector/text/UI (Path2D, ShapeRenderer2D, TextRenderer2D, NineSlice2D, DebugDraw2D), viewport (Viewport2D, Transform2D), and import adapters (TexturePackerAtlas, AsepriteImporter, TiledMapLoader).
- Overflow hardening across pixel/tile allocation, PNG/BMP stride math, blit/draw coordinates, flood-fill, and `Pixels.BlendPixel` compositing.

### Graphics3D

- **Asset import** — glTF: full attribute coverage, real skin + animation (LINEAR/STEP/CUBICSPLINE with Hermite tangents), sparse accessors, `extensionsRequired`, KHR_texture_transform / emissive_strength / unlit / lights_punctual. FBX: scene-node hierarchy, per-vertex skinning, cross-platform texture paths, AnimationStack names.
- **Per-instance morphs** — `Model3D.Instantiate` deep-clones morph meshes so animating one instance doesn't bleed into clones; per-shape tangent deltas added.
- **Material3D** — 6 texture slots each with independent wrap/filter/UV-set/UV-transform; TEXCOORD_1 added end-to-end across all four backends.
- **Canvas3D / backends** — `BackendCapabilities` / `BackendSupports` introspection; deterministic shadow-light selection; PCF shadows + sRGB decode in SW rasterizer; HDR `RenderTarget3D.NewHdr`; ordered GPU postfx chain; dynamic-light cap 8→16; bone limit 128→256.
- **Physics3D** — bodies/contacts/joints grow on demand (overflow-safe doubling); broadphase moves toward O(N log N); CCD substeps 8→64.
- NaN/inf sanitization across Camera3D / Light3D / PostFX3D setters; `Scene3D` threads an inherited animator down child nodes.

### Game runtime

- **Dialogue** rewritten against the real BitmapFont measurement surface; UTF-8 codepoint boundaries for wrap and reveal.
- **Viper.Game hardening** — Entity sweeps tile collisions preserving centipixel remainders; Pathfinder rejects oversized grids; Physics2D exposes `CircleBody` and per-step contacts.
- Correctness fixes across Quadtree, Physics2D, PathFollower, SpriteAnimation, SceneManager, ParticleEmitter, Achievement, and ButtonGroup.
- **Input** — action chord releases on last-key-drop frame; debounced press-edge detection; real UTF-8 encoding up to U+10FFFF.
- **Async UAF fix** — `Async.Run` / `Thread.Start` worker VMs retain the Future payload past worker unwind; pinned by a 25× regression loop.
- `Config.Load` pre-checks file existence and returns NULL for missing configs (documented soft-fail).
- **RNG debiasing** — `rt_random_bounded_u64()` uses rejection sampling to eliminate modulo bias; `rt_rand_int` and `rt_rand_range` now produce statistically uniform results across all bound sizes.

### Collections runtime

- All string-keyed collection types (Map, MultiMap, CountMap, LruCache, TreeMap, FrozenMap, FrozenSet, SortedSet, WeakMap) compare full byte-length keys; embedded NULs no longer truncate identity.
- Ownership fixes across Seq, Deque, List, MultiMap, SparseArray, OrderedMap, and TreeMap — retained values released on overwrite, removal, clear, eviction, and finalization.
- Bytes, BinaryBuffer, Trie, and BiMap use runtime byte lengths for binary payloads; embedded NULs no longer truncate parsing or lookups.
- Snapshot APIs tightened: Trie/SortedSet sequences own copied strings; IntMap/SparseArray return boxed `i64`; Stack iterates bottom-to-top without mutating the source.
- Allocation failure traps cleanly across Bag, MultiMap, LruCache, Deque, UnionFind, FrozenMap, FrozenSet, SparseArray, and BloomFilter.
- WeakMap stores zeroing handles; `Length` / `Has` / `Keys` report live values; `Compact` removes stale slots.
- Overflow hardening across growth paths, capacity counters, hash-table resize, BloomFilter sizing, Bytes base64 sizing, and BinaryBuffer length-prefix writes.

### IO runtime

- File replacement paths (`WriteAllText`, `WriteAllBytes`, `Archive.Finish/Extract`, `SaveData.Save`) write temp sidecars and atomically replace the destination; failed writes clean up instead of leaving partial files.
- Archive: `Create` no longer truncates before `Finish`; ZIP entries validate CRCs, size agreement, and data bounds; parser rejects embedded-NUL names, corrupt offsets, and symlink traversal.
- File paths reject embedded NULs; `File.Size` requires regular files; `File.Copy` enforces no-overwrite; `Move` preserves replace semantics.
- Stream wrappers retain the wrapped object for the wrapper lifetime; closed/null streams trap on all operations except `Close(NULL)`.
- `Dir.RemoveAll` handles top-level symlinks without recursion; `Dir.Files` excludes symlinks on POSIX; `Dir.MakeAll` accepts backslash on POSIX.
- GZIP rejects reserved flags, malformed optional headers, bad FHCRC, CRC/size mismatches; raw DEFLATE rejects trailing data after the final block.
- `SaveData.Load` treats a missing file as empty-success; malformed JSON preserves prior state intact.

### Localization runtime (new)

Eleven-class `Viper.Localization.*` namespace. Zero external dependencies; en-US baked as a static record, all other locales load from JSON at runtime.

- **Locale / LocaleInfo / LocaleManager** — BCP-47 parser with case-canonicalization, fallback-chain walk, rwlock-guarded process-global registry, system-locale detection on all three platforms. No Foundation.framework on macOS.
- **NumberFormat** — Decimal/Integer/Percent/Currency/Scientific/Ordinal format + parse with six rounding modes; strict/lenient group-placement validation; `TryParse*` returns Optional.
- **DateFormat** — CLDR pattern letters, locale-digit emission (Arabic-Indic / Devanagari / Thai / etc.), canonical Short/Medium/Long/Full/DateTime methods.
- **RelativeTimeFormat / MessageBundle / PluralRules** — auto unit selection across 7 thresholds, named/positional placeholders, CLDR cardinal + ordinal rules on a compact AST; INT64_MIN precision fix via `i_d` double operand.
- **Collator / ListFormat / TextDirection** — DUCET-lite weights with sv-SE tailoring, locale-template list joining, strong-RTL classifier for Hebrew/Arabic/Syriac/Thaana/N'Ko.
- Refcount + lifetime discipline: all finalizers route through `rt_obj_release_check0 → rt_obj_free`; `LocaleManager.Unload` uses atomic reads; lazy-init CAS uses `__atomic_compare_exchange_n` on GCC/Clang, `rt_atomic_compare_exchange_ptr` on MSVC.

### Core runtime formatting

- `rt_fmt` locale-aware rewrite — float/currency/percent route through a C-locale isolated `snprintf` (`uselocale` on POSIX, `_create_locale` on Windows) so numeric output is deterministic regardless of process `setlocale` state.
- Float formatter standardized to `%.15g`; `rt_fmt_bool_yn` lowercase (`"yes"` / `"no"`); BASIC `VAL` permissive parse restored (`strtod` + trailing-garbage tolerance); `rt_parse` integer/double parsers tightened.

### Time runtime

- Overflow-checked arithmetic across all modules (DateTime, DateOnly, DateRange, Duration, RelTime, Stopwatch, Countdown) via shared `dt_checked_*` helpers; all paths trap via `rt_trap_ovf` instead of wrapping.
- `DateTime.Create` range-checks components before `mktime`; formatters validate `int64_t → time_t` before `localtime`/`gmtime`.
- `Duration.Abs(INT64_MIN)` and `Neg(INT64_MIN)` now trap; `snprintf` length validation on every string producer.
- Countdown sleeps long waits in chunks; Stopwatch uses checked scale/add math for ns/us/ms conversions.

### Text runtime

- Overflow + validation hardening: JSON streaming adds a per-depth state machine; regex caps internal offsets at `INT_MAX`; StringBuilder, TOML, CSV, Scanner, Template, Codec, GUID, numfmt all got length/depth/byte-count guards.
- `Version.Parse` is strict SemVer 2.0.0; `JsonStream` value accessors (`BoolValue`, `NumberValue`, `StringValue`, `HasNext`, `Skip`) let consumers extract values without re-parsing.
- `TextWrapper.WrapLines` and `Template.Keys` return `Seq<String>`; `rtgen` understands `seq<T>` / `list<T>` return types.

### Threads runtime

- Timeout/deadline math clamps rather than wraps everywhere (Future, Monitor, Channel, ThreadPool, Scheduler, Debounce, ConcQueue, ConcMap) — fixes a ~49-day Win32 hang from sign-extending a negative `int64` to `DWORD`.
- Parallel execution: cross-platform `failed` flag, `setjmp` worker-trap recovery that cleans up the whole block.
- Thread/SafeThread handles carry `VTRD` / `VTSF` magic tags; Gate/Barrier/RwLock gain `closing` + `cancelled` flags with a `can_delete` handshake eliminating unblock-and-delete races.
- `RuntimeBridge::interceptTrap` catches VM traps in async trampolines and surfaces them as Future errors.

### Network

- **TLS-backed servers (new)** — `HttpsServer` + `WssServer` with EC/RSA cert loading, SNI validation, ALPN (h2/http1.1), per-connection send-timeout.
- **HTTP/2 transport (new)** — in-tree HPACK + static Huffman; HTTPS client pools connections; ALPN multi-token bugfix; `HttpReq.SetForceHttp1` knob.
- **HTTP client** — RFC cookie jar, transparent gzip, streaming download, relative-Location resolution, keep-alive + connection pooling, CRLF injection guards.
- **HTTP server** — chunked request bodies, Connection-header-aware framing, thread-safe Start/Stop, ephemeral port via `port=0`.
- **TLS / Crypto** — native RSA (Montgomery ladder, RSA-PSS-SHA256), in-tree X.509 chain validator with EKU (drops `Security.framework`), in-tree `timegm`, PBKDF2 iterations 100K→300K. P-384 ECDSA (`0x0503`) removed from ClientHello signature-algorithm extensions and from `tls_verify_cert_verify` — it was advertised but never implemented, creating a silent verification gap.
- WebSocket subprotocol negotiation, SSE auto-reconnect with `Last-Event-ID`, dual-stack IPv6 UDP, SMTP follow-ups.

### BASIC frontend

- `SemanticAnalyzer::Type::ArrayObject` — distinct type for `DIM arr(N) AS ClassName`; all dispatch paths (LBOUND/UBOUND, FOR EACH, array-assignment, completion) handle it.
- `MethodCallExpr` visitor rewritten: validates runtime class methods via typed `scoreArgMatch` lookup (exact/widening/unknown); case-insensitive OopIndex fallback for mixed-case class registrations.
- `mustReturn` rewritten as data-flow: `ReturnFlow {alwaysReturns, assignedAfter}` propagated through IF/SELECT/TRY/CATCH/FINALLY/loops; partial branches rejected; top-level `RETURN` without GOSUB emits B1008.
- Loop checks: EXIT SUB/FUNCTION validated against procedure kind; FOR counter/start/end/step type-checked as numeric; FOR EACH element type inferred from array kind.
- FINALLY bodies analyzed under surrounding scope; USING initializer type-checked against object/resource; `TryCatchStmt` added to `STMT_KIND_TRAIT`.
- `rt_map_get_opt_str` (new) — returns NULL for missing keys; used by the `Map.get()` optional-String path.
- `Viper.System.String`-typed variables now lower to `Type::Str` instead of `Type::Ptr`, fixing store/load type mismatches in string-variable slots.
- `SourceManager::setSource()` called after file registration so line-snippet printing works without on-disk reads; IL verifier runs post-lowering with failures surfaced through the BASIC emitter at code `B9001`.
- `lowerUsingStmt` rewritten with an `EhEntry` handler block: the exception path calls `DESTROY`, `__dtor`, and `rt_obj_free` before executing `resume.same(tok)` to re-raise the original error token. Previously, USING cleanup only ran on the normal exit path.
- `analyzeClassDecl` now walks constructor, destructor, and method bodies under proper scope context (`activeClassQName_`, `activeMemberHasMe_`, loop-context flags), closing a gap where class member bodies were analyzed without their class-member scope.
- `analyzeMemberAssignment` validates instance-field assignments with typed diagnostics B2001 (type mismatch) and B2002 (missing member); `Bool := Int` implicit widening accepted without error.
- `widen_to` (B9002), `intType` (B9003), and `lowerNumericDispatch` (B9005) now emit structured diagnostics instead of calling `std::abort()`.

### Zia frontend

- Path-aware completion: `CompleteForFile`, `CheckForFile`, `HoverForFile`, `SymbolsForFile`; relative `bind` paths resolve against the active file.
- **Parser** — `isExpressionStart()` predicate; `parseExpressionAllowingStructLiterals()` fixes struct-literal parse in bodies/initializers/returns; `clonePureExpr()` covers all AST node kinds; trailing-comma support; match arms accept semicolons.
- **Sema — types** — `Map.get` returns `Optional<V>`; `list.first/last/isEmpty` wired; postfix-try (`?`) validated; collection literal element-type checking; `finalFields_` writable only during `init()`.
- **Sema — generics + statics** — `inferTypeParamsFromPattern` does full structural inference; multi-type-arg generics parsed; `staticFields_` set with private-visibility propagation.
- **Sema — flow narrowing** — `narrowingKeyForExpr` builds dotted keys (`"self.child"`); null-check narrowing works on field access paths across if-stmt, block, and binary-op analysis.
- **Sema — async** — `types::futureOf(payload)` carries payload as `typeArgs[0]`; `AwaitExpr` reads `typeArgs[0]` directly for typed Future variables.
- **Lowerer** — range `.rev()` / `.step(n)` chains; `widenIntegralToI64` on all index paths; `lowerStructLiteral` typed field defaults; `AddressOf` forward-ref fix; Map subscript traps on absent key; all integer ops emit overflow-checking opcodes unconditionally.
- EH typed-catch rethrow: `eh.entry` + `resume.same(tok)` preserves original error token instead of synthesizing a new trap.
- **VM string cache async race fix** — `initStringCache` pre-allocates null slots; `getStringLiteral` materializes on first access, closing concurrent-init window in async worker VMs.
- **Inline aggregate lowering fix** — `emitFieldLoad` / `emitFieldStore` and class/struct field-layout now treat `Struct`, `Tuple`, and `FixedArray` uniformly as inline aggregates; previously only `FixedArray` was handled inline while struct/tuple fields were scalar-loaded from their first bytes, corrupting data silently. New `emitInlineValueStore` / `emitInlineValueCopy` helpers recurse through nested members and elements with per-element stride, string retain/release, and init-vs-overwrite semantics. Field layout delegates to `getSemanticTypeSize` / `getSemanticTypeAlignment` for all aggregate kinds.
- `getOrCreateStructTypeInfo` gains an unqualified suffix-match fallback so short names (`Pair`) resolve to `Module.Pair` without requiring full qualification at the call site.
- Safety-critical warnings (`W008` missing-return, `W010` division-by-zero, `W015` uninitialized-variable, `W016` optional-without-check, `W019` non-exhaustive-match) are promoted to errors by default; `--no-strict-diagnostics` keeps them as warnings without requiring `-Werror`.
- Zia lexer and semantic diagnostics now use stable `V-ZIA-*` codes for common failures, including unterminated block comments, unterminated strings, undefined identifiers, type mismatches, and fixed-array literal index bounds errors. Nine new entries in `scripts/spec/diagnostics.yaml`.
- `nameTemp(id, name)` attaches source-level variable names to SSA temporaries at definition time; `defineLocal()` and `createSlot()` call it so IR dumps show readable names (e.g., `%player.x`) instead of anonymous `%42` temporaries.
- `SourceManager::setSource()` called after file registration; file-ID overflow now emits a `V-SRC-FILE-ID` diagnostic and returns early; optimizer pipeline failures emit `V-OPT-PIPELINE`; `Verifier::verify` runs post-optimizer.

### Linker, codegen, tools, IL, build

- Linker: Itanium-ABI symbol classification on macOS with `$DARWIN_EXTSN`; `link()` / `strnlen()` added to dynamic-import allowlist.
- Codegen: `RtComponent::Localization` enum entry with `rt_locale_*` prefix classifier; `viper_rt_localization` archive in install manifest.
- IL surface: `Canvas.CopyRect` / `Screenshot` return `Pixels`; `AudioUpdate` added; eleven new `RTCLS_Loc*` RuntimeTypeId entries; `RelativeTime.FormatShort` binding corrected.
- rtgen audit passes with zero findings after classifying 29 previously unclassified symbols.
- Tools: frontend `--` separator, collision-safe temp paths. Build: VERSION `0.2.4-dev` → `0.2.5-snapshot`.
- **Diagnostic infrastructure** — `Diagnostic` gains `SourceRange range` and `std::vector<DiagnosticNote> notes` fields; `diag_expected.cpp` adds source-snippet printing with caret/tilde underlines (`printSourceSnippet`) and a JSON formatter (`printDiagJson` / `printDiagnosticsJson`). `SourceManager::setSource()` populates line caches from in-memory strings without on-disk reads. `SourceManager::addFile()` no longer emits its own diagnostic on overflow — callers own that report.
- **CLI flags** — `--diagnostic-format=text|json` (also `--diagnostic-format json`), `--strict-diagnostics` / `--no-strict-diagnostics`, `--bounds-checks` / `--no-bounds-checks`, and `--show-warnings` / `--quiet-warnings` are available across subcommands; strict diagnostics and bounds checks default on. All diagnostic output routes through `printDiagnostic(format)` for consistent text-or-JSON rendering.
- **BytecodeCompiler** — `compileChecked(module, sourceManager?, assumeVerified=false)` returns `Expected<BytecodeModule>` with a source-located `Diag`; it runs verifier preflight unless the caller explicitly passes `assumeVerified=true`. Internal errors propagate via private `BytecodeCompileFailure` so no undecorated `runtime_error` escapes. Function name, block label, and source location tracked throughout for precise diagnostic attribution (e.g., `V-BC-UNKNOWN-GLOBAL`, `V-BC-UNKNOWN-SSA`, `V-BC-FUNCTION-TABLE`).
- **BytecodeVM** — trap messages enriched to `"Trap @func:block#pc file.zia:line (TrapKindName)"`.
- **Signature Registry** — `register_signature` is mutex-guarded and idempotent: identical re-registration is a no-op; conflicting metadata for the same name throws `std::logic_error`. `registry_version()` added for cache-invalidation consumers.
- `vm_executor`: `compileChecked` integration; `VMExecutorResult::compileFailed` flag; `VMExecutorConfig::sourceManager` for diagnostic context. Pipeline subcommands report all `Verifier::verifyAll` diagnostics at the appropriate stage.

### Linker hardening

All four object-file readers and all three writers received a bounds-checking and correctness pass.

- **Readers** — every reader uses `checkedRange` before buffer access; ELF validates all section/symbol/reloc ranges; COFF validates symbol-table and string-pool bounds on every long-name lookup; Mach-O preserves `__compact_unwind`/`__eh_frame`; Archive validates member/symbol/string ranges.
- **COFF addend decode** — `extractCoffAddend` decodes ARM64 instruction bit-fields (`Branch26/19`, `PageRel21`, `PageOff12A/L`, `ADDR64`) instead of a blind 4-byte read; AMD64 handles 8-byte ADDR64 correctly.
- **ELF reader (pass 2)** — extended section counts from SH0 when `e_shnum == 0`; COMMON → BSS; SHN_ABS absolute values; `.rel` implicit-addend sections; truncated-table detection.
- **COFF reader (pass 2)** — weak external fallback records; associative COMDAT relationships; relocation overflow records; BigObj rejected with a diagnostic.
- **Writers** — COFF section flags and storage class derived precisely from section kind; ELF `sh_flags` and symbol bind/visibility from linkage model; Mach-O `appendSection` for multi-text; anonymous symbols get synthesized names.
- **IL ModuleLinker** — `FunctionRef` export index; `rewriteFunctionRefs` rewrites callee names and GlobalAddr/brArgs across module boundaries; scoped global-name collision rewriting.
- **Infrastructure** — RelocApplier bounds-checks every application; BranchTrampoline reuse keyed by address; DeadStripPass keeps EH personality/LSDA roots; ICF off-by-one fix; `PT_LOAD` alignment 0x1000.

### Codegen

**AArch64**
- RegAllocPass wraps the coalesce + allocate pipeline in try/catch; exceptions become a `V-CG-AARCH64-REGALLOC` diagnostic rather than a crash.
- Peephole `buildPredecessorMap` rewritten to record both conditional-branch targets and fallthrough successor edges; previously missing fallthrough entries caused incorrect single-predecessor detection at boolean-join blocks, silently miscompiling short-circuit boolean expressions.
- Target-platform flags (`--target-darwin/linux/windows`) thread through object-writer, native linker, and assembler target triple.
- `switch.i32` edge arguments route through dedicated edge blocks; larger tables lower as balanced binary decision trees.
- Trap ABI: `idx.chk` → `rt_trap_raise_error`; checked div/rem → `rt_trap_div0` / `rt_trap_ovf`; `trap.from_err` marshals error code into `x0`.
- Checked FP casts lower as `FRintN` + NaN/range guards with distinct `InvalidCast` vs `Overflow` errors; `fptosi` traps NaN and signed overflow; FP compare predicates match IEEE NaN semantics.
- Logical-immediate bitwise: `AndRI`, `OrrRI`, `EorRI` with correct ARM64 encoding. BTI/PAC/compact-unwind gated to Darwin. Unsigned division → `UmulhRRR` magic-multiply.
- **Windows ARM64** — `IMAGE_REL_ARM64_*` COFF relocation constants; section-relative reloc via instruction bit-field decode; MSVC runtime/SEH helper stubs; `ExitProcess` startup import; dynamic import wiring. Build scripts (`build_demos.cmd`, `CMakeLists.txt`) default to host architecture; `--arch` / `VIPER_DEMO_ARCH` override. Portability pass covers 64-bit atomic CAS, Win32 threading timeout, TLS chain, D3D11 enumeration, LC_NUMERIC isolation, and VM bridge alignment.

**x86_64**
- `MOVZXrr8` and `MOVZXrr32` are now distinct MIR opcodes with correct per-form binary encoding.
- `select` lowers through explicit MIR pseudos; large displacements materialized; invalid `alloca` sizes rejected; compare-branch folding preserves live materialized booleans.
- `fptosi` NaN + overflow checks before `CVTTSD2SI`; `sdiv.chk0` traps `INT64_MIN / -1`; incoming `i1` parameters normalized at entry.
- Sub-width checked narrowing preserves annotated result width; Win64 shadow space reserved even for call-free functions.
- `ErrGetMsg` → `rt_throw_msg_get`; `idx.chk` → `rt_trap_raise_error`; unknown opcodes are hard emitter errors.
- `conditionSuffix` and `condCodeFor` throw `std::runtime_error` instead of asserting on unrecognised condition codes.

**IL and VM**
- Variadic functions: `Function::isVarArg`, `...` syntax parsed/serialized/verified; `>= paramCount` arity enforced for variadic callees.
- `err.get_*` min operand count relaxed to 0 for context-implicit native EH lowering.
- **BytecodeVM global variables** — `LOAD_GLOBAL_ADDR` opcode (`0x2B`); `registerGlobals()` compiles IL `Global` declarations at module-load time with typed initializers for I64/F64/Str; string globals initialized via `rt_string_from_bytes`; per-slot ownership tracking (`globalsStringOwned_`) with `clearGlobalStringOwnershipForRawStore()` called on every store path to prevent double-release.
- `VMInit` throws `std::runtime_error` on bad state instead of calling `std::abort()`.
- `OpHandlers_Memory`: `minimumAlignmentFor` returns 0 for unknown memory kinds; `handleLoadImpl` / `handleStoreImpl` detect alignment 0 and dispatch `RuntimeBridge::trap(InvalidOperation)` instead of asserting.
- `Marshal::toI64` / `toF64` call `RuntimeBridge::trap(InvalidOperation)` on type mismatch rather than `std::abort()`.
- `NetworkRuntime` and `ThreadsRuntime` now catch `RuntimeTrapSignal` explicitly before the generic `std::exception` handler so trap payloads are preserved unmodified.
- IL `PassManager`: `verifyEach` diagnostics always write to `std::cerr` unconditionally; the NDEBUG guard has been removed.
- `Mem2Reg::addIncoming` logs to `std::cerr` on unexpected state instead of asserting.
- Codegen `PassManager` wraps each pass in try/catch; uncaught exceptions become a `V-CG-PASS-EXCEPTION` diagnostic.

**IL verifier hardening**
- Duplicate SSA ID detection across params, block params, and results; dominance violations are now hard errors.
- Branch args: unknown temps → "unknown branch arg"; void args → "void branch arg". Stack-escape through GEP chains and block-param edges detected via fixpoint.
- `VerifyStrategy::IntegerBinary` enables sub-word overflow arithmetic (I16/I32); `And`/`Or`/`Xor`/`Shl`/`LShr`/`AShr` added to verifier table.
- `BranchVerifier` emits "unknown branch condition" / "unknown switch.i32 scrutinee" for missing operand temps; `InstrParser` detects trailing characters after result type annotation.
- `OperandParser` resolves call return types from externs, functions, and the runtime signature registry.
- `Verifier::verifyAll(module, maxDiagnostics=50)` collects all violations with deduplication by (severity + code + message + loc) and collapses them into a primary `Diag` with attached `DiagnosticNote` entries; `verify()` now delegates to it rather than stopping at the first error.
- Alloca escape detection: the verifier now rejects alloca pointers stored into non-stack destinations or loaded through escaping GEP chains; two new golden IL fixtures (`alloca_load_escape.il`, `alloca_store_escape.il`) lock in the expected diagnostics.
- `EffectFacts` + `directCalleeEffects` query `RuntimeSignatures`, `HelperEffects`, and `FunctionAttrs` in priority order for per-call-site effect classification.
- Diagnostic codes normalized to `V-IL-WARN` / `V-IL-VERIFY` across all sub-verifiers.

**IL optimizer hardening**
- `CallEffects` priority: registry + function-declaration attrs are authoritative over instruction `CallAttr`; `canEliminateIfUnused` requires both `pure` and `nothrow`.
- `LoadSafety.hpp` — `isLoadKnownNonTrapping` gates dead-load elimination, GVN, and LICM hoisting on pointer provenance.
- `Mem2Reg` SROA offset arithmetic overflow-safe; field iteration order deterministic. `Peephole` brArgs use-counts correct; operand-forwarding scoped to intra-block only.
- `isTerminated(Block&)` replaces stale `BasicBlock::terminated` flag reads across six passes (LICM, CheckOpt, Inline, SiblingRecursion, IndVarSimplify, LoopUnroll).
- DSE / MemorySSA `fullyOverwrites()` requires MustAlias + size coverage; `getAllocaId` walks GEP chains to close pointer-derived escape gap.
- LICM hoists loads through GEP-derived alloca addresses; call classification runs before generic side-effect check.
- LoopRotate: `unordered_map<unsigned,Value>` remap so constants survive cloning; `brArgs` remapped inside `cloneInstr`; latch arity guard.
- LoopUnroll: `checkedAdd()` prevents IV overflow; `isSafeToCloneForFullUnroll()` rejects memory/call/alloca in body.
- IndVarSimplify: `long long` step, `LLONG_MIN` guard. FAdd/FMul removed from `isCommutativeCSE`.
- `ForwardingElimination`: asserts replaced with guarded early returns for fuzz safety.
- DCE alloca elimination wrapped in a do-while fixed-point loop; eliminating one dead store can reveal a second dead alloca that was previously blocked.
- MemorySSA cross-block dead-store analysis rewritten from a BFS-with-`goto` to a recursive memoized DFS with a `visiting` sentinel; back-edges in live loops now correctly return false instead of being treated as killed paths.
- SCCP calls `SimplifyCFG` post-propagation to prune unreachable blocks created by constant-folded terminators.
- `Mem2Reg` SROA pre-reserves `candidates` and `owner` maps to the alloca count before candidate collection, reducing rehashing in large functions.
- `BasicAA::queryRuntimeEffect` now consults `findRuntimeSignature` (O(1)) first, then `all_signatures()` linear scan, then `classifyHelperEffects`; removes stale size-tracking rebuild cache that could diverge from `registry_version`.

### Platform input

- macOS: bare arrow keys no longer map to PageUp/Down/Home/End; mouse-wheel delta preserved across coordinate localization.
- Linux: X11 UTF-8 text input delivers every codepoint; clipboard implemented for editor widgets.
- Low-level library hardening: ViperGUI rejects malformed TrueType table ranges and truncated glyph data, ViperGFX clips extreme drawing coordinates before rasterization and bounds XDND URI parsing, and ViperAUD rejects oversized resample buffers before allocation.

### Tests

- Crackman headless movement regression probe (`movement_probe.zia`) with ARM64 native e2e test (`test_crackman_movement_native.sh`) — compiles to IL, runs `codegen arm64 --native-link -O2`, asserts `RESULT: ok`. `RTZiaCompletionStubTests` — 8 tests verifying all completion stubs return protocol-shaped unavailable payloads. 22 new GUI audit test cases in `test_vg_audit_fixes.c` (15 round-3, 7 round-4) covering layout constraints, image BMP decoding, widget lifecycle retire/tombstone, POSIX regex search, modal event routing, and textinput undo hardening.
- ~20K lines of new coverage: Canvas3D production harness, async 25-iteration race loop, `viper_display` CTest resource lock, human-manager baseball probes, 2D-graphics contract suites, and overflow-boundary tests for every hardened parser.
- 11 Localization test files (~360 assertions); 3 libFuzzer harnesses for plural-rules / CLDR date-patterns / locale-JSON (gated on `VIPER_ENABLE_FUZZ`).
- IL optimizer unit tests: DSE MayAlias/narrow-overwrite preservation, MemorySSA GEP escape, LICM pure-call hoisting, LoopRotate constant-backedge remap, LoopUnroll exit-value correctness, IndVarSimplify loop-local base, BranchVerifier unknown-operand, InstrParser trailing-annotation, ValueKey FAdd non-commutativity.
- Boolean short-circuit goldens (`boolean_andalso` / `boolean_orelse`) for BASIC-to-IL and IL suites; 3D character FBX assets (Knight + RPG Characters packs) for skin/animation regression; 4 IL source benchmarks (`fib_stress`, `inline_stress`, `redundant_stress`, `udiv_stress`) replace compiled binaries.
- New `test_bytecode_compiler_diagnostics.cpp`: `compileChecked` surfaces coded failures (`V-BC-UNKNOWN-GLOBAL`, function-table overflow) with source locations; new `test_vm_executor_compile_diagnostics.cpp` covers the executor compile-fail path.
- New `test_il_verify_all.cpp`: `verifyAll` deduplication and note attachment across multiple violations; new `DiagnosticCliTests.cmake` e2e test for `--diagnostic-format` flag.
- New Zia test `FixedArrayOfStructsUsesInlineElementStride` for the inline aggregate fix; verifier golden fixtures for alloca-load and alloca-store escape; extended `MemorySSATests` (cycle-safe DFS case) and `test_SignaturesPurity` (idempotent registry).
- New `test_vm_init_diagnostics.cpp`: VMInit error raises a typed exception rather than aborting. New `TlsSignaturePolicyTest.cmake`: asserts 0x0503 is absent from ClientHello extensions. `NoAssertFalseTest.cmake` scope widened to subdirectories. 3D baseball smoke probe re-enabled in `CMakeLists.txt`. BASIC sema class-member body tests added. `ThrowingPassBecomesDiagnostic` codegen pass-manager test added.

### Demos & docs

Demos: human-manager baseball franchise simulator (new), Crackman (Pac-Man rewrite with session/progression/audio-bank split), two new 3D demos, Paint gains layers + undo, ViperIDE picks up file-watcher and context-menu null-active-document guards, XENOSCAPE boss + player fixes, Chess click-vs-drag detection fix (dragStartX/Y fields, click-on-origin keeps selection for click-to-move), three `examples/localization/` programs, Windows ARM64 smoke coverage for 3dbowling / 3dscene / baseball, 3D baseball smoke probe re-enabled after model + Zia source fixes, ViperSQL example gains DEFAULT parameters for stored procedures (`FuncParam.hasDefault`, `substituteSQLArgs`). Docs: `viperlib/` sweep across all subsystems, new `viperlib/graphics/production2d.md` and `viperlib/localization/` set, `README.md` master snapshot, `il-guide.md` Optimizer Correctness Contract, Doxygen pass across ~100 runtime files, updated debugging/tools/il-reference/il-guide for new diagnostic flags and verifyAll API, BASIC grammar and runtime reference updated for USING EH semantics and class member scoping, GUI viperlib docs updated for widget ownership guards, regex search, finalizer lifecycle, static Theme class, and image loading.

---

### Commits

See `git log a91d388db..HEAD -- .` for the full 92-commit history. The pattern throughout is feature introduction followed by hardening follow-ups in the same subsystem.

<!-- END DRAFT -->
