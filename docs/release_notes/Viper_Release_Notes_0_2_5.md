# Viper Compiler Platform — Release Notes

> **Development Status:** Pre-Alpha. Viper is under active development and not ready for production use.

## Version 0.2.5 — Pre-Alpha (DRAFT — unreleased)

<!-- DRAFT: release date TBD. v0.2.4 was cut on 2026-04-13. -->

### What this release is about

A polish-and-hardening cycle with two notable new capabilities.

- **Hardening across every runtime subsystem.** Integer-overflow, handle-validation, timeout-clamping, and lifetime-correctness passes applied consistently to graphics, text, threads, audio, and network. Most code paths now fail cleanly on malformed input instead of producing wrapped-small allocations, dangling handles, or stalled state machines.
- **Production 2D Graphics module (new).** ~40 new classes on top of `Pixels` / `Canvas` / `ParticleEmitter` — offscreen surfaces, texture handles, a retained command stream, materials / shaders / post-effects, viewport scaling, tile / object layers, path + shape drawing, text layout, nine-slice UI, debug-draw, animation, a render-pass graph, and external-format adapters.
- **Full 3D asset pipeline.** glTF and FBX now import real skeletons + per-vertex skinning + animations (including glTF sparse-accessor morph deltas and KHR texture-transform / emissive-strength / unlit / lights-punctual extensions). HDR RenderTarget3D, deterministic shadow-light selection, and a backend-capability introspection surface (`Canvas3D.BackendCapabilities` / `BackendSupports`) complete the picture.
- **Network stack became a platform.** TLS-backed `HttpsServer` + `WssServer`, from-scratch HTTP/2 (HPACK + stream reuse), native RSA, in-tree X.509 chain validator (macOS no longer links `Security.framework`), cookie jar, streaming downloads, chunked request bodies, SSE reconnect, connection pooling.

The biggest user-visible new thing is a text-mode baseball-franchise simulator built on the existing baseball engine.

### By the Numbers

| Metric | v0.2.4 | v0.2.5 | Delta |
|---|---|---|---|
| Commits | — | 59 | +59 |
| Source files | 2,869 | 2,902 | +33 |
| Production SLOC | 450K | 498K | +48K |
| Test SLOC | 183K | 203K | +20K |
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

Four rounds of widget audit. Themes: dark-theme palette refresh, HiDPI consistency, keyboard accessibility, lifetime correctness.

- **CodeEditor, TabBar, Toolbar/MenuBar, FindBar, Dropdown, TextInput (single + multi-line), TreeView, ListBox, Spinner, SplitPane, FileDialog, CommandPalette** — keyboard navigation, redo + word-select, multi-select with Ctrl / Shift, ellipsis fitting, press-release coupling, typeahead, panel placement, save-name editing. Behavior now matches desktop-app conventions.
- **Dialog, Tooltip, Notification, Breadcrumb, ContextMenu, FloatingPanel** — rewritten paint: rounded card, scaled metrics, text wrap, fade + slide animation, anchored screen-bounds, clip-aware text.
- **Layout and framework** — flex non-stretch alignment, VBox/HBox margin budgets, SplitPane keyboard nav, synthesized double-click, Tab / Shift+Tab focus traversal at dispatch level.
- Lifetime crashes closed across Tooltip (dangling pointer on destroy), Dialog (nested-close use-after-free), Notification (auto-dismiss), CodeEditor (line-slot stability), VideoWidget (destroy order).

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
- **Quadtree, Physics2D, PathFollower, SpriteAnimation, SceneManager, ParticleEmitter, Achievement, ButtonGroup:** correctness fixes in zero-length segments, ping-pong completion, grid validation, transition latching, alpha/pool behavior.
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

- Linker: C++ Itanium-ABI symbol classification on macOS (with `$DARWIN_EXTSN` handling); `uname` / `gethostname` / `sysctlbyname` routed to the right dylib; `link()` added to the dynamic-import allowlist alongside its `unlink()` partner (closed a runtime-import-audit gap opened by the new POSIX no-clobber atomic-move path in `rt_file_ext`).
- Tools: frontend `--` separator, collision-safe `native_compiler` temp paths, x64 `--asset-blob` gate.
- IL: `Canvas.CopyRect` / `Screenshot` return `Pixels`; `AudioUpdate` added, `AudioEncode` removed.
- Build: VERSION `0.2.4-dev` → `0.2.5-snapshot`; GUI test targets key off `VIPER_BUILD_TESTING`.

### Platform input

- macOS: bare arrow keys no longer map to PageUp/Down/Home/End (Fn-flag gate was too loose); mouse-wheel delta preserved across coordinate localization.
- Linux: X11 UTF-8 text input delivers every codepoint; clipboard operations implemented for editor widgets.

### Tests

Roughly 17K lines of new coverage across runtime, GUI, codegen, linker. Highlights: Canvas3D production-readiness harness with fake backend hooks; async 25-iteration repeat loop; shared `viper_display` CTest resource lock for display-holding smoke tests; human-manager baseball probes; new 2D-graphics contract suites; overflow-boundary tests for every hardened parser.

### Demos & docs

Marquee demo addition is a text-mode human-manager baseball-franchise simulator; Pac-Man was rewritten and renamed as Crackman (session / progression / frontend split, audio banks); two new 3D demos (`3dbaseball`, `3dscene`), Paint gains layers + undo/redo + expanded tools, ViperIDE adopts the new GUI APIs, 3D Bowling gets a cinematic-postfx smoke probe, Chess AI-hardening pass, and XENOSCAPE gets the boss-sink-through-floor fix plus the player-facing `Pixels.FlipH` direction fix. Docs: comprehensive `viperlib/` sweep (audio / GUI / crypto / network / graphics / text / threads / time / io), new `viperlib/graphics/production2d.md`, `README.md` refreshed to a master snapshot, and a Doxygen comment pass across ~100 runtime `.c` files.

---

### Commits

See `git log a91d388db..HEAD -- .` for the full 59-commit history. Commits pair feature-add and follow-up hardening in the same subsystem (e.g. Production 2D → overflow hardening; threads timeout fixes → handle-magic validation; glTF skin import → sparse accessors + KHR extensions; new POSIX atomic-move path → linker-policy `link()` classification).

<!-- END DRAFT -->
