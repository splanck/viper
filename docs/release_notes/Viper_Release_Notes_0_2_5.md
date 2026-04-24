# Viper Compiler Platform — Release Notes

> **Development Status:** Pre-Alpha. Viper is under active development and not ready for production use.

## Version 0.2.5 — Pre-Alpha (DRAFT — unreleased)

<!-- DRAFT: release date TBD. v0.2.4 was cut on 2026-04-13. -->

### What this release is about

A polish-and-hardening cycle with three notable new capabilities.

- **Hardening across every runtime subsystem.** Integer-overflow, handle-validation, timeout-clamping, and lifetime-correctness passes applied consistently to graphics, text, threads, audio, and network. Most code paths now fail cleanly on malformed input instead of producing wrapped-small allocations, dangling handles, or stalled state machines.
- **Production 2D Graphics module (new).** ~40 new classes on top of `Pixels` / `Canvas` / `ParticleEmitter` — offscreen surfaces, texture handles, a retained command stream, materials / shaders / post-effects, viewport scaling, tile / object layers, path + shape drawing, text layout, nine-slice UI, debug-draw, animation, a render-pass graph, and external-format adapters.
- **Full 3D asset pipeline.** glTF and FBX now import real skeletons + per-vertex skinning + animations (including glTF sparse-accessor morph deltas and KHR texture-transform / emissive-strength / unlit / lights-punctual extensions). HDR RenderTarget3D, deterministic shadow-light selection, and a backend-capability introspection surface (`Canvas3D.BackendCapabilities` / `BackendSupports`) complete the picture.
- **Network stack became a platform.** TLS-backed `HttpsServer` + `WssServer`, from-scratch HTTP/2 (HPACK + stream reuse), native RSA, in-tree X.509 chain validator (macOS no longer links `Security.framework`), cookie jar, streaming downloads, chunked request bodies, SSE reconnect, connection pooling.
- **Viper.Localization.* (new).** Eleven-class namespace giving programs locale-aware number / date / relative-time / list formatting, translation catalogs with fallback chains, CLDR plural selection, locale-aware collation, and text-direction utilities. Zero external dependencies (no ICU, no libintl); en-US baked into the runtime, all other locales load from JSON via filesystem or VPA-embedded assets.
- **Native codegen correctness pass.** AArch64 and x86_64 backends received a multi-round hardening sweep: variadic IL (`...`) supported end-to-end through parser / serializer / verifier / both backends / VM; checked FP casts distinguish `InvalidCast` from `Overflow`; sub-width annotated overflow arithmetic uses the annotated type width for range checks; `fptosi` traps on NaN/overflow on both backends; target-platform flags (`--target-darwin` / `--target-linux` / `--target-windows`) thread through object format, native linker, and assembler target; BTI/PAC emission gated to Darwin; `err.get_*` operand relaxed to optional for native EH; `MOVZXrr8` / `MOVZXrr32` split into correctly-encoded distinct MIR opcodes.

The biggest user-visible new thing is a text-mode baseball-franchise simulator built on the existing baseball engine.

### By the Numbers

| Metric | v0.2.4 | v0.2.5 | Delta |
|---|---|---|---|
| Commits | — | 72 | +72 |
| Source files | 2,869 | 2,947 | +78 |
| Production SLOC | 450K | 507K | +57K |
| Test SLOC | 183K | 207K | +24K |
| Demo SLOC | 177K | 189K | +12K |

Counts via `scripts/count_sloc.sh`.

---

### Audio

- WAV / OGG / MP3 now share one decoder dispatch. Float32 WAV streams, large headers, incremental MP3, long SoundBank keys, crossfade pause + resume, ADSR short-note release, silent-channel skip — all fixed.
- `Music.SetLoop(flag)` is IL-bound so Zia can flip loop mid-playback; new `Audio.Update()` drives crossfade ticks from outside `Playlist.Update`.
- Sound / Music handles now carry magic tags (`VSND` / `VMUS`) and validate before dereferencing. Decoded sound size capped at 100 MiB; sample rate capped at 384 kHz.
- vaud mixer picked up `source_channels` tracking, `stream_eof` / `stream_loop_pending` flags, seek-past-end clamping, and voice-ID wrap recovery.
- MusicGen rejects beats at or past the song's max span; Playlist shuffle now uses the runtime's `rt_rand_int` for uniform distribution.

### GUI Library

Four rounds of widget audit plus a late-cycle app-registry + widget-family overhaul. Themes: dark-theme palette refresh, HiDPI consistency, keyboard accessibility, lifetime correctness, handle validity.

- **CodeEditor, TabBar, Toolbar/MenuBar, FindBar, Dropdown, TextInput (single + multi-line), TreeView, ListBox, Spinner, SplitPane, FileDialog, CommandPalette** — keyboard navigation, redo + word-select, multi-select with Ctrl / Shift, ellipsis fitting, press-release coupling, typeahead, panel placement, save-name editing. Behavior now matches desktop-app conventions.
- **Dialog, Tooltip, Notification, Breadcrumb, ContextMenu, FloatingPanel** — rewritten paint: rounded card, scaled metrics, text wrap, fade + slide animation, anchored screen-bounds, clip-aware text.
- **Layout and framework** — flex non-stretch alignment, VBox/HBox margin budgets, SplitPane keyboard nav, synthesized double-click, Tab / Shift+Tab focus traversal at dispatch level.
- Lifetime crashes closed across Tooltip (dangling pointer on destroy), Dialog (nested-close use-after-free), Notification (auto-dismiss), CodeEditor (line-slot stability), VideoWidget (destroy order).
- **App-registry + widget-family refactor.** `rt_gui_app.c` gains `s_registered_apps` + `s_destroyed_app_handles` arrays enabling safe is-this-a-live-handle queries across lifecycle (`rt_gui_is_app_handle_known` / `register_app` / `unregister_app` / `forget_destroyed_app_handle`). Coordinated refactor across codeeditor / features / internal / menus / system / widgets / widgets-complex with typed nullable-string helpers (`rt_str_empty`, size-aware casts) and a handle-validation pass on every public entry; `vg_widgets.h` / `vg_image.c` / `vg_listbox.c` pick up matching polish (dangling paint-callback guards, multi-select accumulator tightening).

### Graphics runtime (2D)

- **Canvas hardening** — `RT_CANVAS_MAGIC` guard on every incoming `void*`; `CopyRect` / `Screenshot` carry `Pixels` return types through IL. Shared HiDPI coordinate helpers + explicit-alpha blend flag.
- **Text, camera, sprite families** — UTF-8 codepoint iteration; parallax layers with independent zoom/rotation/scroll; lifecycle + indexing refinements; canvas magic guard adopted everywhere.
- **Production 2D Graphics module (new)** — ~40 CPU-backed classes layered on `Pixels` / `Canvas` / `ParticleEmitter`:
  - **Core rendering:** `RenderTarget2D` / `Surface2D`, `Texture2D` / `GpuTexture2D`, `Renderer2D` retained command stream, `Material2D`, `Shader2D` / `PostProcess2D` (invert / grayscale / tint / blur), `Sampler2D`, `BlendState2D`.
  - **Tiles:** `TileSet2D`, `TileLayer2D`, `ObjectLayer2D`, `AutoTile2D`, `TileChunkCache2D`, `TilemapRenderer2D`.
  - **Animation + camera:** `AnimationClip2D`, `AnimatedSprite2D`, `SpriteRenderer2D`, `CameraRig2D`.
  - **Vector / text / UI:** `Path2D`, `ShapeRenderer2D`, `TextRenderer2D`, `TextLayout2D`, `SdfFont`, `NineSlice2D`, `DebugDraw2D`.
  - **Viewport + transforms:** `Viewport2D` / `ScreenScaler` (fixed-point 1000 = 1.0x, optional integer scaling), `Transform2D`.
  - **Utility:** `CollisionMask2D`, `Hitbox2D`, `Palette2D`, `Gradient2D`, `RenderPass2D`, `RenderGraph2D`, plus `TexturePackerAtlas` / `AsepriteImporter` / `TiledMapLoader`.
- **Overflow hardening** — pixel/tile allocation, PNG/BMP stride math, blit/draw coordinate arithmetic, flood-fill, viewport / transform / collision coordinates, and `Pixels.BlendPixel` compositing all now use saturating integer math or fail cleanly instead of wrapping.

### Graphics3D

- **Asset import end-to-end.**
  - glTF: full attribute coverage (`COLOR_0` / `TANGENT` / `JOINTS_0-1` / `WEIGHTS_0-1`), triangle strips + fans, matrix-authored node transforms, real skin + animation import (LINEAR / STEP / CUBICSPLINE with Hermite tangents), sparse accessors for morph deltas, `extensionsRequired` enforcement, and support for `KHR_texture_transform`, `KHR_materials_emissive_strength`, `KHR_materials_unlit`, `KHR_lights_punctual`.
  - FBX: real scene-node hierarchy, per-vertex skinning (top-4 control-point influences + hard-edge fan-out), cross-platform texture paths, clean binary-reader error propagation, decoded AnimationStack names.
- **Per-instance morph isolation.** `Model3D.Instantiate` now deep-clones morph-bearing meshes so animating one instance doesn't animate every clone; `rt_morphtarget3d_clone` + per-shape tangent deltas complete the capability.
- **Material3D per-slot metadata.** 6 texture slots (base / normal / specular / emissive / metallic-roughness / AO) each with independent wrap / filter / UV-set / UV-transform. Vertex format grew `uv1[2]` (TEXCOORD_1) end-to-end across all four backends.
- **Canvas3D / backends.**
  - `Canvas3D.BackendCapabilities` / `BackendSupports(string)` introspection surface covering SHADOWS / SKYBOX / INSTANCING / POSTFX / etc.
  - Deterministic directional-shadow-light selection from the deferred draw list (tolerance-based equality prevents frame-to-frame flicker).
  - Software rasterizer: PCF shadows, sRGB texture decode, scratch vertex buffer, wrap/filter modes.
  - Metal: blit-staging readback, dedicated postfx scratch texture. OpenGL: row-flip on upload. D3D11: lazy RTT readback.
  - HDR `RenderTarget3D.NewHdr(w, h)` with linear `RGBA32F` CPU mirror and per-backend tonemap on readback.
  - GPU postfx chain runs in authored order; no longer collapses to per-type snapshot.
  - Per-canvas dynamic-light cap raised 8 → 16; bone palette raised 128 → 256.
- **Physics3D dynamic capacity.** Bodies / contacts / events / joints / broadphase entries all grow on demand (overflow-safe doubling) instead of silently truncating. Broadphase sweep-and-prune drops N-body collision from O(N²) toward O(N log N). CCD substeps raised 8 → 64.
- **MorphTarget3D** ships tangent deltas so blendshape deformation doesn't drift normal-map shading. **Scene3D** threads an inherited animator down child nodes so weapon / hat / accessory attachments skin against the parent's pose.
- **NaN / inf sanitization** across Camera3D / Light3D / PostFX3D / Canvas3D setters so bad inputs can't reach the projection builder or shader state.

### Game runtime

- **Dialogue:** rewritten against the real BitmapFont measurement surface; UTF-8 codepoint boundaries for wrap and reveal.
- **Viper.Game hardening pass:** Entity movement now preserves centipixel remainders, sweeps tile collisions, and handles negative coordinates consistently; Pathfinder rejects oversized grids, fixes heap updates and `FindNearest`; Physics2D exposes `CircleBody` and per-step contacts; Game UI validates handles, clips text, and documents tiled nine-slice behavior.
- **Quadtree, Physics2D, PathFollower, SpriteAnimation, SceneManager, ParticleEmitter, Achievement, ButtonGroup:** correctness fixes in zero-length segments, ping-pong completion, overflow geometry, grid validation, transition latching, alpha/pool behavior, and `-1` selection IDs.
- **Input correctness:** action chord release on the same frame the last held key drops; debounced press-edge detection; KeyChord wrong-order reset; real UTF-8 encoding for text input (up to U+10FFFF).
- **Collections diagnostic:** `rt_list_get` traps now include the actual index and count.
- **Async use-after-free fix:** `Async.Run` / `Thread.Start` worker VMs (both bytecode and native) now retain the Future payload past worker unwind. Pinned down by a 25× regression loop.
- **Config.Load** pre-checks file existence so a missing config returns NULL (documented soft-fail) instead of propagating the hardened I/O-layer trap.

### Collections runtime

- String-keyed Map / MultiMap / CountMap / DefaultMap / OrderedMap / LruCache / TreeMap / FrozenMap / FrozenSet / SortedSet / WeakMap now compare full byte-length keys, so embedded NUL bytes no longer truncate identity.
- Ownership fixes across Seq, Deque, List, MultiMap, FrozenMap, SparseArray, DefaultMap, OrderedMap, and TreeMap: retained values are released on overwrite, removal, clear, eviction, and finalization; List slice / clone and List-to-collection conversions no longer leak temporary `Get` references.
- Bytes, BinaryBuffer, Trie, and BiMap now use runtime string byte lengths for binary text payloads; embedded NUL bytes no longer truncate parsing, prefix lookup, or bidirectional string mappings.
- Snapshot APIs were tightened: Trie / SortedSet string sequences own copied strings, IntMap and SparseArray key/index sequences return boxed `i64` values, and Stack iterators snapshot bottom-to-top order without mutating the source.
- Constructor and insertion hardening now traps cleanly on internal allocation failures across Bag, MultiMap, LruCache, Deque, UnionFind, FrozenMap, FrozenSet, SparseArray, BloomFilter, and BiMap. BiMap prepares replacement entries before removing conflicting mappings.
- WeakMap now stores zeroing weak-reference handles, and direct reference-count frees clear registered weak refs before finalization. `Length`, `Has`, and `Keys` report live weak values; `Compact` removes stale slots.
- Overflow hardening landed across sequence growth, queue / stack / priority queue capacity, hash-table resize, count totals, BloomFilter sizing / merge counts, key-copy allocation paths, Bytes range copies, Bytes base64 output sizing, and BinaryBuffer length-prefix writes. Invalid BloomFilter false-positive rates are sanitized.
- SparseArray `Set(index, NULL)` removes the entry, and SortedSet `Range(from, to)` is inclusive with a null upper bound for open-ended scans.

### IO runtime

- File replacement paths (`WriteAllText`, `WriteAllBytes`, `WriteBytes`, `WriteLines`, `Archive.Finish`, `Archive.Extract`, `Archive.ExtractAll`, and `SaveData.Save`) now write exclusive temp sidecars, flush them, and atomically replace the destination. Failed writes trap and clean up temp files instead of leaving partial destinations.
- `Archive.Create(path)` no longer truncates an existing archive before `Finish()`. Stored ZIP entries now validate compressed/uncompressed size agreement, entry data bounds, CRCs, and inflated sizes before returning data.
- Archive parsing now rejects embedded-NUL entry names, inconsistent central-directory entry counts, corrupt local-header offsets, and `ExtractAll` traversal through existing symlinked directories under the extraction root.
- File path arguments reject embedded NUL bytes. Whole-file reads and `File.Size` require regular files, and `File.Copy` now enforces its no-overwrite contract while `Move` preserves replace semantics.
- Stream `FromBinFile` / `FromMemStream` wrappers retain the wrapped object for the wrapper lifetime. Closed or null streams trap on all operations except `Close(NULL)`, and `Write(NULL)` traps instead of silently succeeding.
- Directory cleanup now propagates recursive delete failures while still treating a missing top-level directory as success. `Dir.RemoveAll` removes a top-level symlink without recursing into its target, `Dir.Files` excludes symlinks to files on POSIX, and `Dir.MakeAll` accepts backslash separators on POSIX too.
- `Path.Dir` preserves roots and trims trailing separators correctly; `Path.ExeDir()` is registered in the runtime catalog. `Glob.Match` documents and enforces `(path, pattern)` order, and glob helpers return false/empty on null inputs.
- `BinFile` now performs the required stdio flush/seek handoff when switching read/write direction in update modes.
- GZIP decompression now rejects reserved flags, malformed optional headers, bad FHCRC, CRC mismatches, and size mismatches; raw DEFLATE inflate rejects trailing data after the final block. Compression string helpers release temporary byte buffers eagerly.
- Filesystem asset fallback now treats only regular files as assets and loads zero-byte files as empty `Bytes` instead of returning null.
- `SaveData.Load()` treats a missing save file as a successful empty load and clears stale in-memory entries; malformed JSON or non-integral/out-of-range numbers still leave the prior state intact.

### Localization runtime (new)

Eleven-class `Viper.Localization.*` namespace providing locale-aware formatting, parsing, translation, collation, and text-direction. Zero external dependencies. en-US baked as a static `rt_locale_data_t` record; every other locale loads at runtime from JSON via `LocaleManager.LoadFromJson(path)` or `LocaleManager.LoadFromAsset(name)`.

- **Locale / LocaleInfo / LocaleManager.** BCP-47 parser with case canonicalization, fallback-chain walk (`en-Latn-US → en-US → en → root`), rwlock-guarded process-global registry, lazy-init bootstrap with system-locale detection (Windows `GetUserDefaultLocaleName`; macOS/Linux `$LANG` / `$LC_ALL` / `$LC_MESSAGES` cascade with `.UTF-8` / `@modifier` suffix stripping). No Foundation.framework dependency on macOS.
- **NumberFormat (format + parse).** `Decimal` / `DecimalN` / `Integer` / `Percent` / `Currency` / `CurrencyOf` / `Scientific` / `Ordinal` format methods paired with `ParseDecimal` / `ParseInteger` / `ParseCurrency` (traps) and `TryParse*` (`Optional<f64>` / `Optional<i64>`). Six rounding modes (`halfEven` default, `halfUp` / `halfDown` / `up` / `down` / `ceiling` / `floor`). Strict mode rejects ambiguous group placements; lenient accepts.
- **DateFormat.** CLDR pattern-letter emitter supporting `y / M / d / E / H / h / m / s / a` plus quoted literals (`'text'`, `''` for literal apostrophe). Canonical `Short` / `Medium` / `Long` / `Full` / `TimeShort` / `TimeMedium` / `DateTimeShort` / `DateTimeMedium` / `Custom` / `DateOnly` methods plus `MonthName` / `DayName` (with abbr toggle) / `AmPm`.
- **RelativeTimeFormat.** Auto unit selection across 7 thresholds (year / month / week / day / hour / minute / second) with plural form picked via `PluralRules`. `FormatFrom(then, now)` + explicit `Numeric(value, unit)` entry. Past/future sign-aware template expansion.
- **MessageBundle.** `FromMap` / `LoadFromJson` / `LoadFromAsset` construction; `Get` / `TryGet` / `Has` lookup with fallback-chain walk (depth-16 cap, self-cycle detection on `Fallback` assignment); `Format` named placeholders `{name}`; `FormatWith` positional `{0}` / `{1}`; `Plural(key, n, vars)` routing through `PluralRules` with `<key>.other` fallback.
- **PluralRules.** CLDR cardinal + ordinal rule chains evaluated on a compact AST (7 node kinds: `TRUE`/`OR`/`AND`/`EQ`/`NE`/`VAR`/`INT`). Five operand variables (`n` / `i` / `v` / `f` / `t`) computed from either int or double inputs with trailing-zero handling. en-US rules cover `one` (cardinal) and `one`/`two`/`few`/`other` (ordinal — correctly handles the 11th/12th/13th teen-exception).
- **Collator.** DUCET-lite weight classifier covering basic Latin + Latin-1 Supplement + Latin Extended-A, with sv-SE tailoring (å/ä/ö after z). Strengths 1 (primary) / 2 (secondary) / 3 (tertiary, default); strength 4 clamps with warning. `IgnoreCase` / `IgnoreAccents` toggles elide tertiary / secondary levels. `SortKey` returns hex-encoded bytes whose byte-wise comparison matches `Compare`. `Sort` returns a fresh ordered `List<String>`.
- **ListFormat.** 0 / 1 / 2 / 3+ item joining via the locale's `And` / `Or` / `Unit` / `Short` templates with `pair` / `start` / `middle` / `end` recursive combine.
- **TextDirection.** Strong-RTL classifier covering Hebrew / Arabic / Syriac / Thaana / N'Ko plus presentation forms. `Detect` / `IsRTL` / `IsLTR` / `FirstStrong` / `Bidi` (RLO/PDF wrapping for mixed runs; does not implement the full Unicode BiDi algorithm).
- **Shared `numfmt_group_digits()` helper.** Extracted from the existing `Viper.Text.NumberFormat.Thousands` into `rt_numfmt_internal.h`; consumed by both the existing `Viper.Text.NumberFormat.*` surface and every new `Viper.Localization.NumberFormat.*` path. Zero public-surface breakage.
- **Hardening continuation.** `LocaleManager` JSON / VPA asset loading + filesystem search-path registry fleshed out (`LoadFromJson` / `TryLoadFromJson` / `LoadFromAsset` / `TryLoadFromAsset` / `AddSearchPath` / `SearchPath` / `Unload` / `Reset` all live). `rt_numformat` format and parse now route through a C-locale isolated `snprintf` wrapper matching `rt_fmt.c`'s pattern (POSIX `uselocale`, Windows `_create_locale` + `_vsnprintf_l`) — eliminates LC_NUMERIC interference from host-locale state. `rt_collator` picks up a finalizer (`col_finalizer`) that releases the captured locale data and the handle reference so Collators no longer leak. `rt_text_direction`'s UTF-8 decoder was rewritten with explicit first-byte range checks (C2-DF / E0-EF / F0-F4) and overlong-encoding rejection via `min_cp` validation.
- **Refcount + lifetime discipline pass across every Localization class.** Finalizers in `rt_plural_rules`, `rt_reltime_format`, `rt_message_bundle`, `rt_collator`, `rt_locale` now route handle release through a canonical `rt_obj_release_check0` → `rt_obj_free` path instead of bare `rt_heap_release` — GC-tracked objects no longer leak their follow-on state on zero-ref. `rt_locale_manager.c` adds atomic (`__atomic_load_n` ACQUIRE) reads of `formatter_refs` so `LocaleManager.Unload` in-use checks observe the real count without serializing through the rwlock. `MessageBundle` now retains the locale-data record directly so `Plural()` lookups work without the caller holding a live `PluralRules` instance.
- **DateFormat pattern emitter: locale-digit + UTF-8.** `rt_dateformat_patterns.c` adds a `digit_spans_t` table so CLDR patterns emit the locale's `digits[]` (Arabic-Indic / Devanagari / Thai / etc.) instead of hard-coded 0-9. UTF-8 codepoint boundaries respected when slicing for narrow MonthName / DayName forms; unknown codepoints fall through to literal emission.
- **PluralRules INT64_MIN precision.** Added an `i_d` (integer part as double) operand alongside `i` (int64). Without it, a rule querying the integer-part absolute value with input `INT64_MIN` overflowed during negation; `i_d = fabs((double)n_i64)` preserves magnitude without losing precision at the signed-64 extreme.

### Core runtime formatting

- **`rt_fmt` locale-aware rewrite.** Float / currency / percent paths now route through an isolated C-locale `snprintf` (LC_NUMERIC swap on POSIX via `uselocale`; `_create_locale` / `_vsnprintf_l` on Windows) so numeric output stays deterministic regardless of process-wide `setlocale` state. Float formatting adds thousands-separator / grouping support and integrates with the updated `rt_string_format` pipeline.
- **Float formatter set to `%.15g`.** `rt_fmt_num` and `rt_format_f64` both emit `%.15g` — 15 significant digits, trailing zeros stripped — which matches the historical Viper / BASIC golden format across `PRINT`, `STR$`, and the six golden-bearing BASIC tests (`basic_random_repro`, `basic_numerics_val`, `arm64` and `vm` `comprehensive_control_flow_strings`). Not a strict IEEE-754 round-trip (values like `1.0/3.0` lose precision), but stable across host locales and matches every pre-existing golden.
- **`rt_fmt_bool_yn` lowercase.** `Viper.Fmt.BoolYN(true)` → `"yes"`; `(false)` → `"no"`. BUG-015 regression contract (lowercase tokens match what users type at yes/no prompts).
- **BASIC `VAL` permissive parse restored.** `rt_val` uses plain `strtod` + leading-whitespace skip + trailing-garbage tolerance, preserving BASIC semantics where `VAL(" -12.5E+1x")` returns `-125.0`. The stricter `INPUT`-style `rt_to_double` / `rt_to_int` retain their all-or-nothing parse contract.
- **`rt_parse` tightening.** Stricter integer and double parsers; expanded `rt_input_numeric_fail` coverage; RTParseTests now exercises the failure matrix.
- **`rt_log` / `rt_numeric` / `rt_numeric_conv` / `rt_string_format`** pick up matching string-allocation, log-level-routing, and representability guards tied to the formatting rewrite.

### Time runtime

- **Overflow-checked arithmetic across every module** (`DateTime`, `DateOnly`, `DateRange`, `Duration`, `RelTime`, `Stopwatch`, `Countdown`). Shared `dt_checked_add_i64` / `dt_checked_sub_i64` / `dt_checked_mul_i64` helpers (preferring `__builtin_*_overflow` on GCC/Clang, portable fallback elsewhere) now route every public arithmetic path. `AddSeconds`, `AddDays`, `Diff`, and their Duration / Stopwatch / Countdown / RelTime counterparts trap on signed 64-bit overflow via `rt_trap_ovf()` instead of wrapping silently. Unit-conversion helpers (days ↔ seconds, ns ↔ μs/ms/s, span composition) are all overflow-checked.
- **Representability hardening for platform time APIs.** `DateTime.Create` now range-checks components before `mktime`, accepts valid timestamp `-1` by round-tripping the normalized `struct tm`, and rejects non-representable results. DateTime and DateRange formatters validate `int64_t` → `time_t` before localtime/gmtime calls. `DateOnly` days-since-epoch conversion now traps cleanly for extreme year/day inputs instead of overflowing intermediate calendar math.
- **Countdown and Stopwatch timing fixes.** Countdown now traps if no monotonic or realtime clock is available, uses checked millisecond math, and sleeps long waits in chunks instead of returning early after one platform-limited sleep. Stopwatch and Clock timestamp conversions use checked scale/add math for ns/us/ms values.
- **Duration edge-case correctness.** `Duration.Abs(INT64_MIN)` and `Duration.Neg(INT64_MIN)` now trap because the positive magnitude cannot be represented; ISO duration formatting also avoids zero-length `snprintf` writes after its fixed buffer fills.
- **snprintf length validation** on every string producer (`ToIso`, `ToLocal`, `ToString`, format helpers): negative returns become empty-on-error, and lengths `>= sizeof(buffer)` clamp before handing bytes to `rt_string_from_bytes`, eliminating a class of out-of-bounds reads on truncation.
- **Tightened constructor validation.** `Create`, `FromComponents`, `ParseIso` reject out-of-range fields up front instead of reaching the underlying `mktime` / `timegm` with undefined inputs.

### Text runtime

- **Overflow + validation hardening across the parsers.** JSON streaming gained an explicit per-depth context state machine (malformed `{key,}` / `[,,]` fail at the offending token); regex enforces `INT_MAX` on internal offsets; StringBuilder raises explicit overflow traps; TOML, CSV, Scanner, Template, Codec, GUID, numfmt, parse all got length / depth / byte-count guards.
- **SemVer:** `Version.Parse` is strict SemVer 2.0.0 (required `MAJOR.MINOR.PATCH`, identifier rules, numeric overflow, no fixed format buffers).
- **JsonStream value accessors:** `BoolValue`, `NumberValue`, `StringValue`, `HasNext`, `Next`, `Skip`, `Error` let streaming consumers extract values directly instead of exiting and re-parsing.
- **Typed sequence returns:** `TextWrapper.WrapLines` and `Template.Keys` now return `Seq<String>` instead of stringly-joined blobs. `rtgen` recognizes `seq<T>` / `list<T>` IL types and emits proper Zia generics.

### Threads runtime

- **Timeout and deadline math** clamp rather than wrap everywhere (Future, Monitor, Channel, ThreadPool, Scheduler, Debounce, Async, ConcQueue, ConcMap). Eliminates a ~49-day Win32 hang caused by sign-extending a negative int64 to `DWORD`.
- **Parallel execution:** cross-platform `failed` flag, centralized sync teardown, `setjmp` worker-trap recovery that cleans up the whole block.
- **Thread-handle validation:** `VTRD` / `VTSF` magic tags; `SafeThreadCtx` captures thread-callback traps into an error buffer the joiner can observe.
- **Gate / Barrier / RwLock lifetime:** `closing` + `cancelled` flags with a `can_delete` handshake — no more unblock-and-delete races. Trap messages identify the primitive type.
- **VM trap-interceptor:** `RuntimeBridge::interceptTrap` lets thread / async trampolines catch VM traps and surface them as Future errors instead of unwinding across thread boundaries.

### Network

- **TLS-backed servers (new).** `HttpsServer` + `WssServer` with cert + EC/RSA private-key loading, SNI validation against leaf CN/SAN, ALPN for h2 / http/1.1, per-connection send-timeout.
- **HTTP/2 transport (new).** In-tree frame layer + HPACK (static and dynamic tables) + static Huffman. HTTPS client pools HTTP/2 connections across requests; `HttpsServer` auto-switches to HTTP/2 when ALPN selects h2 — same route / handler surface. Critical ALPN bugfix: `clienthello_offers_alpn` was silently rejecting every multi-token preferred list due to `strlen()` on a non-NUL-terminated substring. `HttpReq.SetForceHttp1` pins to HTTP/1.1 when needed.
- **HTTP client.** RFC-compliant cookie jar, transparent gzip, streaming `Http.Download()`, relative-Location resolution, non-blocking connect, keep-alive + connection pooling, cross-origin redirect strips credentials, CRLF injection guards.
- **HTTP server.** Chunked request bodies, Connection-header-aware framing (keep-alive vs close), thread-safe Start/Stop, ephemeral port via `port=0`, HTTP/1.0 default close.
- **TLS.** Per-request `SetTlsVerify`, comma-separated ALPN preference, thread-local error diagnostics. New native RSA (PKCS#1 + SPKI parsing, Montgomery ladder, RSA-PSS-SHA256). In-tree X.509 chain validator with EKU enforcement drops macOS `Security.framework` dependency. In-tree `timegm` replaces the last platform time call.
- **Crypto.** PBKDF2 iterations raised 100K → 300K (and `Password.Hash` floor 10K → 100K); input validation tightened; existing ciphertexts round-trip.
- **Other.** WebSocket subprotocol negotiation, multipart quoted-parameter escaping, SSE auto-reconnect with `Last-Event-ID`, dual-stack IPv6 UDP, SMTP pipeline follow-ups.

### Zia frontend

- Path-aware completion: `CompleteForFile`, `CheckForFile`, `HoverForFile`, `SymbolsForFile`. Relative `bind` paths resolve against the active file.
- Lowerer decomposed into helper classes; one recursive `appendTypeString` covers every type display.
- Tighter sema diagnostics: targeted errors for bare `var x = null;` and for Optional access without `?.` / `!.`.

### Linker, codegen, tools, IL, build

- Linker: C++ Itanium-ABI symbol classification on macOS (with `$DARWIN_EXTSN` handling); `uname` / `gethostname` / `sysctlbyname` routed to the right dylib; `link()` + `strnlen()` added to the dynamic-import allowlist alongside their partners (closed two runtime-import-audit gaps opened by the new POSIX no-clobber atomic-move path in `rt_file_ext` and by the localization BCP-47 parser).
- Codegen: `RtComponent::Localization` enum entry with `rt_locale_*` symbol prefix classifier and `Viper.Localization.*` namespace classifier; `viper_rt_localization` archive participates in the toolchain install manifest.
- BASIC frontend: four references in `IoStatementLowerer.cpp` namespace-qualified (`il::frontends::basic::runtime::kConvertToDouble` / `kConvertToInt` / `kParseDouble` / `kParseInt64`) to resolve an rtgen-emitted alias-name collision.
- IL: `Canvas.CopyRect` / `Screenshot` return `Pixels`; `AudioUpdate` added, `AudioEncode` removed; eleven new `RTCLS_Loc*` `RuntimeTypeId` entries. `Viper.Time.RelativeTime.FormatShort` binding corrected from the two-arg Localization method `rt_reltimefmt_short` back to the one-arg free function `rt_reltime_format_short` (the rename-to-avoid-collision pass had flipped the binding).
- rtgen audit: 29 unclassified header symbols now classified via `RuntimeSurfacePolicy.inc` — three internal headers (`rt_collator.h`, `rt_numfmt_internal.h`, `rt_locale_platform.h`) plus eleven individual internal symbols (locale bind / get / manager refcount helpers, plural-rule engine internals). `rtgen --audit` passes with zero findings.
- Tools: frontend `--` separator, collision-safe `native_compiler` temp paths, x64 `--asset-blob` gate.
- Build: VERSION `0.2.4-dev` → `0.2.5-snapshot`; GUI test targets key off `VIPER_BUILD_TESTING`; `scripts/clean.sh` polish.

### Codegen

AArch64 and x86_64 backends received a focused correctness pass spanning target-platform plumbing, ABI fixes, new IL surface wiring, and peephole correctness.

**AArch64**
- `--target-darwin`, `--target-linux`, and `--target-windows` now thread through native object-writer selection, native linker platform, and system-assembler target triple; previously all three fell back silently to the host.
- `switch.i32` edge arguments route through dedicated edge blocks with phi spill-slot stores; untaken cases can no longer clobber the taken successor's values. Larger switch tables lower as balanced binary decision trees instead of flat compare chains.
- Trap ABI: `idx.chk` raises structured bounds errors via `rt_trap_raise_error`; checked div/rem call `rt_trap_div0` / `rt_trap_ovf`; `trap.from_err` marshals the error code into `x0` before calling `rt_trap_raise_error`.
- Checked FP casts (`cast.fp_to_si.rte.chk` / `cast.fp_to_ui.rte.chk`) lower as `FRintN` (round-to-even) + explicit NaN and range guards. NaN and invalid-unsigned inputs raise `InvalidCast`; finite out-of-range values raise `Overflow`.
- Checked FP casts now honor `i16`, `i32`, and `i64` result widths consistently in VM, constant folding, x86_64, and AArch64.
- FP compare predicates on AArch64 now match IEEE/IL NaN semantics for `fcmp_eq`, `fcmp_ne`, and `fcmp_le`.
- AArch64 `FMovGR` is used for FP constant bit-casts; binary emission now rejects class-invalid `FMovRR`.
- AArch64 base-register load/store displacements are preserved; phi-edge copies reject GPR/FPR class mismatches instead of converting.
- AArch64 register allocation has reserved emergency scratch registers for spilled-operand reloads under extreme pressure.
- Sub-width annotated checked arithmetic (`iadd.ovf : i32`, `isub.ovf : i32`, `imul.ovf : i32`) routes to width-sensitive generic lowering; the fast-path dispatcher no longer intercepts sub-width forms before the range guard executes.
- Logical-immediate bitwise: `AndRI`, `OrrRI`, `EorRI` MIR opcodes added; text emitter and binary encoder generate the correct ARM64 logical-immediate encoding for all three.
- Plain `fptosi` traps on NaN and signed `i64` overflow before `FCvtZS`.
- BTI / PACIASP / AUTIASP emission gated on target policy — Darwin targets emit the hardening sequence; Linux and Windows targets skip it. Compact-unwind records emitted only for Darwin objects.
- Unsigned division by arbitrary constants strength-reduces to magic-multiply via `UmulhRRR`.
- `ErrGetMsg` lowers to `rt_throw_msg_get`.

**x86_64**
- `MOVZXrr8` (byte `SETcc` zero-extension, encodes as `movzx r64, r8`) and `MOVZXrr32` (32-bit-write zero-extension, encodes as `movl`) are now distinct MIR opcodes with the correct per-form binary encoding; the prior single opcode was a latent encoding-table divergence.
- `select` now lowers through explicit MIR pseudos, and both text/binary emission fail if a select pseudo survives ISel.
- Large GEP/load/store displacements are materialized instead of truncated to imm32; invalid `alloca` sizes are rejected.
- Compare-branch folding now preserves materialized booleans that are still live, and regalloc liveness ignores in-block local branch labels when building CFG edges.
- Plain `fptosi` performs explicit NaN and signed `i64` range checks before `CVTTSD2SI`; traps on failure, matching VM semantics (no longer UB).
- Sub-width checked narrowing (`cast.si_narrow.chk`, `cast.ui_narrow.chk`) preserves the annotated result width and traps `Overflow` on range failure.
- Incoming `i1` parameters normalized at function entry on both register and stack paths — backend code never observes a non-canonical truthy value.
- Win64: `main` reserves the mandatory 32-byte shadow space for the injected stack-safety init call even when the user function contains no other calls.
- `sdiv.chk0` traps `INT64_MIN / -1` via `rt_trap_ovf`; `srem.chk0` returns `0` for `INT64_MIN % -1` (VM-contract preservation — the remainder is mathematically defined as zero but the divide would overflow).
- `ErrGetMsg` lowers to `rt_throw_msg_get`; `idx.chk` and checked-cast failures use `rt_trap_raise_error` instead of raw `ud2`.
- Unknown MIR opcodes are hard emitter errors; the text emitter no longer comments them out and continues.
- Target-platform flags drive assembly dialect, native object format, symbol mangling, and native-linker platform together.

**IL and VM**
- Variadic functions: `Function::isVarArg` field; `...` trailing-param syntax parsed, serialized in the IL round-trip, and enforced by the verifier (`>= paramCount` arity for variadic callees, exact match for fixed). BytecodeVM dispatch updated.
- `err.get_kind` / `err.get_code` / `err.get_ip` / `err.get_line` / `err.get_msg`: min operand count relaxed from 1 to 0 — native EH lowering can emit the no-operand (context-implicit) form while source IL continues to pass an explicit `%err` operand. VM schema and `SpecTables.cpp` updated to match.
- `rtgen` audit: `rt_canvas_is_handle`, `rt_pixels_generation`, and `rt_locale_manager_lookup_data_retained` classified as internal in `RuntimeSurfacePolicy.inc`; audit passes with zero findings.

### Platform input

- macOS: bare arrow keys no longer map to PageUp/Down/Home/End (Fn-flag gate was too loose); mouse-wheel delta preserved across coordinate localization.
- Linux: X11 UTF-8 text input delivers every codepoint; clipboard operations implemented for editor widgets.

### Tests

Roughly 20K lines of new coverage across runtime, GUI, codegen, linker. Highlights: Canvas3D production-readiness harness with fake backend hooks; async 25-iteration repeat loop; shared `viper_display` CTest resource lock for display-holding smoke tests; human-manager baseball probes; new 2D-graphics contract suites; overflow-boundary tests for every hardened parser; 11 new Localization runtime test files (~360 assertions); 3 libFuzzer harnesses for the plural-rule parser, CLDR date-pattern parser, and locale-JSON loader (gated on `VIPER_ENABLE_FUZZ`).

### Demos & docs

Marquee demo addition is a text-mode human-manager baseball-franchise simulator; Pac-Man was rewritten and renamed as Crackman (session / progression / frontend split, audio banks); two new 3D demos (`3dbaseball`, `3dscene`), Paint gains layers + undo/redo + expanded tools, ViperIDE adopts the new GUI APIs, 3D Bowling gets a cinematic-postfx smoke probe, Chess AI-hardening pass, XENOSCAPE gets the boss-sink-through-floor fix plus the player-facing `Pixels.FlipH` direction fix, and three new `examples/localization/` Zia programs (`hello-localized` / `intl-numbers` / `translated-app`) ship with en-US + fr-FR + messages-en-US JSON data files. Docs: comprehensive `viperlib/` sweep (audio / GUI / crypto / network / graphics / text / threads / time / io), new `viperlib/graphics/production2d.md`, new `viperlib/localization/` set (README + locale + formatting + messages + collation + data-files), `README.md` refreshed to a master snapshot, `docs/basic-namespaces.md` + `basic-reference.md` pick up the Localization namespace, `cross-platform/platform-differences.md` documents locale-detection behavior, and a Doxygen comment pass across ~100 runtime `.c` files.

---

### Commits

See `git log a91d388db..HEAD -- .` for the full 72-commit history. Commits pair feature-add and follow-up hardening in the same subsystem (e.g. Production 2D → overflow hardening; threads timeout fixes → handle-magic validation; glTF skin import → sparse accessors + KHR extensions; new POSIX atomic-move path → linker-policy `link()` classification; `Viper.Localization.*` introduction → `numfmt_group_digits()` extraction + rt_fmt C-locale isolation + GUI app-registry refactor; variadic IL introduction → sub-width checked arithmetic hardening + err-opcode operand relaxation + MOVZXrr8/rr32 split in successive passes).

<!-- END DRAFT -->
