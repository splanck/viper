# Viper Compiler Platform — Release Notes

> **Development Status:** Pre-Alpha. Viper is under active development and not ready for production use.

## Version 0.2.5 — Pre-Alpha (DRAFT — unreleased)

<!-- DRAFT: release date TBD. v0.2.4 was cut on 2026-04-13. -->

### What this release is about

A polish-and-hardening cycle that grew a notable new capability along the way. Most of the work is in four areas: the audio runtime got a big consolidation pass, the GUI widget library went through a multi-round audit, the 3D graphics stack picked up the correctness fixes that were piling up, and the network runtime grew from "complete HTTPS client" into "full TLS-aware platform" — cookie jar, transparent gzip, streaming downloads, chunked server request bodies, SSE reconnect, keep-alive / connection pooling, per-request TLS controls, a from-scratch TLS-backed server (`HttpsServer` + `WssServer`), native RSA support alongside the existing ECDSA-P256 path, an in-tree X.509 chain validator that lets macOS finally drop its `Security.framework` link dependency, and a from-scratch HTTP/2 transport (client + server) with HPACK so HTTPS connections now negotiate h2 or http/1.1 and reuse HTTP/2 streams across requests. The Zia frontend, linker, and codegen also got smaller targeted fixes. The biggest user-visible new thing is a text-mode human-manager simulator built on the existing baseball engine.

### By the Numbers

| Metric | v0.2.4 | v0.2.5 | Delta |
|---|---|---|---|
| Commits | — | 36 | +36 |
| Source files | 2,869 | 2,898 | +29 |
| Production SLOC | 450K | 479K | +29K |
| Test SLOC | 183K | 193K | +10K |
| Demo SLOC | 177K | 188K | +11K |

Counts via `scripts/count_sloc.sh`. Most of the growth is in demos (Crackman split, Paint feature pass, baseball shell) and the GUI runtime.

---

### Audio

A two-step rework: first consolidate, then harden.

**Consolidation.** WAV / OGG / MP3 used to take three separate code paths through the runtime. They now go through one decoder dispatch in `rt_audio.c`. The standalone `rt_audio_codec` translation unit is gone; the encoder side (`Viper.Sound.Encode`) is gone too — encoding lives in the asset pipeline now and the runtime doesn't need a parallel entry point. New `Viper.Sound.Audio.Update()` drives crossfade ticks from outside `Playlist.Update`.

**Fixes.**
- Float32 WAV music streams used to decode as integer PCM. Now the format is plumbed through `vaud_wav_open_stream` and the mixer converts correctly.
- WAV stream header scanning had a 256-byte ceiling, which rejected BWF/metadata-heavy files. Removed.
- MP3 music playback no longer expands the whole file to PCM up front — it streams frame-by-frame. Sound effects (short clips) keep the eager-decode path because it's the right trade-off for them.
- Unsupported MP3 Huffman tables now fail cleanly instead of silently emitting noise.
- `SoundBank` keys longer than 31 bytes used to truncate. They're now stored as full retained strings.
- Pause / stop / volume on a playlist with an in-flight crossfade only touched one of the two tracks. They now reach both.
- Crossfade fade-clock kept advancing through pause. Fixed — pausing now freezes the clock.
- Resume during a crossfade could restart the wrong track as foreground. Fixed.
- `Music.Seek(ms)` only seeks that stream now; it no longer affects unrelated music.

**MusicGen / Synth.**
- ADSR envelope release used `sustain` as the start level even when note-off happened during attack or decay, producing audible clicks on short notes. Now the release fades from the actual envelope level at note-off.
- Empty / muted channels no longer participate in the render loop or the per-channel normalization count, so placeholder channels don't quietly dim everything else.

**New APIs.** `Music.SetLoop(flag)` is IL-bound (`Viper.Sound.Music.SetLoop`) so Zia can flip the loop flag mid-playback without restarting. The companion helpers `rt_music_pause_related` / `resume_related` / `stop_related` / `set_crossfade_pair_volume` stay internal — they're classified in `RuntimeSurfacePolicy.inc` and route through the existing `Music.Pause` / `Resume` / `Stop` IL methods.

---

### GUI Library

Four rounds of widget audit. The big themes: lifetime correctness, HiDPI consistency, a dark-theme palette refresh, and keyboard accessibility / modifier-aware selection.

**Dark theme.** New cooler-tinted palette (deeper, more saturated background ramp; warmer accents). Default font sizes nudged up (normal 13 → 13.5, large 16 → 17, heading 20 → 21). Button and input rows aligned at 28 px height with a wider border radius. Scrollbar metrics retuned.

**CodeEditor.**
*New APIs.* `CanUndo`, `CanRedo`, `SetTabSize` / `GetTabSize` (1–16), `SetWordWrap` / `GetWordWrap`, `GetLineAtPixel` / `GetColAtPixel`.

*Word-wrap correctness.* Word-wrap now drives cursor movement, scrollbar math, hit-testing, and `ScrollToLine` — previously it was paint-only and the rest of the widget thought every line was unwrapped. Fold gutters render and toggle.

*Stability.* Line-slot metadata is cleared on `SetText` and language switch (fixes a ViperIDE crash on file open). The per-glyph paint loop is factored into `draw_text_slice` / `draw_colored_slice` helpers so syntax-colored runs and plain runs share one layout path — sets up upcoming inline-diagnostics and squiggle-underline work.

**TabBar.**
*Visuals + drag.* Tab tooltips. HiDPI scaling on tab metrics; ellipsis on long titles. Drag now requires 6 px of pointer movement before reordering (small click-jitter no longer scrambles tab order).

*Keyboard.* Built-in navigation / reorder / close — `Left` / `Right` move the active tab, `Home` / `End` jump to ends, `Ctrl+W` closes, `Ctrl+Shift+Arrow` reorders.

*Click semantics.* Press-and-release coupled — the activate / close action fires on mouse-up *only* if the pointer is still over the same target. Mouse-down on tab A then drag-and-release on tab B cancels cleanly instead of firing the wrong target. Stable close-click index that survives `auto_close`.

**Toolbar / MenuBar.**
*Overflow + menus.* Real overflow popup (was a stub). Disabled top-level menus paint and behave as disabled. MenuBar measures to zero height when the macOS native main menu is active.

*Icons.* Pixel-icon setters create real image icons instead of casting pointers as glyphs. Custom image-icon paint path with explicit alpha compositing replaces the previous best-effort blit.

*Keyboard.* Toolbar gains full keyboard navigation — arrow keys move focus across visible items, Home/End snap to first/last (with End reaching the overflow button), Enter / Space activates the focused item, Tab cycles focus out cleanly.

**FindBar.** Live `GetFindText` / `GetReplaceText`. `SetVisible` routes through the standard widget visibility path. UTF-8-safe match advance.

**Popup routing and overlays.** ContextMenu now anchors against widget screen bounds instead of local coordinates, captures input while open, and reliably dismisses on outside click without click-through. FloatingPanel clips its child subtree to the panel bounds, and the shared glyph renderer now respects the active clip rect, fixing overlay text bleeding out of panels such as the ViperIDE settings dialog. Breadcrumb overflow menus now render as real interactive dropdowns instead of dead state.

**ScrollView.** Auto-hide stabilizes (cross-axis case where one bar forces the other no longer ping-pongs). Drag keeps capture until mouse-up even outside the widget. Thumb drag now preserves the within-thumb grab offset instead of snapping.

**Tooltip.** Multi-line wrap. Rounded card paint. Hides automatically when the anchored widget is hidden or destroyed (was a dangling-pointer crash). Wrap loop terminates on whitespace-only input. Hide delay and timed auto-hide now work correctly across repeated hovers on the same widget.

**Dialog.** Rewritten paint and layout — rounded card with stroke, ui-scale-aware metrics (padding, gaps, title bar, button bar, close glyph, icon), and real text wrapping for the message body so dialogs size themselves against the host window instead of overflowing. Button-preset helpers drive Ok/Cancel/Yes/No/etc. row layout. Existing fixes preserved: re-entrancy guard for `on_result` calling `vg_dialog_close` again, and dual `user_data` slots so `on_result` and `on_close` each get their own context.

**Button / Slider / ProgressBar / FloatingPanel / Breadcrumb.** Visual unification across the primitive widgets — rounded paint, scaled metrics, state-aware fills. Slider gets interaction polish to match. Breadcrumb picks up `SetSeparator` / `SetMaxItems` alongside the visual pass.

**Font inheritance.** The complex-widget bridge (`rt_gui_app.c`) now gates metric queries on a font-handle sanity check (`(uintptr_t)font >= 4096u`) and provides a lazy `rt_gui_inherit_font_to_widget` path that copies a font handle + size into a widget subtree without dereferencing it. Opaque sentinel handles used by runtime tests no longer crash the metric path, and construction-time inheritance of a not-yet-loaded font no longer requires every widget setter to guard for itself.

**Dropdown.**
*Keyboard + wheel.* Navigation on the open popup (arrow / page / home / end). Pressing a key on a closed dropdown opens it. Mouse wheel scrolls the open popup.

*Placement.* Panel flips above the trigger when there's no room below. Popup placement and hit-testing now agree in nested layouts; popup row paint tracks fractional scroll instead of jumping a whole row.

*Typeahead + sizing.* Typing letters jumps to the first item whose visible text starts with the typed prefix (resets after a 1-second idle). Panel sizes to the longest item rather than the trigger width.

**TextInput.**
*Single-line.* Max-length counts UTF-8 codepoints, not bytes. Single-line ignores newline character input. Read-only navigation collapses the selection. Password mask handles long pasted secrets via heap allocation instead of capping at 1023 asterisks.

*Multiline.* Real line-based paint, hit-testing, cursor movement, drag selection, and wheel scrolling.

*Standard editor expectations.* `Ctrl+Shift+Z` performs redo (`Ctrl+Z` undo was already there). Double-click selects the word under the cursor. Programmatic `SetText` fires `on_change` and resets the undo baseline so subsequent undo doesn't roll back to a stale prior state.

*Visual states.* Focus / hover / read-only / disabled all paint distinctly.

**TreeView.**
*Selection.* Click on the blank area of a nested row selects rather than toggling expand (matches IDE convention). Scroll clamps after collapse.

*Visual.* Per-node glyph icons and loading indicators finally render. Ellipsis-aware text fitting keeps deeply-nested nodes from painting past the viewport.

*Drag-and-drop.* Actually works now — `suppress_click` swallows the synthetic click that would otherwise fire after drop, drop-target validation respects the tree's hierarchy rules.

**ListBox.**
*Change detection.* Virtual-mode now compares against `prev_selected_index` so virtual lists actually report selection changes. Add/remove/clear/select invalidate layout/paint immediately; item labels are clipped to the viewport.

*Multi-select.* With Ctrl and Shift modifiers — plain click clears + selects, Ctrl+click toggles, Shift+click extends a range from the anchor. Virtual-mode and non-virtual-mode share matching helpers so semantics are identical regardless of backing storage.

**Spinner.** The numeric field is directly editable now: typing starts inline numeric entry, `Enter` commits, and `Escape` cancels back to the formatted value.

**Layout.** Flex non-stretch alignment preserves the child's measured cross size (no more few-pixel descender clip). VBox/HBox budget child margins when distributing space. SplitPane proportional clamping when min sizes exceed available.

**SplitPane.** Keyboard navigation: SplitPane now advertises `can_focus`. Arrow keys (Left/Right for horizontal split, Up/Down for vertical) adjust the split position by a sensible pixel delta; Home / End snap to the edges, respecting min sizes.

**FileDialog.** Layout metrics extracted to named constants. Multi-select dialogs snapshot the accepted-paths list on success instead of aliasing backend memory (fixes repeat-show + destroy lifetime). The in-app dialog now scrolls long bookmark/file lists, keeps keyboard selection visible, clips long path text, and supports caret-aware save-name editing (`Left` / `Right`, `Home`, `End`, `Backspace`, `Delete`).

**CommandPalette / focus routing.** CommandPalette now keeps the current selection visible while keyboarding or wheel-scrolling through long result sets. Toolkit-level `Tab` / `Shift+Tab` focus traversal is wired back into event dispatch, so keyboard-only navigation works across focusable widgets again. Double-click synthesis also moves into the event dispatcher — widgets read a synthesized double-click event from the framework instead of each implementing their own timer + coordinate-distance heuristic, so timing stays consistent across the toolkit.

**Notification.** Lazy `created_at` stamp (toasts no longer vanish on the first frame). Fade math guards `fade_duration_ms > 0`. Toasts now use wrapped title/body/action layout plus coordinated fade/slide animation on both entry and dismissal.

**Lifetime fixes.** Tooltip dangling pointer on widget destroy. Dialog use-after-free in nested close. Notification auto-dismiss. CodeEditor line-slot pointer stability. VideoWidget destroy hardening.

---

### Graphics runtime (2D)

**Canvas.** New `RT_CANVAS_MAGIC` guard field; `GetPixel` / `CopyRect` / `SaveBmp` / `SavePng` route every incoming `void*` through a validator so a non-Canvas object fails safely instead of getting reinterpret-cast. `Canvas.CopyRect` and `Canvas.Screenshot` carry their `Pixels` return type through the IL system instead of erasing to bare `obj`.

**Shared coordinate helpers.** `rt_graphics_internal.h` adds four inline helpers (`rtg_sanitize_scale`, `rtg_round_scaled`, `rtg_scale_up_i64`, `rtg_scale_down_i64`) so every 2D drawing site does logical↔physical conversion the same way. New `RT_COLOR_EXPLICIT_ALPHA_FLAG` distinguishes a caller-specified alpha byte from the `0xFF000000` default, routing through different blend paths.

**Text.** UTF-8 codepoint iteration in BitmapFont and Canvas text — multi-byte glyphs now hit-test and render correctly. Old font objects are released when replaced.
Clip-sensitive GUI text/image paths now honor the active `vgfx` clip rect even when they render directly into the framebuffer, which prevents scrolled or clipped widgets from bleeding pixels outside their viewport.

**Camera.** Parallax layers each get independent zoom/rotation/scroll with integer-floor-div tile wrapping (no seam at any zoom).

**Sprite / SpriteBatch / SpriteSheet / Tilemap.** Lifecycle and indexing refinements; canvas magic guard adopted across drawing surfaces.

---

### Graphics3D

A correctness-and-hardening pass spanning every subsystem.

**Skeletal animation.** `add_bone` rejects parent indices below `-1` (was only checking the upper bound). `set_bone_weights` range-checks each influence against `VGFX3D_MAX_BONES` before the `uint8_t` cast (was silently wrapping `256`→`0`). Crossfade bind-pose decomposes the actual TRS instead of snapping to identity.

**AnimController3D.** Root motion now tracks rotation in addition to translation.

**Camera3D.** Input sanitizers for aspect, near/far clip, FOV, and ortho size catch pathological values before they hit the projection builder. `look_at` with degenerate eye==target preserves the camera position instead of returning identity. `Canvas3D.Begin` uses the active output aspect when building that frame's projection, and `ScreenToRay` uses the shaken render pose during camera shake so picking matches the visible image.

**Canvas3D.** `Begin2D` caches the full V·P (matched to `Begin3D`) so overlay unprojection has consistent semantics. Camera params now carry world-space forward and an `is_ortho` flag — backends specialize fog / rim / specular per mode. Cleanup paths on `Canvas3D.New` / `RenderTarget3D.New` release partially initialized wrappers on failure.

**Reference ownership (cross-cutting).** Every Graphics3D subsystem that holds references to other graphics objects now uses a uniform retain-then-release slot pattern: assign retains the new value first, then releases the old, so re-assigning a slot to its current value can't briefly drop the refcount to zero. Rolled out across Mesh3D, MorphTarget3D, Terrain3D, Water3D, Particles3D, Scene3D, InstBatch3D, Cubemap3D.

**Terrain3D / Heightfield.** Normal computation now multiplies the Y component by both horizontal scales (was Y by X only); lateral components carry the perpendicular horizontal scale. Non-uniformly scaled terrain reports correct lighting and collision. Splat texture rebakes lazily off a `splat_dirty` flag. Degenerate `1×1` splat maps treated as uniform coverage instead of dividing by zero.

**Water3D.** Gerstner phase switched to standard `k·x − ω·t` form so waves travel in the user's `+direction` instead of against it.

**Particles3D.** Spawn `delta_time` clamped to ~4 frames so a frame hiccup doesn't dump thousands of particles in one tick. Sphere emitters sample volume uniformly; cone emitters sample solid angle uniformly. Additive particles use an explicit additive blend path while preserving per-particle alpha.

**MorphTarget3D.** Weights clamped to `[-1, 1]`. Packed delta arrays rebuilt lazily off a generation counter — weight-only changes skip the rebuild; shape edits or delta edits bump the generation.

**MorphTarget3D / Canvas3D.** Morph targets no longer hard-stop at 32 shapes in the shared runtime object. Storage grows on demand. Metal keeps oversized morph sets on the GPU path; OpenGL and D3D11 now fall back to CPU morphing once the active shape count would exceed their shader payload limits instead of silently truncating or trapping.

**glTF / Model3D.** glTF mesh extraction now handles `COLOR_0`, `TANGENT`, `JOINTS_0`, and `WEIGHTS_0` in addition to positions, normals, and UVs. Triangle strips/fans are triangulated correctly. Matrix-authored node transforms are decoded in the right column-major order. Tangents are synthesized when a normal-mapped primitive omits them. `test_rt_model3d` is back in CTest; the demo-FBX texture case self-skips when that optional asset is not present in the checkout.

**Skeleton3D / Scene3D / Canvas3D.** The shared skeleton limit is now 256 bones end to end. OpenGL, D3D11, and Metal all accept the expanded palette size on their GPU skinning paths instead of silently truncating larger rigs at the old ceiling. The direct skinned draw path and `Scene3D.Draw` now share the same fallback helper, so scene-bound animators CPU-skin correctly on the software backend and keep full palette payloads on GPU backends.

**Canvas3D / Camera3D / Light3D.** `Canvas3D.Begin` now builds an output-specific projection for the active window or render target without mutating the camera object's stored aspect/projection, so one camera can render to multiple outputs in the same frame safely. Deferred sort keys are now bounds-aware view-depth keys instead of model-origin distance proxies, which improves transparent ordering and gives opaque instanced batches a representative aggregate sort depth. Directional shadowing now selects the two strongest shadow-casting lights deterministically, assigns them contiguous shadow slots, and preserves stable slot selection even if light slot order changes. `SetOcclusionCulling(true)` also now performs coarse frustum rejection before the existing front-to-back opaque submission pass instead of only reordering draws.

**Canvas3D / Light3D / GPU backends.** The per-canvas dynamic-light ceiling is now 16 across the shared runtime, OpenGL, Metal, and D3D11 instead of silently truncating scenes at 8 lights on the GPU path. The last public light slot is covered by regression tests so the frontend slot range and backend payload limit stay aligned.

**RenderTarget3D / PostFX3D / GPU backends.** `RenderTarget3D` CPU color/depth buffers are now allocated lazily instead of up front. `AsPixels()`, `Screenshot()`, and render-target postfx reads allocate/sync on demand, and the software backend now ensures those host buffers exist when it binds an RTT. D3D11 RTT color readback now follows the same lazy sync model as Metal/OpenGL instead of forcing a staging copy every RTT frame. `RenderTarget3D.NewHdr(width, height)` now adds a real HDR RTT path: GPU backends keep `RGBA16F` internal color storage for the offscreen target, `RenderTarget3D.IsHdr` exposes that format choice to user code, and backend readback tonemaps HDR RGB back into `Pixels` during `AsPixels()` so screenshots and bake-outs stay deterministic. `PostFX3D` chains also grow past the old fixed 8-effect limit instead of silently truncating later passes. GPU post-processing now preserves the authored effect order end to end instead of collapsing the chain into one per-type snapshot, so repeated effects and screenshots/readback match the CPU postfx path.

**NavMesh3D.** Edge keys switched from `lo * 1_000_000 + hi` to bit-packed `(lo << 32) | hi`. The old formula collided on meshes with vertex indices ≥ 1M (e.g. edges `(1, 2_000_000)` and `(2, 1_000_000)` both hashed to `3_000_000`).

**Physics3D / Collider3D.** `capsule_axis_endpoints` documents its Y-only contract; opt-in `RT_PHYSICS3D_STRICT_CAPSULE_AXIS` build flag traps on non-identity orientation.

**Cubemap3D.** New `cubemap_direction_to_face_uv` and `cubemap_face_uv_to_direction` helpers cover the standard cubemap geometry. Public `rt_cubemap_sample_roughness(cm, dir, roughness)` consumes them for blurred reflection sampling.

**Canvas3D / CubeMap3D.** Backends without a native skybox draw hook now cache the generated CPU fallback skybox by cubemap generation, output size, and camera state. Stable skybox frames blit the cached RGBA image instead of resampling every pixel from the cubemap each frame, while cubemap face mutations invalidate the cache through the existing generation key.

**FBX loader.** Cross-platform texture-path normalization (`fbx_is_absolute_path` recognizes POSIX, UNC, and Windows drive prefixes; `fbx_normalize_path` normalizes separators). FBX assets exported on Windows now resolve identically on macOS and Linux. Binary reader short reads are now hard parse errors, and long node names advance by the encoded byte length even when copied into a fixed-size internal name buffer, so truncated/corrupt FBX files fail cleanly instead of producing partial imports.

**FBX / Model3D.** The FBX loader now exports a real scene-node root instead of only loose mesh/material arrays, and `Model3D.Load(.fbx)` clones that authored `Model` hierarchy into its template tree. Local TRS, parent/child grouping, mesh attachments, and material bindings now survive FBX import. The FBX asset finalizer also releases owned meshes, materials, skeletons, animations, morph targets, and the scene root correctly instead of leaking them.

**Backends.** Morph-handoff surface unified across D3D11, Metal, OpenGL, and the software rasterizer — backends consume the shared packed-delta buffer directly. PBR-aware alpha-blend gate replaces the old "always opaque" default for sub-1.0 alpha materials. OpenGL texture and cubemap uploads flip top-left-origin pixel rows before upload to match the other backends.

**Lights / PostFX.** Color/intensity init and finalizer plumbing so lights are safely retained / released through scene-graph mutations.

---

### Game runtime

**Dialogue.** Rewritten against the real BitmapFont measurement surface — no more hard-coded 8×10 character cells. Per-line state distinguishes byte length from codepoint length, so UTF-8 text wraps and reveals at codepoint boundaries.

**Quadtree.** Per-node items array switched from a fixed-size stack array to a heap allocation with explicit capacity. Inserts grow past the initial cap instead of silently dropping items.

**Physics2D.** Joint cleanup on body removal — stale joints pointing at destroyed bodies can no longer fire during the next simulation step.

**PathFollower / SpriteAnimation / SpriteSheet / SceneManager / ParticleEmitter.** Various correctness fixes: zero-length segment traversal, single-frame ping-pong completion, grid validation, transition progress latching, alpha and pool-saturation behavior.

**Achievement / ButtonGroup.** Achievement now threads runtime string handles end-to-end. ButtonGroup clears the active selection when the selected button is removed.

**Input correctness.** Four edge-vs-level fixes:
- Action chord release now fires on the same frame held drops to false.
- Debounced key-press uses the press-edge instead of the held state, so a held key doesn't re-fire every frame after the cooldown.
- KeyChord combos reset on a wrong-order press in the chord's key set (instead of silently extending).
- `rt_keyboard_text_input` got real UTF-8 encoding (1- to 4-byte codepoints up to U+10FFFF). Above-127 codepoints used to drop silently.

---

### Network

Broad hardening and feature pass across the HTTP client, HTTP server, SSE, and TLS.

**HTTP client.**
*Features.* RFC-compliant cookie jar — `Set-Cookie` lines parsed into typed entries (name, value, domain, path, `Expires`, `Max-Age`, `Secure`, `HttpOnly`), indexed by domain/path scope, and attached to outgoing requests automatically. Transparent gzip: outgoing requests advertise `Accept-Encoding: gzip` and `Content-Encoding: gzip` responses are decoded inline (including chunked+gzip). `Http.Download()` streams bytes straight to disk so multi-GB downloads don't need matching RAM (download path keeps `Accept-Encoding: identity` so the file on disk is byte-for-byte what the server sent).

*Protocol correctness.* Relative `Location:` headers resolve against the current URL; 303 See Other joins the redirect set. Strict `Content-Length` parsing rejects negative / non-numeric / whitespace-only values instead of silently treating them as 0. HEAD / 204 / 304 / 1xx handling centralised. 1xx informational responses (100 Continue, 101 Switching Protocols, 102/103) are consumed and discarded before the client reads the real response.

*Performance.* Non-blocking connect with proper timeout replaces blocking `connect`. End-to-end transport reuse now covers both HTTP/1.1 keep-alive and negotiated HTTP/2 streams: idle TCP/TLS HTTP/1.1 connections are cached per `(host, port, tls)` with LRU eviction and idle-timeout scrub, while HTTPS connections that negotiate `h2` are reused as sequential streams on the same TLS session. `HttpClient.KeepAlive`, `HttpClient.SetPoolSize`, and `HttpReq.SetKeepAlive(i1)` give callers per-client and per-request control.

*Security.* Cross-origin redirects strip credential-bearing headers (`Authorization`, `Cookie`, etc.) so a redirect to a different origin can't leak tokens. URL parsing rejects CRLF sequences to block header-injection attacks. Cookie-jar match tests prevent cross-domain and cross-path leakage; expired cookies are purged.

*Multipart + WebSocket hardening.* Multipart form-data building now escapes quoted `name` / `filename` parameters and strips CR/LF from emitted part headers; the parser now handles quoted boundaries plus escaped quoted parameters instead of `strstr`-style extraction. WebSocket client handshakes emit canonical `Host` authorities (default ports omitted, IPv6 bracketed), client/server subprotocol negotiation is now supported for single-token `Sec-WebSocket-Protocol` flows, and `WsServer` / `WssServer` reject malformed `Sec-WebSocket-Key` values or invalid `Host` headers before upgrading.

**HTTP server.**
*Request handling.* `Transfer-Encoding: chunked` request bodies are now decoded correctly — browser streaming uploads and `curl --data-binary @-` finally work. Header token scanning is robust against substring false positives.

*Connection framing.* The response path is Connection-header-aware: the server inspects the incoming `Connection:` value, honours `keep-alive` on HTTP/1.1 by default (and `close` when asked), and emits matching response headers plus proper `Content-Length` / `Transfer-Encoding` framing so the client knows where responses end. HTTP/1.0 clients correctly default to `Connection: close` (RFC 1945) and only get keep-alive when they opt in. Invalid HTTP version strings are rejected with a 505 / connection close instead of being silently treated as HTTP/1.1.

*Lifecycle.* Thread-safe — `IsRunning` reads and `Stop` calls no longer race on the running flag. The constructor accepts `port=0` to ask the kernel for an ephemeral port (the assigned port is reported via `Port` after `Start`).

**HttpsServer.** TLS-backed HTTP server (`Viper.Network.HttpsServer`) now speaks both HTTP/1.1 and HTTP/2. It mirrors the `HttpServer` surface — `Get` / `Post` / `Put` / `Delete` route registration, `BindHandler` for native handler binding, `Start` / `Stop` lifecycle, `Port` / `IsRunning` properties — but every accepted connection goes through TLS first. Constructor takes `(port, cert_path, key_path)`. ALPN now advertises `h2,http/1.1`; when the client selects `h2`, the runtime translates HTTP/2 request/response streams through the same route/handler machinery used by the HTTP/1.1 path. Per-connection send-timeout bounds slow-loris-style attacks. Both the BASIC and the Zia frontends lower literal-route registrations on `HttpsServer` through the same handler-binding pattern as `HttpServer`, so Zia code with `server.Get("/path", "handler")` works identically against either server type.

**WssServer.** New TLS-backed WebSocket server (`Viper.Network.WssServer`). Same constructor shape as HttpsServer (`port, cert_path, key_path`); broadcast surface (`Broadcast(text)` + `BroadcastBytes(bytes)`) plus `ClientCount`, `Port`, `IsRunning`. The wire sequence per client is TCP accept → TLS handshake → HTTP/1.1 upgrade → WebSocket framing. The upgrade path validates the WebSocket `Origin` header against the `Host` header to block cross-origin WebSocket abuse from browser-based attackers.

**RestClient.** Keep-alive and pool-size configuration thread through to the underlying HTTP client, so REST-heavy workflows (microservices, paginated API loops) reuse connections transparently without changing call sites.

**Server-code consolidation.** HTTP and HTTPS server share a common request/response helper set (`rt_http_server_shared.inc`) — the line-terminator scanner, header-end finder, bounded `memchr`, response framing, and chunk-parser status handling are now defined once and pulled in by both translation units. WebSocket and WSS servers got the same treatment (`rt_ws_shared.inc`) — Upgrade-header tokenisation, trimmed-strdup, and the rest of the upgrade-handshake plumbing live in one place. Future protocol fixes land in both transport variants automatically instead of having to be ported between near-duplicate files. Net code reduction across the four servers despite adding the shared headers.

**SSE (Server-Sent Events).** Automatic reconnect-after-disconnect. The client re-opens the connection when the server drops and honours `Last-Event-ID` to resume where the stream left off, instead of silently ending on transient network failures. Matching non-blocking connect + timeout behaviour as the HTTP client.

**HTTP/2 transport.** New in-tree HTTP/2 implementation (`rt_http2.c` / `rt_http2.h`, ~2,150 LOC) for both client and server roles. Covers the frame layer (HEADERS, DATA, SETTINGS, PING, WINDOW_UPDATE, RST_STREAM, GOAWAY), HPACK header compression with the static table plus a dynamic table, and static Huffman coding. Connection-specific headers (`Connection`, `Keep-Alive`, `Transfer-Encoding`, `Upgrade`, `Proxy-Connection`) are rejected per RFC 7540 §8.1.2.2. Stream IDs advance by 2 per request in each role (client odd, server even), so a single HTTP/2 connection can carry multiple concurrent or serial requests.

*Integration.* When ALPN negotiates `h2` after the TLS handshake, `HttpsServer` auto-switches to HTTP/2 transport — an adapter (`http2_request_to_server_req`) translates HTTP/2 request frames into the existing `server_req_t` struct so the router and handler surface work unchanged over HTTP/2; responses map back through the same reciprocal path. High-level HTTP client (`rt_http_req_send` / `HttpClient.Get` / etc.) does the same thing on the client side — when ALPN selects `h2`, the client constructs an `rt_http2_conn_t` and reuses it across keep-alive-pool hits, so multiple HTTPS requests to the same origin share one HTTP/2 connection with different stream IDs. Response construction translates HTTP/2 pseudo-headers back into the public `rt_http_res_*` accessors so callers see the same surface regardless of which transport negotiated.

*Force-HTTP/1.1 knob.* New public API `rt_http_req_set_force_http1(req, force)` (`Viper.Network.HttpReq.SetForceHttp1`) lets callers opt out of HTTP/2 and advertise only `http/1.1` via ALPN — useful when code depends on HTTP/1.1-specific framing (e.g. the `Connection: keep-alive` response header that HTTP/2 omits because persistence is implicit).

*ALPN bugfix.* The server-side ALPN selection loop had a buffer-termination bug that silently caused *every* `clienthello_offers_alpn` match to fail when the preferred list had more than one entry — the tokenizer handed the matcher a substring pointer into a longer comma-separated list, and the matcher called `strlen()` on it. The consequence: servers that advertised `h2,http/1.1` always picked *nothing* and sent no ALPN extension back, so every TLS session silently fell through to HTTP/1.1 regardless of what either side asked for. Fixed by passing explicit `(ptr, len)` through the matcher. The fix is what makes the new HTTP/2 transport actually negotiate.

**TLS.**
*Client side.* `HttpReq.SetTlsVerify(bool)` (`Viper.Network.HttpReq.SetTlsVerify`) gives per-request verification control — useful for dev servers with self-signed certs and staging environments with internal CAs. Default stays secure (verification on). `alpn_protocol` on `rt_tls_config_t` now accepts a comma-separated preference list (for example `h2,http/1.1`) instead of a single token; the HTTPS client uses that to negotiate HTTP/2 automatically and records the selected protocol on the TLS session. `rt_tls_last_error()` captures connect/handshake errors in thread-local storage so trap messages surface the underlying diagnostic (hostname mismatch, cert expired, handshake protocol error) instead of generic "TLS handshake failed".

*Server side.* From-scratch TLS 1.3 server implementation supporting the new HttpsServer / WssServer. PEM cert + EC private key loading (both SEC1 and PKCS#8 formats via a small DER TLV walker), cert-chain support, separate handshake-key and application-secret derivation, and a thread-local server-side error mirror (`rt_tls_server_last_error`) matching the client-side pattern. The server validates the client's SNI extension against the leaf certificate's CN/SAN before completing the handshake — blocks a client from connecting to one host and receiving another host's cert from a multi-tenant TLS server (bare IPv4/IPv6 literals are correctly recognised as having no SNI). Cert / key loading now auto-detects the leaf algorithm and routes through either the ECDSA-P256 or the new RSA-PSS-SHA256 path so RSA-keyed certificates work alongside the existing EC-keyed ones.

*Native RSA.* New `rt_rsa.c` / `rt_rsa.h` implement RSA public-key parsing (PKCS#1 RSAPublicKey and SubjectPublicKeyInfo) plus modular exponentiation and RSA-PSS-SHA256 signature verification — the algorithm most public-CA TLS certificates use. Hooks into the cert-chain validator and the TLS server's `CertificateVerify` path. No external crypto dependency: same zero-dep posture as the rest of the network stack.

*X.509 in-tree.* `rt_tls_verify.c` was rewritten as a from-scratch X.509 chain validator (~2 KLOC) covering certificate parsing, signature verification (ECDSA-P256 and RSA-PSS-SHA256), validity-window checks, hostname matching against CN/SAN, Extended Key Usage enforcement (the leaf cert must be valid for TLS server authentication; certificates issued for code signing / S/MIME / other purposes are rejected outright), and chain-of-trust walking against the bundled CA roots. The macOS-specific `Security.framework` / `CoreFoundation` link directives in `src/runtime/CMakeLists.txt` are gone — `viper_runtime` now links the same on Linux, macOS, and Windows for the TLS path, matching what was already true for the rest of the network stack.

*In-tree time.* The platform `timegm()` extern in the TLS validator was replaced by an in-tree `rt_network_timegm_utc` helper (`rt_network_time.inc`) using Howard Hinnant's days-from-civil algorithm. Removes the last platform-specific time call from the network stack and gives certificate validity-window checks the same zero-dependency posture as the rest of the validator.

*Underneath.* The ECDSA-P256 module gained the wide-arithmetic and modular-math primitives (`u256_mul_wide`, `u512_mod_u256`, `u256_mod_{add,double,mul}`) needed for the certificate signature path that most modern TLS uses.

**Crypto / KDF defaults.** PBKDF2 iteration counts raised from 100,000 to 300,000 across the runtime (`AES_STR_PBKDF2_ITERATIONS` in `rt_aes.c`, `CIPHER_PBKDF2_ITERATIONS` in `rt_cipher.c`, `DEFAULT_ITERATIONS` in `rt_password.c`). The `Password.Hash` minimum iteration floor (`MIN_ITERATIONS`) bumped from 10,000 to 100,000 — custom requests below the new floor clamp up rather than being accepted at the lower count. Aligns with current OWASP guidance while staying practical for interactive use (~150 ms on typical desktop CPUs vs. the prior ~50 ms). `rt_keyderive_pbkdf2_sha256_raw` input validation tightened to reject empty salt, zero iterations, and zero / over-1024-byte output length explicitly instead of silently misbehaving. Old ciphertexts and password hashes encoded with the prior counts continue to verify correctly — the iteration count is stored in the hash format, so both old and new digests round-trip cleanly.

**UDP.** Substantial socket-layer rewrite. Dual-stack IPv4 / IPv6 sockets work transparently — the same `Udp` instance can send to and receive from both address families. Sender address / family / port are captured on `recv_from` so callers can reply without re-resolving. Address resolution is centralised across unicast and multicast send paths.

**WebSocket / SMTP.** Small correctness follow-ups on header parsing and error paths consistent with the HTTP/TLS changes above.

**Follow-up hardening.** A second pass pulled duplicated HTTP/HTTPS request-parsing, chunked-body decoding, and response-framing code into shared runtime helpers so fixes now land once for both cleartext and TLS servers. Cross-origin redirect stripping is shared across `HttpReq`, `HttpClient`, and `RestClient`; manual `HttpClient.SetCookie` entries are exact-host cookies instead of overly broad domain cookies; `SseClient` follows initial redirects and rejects unsupported content encodings instead of misparsing compressed streams; SMTP now parses `EHLO` capability lines before `STARTTLS` / `AUTH LOGIN` and accepts `251` / `252` recipient replies; `ConnectionPool` now tracks fresh checked-out sockets immediately and clamps invalid `maxSize` inputs instead of silently turning `0` into a 128-slot pool; `AsyncSocket` converts worker-thread transport traps into failed futures instead of unwinding out of the thread pool; and the TLS/HTTP stack now has an in-tree HTTP/2 transport (`rt_http2.c`) with HPACK decoding, ALPN-driven HTTPS client negotiation, sequential stream reuse, HTTP/2 trailer preservation, and `HttpsServer` HTTP/2 request handling through the existing route surface. Concurrent request streams that arrive while the server is still consuming one request are refused with `RST_STREAM` instead of tearing down the whole TLS session. Server `Start()` paths (`HttpServer`, `HttpsServer`, `WsServer`, `WssServer`) also fail cleanly on bind/thread-start errors instead of entering partial running states.

**Crypto follow-up.** Password-based defaults were raised to 300,000 PBKDF2 rounds across `Password.Hash`, password-based `Cipher`, and `Aes.EncryptStr`. `Password.Verify` now treats null/malformed stored hashes as a simple mismatch, `Cipher.Decrypt` returns `NULL` on authentication failure instead of trapping, and the in-tree TLS verifier now rejects leaf certificates that do not advertise TLS server authentication through EKU / compatible key usage.

---

### Zia frontend

**Completion.** Path-aware overloads: `CompleteForFile`, `CheckForFile`, `HoverForFile`, `SymbolsForFile`. Relative `bind` paths now resolve against the active file, so multi-file projects keep completion accuracy when the IDE's working directory differs from the file's directory.

**Lowerer.** Three monolithic lowering files (`Lowerer_Expr_Binary`, `_Call`, `_Collections`) shed ~540 LOC into helper classes (`BinaryOperatorLowerer`, `CallArgumentLowerer`, `CollectionLowerer`). No behavior change; the surface is now navigable.

**Type display.** One recursive `appendTypeString` helper covers every type. Diagnostics show terser user-grade names — `String?` instead of `Optional<String>`, `List[Integer]` instead of the internal form.

**Sema errors.** Tightened across the board. Notably: `var x = null;` without an explicit type fails with a targeted "cannot infer type from null initializer" message instead of silently producing `Optional<Unknown>`. Optional member access without `?.` or `!.` produces a targeted error naming the Optional type.

---

### Linker

`uname`, `gethostname`, and `sysctlbyname` added to the dynamic-symbol policy so `Viper.Machine.OS` / `Hostname` link cleanly. New `dynamicSymbolHasPrefix` plus `isKnownMacLibcxxDynamicSymbol` classify Itanium-ABI C++ runtime symbols (`ZNSt`, `ZSt`, `Zna`/`Znw`/`Zda`/`Zdl`, `cxa_`, `gxx_personality_`) with leading-underscore handling for macOS. The three platform planners consume the new helper so C++ runtime symbols route through the correct dylib or import library. `stripDynamicSymbolLeadingUnderscores` now also strips the macOS `$DARWIN_EXTSN` suffix that Mach-O sometimes appends to libsystem symbols — without this, symbols like `_readdir$DARWIN_EXTSN` didn't match their unsuffixed dynamic-import entries and the Mac import planner fell back to static resolution.

---

### Tools & codegen

- The frontend tools' `--` program-args separator now matches before the generic flag-forwarding branch. `viper run file.zia -- --foo` actually reaches the user program now.
- `native_compiler` temp paths combine PID, steady-clock tick, and an atomic counter so two parallel compiles in one process can't collide.
- x64 `--asset-blob` errors cleanly when combined with text-asm mode (requires `--native-asm` or a companion `--extra-obj`) instead of silently dropping the asset data.

---

### Platform input

- macOS no longer maps bare arrow keys to PageUp/PageDown/Home/End. The Fn+arrow translation block was gating on `NSEventModifierFlagFunction`, which Cocoa sets on every arrow press — so the gate intercepted ordinary arrow input. Real Fn+arrow on compact keyboards keeps working through the character switch.
- Mouse wheel events no longer have their delta destroyed by coordinate localization. The `mouse` and `wheel` payloads share a union, so any path that wrote `mouse.x/y` on a wheel event silently zeroed the scroll delta.
- Linux text-input events now enqueue every UTF-8 codepoint committed by X11 input methods instead of dropping everything after the first codepoint. Linux text clipboard operations are implemented again for editor widgets and other GUI text surfaces.

---

### IL

- `Canvas.CopyRect` and `Canvas.Screenshot` declare `obj<Viper.Graphics.Pixels>` return types so the `Pixels` type carries through to callers instead of erasing to `obj`.
- Audio surface edit: `AudioUpdate` (`Viper.Sound.Audio.Update`) added; `AudioEncode` (`Viper.Sound.Encode`) removed.

---

### Build

- VERSION bumped `0.2.4-dev` → `0.2.5-snapshot`.
- GUI test targets now key off `VIPER_BUILD_TESTING`, matching the rest of the tree. Fixes a long-standing registration bug where GUI tier tests existed in source but were silently absent from CTest.

---

### Tests

Net additions across the cycle: a few thousand lines of new test coverage spread across runtime, GUI, codegen, and linker. Highlights:

- Six new 2D-graphics contract suites lock down the new HiDPI-shared helpers and explicit-alpha flag.
- `test_vg_tier3_fixes` now covers synthesized double-click dispatch, `Tab` / `Shift+Tab` focus traversal, screen-space context-menu anchoring + capture, command-palette visible-window management, and file-dialog save-field editing/scroll behavior.
- `RTDialogueContractTests`, `RTQuadtreeTests`, `RTPhysics2DTests`, and `test_rt_physics_joints` cover the new game-runtime fixes.
- Audio coverage: new `TestWavStream` (float WAV, metadata-heavy headers); `TestMp3Decode` adds incremental-decode and Huffman-table cases; `RTAudioIntegrationTests` covers crossfade pause, foreground reclaim, seek isolation, and playlist clamping; `RTSoundBankTests` guards long-name keys; `RTMusicGenTests` covers short-note release and silent-channel skip.
- New `test_vg_audit_fixes` (19 cases) and `test_vg_tier1_fixes` / `test_vg_tier2_fixes` cover the GUI widget audit work — tabbar drag threshold, dropdown keyboard nav and wheel, textinput UTF-8 max-length, treeview nested click, scrollview thumb drag, and the rest.
- `test_runtime_import_audit` audits the runtime surface against the dynamic-symbol policy. New `test_3dbowling_native_build.sh` smoke-tests the 3D bowling demo through the native build pipeline.
- New `RTKeyboardTests` UTF-8 case, `RTKeyChordTests` wrong-order reset, `RTActionMappingTests` chord release edge.
- New `FrontendToolAndNativeCompilerTests` covers the `--` arg parsing, x64 asset-blob gate, and unique-temp-path contract.
- Three new baseball probes (`human_smoke_probe`, `human_legality_probe`, `human_pacing_probe`) confirm the new manager classes are behavior-preserving against the AI baseline.
- Tier-2 / tier-3 / `widgets_new` GUI suites grow new cases for the multiline TextInput surface, editable Spinner, dialog button presets and text wrapping, notification fade/slide, and TabBar keyboard shortcuts. The `scrollview_hit_test_excludes_scrollbar_gutter` audit case got fixed up to actually force a scrollbar visible — the previous setup was passing under the old conservative gutter heuristic but didn't exercise the precise visibility gate that the production code now uses.
- Two tests temporarily disabled with re-enable markers: `test_rt_model3d` and `zia_smoke_3dbaseball`.

---

### Demos & docs

**Demos.** Pac-Man rewritten as Crackman (split into session/progression/frontend, smoke probe, audio banks). Paint gains layers, undo/redo, and an expanded feature set. ViperIDE adopts the new GUI APIs (per-file IntelliSense, pixel-position hover, custom fonts, theme toggle, font zoom). Two new 3D demos (`3dbaseball`, `3dscene`, ~1.1 KLOC combined). The marquee new piece is a text-mode human-manager franchise shell on top of the existing baseball engine — pacing profiles, interactive lineup building, save slots, three new probes — registered across all four `build_demos.*` scripts.

**Docs.** Comprehensive sweep across `viperlib/` (audio, GUI, crypto, network) reflecting this cycle's runtime changes, plus Zia-reference clarifications (`?` vs `?.`, named-argument rules, `foreign func`) and cross-platform notes for the macOS framework removal. A comment-grinding pass added or improved Doxygen blocks across ~50 `.c` files in `src/runtime/{graphics,text,core}`, then extended through the network + crypto core (rt_rsa.c full bigint stack — DER walker, PKCS#1 / SubjectPublicKeyInfo parsers, Montgomery R² / N0-inverse / CIOS multiplication / modexp ladder with constant-time cswap, PSS encode + verify, PKCS#1 v1.5 verify; rt_ecdsa_p256.c critical functions — bigint helpers, P-256 field arithmetic, Jacobian point operations, sign with nonce-loop + low-S canonicalization). No behavior change in any of it; eliminates the last placeholder `/// @brief X the foo.` stubs and gives the crypto-correctness core the documentation density a future security audit deserves.

---

### Commits Included

| Commit | Date | Summary |
|---|---|---|
| `d58df4f98` | 2026-04-14 | pacman → crackman binary rename, chess + crackman UI polish, VERSION → 0.2.5-snapshot |
| `74f4ec4c7` | 2026-04-14 | Crackman split into session/progression/frontend, smoke probe, audio banks |
| `8126432f6` | 2026-04-15 | Runtime surface hardening, Crackman progression |
| `a34c3d555` | 2026-04-15 | vipersql/xenoscape demo annotations, Paint expansion, runtime polish |
| `06c33c339` | 2026-04-15 | Tab tooltips, CodeEditor APIs, HiDPI toolbar/tabbar polish |
| `d54e03b9d` | 2026-04-16 | Per-file IntelliSense, pixel-position hover, lowerer refactor, custom fonts |
| `2fe3b9a1e` | 2026-04-16 | Tutorial-comment sweep across 8 demos, GUI widget pass, Widget.Focus runtime API |
| `6c600dbd9` | 2026-04-16 | GUI correctness sweep (dialog, tooltip, notification, findreplace, tabbar, etc.) + lowerer refactor |
| `deccd1978` | 2026-04-16 | macOS arrow-key fix, mouse-wheel union aliasing, dropdown flip-above, widget polish |
| `c5b0685af` | 2026-04-16 | Skeleton/bone validation, terrain normals, capsule contract, navmesh hash, Canvas3D V·P, parallax, UTF-8 text |
| `cd07b31af` | 2026-04-16 | Camera3D sanitizers, Mesh3D ref slots, lazy morph rebuild, packed-delta backend handoff |
| `d5d938e55` | 2026-04-16 | Retain-then-release rolled out across Graphics3D; Scene3D TRS decomposition |
| `5f29ba785` | 2026-04-16 | AnimController3D root-motion rotation, Camera3D shake-aware ScreenToRay + aspect sync, backend resize contract |
| `604a8dd68` | 2026-04-16 | Cubemap3D roughness sampling + face-UV helpers, backend camera-forward/ortho, PBR alpha-blend gate |
| `edc36bf84` | 2026-04-17 | Mac C++ dynamic-symbol classification, FBX path normalization, Scene3D subtree bounds, two new 3D demos |
| `1dca700b1` | 2026-04-17 | Shared HiDPI scale helpers + explicit-alpha flag, canvas state mirror, six new 2D contract suites |
| `23cf1d590` | 2026-04-17 | Dialogue font-aware wrap + UTF-8, Quadtree dynamic capacity, Physics2D joint cleanup |
| `7b3a66211` | 2026-04-17 | Baseball human-manager franchise shell, audio codec consolidation, frontend `--` arg fix, x64 `--asset-blob` gate |
| `605ea3555` | 2026-04-17 | Audio fixes (WAV float, incremental MP3, SoundBank long keys, crossfade pause/stop), music companion APIs, baseball in `build_demos.*` |
| `0d4ff3147` | 2026-04-17 | Crossfade pause clock + foreground-reclaim, MusicGen ADSR short-note release, silent-channel skip |
| `f8a565a0b` | 2026-04-17 | Dark-theme palette refresh, dropdown keyboard+wheel, tabbar drag threshold, textinput UTF-8 + state-aware paint, runtime input edge-vs-level fixes |
| `0e5b49868` | 2026-04-17 | Dialog/Notification rewrites (rounded card, text wrap, fade/slide), multiline TextInput, editable Spinner, Tooltip card + hide-delay, TabBar keyboard nav, Button/Slider/ProgressBar/FloatingPanel polish, ViperIDE settings/about overlays |
| `35613e928` | 2026-04-18 | rtgen audit cleanup (`Music.SetLoop` IL surface + 4 internal helpers classified), font-handle metric-safety guard + lazy inheritance, Breadcrumb rounded-card rewrite, ViperIDE monitor-aware window bounds |
| `c5b491911` | 2026-04-18 | Keyboard nav + accessibility pass (Toolbar / SplitPane / Dropdown), TreeView drag-drop, ListBox multi-select with Ctrl/Shift modifiers, TextInput redo + double-click word select, TabBar press-release coupling, paint-flag invalidation fix |
| `ad2948be5` | 2026-04-18 | Framework double-click synthesis + Tab-focus dispatch, FileDialog editable save-name + list scroll, popup routing (ContextMenu / FloatingPanel / Breadcrumb overflow), Linux X11 UTF-8 text input + clipboard |
| `2f103a8ce` | 2026-04-18 | HTTP cookie jar + gzip decode + streaming download + relative redirects, chunked request-body parsing on HttpServer, SSE reconnect, TLS per-request verify (`HttpReq.SetTlsVerify`) + ALPN + error diagnostics |
| `b240f18be` | 2026-04-18 | HTTP keep-alive + connection pooling across `HttpClient` / `HttpReq` / `RestClient`, HttpServer `Connection`-header awareness, macOS `$DARWIN_EXTSN` handling in DynamicSymbolPolicy, runtime-surface classification follow-ups |
| `0ec6b1cd4` | 2026-04-18 | New `HttpsServer` + `WssServer` (server-side TLS), full from-scratch TLS-server handshake with EC cert/key loading (SEC1 + PKCS#8), ECDSA-P256 math expansion, BASIC frontend route lowering for `HttpsServer` |
| `a1484835b` | 2026-04-18 | Network security hardening — cross-origin redirect strips sensitive headers, TLS server-side SNI validation, WSS Origin/Host check, CRLF injection guard, HTTP/1.0 keep-alive defaults, dual-stack IPv6 UDP, ephemeral port + thread-safe server state, Zia frontend `HttpsServer` lowering |
| `855ee2922` | 2026-04-19 | Native RSA (rt_rsa.c/h — RSA-PSS-SHA256 verify, PKCS#1 / SubjectPublicKeyInfo parsing) + TLS server key-type discrimination, in-tree X.509 chain validator drops macOS `Security.framework` link, ~50-file Doxygen comment-grinding pass across runtime graphics / text / core, Zia-reference + crypto/network/cross-platform doc clarifications |
| `55dbe72b7` | 2026-04-19 | PBKDF2 iteration default 100K → 300K + `Password.Hash` floor 10K → 100K (rt_aes / rt_cipher / rt_password), TLS validator enforces Extended Key Usage / TLS server-auth on leaf cert, in-tree `rt_network_timegm_utc` replaces platform `timegm()` (last platform time-call removed from network stack), HTTP / HTTPS server shared-helper consolidation (`rt_http_server_shared.inc`), WS / WSS server shared-helper consolidation (`rt_ws_shared.inc`), expanded RTPasswordTests / RTCipherTests / RTHighLevelNetworkTests / RTRestClientTests / RTNetworkHardenTests coverage |
| `18f1cf310` | 2026-04-19 | WebSocket subprotocol negotiation (`WsServer` / `WssServer.SetSubprotocol` + `get_Subprotocol` IL surface, `rt_ws_connect_protocol` / `rt_ws_connect_for_protocol` client entries, shared `Sec-WebSocket-Protocol` validation in `rt_ws_shared.inc`), multipart escape correctness (RFC 7578 quoted-string handling for `name` / `filename` parameters), connection-pool refactor (`close_tcp_connection` / `remove_entry_at` / `track_connection` extracted helpers, ~30% deduplication), Doxygen grind on rt_rsa.c bigint stack + rt_ecdsa_p256.c critical crypto functions, new RTMultipartTests + expanded RTWebSocketTests / RTHighLevelNetworkTests coverage |
| `ce5db6d9a` | 2026-04-19 | Full in-tree HTTP/2 transport (`rt_http2.c` / `rt_http2.h`, ~2,150 LOC — HPACK with Huffman, frame layer, client + server roles, connection-specific-header rejection), `HttpsServer` auto-switches to HTTP/2 when ALPN negotiates `h2` (same router / handler surface, no caller changes), high-level HTTP client pools HTTP/2 connections across requests with per-request stream-ID advancement, new `rt_http_req_set_force_http1` public API for HTTP/1.1-specific framing, critical ALPN bugfix — `clienthello_offers_alpn` was silently failing for every multi-token preferred list due to `strlen()` on a non-NUL-terminated substring, so h2 never negotiated until now; ALPN selection is now plumbed through the internal HTTPS/h2 runtime path and covered by RTHttp2Tests, new RTHttp2Tests suite + expanded HTTPS+h2 end-to-end coverage in RTHighLevelNetworkTests |
| `f56d9c754` | 2026-04-19 | glTF extended attribute coverage (COLOR_0 / TANGENT / JOINTS_0 / WEIGHTS_0) + triangle-strip/fan triangulation + TRS matrix decode correctness, Skeleton3D bone palette 128 → 256 via `VGFX3D_MAX_BONES` (D3D11 / Metal / OpenGL backends accept the expanded palette), MorphTarget3D dynamic shape capacity replaces fixed-size arrays, Canvas3D sort-key helpers + deterministic shadow-directional-light selection + instanced-batch depth key, Scene3D uses `rt_canvas3d_draw_mesh_matrix_skinned_keyed` in place of the palette-swap hack, Camera3D non-mutating `rt_camera3d_get_render_projection` for multi-output aspect ratios, `HttpReqSetForceHttp1` IL binding (`Viper.Network.HttpReq.SetForceHttp1`) registered in `runtime.def` so the HTTP/1.1-pin knob is reachable from Zia and BASIC, `RuntimeSurfacePolicy.inc` classifies `rt_skeleton3d_internal.h` / `rt_http2.h` / `rt_rsa.h` as internal and annotates the cross-origin redirect / ALPN helpers, expanded `test_rt_canvas3d_gpu_paths` (+328), `test_rt_gltf` (+225), `test_rt_morphtarget3d` (+49), `test_rt_scene3d_bindings` (+110) coverage |
| `78c9ba8b0` | 2026-04-19 | Per-canvas dynamic-light cap raised 8 → 16 across runtime ceiling + D3D11 cbuffer Light array + Metal fragment uniform array + OpenGL uniform/sampler arrays (shader sources stringify `VGFX3D_MAX_LIGHTS` so a future bump touches one constant instead of nine literals), `RenderTarget3D` CPU-side color/depth buffers allocated lazily on first CPU touch (new `vgfx3d_rendertarget_ensure_color` / `_ensure_depth` inlines) — `AsPixels` / `Screenshot` / postfx-on-RTT / SW-backend bind all call ensure-on-read, D3D11 RTT readback now follows the same lazy `sync_color`-callback contract as Metal/OpenGL (staging copy + map + memcpy moved out of `end_frame`), `PostFX3D` chain switched from fixed `effects[8]` to growable heap buffer with doubling capacity (no more silent truncation past 8 effects), expanded `test_rt_canvas3d` / `test_rt_canvas3d_gpu_paths` / `test_rt_postfx3d_snapshot` coverage |
| `02db42637` | 2026-04-19 | Ordered GPU postfx chain — backends now consume the full `vgfx3d_postfx_chain_t` (ordered effect list) instead of the legacy flat `vgfx3d_postfx_snapshot_t` that collapsed a chain into a last-write-wins per-type snapshot; authored order and repeated-effect passes are preserved on the GPU path to match CPU behavior. New public helpers `vgfx3d_postfx_get_chain` / `_chain_copy` / `_chain_reset` / `_chain_free`; vgfx3d_backend `present_postfx` + `set_gpu_postfx_snapshot` hooks retyped to take the ordered chain; OpenGL adds a ping-pong `gl_apply_postfx_chain` path with a readback FBO so multi-pass chains alternate between offscreen targets; D3D11 and Metal rewired to walk the chain in order; canvas latches a per-frame `frame_postfx_chain` so backend post-present reads (screenshots / GPU readback) see the same effects that were authored at frame start. FBX scene-graph import — `rt_fbx_loader` gains real `fbx_build_scene_root` extraction (walks FBX `Model` objects, decodes namespace-stripped names, extracts per-model TRS with Z-up→Y-up correction, binds meshes + materials via the connection table, and assembles a `SceneNode3D` tree); new `rt_fbx_get_scene_root` asset API; `rt_fbx_asset` finalizer releases owned meshes / materials / skeletons / animations / morph targets / scene root correctly; `Model3D.Load(.fbx)` clones the authored FBX hierarchy into its template tree instead of collapsing to synthetic `mesh_N` nodes (synth-mesh path remains as fallback when the source has no hierarchy). 3D runtime comment grind — Doxygen added to previously-undocumented helpers across rt_camera3d / rt_canvas3d / rt_material3d / rt_model3d / rt_navagent3d / rt_navmesh3d / rt_particles3d / rt_postfx3d / rt_raycast3d / rt_rendertarget3d / rt_fbx_loader; expanded `test_rt_model3d` (FBX scene-root import coverage) + `test_rt_postfx3d_snapshot` (chain-ordering regression coverage) + `test_rt_canvas3d_gpu_paths` (ordered-chain backend hooks) |
| `ecc26b616` | 2026-04-19 | HDR RenderTarget3D path — new public `RenderTarget3D.NewHdr(width,height)` + `RenderTarget3D.IsHdr`, shared backend readback helpers (`vgfx3d_half_to_float`, `vgfx3d_hdr_to_unorm8`, RGBA16F/32F→RGBA8 conversion), GPU RTT caches now key on target format as well as size so rebinding a same-size HDR/LDR RTT cannot silently reuse the wrong texture format, Metal/OpenGL/D3D11 RTT readback tonemaps HDR RGB into `Pixels` during `AsPixels()`, expanded `test_rt_canvas3d` + `test_vgfx3d_backend_utils` coverage |

<!-- END DRAFT -->
