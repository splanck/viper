# Viper Compiler Platform — Release Notes

> **Development Status:** Pre-Alpha. Viper is under active development and not ready for production use.

## Version 0.2.5 — Pre-Alpha (DRAFT — unreleased)

<!-- DRAFT: release date TBD. v0.2.4 was cut on 2026-04-13. -->

### What this release is about

A polish-and-hardening cycle that grew a notable new capability along the way. Most of the work is in four areas: the audio runtime got a big consolidation pass, the GUI widget library went through a multi-round audit, the 3D graphics stack picked up the correctness fixes that were piling up, and the network runtime grew from "complete HTTPS client" into "full TLS-aware platform" — cookie jar, transparent gzip, streaming downloads, chunked server request bodies, SSE reconnect, keep-alive / connection pooling, per-request TLS controls, and a from-scratch TLS-backed server (`HttpsServer` + `WssServer`). The Zia frontend, linker, and codegen also got smaller targeted fixes. The biggest user-visible new thing is a text-mode human-manager simulator built on the existing baseball engine.

### By the Numbers

| Metric | v0.2.4 | v0.2.5 | Delta |
|---|---|---|---|
| Commits | — | 29 | +29 |
| Source files | 2,869 | 2,890 | +21 |
| Production SLOC | 450K | 475K | +25K |
| Test SLOC | 183K | 191K | +8K |
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

**Camera3D.** Input sanitizers for aspect, near/far clip, FOV, and ortho size catch pathological values before they hit the projection builder. `look_at` with degenerate eye==target preserves the camera position instead of returning identity. `Canvas3D.Begin` syncs aspect against the active output before rebuilding the projection. `ScreenToRay` uses the shaken render pose during camera shake so picking matches the visible image.

**Canvas3D.** `Begin2D` caches the full V·P (matched to `Begin3D`) so overlay unprojection has consistent semantics. Camera params now carry world-space forward and an `is_ortho` flag — backends specialize fog / rim / specular per mode. Cleanup paths on `Canvas3D.New` / `RenderTarget3D.New` release partially initialized wrappers on failure.

**Reference ownership (cross-cutting).** Every Graphics3D subsystem that holds references to other graphics objects now uses a uniform retain-then-release slot pattern: assign retains the new value first, then releases the old, so re-assigning a slot to its current value can't briefly drop the refcount to zero. Rolled out across Mesh3D, MorphTarget3D, Terrain3D, Water3D, Particles3D, Scene3D, InstBatch3D, Cubemap3D.

**Terrain3D / Heightfield.** Normal computation now multiplies the Y component by both horizontal scales (was Y by X only); lateral components carry the perpendicular horizontal scale. Non-uniformly scaled terrain reports correct lighting and collision. Splat texture rebakes lazily off a `splat_dirty` flag. Degenerate `1×1` splat maps treated as uniform coverage instead of dividing by zero.

**Water3D.** Gerstner phase switched to standard `k·x − ω·t` form so waves travel in the user's `+direction` instead of against it.

**Particles3D.** Spawn `delta_time` clamped to ~4 frames so a frame hiccup doesn't dump thousands of particles in one tick. Sphere emitters sample volume uniformly; cone emitters sample solid angle uniformly. Additive particles use an explicit additive blend path while preserving per-particle alpha.

**MorphTarget3D.** Weights clamped to `[-1, 1]`. Packed delta arrays rebuilt lazily off a generation counter — weight-only changes skip the rebuild; shape edits or delta edits bump the generation.

**NavMesh3D.** Edge keys switched from `lo * 1_000_000 + hi` to bit-packed `(lo << 32) | hi`. The old formula collided on meshes with vertex indices ≥ 1M (e.g. edges `(1, 2_000_000)` and `(2, 1_000_000)` both hashed to `3_000_000`).

**Physics3D / Collider3D.** `capsule_axis_endpoints` documents its Y-only contract; opt-in `RT_PHYSICS3D_STRICT_CAPSULE_AXIS` build flag traps on non-identity orientation.

**Cubemap3D.** New `cubemap_direction_to_face_uv` and `cubemap_face_uv_to_direction` helpers cover the standard cubemap geometry. Public `rt_cubemap_sample_roughness(cm, dir, roughness)` consumes them for blurred reflection sampling.

**FBX loader.** Cross-platform texture-path normalization (`fbx_is_absolute_path` recognizes POSIX, UNC, and Windows drive prefixes; `fbx_normalize_path` normalizes separators). FBX assets exported on Windows now resolve identically on macOS and Linux.

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

*Performance.* Non-blocking connect with proper timeout replaces blocking `connect`. End-to-end keep-alive / connection pooling — idle TCP/TLS connections cached per `(host, port, tls)` with LRU eviction and idle-timeout scrub. `HttpClient.KeepAlive`, `HttpClient.SetPoolSize`, and `HttpReq.SetKeepAlive(i1)` give callers per-client and per-request control.

*Security.* Cross-origin redirects strip credential-bearing headers (`Authorization`, `Cookie`, etc.) so a redirect to a different origin can't leak tokens. URL parsing rejects CRLF sequences to block header-injection attacks. Cookie-jar match tests prevent cross-domain and cross-path leakage; expired cookies are purged.

**HTTP server.**
*Request handling.* `Transfer-Encoding: chunked` request bodies are now decoded correctly — browser streaming uploads and `curl --data-binary @-` finally work. Header token scanning is robust against substring false positives.

*Connection framing.* The response path is Connection-header-aware: the server inspects the incoming `Connection:` value, honours `keep-alive` on HTTP/1.1 by default (and `close` when asked), and emits matching response headers plus proper `Content-Length` / `Transfer-Encoding` framing so the client knows where responses end. HTTP/1.0 clients correctly default to `Connection: close` (RFC 1945) and only get keep-alive when they opt in. Invalid HTTP version strings are rejected with a 505 / connection close instead of being silently treated as HTTP/1.1.

*Lifecycle.* Thread-safe — `IsRunning` reads and `Stop` calls no longer race on the running flag. The constructor accepts `port=0` to ask the kernel for an ephemeral port (the assigned port is reported via `Port` after `Start`).

**HttpsServer.** New TLS-backed HTTP/1.1 server (`Viper.Network.HttpsServer`). Mirrors the `HttpServer` surface — `Get` / `Post` / `Put` / `Delete` route registration, `BindHandler` for native handler binding, `Start` / `Stop` lifecycle, `Port` / `IsRunning` properties — but every accepted connection goes through TLS first. Constructor takes `(port, cert_path, key_path)`. Inherits the keep-alive + pooling work, so TLS sessions are pooled where keep-alive is requested and the cost of the TLS handshake is amortised across requests. Per-connection send-timeout bounds slow-loris-style attacks. Both the BASIC and the Zia frontends lower literal-route registrations on `HttpsServer` through the same handler-binding pattern as `HttpServer`, so Zia code with `server.Get("/path", "handler")` works identically against either server type.

**WssServer.** New TLS-backed WebSocket server (`Viper.Network.WssServer`). Same constructor shape as HttpsServer (`port, cert_path, key_path`); broadcast surface (`Broadcast(text)` + `BroadcastBytes(bytes)`) plus `ClientCount`, `Port`, `IsRunning`. The wire sequence per client is TCP accept → TLS handshake → HTTP/1.1 upgrade → WebSocket framing. The upgrade path validates the WebSocket `Origin` header against the `Host` header to block cross-origin WebSocket abuse from browser-based attackers.

**RestClient.** Keep-alive and pool-size configuration thread through to the underlying HTTP client, so REST-heavy workflows (microservices, paginated API loops) reuse connections transparently without changing call sites.

**SSE (Server-Sent Events).** Automatic reconnect-after-disconnect. The client re-opens the connection when the server drops and honours `Last-Event-ID` to resume where the stream left off, instead of silently ending on transient network failures. Matching non-blocking connect + timeout behaviour as the HTTP client.

**TLS.**
*Client side.* `HttpReq.SetTlsVerify(bool)` (`Viper.Network.HttpReq.SetTlsVerify`) gives per-request verification control — useful for dev servers with self-signed certs and staging environments with internal CAs. Default stays secure (verification on). `alpn_protocol` on `rt_tls_config_t` declares a single protocol (`http/1.1`, `h2`) during the handshake; the HTTP client wires this up from the request URL scheme. `rt_tls_last_error()` captures connect/handshake errors in thread-local storage so trap messages surface the underlying diagnostic (hostname mismatch, cert expired, handshake protocol error) instead of generic "TLS handshake failed".

*Server side.* From-scratch TLS 1.3 server implementation supporting the new HttpsServer / WssServer. PEM cert + EC private key loading (both SEC1 and PKCS#8 formats via a small DER TLV walker), cert-chain support, separate handshake-key and application-secret derivation, and a thread-local server-side error mirror (`rt_tls_server_last_error`) matching the client-side pattern. The server validates the client's SNI extension against the leaf certificate's CN/SAN before completing the handshake — blocks a client from connecting to one host and receiving another host's cert from a multi-tenant TLS server (bare IPv4/IPv6 literals are correctly recognised as having no SNI).

*Underneath.* The ECDSA-P256 module gained the wide-arithmetic and modular-math primitives (`u256_mul_wide`, `u512_mod_u256`, `u256_mod_{add,double,mul}`) needed for the certificate signature path that most modern TLS uses.

**UDP.** Substantial socket-layer rewrite. Dual-stack IPv4 / IPv6 sockets work transparently — the same `Udp` instance can send to and receive from both address families. Sender address / family / port are captured on `recv_from` so callers can reply without re-resolving. Address resolution is centralised across unicast and multicast send paths.

**WebSocket / SMTP.** Small correctness follow-ups on header parsing and error paths consistent with the HTTP/TLS changes above.

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

Pac-Man renamed to Crackman and split into session/progression/frontend with a smoke probe and audio banks. Paint gains layers, undo/redo, and an expanded feature set. ViperIDE wires up the new GUI APIs (per-file IntelliSense, pixel-position hover, custom fonts, theme toggle, font zoom), lifts the settings + about modals out of `main.zia` into a dedicated `IdeOverlays` overlay manager, and clamps window bounds against the active monitor so the IDE can't boot into a window that overflows the display. All ten demos in `build_demos_mac.sh` carry tutorial-style annotations. Two new 3D demos (`3dbaseball`, `3dscene`, ~1,100 LOC of Zia combined). The baseball engine grows a text-mode human-manager franchise shell — pacing profiles, interactive lineup building, save-slot management, three new probes — registered across all four `build_demos.*` scripts; `--auto-season` preserves the legacy regression path; `baseball_saves/` is now gitignored so the franchise shell's interactive save root doesn't clutter `git status`. New codemaps for the bytecode VM and graphics-disabled runtime stubs; `viperlib/audio.md` picks up the new music APIs; `viperlib/gui/{application,containers,widgets}.md` refreshed alongside the widget overhaul; clarifications to the optimizer rehab status, `--no-mem2reg` behavior, graphics-stub policy, and cross-platform validation language.

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

<!-- END DRAFT -->
