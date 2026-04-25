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
| Commits | — | 80 | +80 |
| Source files | 2,869 | 2,948 | +79 |
| Production SLOC | 450K | 514K | +64K |
| Test SLOC | 183K | 212K | +29K |
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

### BASIC frontend

**Type system:**
- `SemanticAnalyzer::Type::ArrayObject` is a new distinct type for `DIM arr(N) AS ClassName`; was collapsed into `ArrayInt`, silently permitting primitive element assignment. Completion, hover, symbol-server, LBOUND, UBOUND, array-assignment, and FOR EACH all handle the new kind.
- `MethodCallExpr` visitor rewritten: validates runtime class methods via typed argument lookup, falls back to user-class overload resolution, then `Viper.Core.Object` fallback; array-field index arguments validated with B2001 / B2002 diagnostics.
- `isPrimitive` predicates extended to include `ArrayString` / `ArrayObject` so member dispatch works on all array kinds.
- `mustReturn` rewritten as a proper data-flow analysis: `ReturnFlow {alwaysReturns, assignedAfter}` propagated through every control construct (IF / ELSEIF / ELSE, SELECT CASE, TRY / CATCH / FINALLY, EXIT FUNCTION, loops). Partial branches are no longer accepted as exhaustive; function-name implicit return must cover all paths.
- `mainHasGosub_` flag: DFS through main body at analyze time. Top-level `RETURN` without a GOSUB emits B1008; RETURN with a value remains procedure-only.
- `Check_Loops`: EXIT SUB / EXIT FUNCTION validated against active procedure kind; FOR counter, start, end, and step type-checked as numeric; FOR EACH array element type inferred from the array kind and checked against the loop variable.
- FINALLY bodies now analyzed under the surrounding scope (was silently skipped); empty TRY/CATCH/FINALLY warning requires all three bodies empty.
- USING initializer type-checked: scalar initializers produce B3204 "USING initializer must produce an object/resource".
- `TryCatchStmt` added to `ASTUtils.hpp` `STMT_KIND_TRAIT` for `as<>` / `is<>` queries in flow-analysis helpers.

**Lowering:**
- `RuntimeMethodIndex::find(classQName, method, argTypes)`: typed overload scores candidates via `scoreArgMatch` (exact=0, widening=1, unknown=2, incompatible=nullopt) and returns the best non-ambiguous match. All sema and lowering call sites upgraded from arity-only lookup.
- `OopIndex.findMethod` / `findMethodInHierarchy`: case-insensitive fallback scan when exact key lookup fails — fixes BASIC case-insensitive method dispatch for mixed-case class registrations.
- ME and NEW expressions produce `ExprType::Obj` in the scan phase; `MethodCallExpr` return types inferred via typed RuntimeMethodIndex lookup rather than returning `I64` unconditionally.
- `makeRuntimeArgTypes()` / `makeAstArgTypes()` lambdas replace duplicated type-mapping chains in static and instance dispatch.
- Hardcoded `"Viper.Object.ToString"` / `"Viper.Object.Equals"` call sites replaced with `il::runtime::RTCLASS_OBJECT` constant (`"Viper.Core.Object"`).
- Self-method calls perform overload resolution before lowering and coerce arguments per declared parameter types.

**Runtime:**
- `rt_map_get_str`: unsafe direct cast fixed — validates string handle or box type before returning, traps on type mismatch.
- `rt_map_get_opt_str` (new): returns `NULL` for missing keys; used by the `Map.get()` optional-String path so absent keys yield `null` rather than an empty string.

---

### Zia frontend

- Path-aware completion: `CompleteForFile`, `CheckForFile`, `HoverForFile`, `SymbolsForFile`. Relative `bind` paths resolve against the active file.
- Lowerer decomposed into helper classes; one recursive `appendTypeString` covers every type display.
- Tighter sema diagnostics: targeted errors for bare `var x = null;` and for Optional access without `?.` / `!.`.

**Sema and lowerer correctness pass** (see `fix(zia,vm,il)` commit):

- `Map.get(key)` now returns `Optional<V>` instead of `V` in sema, matching the runtime's key-absent semantics; callers must unwrap with `??` or `!.`.
- `list.first` / `list.last` / `list.isEmpty` wired into the collection method dispatch table and lowered to `Viper.Collections.List.First` / `.Last` / `rt_list_is_empty`.
- `list.lastIndexOf` removed from `listMethods[]` (not implemented in the runtime).
- Postfix-try (`?`) expression sema (`analyzeTry`): validates operand is Optional, enclosing function returns Optional, and inner types are compatible. Registered in `analyzeExpr` under `ExprKind::Try`. Lowered `None` early-return now emits `Value::null()` instead of `Value::constInt(0)`.
- `analyzeCoalesce` now diagnoses a non-Optional left operand and checks that the right type is assignable to the unwrapped inner type.
- Collection literal type checking: incompatible element types in list / map / set literals produce specific errors ("List literal contains incompatible element type …"); element type set to `types::error()` so downstream mismatches propagate cleanly.
- Static fields: `staticFields_` set tracks field keys of the form `"TypeName.fieldName"`; inherited field propagation skips static entries; visibility is propagated from parent key into child key; `resolveStaticField()` helper resolves qualified static field access on module-typed bases with private-visibility checking. Generic instantiation arms also mark static fields.
- Generic type inference extended: `inferTypeParamsFromPattern` does full structural inference (Named, Generic, Optional, Tuple, Function, FixedArray parameter shapes); replaces the prior name-only check. Multi-type-argument generics (`func[A, B](…)`) parsed by extending the postfix-index parser to collect comma-separated type args into a `TupleExpr`.
- `genericTypeSubstitutions_` / `genericFunctionSubstitutions_` store the substitution map per mangled name so `pushSubstitutionContext` can restore it during body lowering in a separate pass.
- `mangleGenericName` uses a `mangleTypeArg` helper that recurses through `typeArgs` and replaces non-alphanumeric characters with underscores, making mangled names safe as IL identifiers for complex type arguments.
- All integer arithmetic in the lowerer (`+`, `-`, `*`, `/`, `%`, unary negation, fixed-array stride multiply) unconditionally emits overflow-checking opcodes (`IAddOvf`, `ISubOvf`, `IMulOvf`, `SDivChk0`, `SRemChk0`). The `overflowChecks` compile option no longer gates these paths.
- Static field stores (assignment to `module.field`) resolve the target through `globalVariables_` by qualified name before falling through to setter / instance-field lowering.
- Static getter calls skip the `self` argument push when the runtime descriptor has zero parameters.
- `stmtAlwaysExits` returns `true` for `StmtKind::Throw` so control-flow analysis terminates the block and post-throw code is not reachable.
- EH typed-catch rethrow block rewritten: was `trap.from_err(kind_i32)` which discarded the original exception and synthesised a new trap; now `eh.entry` + `resume.same(tok)` re-raises the original error token preserving all exception context.
- Post-lowering IL verification added to `Compiler.cpp` after phase 4; compilation returns early with a diagnostic if emitted IL fails structural checks.
- `Sema` constructor calls `types::clearClassInheritance()` for clean test isolation.
- **BytecodeVM string cache** (async race fix): `initStringCache()` pre-allocates null slots rather than eagerly calling `rt_const_cstr` for every string. `getStringLiteral(idx)` materializes the `rt_string` handle on first access; both LOAD_STR handlers call it. The eager initialization path had a concurrent-init window when async worker VMs shared the same module object, producing intermittent `invalid runtime string handle` traps (exposed by the 25× async repeat regression loop).

**Parser correctness pass:**
- `isExpressionStart()` predicate covers all literal types, unary operators, and keyword expressions (`if`, `match`, `await`, `new`, `not`); replaces ad-hoc `nextKind` checks for match-arm disambiguation.
- `parseExpressionAllowingStructLiterals()` wrapper enables struct literals in single-expression function/method bodies, variable initializers, field initializers, return/throw statements, and interpolated string expressions — fixes struct-literal parse failures that silently produced malformed ASTs.
- `parseCallArgs(bool&)` / `parseParameters(bool&)` error-propagating overloads abort cleanly on malformed call sites.
- `clonePureExpr()` replaces `cloneLvalueExpr()`: full coverage for SuperExpr, all literal kinds, Field, Index, Unary, Binary (non-Assign), Ternary, Coalesce, Range. Prevents nullptr dereferences in ternary/coalesce lowering from incomplete clone paths.
- Trailing-comma support in parameter lists and list literals; match arm body accepts semicolons as well as commas.

**Sema correctness pass (parser / sema):**
- `propertyDeclForLowering(className, field, &declaringOwner)` walks the inheritance chain and records the defining class; inherited property getters/setters now emit the correct symbol (e.g. `Base.get_answer`, not `Child.get_answer`).
- Struct literal sema: duplicate field names produce "Duplicate field X" errors; field-value type mismatches produce `errorTypeMismatch` diagnostics (was `(void)` no-op).
- `isAssignableTarget()` / `isReadOnlyBuiltinProperty()` guard assignment: non-lvalue targets and count-like properties on collections emit diagnostics.
- Assignment of Unit literal in variable initializer or return statement produces a targeted diagnostic instead of propagating through lowering.
- Unknown field on `List` / `Map` / `Set` / `String` is a hard error with collection type name in the message.
- Optional chain property resolution walks property declarations via `propertyDeclForLowering` and emits getter calls with the correct declaring owner; struct receiver is unwrapped before field load.
- `conversionCost` removes the Unit→Optional shortcut (was cost=1, masking invalid coercions). `defineSymbol` distinguishes externs from variable duplicates.
- `Map.keys()` / `Map.values()` return typed `Seq<K>` / `Seq<V>` (`KeySeqType` / `ValueSeqType` return-kind variants added).
- `Set.remove()` lowering returns `Boolean` (was `Void`), matching the runtime signature.
- String zero-arg methods enforce arity via `checkArgCount(0, …)`; `sortDesc` / `shuffle` added to the zero-arg check.
- `finalFields_` set tracks `"TypeName.fieldName"` for `final` fields across class declarations, inheritance, and generic instantiations. `currentMethod_` pointer distinguishes `init()` context so fields may be written during construction only.
- Optional member assignment (field-expr where base is `Optional`) emits "Cannot assign member on Optional type" rather than silently unwrapping.
- `analyzeField`: direct member access on an `Optional` type is now a hard error — callers must use optional chaining, force unwrap, or a null-check guard.
- `stmtAlwaysExits` (block case) checks only the last statement, not the first exiting one. Unreachable-code warning continues analyzing subsequent statements but suppresses duplicate W002s.
- `analyzeReturnStmt` guards against `expectedReturnType_ == nullptr` (return outside a function/method context).
- Tuple `for a, b in x` validated to require exactly 2-element binding; 3+ emits "Tuple binding requires a 2-element Tuple, got N elements".

**Sema — flow-narrowing extension:**
- `narrowingKeyForExpr(expr)` builds stable dotted keys for field-path narrowing (`"self.child"`, `"obj.field"`).
- `tryExtractNullCheck` generalised: accepts field-expr and ident operands, populates a `TypeRef *checkedType` out parameter so callers don't need a second `lookupVarType` call. All null-check narrowing sites in `analyzeIfStmt`, `analyzeBlockStmt`, and `analyzeBinary` use it.
- `lookupNarrowedType(key)` extracted from `lookupVarType`; called from `analyzeField` to resolve narrowed field types before member access, enabling patterns like `if self.child != null { self.child.name }`.

**Sema — async Future[T] payload type:**
- `types::futureOf(payload)` creates a `Ptr` typed `Future[T]` carrying `payload` as `typeArgs[0]`.
- `functionTypeForDecl` uses `futureOf(declaredReturn)` for async functions; was `runtimeClass("Viper.Threads.Future")` which lost the payload type.
- `analyzeExpr` for `AwaitExpr` reads `typeArgs[0]` directly when the awaited expression is a typed `Future[T]` variable, returning `T` without requiring re-inspection of the original call.

**Lowerer correctness pass:**
- `lowerUnitLiteral()` emits `ConstInt(0)` with Void type.
- `collectRangeModifierChain()` / `lowerRange()` / `lowerRangeWithModifiers()`: range expressions used as rvalues materialize into a counted loop; `.rev()` and `.step(n)` modifier chains supported; step validated ≥ 1 via `emitPositiveStepCheck()` (replaces the prior `IdxChk` that rejected `INT64_MAX`).
- `widenIntegralToI64()` applied to all list `get` / `set` / `insert` / `removeAt` index paths, string index, fixed-array index, and boxed collection index — `Byte`-typed indexes now correctly widen before dispatch.
- `emitIndexCheck()` applied to every fixed-array element access regardless of index integer width.
- `Map.get` optional-String path uses `kMapGetOptStr` — missing keys return `null`, not an empty string.
- `lowerStructLiteral` overhaul: typed defaults per field kind (bool=`false`, float=`0.0`, str=empty-string, ptr=`null`); field initializer expressions lowered for omitted fields; provided fields coerced to declared type via `coerceValueToType`.
- `lowerUnary(AddressOf)`: moved to an early-return path before operand lowering; uses `getFunctionDecl` + `loweredFunctionName` to emit the correct mangled symbol for forward-declared callbacks (`&worker` declared after its use now compiles).
- Unknown identifier and unsupported assignment target emit V3000 diagnostics instead of silently returning `constInt(0)`.
- Map subscript `m[key]` emits a `containsKey` check + conditional `Trap` before `MapGet`; absent keys trap deterministically at the access site.

### Linker, codegen, tools, IL, build

- Linker: C++ Itanium-ABI symbol classification on macOS (with `$DARWIN_EXTSN` handling); `uname` / `gethostname` / `sysctlbyname` routed to the right dylib; `link()` + `strnlen()` added to the dynamic-import allowlist alongside their partners (closed two runtime-import-audit gaps opened by the new POSIX no-clobber atomic-move path in `rt_file_ext` and by the localization BCP-47 parser).
- Codegen: `RtComponent::Localization` enum entry with `rt_locale_*` symbol prefix classifier and `Viper.Localization.*` namespace classifier; `viper_rt_localization` archive participates in the toolchain install manifest.
- BASIC frontend: four references in `IoStatementLowerer.cpp` namespace-qualified (`il::frontends::basic::runtime::kConvertToDouble` / `kConvertToInt` / `kParseDouble` / `kParseInt64`) to resolve an rtgen-emitted alias-name collision.
- IL: `Canvas.CopyRect` / `Screenshot` return `Pixels`; `AudioUpdate` added, `AudioEncode` removed; eleven new `RTCLS_Loc*` `RuntimeTypeId` entries. `Viper.Time.RelativeTime.FormatShort` binding corrected from the two-arg Localization method `rt_reltimefmt_short` back to the one-arg free function `rt_reltime_format_short` (the rename-to-avoid-collision pass had flipped the binding).
- rtgen audit: 29 unclassified header symbols now classified via `RuntimeSurfacePolicy.inc` — three internal headers (`rt_collator.h`, `rt_numfmt_internal.h`, `rt_locale_platform.h`) plus eleven individual internal symbols (locale bind / get / manager refcount helpers, plural-rule engine internals). `rtgen --audit` passes with zero findings.
- Tools: frontend `--` separator, collision-safe `native_compiler` temp paths, x64 `--asset-blob` gate.
- Build: VERSION `0.2.4-dev` → `0.2.5-snapshot`; GUI test targets key off `VIPER_BUILD_TESTING`; `scripts/clean.sh` polish.

### Linker hardening

All four object-file readers (ELF, Mach-O, COFF, Archive) and all three writers (ELF, Mach-O, COFF) received a comprehensive correctness and bounds-checking pass.

- **Reader validation.** Every reader now calls `checkedRange(off, len, size)` before any buffer dereference and uses `readBoundedString` in place of unbounded `reinterpret_cast<const char*>` reads. ELF validates class, endianness, type, section-header size, section data ranges, symbol-table ranges, and relocation ranges. Mach-O preserves `__compact_unwind` / `__eh_frame` as unwind roots and skips debug sections. COFF validates the symbol-table range and string-pool bounds on every long-name lookup; an unsupported machine type is a hard reader error.
- **COFF addend decoding.** `extractCoffAddend` decodes the *instruction bit-fields* for ARM64 COFF relocations (`Branch26`, `Branch19`, `PageRel21`, `PageOff12A`, `PageOff12L`, `ADDR64`) instead of doing a blind 4-byte read, so writer-emitted placeholder instructions do not produce bogus addends. AMD64 correctly handles 8-byte `ADDR64` addends and 4-byte signed addends for all other reloc kinds. Relocation symbol indexes are validated against `NumberOfSymbols` before use.
- **Mach-O reader.** ARM64 `ADDEND` relocation payloads are sign-extended. Non-extern section-relative relocations resolve through synthetic local section symbols, and `__DWARF` debug sections are preserved as non-alloc sections.
- **Archive reader.** Member ranges, symbol-table sizes, string-pool bounds, and long-name offsets all validated. Duplicate archive-index entries keep the first definition, matching standard archive resolution behavior.
- **NativeLinker input compatibility.** Before generating dynamic-import stubs, every real input object is checked against the requested output target — ELF for Linux, Mach-O for macOS, COFF for Windows, with machine-code match enforced. Format and machine mismatches are hard link errors.
- **RelocApplier.** Invalid relocation symbol indexes rejected before address resolution. AArch64 branch targets validated to be 4-byte aligned. Page-offset load/store relocations validate scaled alignment for the instruction size. `Abs32` range checked.
- **BranchTrampoline.** Local targets resolved from the merged section location map by address; trampoline reuse keyed by target address, not display name, so duplicate local labels from different objects cannot alias.
- **Dead strip + ICF.** Dead stripping keeps `.eh_frame`, `.gcc_except_table`, `__compact_unwind`, and `__eh_frame` sections alive as implicit roots. ICF includes local relocation identity in function signatures and skips candidates with extra local symbols in the function section, preventing folds that would strand non-redirectable local labels.
- **MachO writer.** Multi-text emission merges atoms through `CodeSection::appendSection()` before emitting one `__TEXT,__text` section with subsection-by-symbol semantics. Symbol naming: non-local, non-`L`/`.`-prefixed names receive the `_` ABI prefix at serialization; canonical `_`-prefixed C names correctly gain the second underscore. `LC_MAIN` honors a custom `layout.entryAddr` before falling back to `main` / `_main`. Relocations and compact-unwind entries fail fast on an unknown symbol index.
- **COFF writer.** Sections with more than 65,535 relocations use the COFF relocation-overflow record instead of failing or truncating the count.
- **CodeSection.** `appendSection()` merges atoms, relocations, and symbols in order without renaming local symbols. `alignTo(0)` and `alignTo(1)` are no-ops; larger alignments pad with zero bytes.
- **SymbolTable.** `add()` refreshes name lookup on duplicate names while existing relocation entries keep their captured symbol indexes.
- **Binary encoders.** Encoders record canonical, unmangled names in `CodeSection`; platform ABI spelling is applied by the object writer at serialization. A64BinaryEncoder rejects negative or wider-than-32-bit frame sizes before narrowing into instruction immediates.

**Second linker hardening pass** (see `fix(linker,il): native linker + IL module linker hardening pass`):

- **IL ModuleLinker.** Single-module fast path removed — all inputs now run through the full validation pipeline. `buildExportIndex` uses a new `FunctionRef {moduleIndex, functionIndex}` struct and returns an error on duplicate export names. `rewriteFunctionRefs` replaces the old `rewriteCalls` and rewrites both callee-name operands and GlobalAddr / brArgs references across module boundaries. Global-name collision rewriting is scoped per owning module so identically-named private globals in separate modules don't interfere. `booleanInteropCompatible`, `sameSignature`, and `makeUniqueName` helpers complete the merge logic.
- **InteropThunks.** `generateBooleanThunks` skips mismatched-arity function pairs rather than emitting a malformed thunk; non-i1↔i64 type pairs set an `incompatible` flag to abort. Thunk values with empty names receive auto-generated `"t{i}"` names.
- **ELF reader.** Extended section counts read from SH0 `sh_size` when `e_shnum == 0` (ELF spec §4.6). COMMON symbols materialized as zero-filled BSS. Absolute symbols (SHN_ABS) keep their absolute values. Implicit-addend (`.rel`) relocation sections now supported alongside `.rela`. Truncated relocation tables and mismatched `sh_entsize` are hard errors.
- **Mach-O reader.** `__DWARF` and debug sections preserved as non-alloc. ARM64 ADDEND records validated; dangling ADDEND records (not immediately followed by a paired relocation) detected and reported.
- **COFF reader.** Weak external (`IMAGE_SYM_CLASS_WEAK_EXTERNAL`) fallback records parsed. Associative COMDAT section relationships decoded. Relocation overflow records handled. BigObj format rejected with a specific diagnostic instead of being silently misread.
- **All three writers.** COFF: section attribute flags derived precisely from section kind; storage-class assignments corrected for static/external/COMDAT linkage; ARM64 relocation types use `extractCoffAddend` instruction-field decode. ELF: `sh_flags` set correctly for writable/executable/alloc; symbol `st_bind`/`st_visibility` derived from linkage model. Mach-O: `appendSection` used for multi-text outputs; anonymous symbols receive synthesised names before writing.
- **x86-64 binary encoder.** RIP-relative `.text`→`.rodata` references now record a symbolic relocation with the `.text` symbol as entry and `.rodata` target as hint; object writers resolve by name so indexes are never interchanged.
- **Infrastructure.** RelocApplier: bounds-check before each application, PC-relative branch overflow detection. BranchTrampoline: local-target resolution against merged section base. DeadStripPass: EH personality and LSDA added as implicit roots. ICF: off-by-one fix in content-hash comparison with padding. SectionMerger: alignment padding accounts for accumulated offset. SymbolResolver: strong definition wins over weak when both present in one link step. NativeLinker: SectionMerger ordering fix when ICF disabled. ElfExeWriter: `PT_LOAD` alignment written as `0x1000` instead of `0x200000`.

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

**IL verifier hardening** (see `fix(il,verify)` and `fix(il)` passes):

- Duplicate SSA ID detection: function params, block params, and instruction results each call `defineTemp()` through a single registry that rejects duplicate IDs with precise messages identifying both definition sites.
- Dominance violations and uses of values defined in unreachable blocks are now hard errors (`Expected<void>`) rather than `sink.report()` warnings — the old path was silently discarded in release builds.
- Branch argument checking: unknown temps in branch args produce "unknown branch arg" errors; void-typed args produce "void branch arg" errors; the previous code accepted void as wildcard-compatible.
- Stack-escape analysis propagates stack-derived-ness through GEP chains and block-parameter edges via a fixpoint loop, catching `ret gep(alloca, offset)` patterns forwarded through back-edge block params.
- `ResultTypeChecker` now enforces fixed-result-type schema values instead of computing then discarding them; cast opcodes with strategy-validated widths are exempted.
- `InstructionChecker_Runtime`: `call.indirect` callee must be `Ptr`; instruction-level `pure`/`readonly`/`nothrow` attrs validated against known callee metadata and rejected when contradictory.
- Block param duplicate-name check extended to also reject duplicate numeric IDs; extern/function signature mismatch and name collision detected before body verification.
- `VerifyStrategy::IntegerBinary` added for `IAddOvf` / `ISubOvf` / `IMulOvf` / `SDivChk0` / `UDivChk0` / `SRemChk0` / `URemChk0`: operand and result type class changed from the hardcoded `I64` to `InstrType`, enabling i16/i32 sub-word overflow arithmetic. `And`/`Or`/`Xor`/`Shl`/`LShr`/`AShr` added to the verifier table with enforced operand types.
- `OperandTypeChecker` permits i16/i32 temporaries to satisfy an expected i64 operand for `And`/`Or`/`Xor` (the BASIC widening-mask idiom).
- `FunctionVerifier` exempts entry-block params that alias a function param by numeric ID and type — the canonical ABI lowering pattern.

**IL optimizer correctness** (see `fix(il): optimizer correctness + verifier hardening pass`):

- `CallEffects` priority inverted: runtime registry and function-declaration attributes are authoritative; instruction-level `CallAttr` is used only when the callee is unknown to all registries. `canEliminateIfUnused()` now requires both `pure` and `nothrow` — a throwing call cannot be deleted even when its result is dead.
- `BasicAA::CallEffect` gains a `known` flag; `modRef()` prefers registry metadata over `CallAttr` when the flag is set.
- `LoadSafety.hpp` (new shared header): `isLoadKnownNonTrapping(fn, load)` walks pointer provenance through alloca and GEP def chains. DCE dead-load elimination, GVN redundant-load elimination, and LICM loop-invariant load hoisting all gate on this predicate — loads from unknown or external pointers are kept because the trap is their observable effect.
- `Mem2Reg` SROA offset arithmetic uses overflow-safe subtraction before the bounds check; field iteration is sorted by offset before inserting alloca instructions (previously non-deterministic across runs). `addIncoming()` bounds-checks the target index against `labels.size()`.
- `Peephole` use-count map now counts uses in `brArgs` bundles so branch-argument-only temps are no longer incorrectly marked dead. Operand-forwarding peepholes call `allUsesLocalAfter()` before rewriting and skip if any use is in a different block; `replaceLocalUsesAfter()` handles the safe intra-block case.
- `Peephole` rule table: FAdd +0.0 rules removed (IEEE signed-zero: `(-0.0) + 0.0 == +0.0 ≠ -0.0`); `0/x` and `0%x` rules for checked division removed (must execute to trap when denominator is zero).
- `PassManager::verifyBetweenPasses_` is always `true`; the `#ifndef NDEBUG` gate was suppressing exactly the class of IR corruption these fixes address.

### Platform input

- macOS: bare arrow keys no longer map to PageUp/Down/Home/End (Fn-flag gate was too loose); mouse-wheel delta preserved across coordinate localization.
- Linux: X11 UTF-8 text input delivers every codepoint; clipboard operations implemented for editor widgets.

### Tests

Roughly 20K lines of new coverage across runtime, GUI, codegen, linker. Highlights: Canvas3D production-readiness harness with fake backend hooks; async 25-iteration repeat loop; shared `viper_display` CTest resource lock for display-holding smoke tests; human-manager baseball probes; new 2D-graphics contract suites; overflow-boundary tests for every hardened parser; 11 new Localization runtime test files (~360 assertions); 3 libFuzzer harnesses for the plural-rule parser, CLDR date-pattern parser, and locale-JSON loader (gated on `VIPER_ENABLE_FUZZ`).

### Demos & docs

Marquee demo addition is a text-mode human-manager baseball-franchise simulator; Pac-Man was rewritten and renamed as Crackman (session / progression / frontend split, audio banks); two new 3D demos (`3dbaseball`, `3dscene`), Paint gains layers + undo/redo + expanded tools, ViperIDE adopts the new GUI APIs, 3D Bowling gets a cinematic-postfx smoke probe, Chess AI-hardening pass, XENOSCAPE gets the boss-sink-through-floor fix plus the player-facing `Pixels.FlipH` direction fix, and three new `examples/localization/` Zia programs (`hello-localized` / `intl-numbers` / `translated-app`) ship with en-US + fr-FR + messages-en-US JSON data files. Docs: comprehensive `viperlib/` sweep (audio / GUI / crypto / network / graphics / text / threads / time / io), new `viperlib/graphics/production2d.md`, new `viperlib/localization/` set (README + locale + formatting + messages + collation + data-files), `README.md` refreshed to a master snapshot, `docs/basic-namespaces.md` + `basic-reference.md` pick up the Localization namespace, `cross-platform/platform-differences.md` documents locale-detection behavior, and a Doxygen comment pass across ~100 runtime `.c` files.

---

### Commits

See `git log a91d388db..HEAD -- .` for the full 80-commit history. Commits pair feature-add and follow-up hardening in the same subsystem (e.g. Production 2D → overflow hardening; threads timeout fixes → handle-magic validation; glTF skin import → sparse accessors + KHR extensions; new POSIX atomic-move path → linker-policy `link()` classification; `Viper.Localization.*` introduction → `numfmt_group_digits()` extraction + rt_fmt C-locale isolation + GUI app-registry refactor; variadic IL introduction → sub-width checked arithmetic hardening + err-opcode operand relaxation + MOVZXrr8/rr32 split; linker feature work → reader validation + COFF addend decode + reloc bounds + ICF/dead-strip correctness + IL module linker validation + reader/writer second pass; IL optimizer/verifier work → CallEffects priority inversion + LoadSafety + Mem2Reg SROA + Peephole scoped rewrites + always-on pass verification + sub-width verifier table + structural type inference; Zia sema/lowerer → Map.get Optional return + static fields + EH rethrow + overflow-always arithmetic + VM string cache async race fix; Zia parser/sema/lowerer correctness → isExpressionStart + struct-literal contexts + clonePureExpr + propertyDeclForLowering + read-only property guards + Unit literal diagnostics + range/optional lowering; BASIC + Zia OOP/type hardening → ArrayObject type + typed RuntimeMethodIndex overload + OopIndex case-insensitive fallback + rt_map_get_opt_str + lowerStructLiteral typed defaults + address-of forward refs + emitPositiveStepCheck; flow-narrowing + BASIC control-flow → field-path narrowing keys + tryExtractNullCheck generalisation + direct-optional hard error + finalFields_ enforcement + Future[T] payload type + mustReturn data-flow rewrite + RETURN/GOSUB validation + FOR/FOR EACH type checking + FINALLY analysis + USING type check).

<!-- END DRAFT -->
