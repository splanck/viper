# Viper / Zia Bugs & Issues Log

Running log of compiler, language, runtime, and tooling problems encountered while
building the Paint overhaul (see `paint.md`). **Address later** — this file is a
capture buffer, not a work queue for the paint task itself.

Update **immediately** whenever a Viper runtime or Zia issue is hit.

Severity: 🔴 blocker · 🟠 major (forces workaround) · 🟡 minor/papercut · ⚪ missing feature / enhancement.
Component: `zia-fe` (frontend: lexer/parser/sema/lowerer) · `il` · `vm` · `codegen` · `runtime` · `tooling`.

---

## Resolved

### ISSUE-001 — ✅ runtime: no `Pixels` text-rendering primitive
- **Component:** runtime (graphics/2d)
- **What:** `Viper.Graphics.Canvas` has `Text`, `TextBg`, `TextCentered`, `TextCenteredScaled`, `TextRight`, `TextWidth`, `TextHeight`, but `Viper.Graphics.Pixels` exposes **no** text method (no `DrawText`). You cannot rasterize text directly into an offscreen `Pixels` layer buffer.
- **Impact:** A paint "text tool" can't write glyphs into a layer the normal way. Forces a workaround: draw text onto the on-screen `Canvas`, then `Canvas.CopyRect(...) -> Pixels` and `Pixels.Copy` into the active layer — hacky (depends on visible canvas region / scratch area) and can't render text larger than the window.
- **Repro:** Survey of `docs/viperlib/graphics/pixels.md` + `src/runtime/graphics/2d/rt_pixels_*.c` — no text entry point.
- **Expected:** `Pixels.DrawText(x, y, text, color)` (+ scaled/bg variants) mirroring the Canvas font API, writing into the buffer.
- **Workaround:** `Canvas.Text` → `Canvas.CopyRect` → `Pixels.Copy` (see plan C6).
- **Resolution (2026-06-16):** Added `Pixels.DrawText`, `DrawTextBg`, scaled/background variants, alignment helpers, and text metric bindings. Text now rasterizes directly into `Pixels` buffers with clipping and alpha-preserving color handling.
- **Coverage:** `test_rt_pixels_draw`, `test_zia_bugfixes`.

---

### ISSUE-002 — ✅ zia-fe: unused-parameter warning `W001` has no suppression
- **Component:** zia-fe (sema)
- **What:** A parameter that is declared but unused emits `W001`. There is **no** suppression mechanism — prefixing with `_` (e.g. `_x`) does **not** silence it (confirmed: `_x`/`_y` still warned).
- **Impact:** Interface implementations / overrides must match a fixed signature, so a tool that legitimately ignores some params (e.g. `Fill.onDrag(x, y, ctx)`) emits unavoidable warnings. A polished demo can't be fully warning-clean without hacks (referencing the param in a no-op).
- **Repro:** `/tmp/zia_iface_probe.zia` — `expose func act(_x: Integer, _y: Integer, tag: Integer)` → two `W001`.
- **Expected:** `_`-prefix (or `@unused` / `_` bare name) should mark a parameter intentionally unused and suppress `W001`.
- **Workaround:** Keep interface param lists minimal (pass a context object), accept a few warnings, or add a `_ = x;`-style no-op.
- **Resolution (2026-06-16):** Zia now suppresses unused-parameter `W001` for parameter names that start with `_`, while preserving ordinary local-variable unused diagnostics.
- **Coverage:** `test_zia_bugfixes`.

### ISSUE-003 — ✅ zia-fe: null comparison rejected for interface types but allowed for class types
- **Component:** zia-fe (sema)
- **What:** `x != null` / `x == null` is a hard error (`V-ZIA-SEMA`: "Cannot compare non-nullable Tool with null") when `x` is an **interface** type, but the same comparison compiles for **class** types — e.g. `GameButton` field in `ui/button.zia:150` (`if widget == null`) and `List[PaintLayer].get()` result in `layers.zia:161`. Relatedly, `List[Interface].get(i)` is typed **non-nullable** whereas `List[Class].get()` is treated as nullable in existing code.
- **Impact:** You cannot use `null` as a "no current tool"/"absent" sentinel for an interface-typed field; must use a Null-Object (as `gamebase.zia` does with `NullScene`). Asymmetry is surprising and inconsistent.
- **Repro:** `/tmp/zia_iface_probe.zia` with `var t = tools.get(i); if t != null { ... }` → error at the comparison.
- **Expected:** Consistent nullability rules across class and interface types (either both comparable to null, or neither, with a documented Optional path).
- **Workaround:** Null-Object pattern; guarantee registry always returns a real `Tool`; avoid null checks on interface values.
- **Resolution (2026-06-16):** Interface types now accept the same null literal assignment/comparison path as class and pointer values, removing the class/interface asymmetry.
- **Coverage:** `test_zia_bugfixes`.

### ISSUE-004 — ✅ runtime/docs: `Gradient2D.Sample` rejects a float argument
- **Component:** runtime (graphics/2d) / docs
- **What:** `docs/viperlib/graphics/rendering2d.md` documents `Gradient2D.Sample(t)` with `t` a Number 0.0–1.0, but calling `g.Sample(0.5)` from Zia fails to compile: `error[V-ZIA-TYPE-MISMATCH]: expected Integer, got Number`. The binding appears to expect an Integer (possibly a step index 0..steps-1), contradicting the docs.
- **Impact:** Can't sample a gradient by normalized position as documented. Worked around by computing gradient colours with `Color.Lerp(c1, c2, pct)` (integer 0–100) instead, so the paint gradient tool doesn't use `Gradient2D`.
- **Repro:** `/tmp/zia_grad_probe.zia` — `var g = Gradient2D.New(fg,bg,256); g.Sample(0.5);`.
- **Expected:** Either accept a Number per docs, or fix docs to show the Integer index form. `Gradient2D.New` and `FillVertical` should also be re-verified.
- **Workaround:** `Color.Lerp` for gradient interpolation.
- **Resolution (2026-06-16):** `Gradient2D.Sample` and `SampleRGBA` now accept normalized `Number` positions (`0.0..1.0`) per docs, with explicit `SamplePct`/`SampleRGBAPct` methods for legacy integer-percent calls.
- **Coverage:** `test_rt_graphics2d`, `test_zia_bugfixes`.

## Watch list (pre-existing, from project memory — confirm if hit during this work)

- **Cross-module inherited `init()` with args** → "store operand type mismatch" IL error (`zia-fe`/`il`). Workaround: named setup methods (`initGame()`-style) instead of inheriting `init()`. Relevant because this work adds an interface + class hierarchy for tools.
- **`viper bench`** may hang on some IL files (`tooling`).

## Xenoscape Upgrade Findings

### ISSUE-005 — 🟠 codegen: native macOS aggregate-return miscompile
- **Component:** codegen
- **What:** `examples/games/xenoscape/physics.zia` documents that `MoveResult`
  must remain a heap class because the native-linked macOS demo binary currently
  miscompiles aggregate returns.
- **Impact:** Zia code that should naturally return a lightweight record/struct
  has to allocate or reuse a class instance instead.
- **Workaround:** `PhysicsHelper` owns one reusable `MoveResult` instance and
  mutates it in place.
- **Status:** Confirmed by Xenoscape source notes; preserve the workaround
  during this upgrade.

### ISSUE-006 — 🟡 zia-fe: broad namespace binds trigger ambiguous helper warnings
- **Component:** zia-fe
- **What:** Building Xenoscape emits `warning[V3001]` because
  `Viper.Math.Lerp` conflicts with `Viper.Graphics.Color.Lerp` when both modules
  are bound.
- **Impact:** Demos that reasonably bind graphics and math APIs get warning
  noise for common helper names.
- **Expected:** Alias/import guidance or frontend behavior that lets common
  helpers coexist without broad-bind warning churn.
- **Workaround:** Prefer targeted aliases for one side of a conflict when
  touching affected files.

### ISSUE-007 — 🟡 zia-fe/tooling: intentional fixed-count loops still produce unused-variable noise
- **Component:** zia-fe / tooling
- **What:** Xenoscape builds emit `warning[W001]` for loops such as
  `for i in 0..MAX_ENEMIES` when the loop variable is intentionally unused.
- **Impact:** Straightforward fixed-count initialization loops are warning-noisy
  unless rewritten as manual `while` loops.
- **Expected:** A documented discard pattern for loop variables, or warning
  suppression for a conventional intentionally-unused loop name.
- **Workaround:** Use `while` loops in newly touched code when the index is not
  needed.

### ISSUE-008 — 🟠 zia-fe: whole-project builds can mask missing per-module imports
- **Component:** zia-fe
- **What:** `examples/games/xenoscape/save.zia` and `lore.zia` used `Int(...)`
  without binding `Viper.Text.Fmt`. A whole-project build through
  `main.zia` compiled successfully, but building the new
  `progression_probe.zia` exposed `V-ZIA-UNDEFINED` errors for those same
  `Int(...)` calls.
- **Impact:** Import availability appears to depend on project entry/import
  order, so missing module-local binds can be hidden until a different probe or
  entrypoint imports the module graph.
- **Expected:** Each module should require its own imports consistently, or the
  frontend should make any intentionally-global import behavior explicit and
  deterministic.
- **Workaround:** Add explicit module-local binds for every external symbol used
  by a file; targeted probes are useful because they expose hidden import
  coupling.

### ISSUE-009 — 🟡 docs/runtime: Canvas fullscreen docs call nonexistent keyboard API
- **Component:** docs / runtime API
- **What:** `docs/viperlib/graphics/canvas.md` demonstrates fullscreen toggling
  with `Viper.Input.Keyboard.Pressed(300)`, but the live runtime API exposed to
  Zia has no `Keyboard.Pressed` method. Xenoscape failed to build when using
  that documented call; the available edge-triggered API is
  `Keyboard.WasPressed`.
- **Impact:** Users following the fullscreen example get a compile-time
  `Runtime class 'Viper.Input.Keyboard' has no method 'Pressed'` error.
- **Expected:** Either update the docs to `Keyboard.WasPressed(...)` or provide
  a compatibility alias if `Pressed` is still intended to exist.
- **Workaround:** Use `Viper.Input.Keyboard.WasPressed(KEY_F11)` for one-shot
  fullscreen toggles.

### ISSUE-010 — ✅ runtime: macOS 2D Canvas fullscreen could fail or keep a window-sized drawable
- **Component:** runtime / graphics (ViperGFX macOS backend)
- **What:** Xenoscape's `canvas.Fullscreen()` path still did not enter
  fullscreen after the demo switched to the correct `Keyboard.WasPressed`
  input API. The 2D Canvas path delegates to ViperGFX; on macOS the backend used
  `NSWindow toggleFullScreen:` but Canvas windows are fixed-size by default and
  were not explicitly marked as fullscreen-capable. After native fullscreen was
  enabled, a second failure mode kept the game drawable at its old windowed
  size inside the fullscreen Space.
- **Impact:** 2D Canvas games could call the documented fullscreen API and see
  no visible fullscreen transition, or enter fullscreen while the rendered game
  stayed at the original window size, especially for default non-resizable game
  windows.
- **Expected:** `Canvas.Fullscreen()` should request real native fullscreen
  regardless of whether the window is user-resizable in normal windowed mode,
  while preserving the Canvas's designed logical resolution and scaling draw
  calls/input to the fullscreen framebuffer.
- **Resolution (2026-07-07):** `vgfx_platform_macos.m` now marks windows with
  `NSWindowCollectionBehaviorFullScreenPrimary`, temporarily adds the resizable
  style while entering fullscreen for fixed-size windows, focuses/activates the
  window before toggling, makes the framebuffer view track the window content
  size, accepts AppKit's proposed fullscreen content size, resyncs framebuffer
  metrics from the fullscreen content rect, and restores the original fixed
  window size/style after exiting. The Canvas runtime now stores its logical
  drawing size separately and, in fullscreen, computes an effective coordinate
  scale from `framebuffer_size / logical_canvas_size` so a `1280x720` game fills
  a `1920x1080` display at `1.5x` instead of drawing into only part of the
  enlarged framebuffer.
- **Coverage:** Incremental `vipergfx`/runtime/`viper` build, focused
  `test_window` and `test_input` executables, Xenoscape project build, plus a
  real macOS ViperGFX fullscreen probe that grew a fixed `960x640` window to
  `1920x1080` fullscreen and restored it back to `960x640`; a coordinate-scale
  probe confirmed `1280x720 -> 1920x1080` fullscreen uses scale `1.500` while
  public logical size remains `1280x720`.
