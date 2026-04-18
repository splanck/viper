# Viper Compiler Platform — Release Notes

> **Development Status:** Pre-Alpha. Viper is under active development and not ready for production use.

## Version 0.2.5 — Pre-Alpha (DRAFT — unreleased)

<!-- DRAFT: release date TBD. v0.2.4 was cut on 2026-04-13. -->

### What this release is about

A polish-and-hardening cycle. Most of the work is in three areas: the audio runtime got a big consolidation pass, the GUI widget library went through a multi-round audit, and the 3D graphics stack picked up the correctness fixes that were piling up. The Zia frontend, linker, and codegen also got smaller targeted fixes. The biggest user-visible new thing is a text-mode human-manager simulator built on the existing baseball engine.

### By the Numbers

| Metric | v0.2.4 | v0.2.5 | Delta |
|---|---|---|---|
| Commits | — | 22 | +22 |
| Source files | 2,869 | 2,884 | +15 |
| Production SLOC | 450K | 465K | +15K |
| Test SLOC | 183K | 189K | +6K |
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

**New runtime APIs.** `rt_music_set_loop`, `rt_music_pause_related`, `rt_music_resume_related`, plus `vaud_music_set_loop` on the lib side.

---

### GUI Library

Three rounds of widget audit. The big themes: lifetime correctness, HiDPI consistency, and a dark-theme palette refresh.

**Dark theme.** New cooler-tinted palette (deeper, more saturated background ramp; warmer accents). Default font sizes nudged up (normal 13 → 13.5, large 16 → 17, heading 20 → 21). Button and input rows aligned at 28 px height with a wider border radius. Scrollbar metrics retuned.

**CodeEditor.** New APIs: `CanUndo`, `CanRedo`, `SetTabSize` / `GetTabSize` (1–16), `SetWordWrap` / `GetWordWrap`, plus `GetLineAtPixel` / `GetColAtPixel`. Word-wrap now drives cursor movement, scrollbar math, hit-testing, and `ScrollToLine` — previously it was paint-only and the rest of the widget thought every line was unwrapped. Fold gutters render and toggle. Line-slot metadata is cleared on `SetText` and language switch (fixes a ViperIDE crash on file open).

**TabBar.** Tab tooltips. Stable close-click index that survives `auto_close`. Drag now requires 6 px of pointer movement before reordering (small click-jitter no longer scrambles the tab order). HiDPI scaling on tab metrics; ellipsis on long titles. Keyboard navigation/reorder/close (`Left` / `Right`, `Home` / `End`, `Ctrl+W`, `Ctrl+Shift+Arrow`) is now built in.

**Toolbar / MenuBar.** Real overflow popup (was a stub). Disabled top-level menus paint and behave as disabled. Pixel-icon setters create real image icons instead of casting pointers as glyphs. MenuBar measures to zero height when the macOS native main menu is active.

**FindBar.** Live `GetFindText` / `GetReplaceText`. `SetVisible` routes through the standard widget visibility path. UTF-8-safe match advance.

**ScrollView.** Auto-hide stabilizes (cross-axis case where one bar forces the other no longer ping-pongs). Drag keeps capture until mouse-up even outside the widget. Thumb drag now preserves the within-thumb grab offset instead of snapping.

**Tooltip.** Multi-line wrap. Rounded card paint. Hides automatically when the anchored widget is hidden or destroyed (was a dangling-pointer crash). Wrap loop terminates on whitespace-only input. Hide delay and timed auto-hide now work correctly across repeated hovers on the same widget.

**Dialog.** Rewritten paint and layout — rounded card with stroke, ui-scale-aware metrics (padding, gaps, title bar, button bar, close glyph, icon), and real text wrapping for the message body so dialogs size themselves against the host window instead of overflowing. Button-preset helpers drive Ok/Cancel/Yes/No/etc. row layout. Existing fixes preserved: re-entrancy guard for `on_result` calling `vg_dialog_close` again, and dual `user_data` slots so `on_result` and `on_close` each get their own context.

**Button / Slider / ProgressBar / FloatingPanel.** Visual unification across the primitive widgets — rounded paint, scaled metrics, state-aware fills. Slider gets interaction polish to match.

**Dropdown.** Keyboard navigation on the open popup (arrow / page / home / end). Pressing a key on a closed dropdown opens it. Mouse wheel scrolls the open popup. Panel flips above the trigger when there's no room below. Popup placement and hit-testing now agree in nested layouts; popup row paint tracks fractional scroll instead of jumping a whole row.

**TextInput.** Max-length now counts UTF-8 codepoints, not bytes. Single-line ignores newline character input. Read-only navigation collapses the selection. Focus / hover / read-only / disabled all paint distinctly. Password mask handles long pasted secrets via heap allocation instead of capping at 1023 asterisks. Multiline editing now has real line-based paint, hit-testing, cursor movement, drag selection, and wheel scrolling.

**TreeView.** Click on the blank area of a nested row selects rather than toggling expand (matches IDE convention). Scroll clamps after collapse. Per-node glyph icons and loading indicators finally render.

**ListBox.** Virtual-mode change detection now compares against `prev_selected_index` so virtual lists actually report selection changes. Add/remove/clear/select now invalidate layout/paint immediately, and item labels are clipped to the viewport.

**Spinner.** The numeric field is directly editable now: typing starts inline numeric entry, `Enter` commits, and `Escape` cancels back to the formatted value.

**Layout.** Flex non-stretch alignment preserves the child's measured cross size (no more few-pixel descender clip). VBox/HBox budget child margins when distributing space. SplitPane proportional clamping when min sizes exceed available.

**FileDialog.** Layout metrics extracted to named constants. Multi-select dialogs snapshot the accepted-paths list on success instead of aliasing backend memory (fixes repeat-show + destroy lifetime).

**Notification.** Lazy `created_at` stamp (toasts no longer vanish on the first frame). Fade math guards `fade_duration_ms > 0`. Toasts now use wrapped title/body/action layout plus coordinated fade/slide animation on both entry and dismissal.

**Lifetime fixes.** Tooltip dangling pointer on widget destroy. Dialog use-after-free in nested close. Notification auto-dismiss. CodeEditor line-slot pointer stability. VideoWidget destroy hardening.

---

### Graphics runtime (2D)

**Canvas.** New `RT_CANVAS_MAGIC` guard field; `GetPixel` / `CopyRect` / `SaveBmp` / `SavePng` route every incoming `void*` through a validator so a non-Canvas object fails safely instead of getting reinterpret-cast. `Canvas.CopyRect` and `Canvas.Screenshot` carry their `Pixels` return type through the IL system instead of erasing to bare `obj`.

**Shared coordinate helpers.** `rt_graphics_internal.h` adds four inline helpers (`rtg_sanitize_scale`, `rtg_round_scaled`, `rtg_scale_up_i64`, `rtg_scale_down_i64`) so every 2D drawing site does logical↔physical conversion the same way. New `RT_COLOR_EXPLICIT_ALPHA_FLAG` distinguishes a caller-specified alpha byte from the `0xFF000000` default, routing through different blend paths.

**Text.** UTF-8 codepoint iteration in BitmapFont and Canvas text — multi-byte glyphs now hit-test and render correctly. Old font objects are released when replaced.

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

### Zia frontend

**Completion.** Path-aware overloads: `CompleteForFile`, `CheckForFile`, `HoverForFile`, `SymbolsForFile`. Relative `bind` paths now resolve against the active file, so multi-file projects keep completion accuracy when the IDE's working directory differs from the file's directory.

**Lowerer.** Three monolithic lowering files (`Lowerer_Expr_Binary`, `_Call`, `_Collections`) shed ~540 LOC into helper classes (`BinaryOperatorLowerer`, `CallArgumentLowerer`, `CollectionLowerer`). No behavior change; the surface is now navigable.

**Type display.** One recursive `appendTypeString` helper covers every type. Diagnostics show terser user-grade names — `String?` instead of `Optional<String>`, `List[Integer]` instead of the internal form.

**Sema errors.** Tightened across the board. Notably: `var x = null;` without an explicit type fails with a targeted "cannot infer type from null initializer" message instead of silently producing `Optional<Unknown>`. Optional member access without `?.` or `!.` produces a targeted error naming the Optional type.

---

### Linker

`uname`, `gethostname`, and `sysctlbyname` added to the dynamic-symbol policy so `Viper.Machine.OS` / `Hostname` link cleanly. New `dynamicSymbolHasPrefix` plus `isKnownMacLibcxxDynamicSymbol` classify Itanium-ABI C++ runtime symbols (`ZNSt`, `ZSt`, `Zna`/`Znw`/`Zda`/`Zdl`, `cxa_`, `gxx_personality_`) with leading-underscore handling for macOS. The three platform planners consume the new helper so C++ runtime symbols route through the correct dylib or import library.

---

### Tools & codegen

- The frontend tools' `--` program-args separator now matches before the generic flag-forwarding branch. `viper run file.zia -- --foo` actually reaches the user program now.
- `native_compiler` temp paths combine PID, steady-clock tick, and an atomic counter so two parallel compiles in one process can't collide.
- x64 `--asset-blob` errors cleanly when combined with text-asm mode (requires `--native-asm` or a companion `--extra-obj`) instead of silently dropping the asset data.

---

### Platform input

- macOS no longer maps bare arrow keys to PageUp/PageDown/Home/End. The Fn+arrow translation block was gating on `NSEventModifierFlagFunction`, which Cocoa sets on every arrow press — so the gate intercepted ordinary arrow input. Real Fn+arrow on compact keyboards keeps working through the character switch.
- Mouse wheel events no longer have their delta destroyed by coordinate localization. The `mouse` and `wheel` payloads share a union, so any path that wrote `mouse.x/y` on a wheel event silently zeroed the scroll delta.

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

Pac-Man renamed to Crackman and split into session/progression/frontend with a smoke probe and audio banks. Paint gains layers, undo/redo, and an expanded feature set. ViperIDE wires up the new GUI APIs (per-file IntelliSense, pixel-position hover, custom fonts, theme toggle, font zoom) and lifts the settings + about modals out of `main.zia` into a dedicated `IdeOverlays` overlay manager. All ten demos in `build_demos_mac.sh` carry tutorial-style annotations. Two new 3D demos (`3dbaseball`, `3dscene`, ~1,100 LOC of Zia combined). The baseball engine grows a text-mode human-manager franchise shell — pacing profiles, interactive lineup building, save-slot management, three new probes — registered across all four `build_demos.*` scripts; `--auto-season` preserves the legacy regression path; `baseball_saves/` is now gitignored so the franchise shell's interactive save root doesn't clutter `git status`. New codemaps for the bytecode VM and graphics-disabled runtime stubs; `viperlib/audio.md` picks up the new music APIs; `viperlib/gui/{application,containers,widgets}.md` refreshed alongside the widget overhaul; clarifications to the optimizer rehab status, `--no-mem2reg` behavior, graphics-stub policy, and cross-platform validation language.

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

<!-- END DRAFT -->
