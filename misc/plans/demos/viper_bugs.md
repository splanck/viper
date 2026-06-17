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
