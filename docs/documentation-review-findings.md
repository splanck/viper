---
status: active
audience: maintainers
last-verified: 2026-07-14
---

# Documentation Review Findings

This is the living issue log for the repository-wide documentation review. It records product,
compiler, API, and documentation inconsistencies found while checking every file under `docs/`
against the current registry and implementation.

The review intentionally does not build Viper or run CTest. Reproductions use source inspection,
the checked-in generators, documentation audits, and already-existing binaries only.

## Status and Classification

- **Confirmed bug**: implementation behavior contradicts its own contract or produces an invalid
  compiler state.
- **API inconsistency**: implemented and callable, but related public APIs behave incompatibly or
  the exposed type/signature cannot express the runtime result safely.
- **Needs triage**: evidence indicates a likely defect, but intended behavior needs a maintainer
  decision.
- **Documentation mismatch**: implementation is internally consistent and the handwritten claim
  was stale or wrong. These entries are marked corrected when the documentation patch is present.

## Open Product and Compiler Findings

### VDOC-001 — Zia cannot type the Result returned by `Option.OkOr*`

- **Classification:** API inconsistency / needs triage
- **Area:** Zia runtime-member typing and runtime registry signatures
- **Evidence:** `Viper.Option.OkOr` and `OkOrStr` are registered as returning unqualified `obj`,
  even though `rt_option_ok_or*` always return `Viper.Result` objects. With the existing installed
  compiler, the following reports `Type 'Viper.Option' has no member 'IsErr'`:

  ```rust
  var none = Viper.Option.None();
  var failed = none.OkOrStr("missing");
  SayBool(failed.IsErr);
  ```

- **Impact:** natural Option-to-Result chaining does not type-check. The current workaround is the
  explicit-receiver form `Viper.Result.get_IsErr(failed)`.
- **Likely repair point:** use `obj<Viper.Result>` return types in
  `src/il/runtime/defs/classes/game_ui.def`'s Option block, or correct Zia's propagation of an
  unqualified object return if retaining `obj` is intentional.

### VDOC-002 — BASIC lowers an object-to-integer assignment into invalid IL

- **Classification:** confirmed compiler bug
- **Area:** BASIC type checking/lowering
- **Evidence:** `Viper.Input.Keyboard.GetPressed()` returns a `Seq` of boxed integers. Assigning
  `pressed.Get(i)` directly to an `INTEGER` reaches IL verification and fails with
  `store ... operand type mismatch: operand 1 must be i64` instead of producing a BASIC type
  diagnostic or an explicit unbox:

  ```basic
  DIM pressed AS OBJECT = Viper.Input.Keyboard.GetPressed()
  DIM key AS INTEGER
  key = pressed.Get(0)
  ```

- **Impact:** a source-level type error escapes semantic analysis and creates invalid compiler
  output. User code must currently unbox with `Viper.Core.Box.ToI64(pressed.Get(0))`.
- **Reproduction:** `python3 scripts/audit_bible_examples.py docs/viperlib/input.md` found this in
  the pre-correction “Displaying Pressed Keys” example.

### VDOC-003 — `Viper.Functional.Lazy` has no public deferred factory

- **Classification:** API inconsistency
- **Area:** runtime functional API
- **Evidence:** the registry exposes only `Lazy.Of`, `OfStr`, and `OfI64`; each implementation sets
  `evaluated = 1`. The internal `rt_lazy_new(supplier)` is not registered. `Lazy.Map` also forces
  its source and invokes the callback immediately.
- **Impact:** every Lazy constructible from Zia/BASIC is already evaluated, despite the public type
  and method names promising deferred evaluation. `IsEvaluated` is immediately true, and `Map` is
  eager.
- **Source:** `src/runtime/oop/rt_lazy.c` and the Lazy block in
  `src/il/runtime/defs/classes/extended_tooling.def`.

### VDOC-004 — Option/Result combinators silently change semantics for typed payloads

- **Classification:** API inconsistency / needs triage
- **Area:** runtime functional API
- **Evidence:** callback signatures accept only generic object pointers. For string, integer,
  boolean-storage, and double payloads:

  - `Option.Map` and `Option.AndThen` return the original typed Some unchanged;
  - `Option.Filter` returns None without calling the predicate;
  - `Result.Map`, `MapErr`, `AndThen`, and `OrElse` return the original Result unchanged.

- **Impact:** the same public combinator has materially different, mostly silent behavior based on
  which `Some*`/`Ok*`/`Err*` factory created the value.
- **Source:** `src/runtime/oop/rt_option.c` and `src/runtime/oop/rt_result.c`.

### VDOC-005 — Object accessors can reinterpret typed Option/Result payloads as pointers

- **Classification:** confirmed runtime type-safety bug / needs triage
- **Area:** runtime functional API
- **Evidence:** `Option.Value`, `Option.Expect`, `Result.OkValue`, `Result.ErrValue`, `Result.Expect`,
  and `Result.ExpectErr` return the `value.ptr` union member without checking `value_type`. The
  registry permits these methods on Options/Results created by typed primitive factories.
- **Impact:** calling an object accessor on `SomeI64`, `SomeF64`, `OkI64`, or `OkF64` can expose the
  primitive bit pattern as an object pointer. Typed unwrap methods are safe and should be used as a
  workaround.
- **Source:** `src/runtime/oop/rt_option.c` and `src/runtime/oop/rt_result.c`.

### VDOC-006 — Absolute mouse deltas lag one poll behind

- **Classification:** confirmed bug
- **Area:** 2D/3D input frame sequencing
- **Evidence:** `rt_mouse_begin_frame()` calculates `current - previous` before `Canvas.Poll()`
  pumps the current frame's mouse-move events. Event processing updates `g_mouse_x/y` afterward,
  without recalculating the ordinary absolute delta. Native/fallback Canvas3D relative paths
  overwrite the delta later, but normal absolute motion does not.
- **Impact:** after a 2D Canvas poll, `Mouse.X/Y` reflect the newest position while
  `Mouse.DeltaX/Y` describe the preceding position change.
- **Source:** `rt_mouse_begin_frame()` in `src/runtime/graphics/input/rt_input.c` and
  `rt_canvas_poll()` in `src/runtime/graphics/2d/rt_canvas.c`.

### VDOC-008 — `Mouse.Capture()` does not lock or confine the pointer

- **Classification:** API inconsistency
- **Area:** mouse cursor control
- **Evidence:** `rt_mouse_capture()` sets a boolean and hides the cursor. It does not invoke an OS
  capture/confine operation or warp the pointer. The implementation comment explicitly describes
  it as a state flag tied to visibility.
- **Impact:** code using `Capture()` alone for FPS-style motion still receives bounded absolute
  mouse movement. Use Canvas3D relative mode for actual unbounded look input.
- **Source:** `rt_mouse_capture()` in `src/runtime/graphics/input/rt_input.c`.

### VDOC-009 — `Mouse.SetRelativeMode()` is applied only by Canvas3D polling

- **Classification:** API inconsistency / needs triage
- **Area:** shared input API versus canvas backends
- **Evidence:** the public method only records a request and capture state. The only production call
  to `vgfx_set_relative_mouse()` based on that request is in `rt_canvas3d_poll()`; the 2D Canvas
  and GUI poll paths do not apply native relative mode or the center-warp fallback.
- **Impact:** the namespace-global `Viper.Input.Mouse.SetRelativeMode(true)` behaves differently
  depending on which canvas implementation drives polling. `RelativeMode` can be true in a 2D app
  even though `RelativeModeNative` is false and no unbounded fallback is active.
- **Source:** `src/runtime/graphics/3d/render/rt_canvas3d.c` and
  `src/runtime/graphics/input/rt_input.c`.

### VDOC-010 — InputManager debounce delay does not implement held-key repeat

- **Classification:** needs triage
- **Area:** `Viper.Input.Manager`
- **Evidence:** `KeyPressedDebounced` requires `Keyboard.WasPressed(key)`, which is only true on a
  down edge. Waiting while the key remains held cannot retrigger it. Once the key is released, the
  debounce timer is immediately reset to zero, so the next press is not delayed either.
- **Impact:** `DebounceDelay` does not provide the documented “wait before allowing repeat” behavior;
  for ordinary input it is nearly equivalent to `KeyPressed`.
- **Source:** `rt_inputmgr_key_pressed_debounced()` in
  `src/runtime/graphics/input/rt_inputmgr.c`.

### VDOC-011 — Runtime signature token documentation and parsers disagree

- **Classification:** API/tooling inconsistency / needs triage
- **Area:** runtime registry signature parser
- **Evidence:** the signature grammar comment in `src/il/runtime/runtime.def` lists `i8` and `f32`.
  `RuntimeClasses.cpp` accepts those spellings and widens them to the available scalar categories,
  while `RuntimeSignatureParser.cpp` rejects them. Neither the normative IL reference nor
  `src/il/core/Type.hpp` defines an `i8` or `f32` IL type.
- **Impact:** registry consumers disagree about whether these tokens are valid, so the same runtime
  signature can be accepted by class-surface parsing and rejected by function-signature parsing.
- **Likely repair point:** define one runtime-signature grammar, make both parsers enforce it, and
  correct the grammar comment. Active public definitions currently use the supported widened
  types.

### VDOC-012 — Two x86-64 encoding inventories disagree

- **Classification:** needs triage
- **Area:** code-generation specification data
- **Evidence:** `docs/specs/x86_64_encodings.yaml` contains 48 encoding entries while the current
  JSON inventory contains 71. The YAML file appears unused by current generation paths.
- **Impact:** readers and tools selecting the YAML inventory may see an incomplete instruction set.
- **Next check:** establish whether the YAML is intentionally historical, should be generated, or
  should be removed in favor of the active JSON source.

### VDOC-013 — CLI uses a separate form for running IL

- **Classification:** CLI/API inconsistency
- **Area:** `viper` command-line interface
- **Evidence:** `viper run` accepts source-language inputs but not `.il`; IL execution uses the
  legacy top-level `viper -run file.il` form.
- **Impact:** the most discoverable run subcommand cannot execute the project's normative
  intermediate language, and several man-page examples had drifted into the unsupported form.
- **Next check:** finish the man-page review and decide whether the fix belongs in docs, argument
  parsing, or both.

## Corrected Documentation Mismatches

### VDOC-D001 — Runtime registry and generated-artifact architecture

- **Status:** corrected in `docs/viperlib/architecture.md`
- **Mismatch:** the guide described the registry as one X-macro file and omitted generated outputs.
  The current source is a modular manifest under `src/il/runtime/defs`, parsed by `rtgen`, which
  emits five named artifacts. GC trial-count initialization, automatic cycle scans, weak-reference
  ownership, and collection ownership claims were also stale.

### VDOC-D002 — Nonexistent `Viper.Debug.Protocol`

- **Status:** corrected in `docs/viperlib/diagnostics.md`
- **Mismatch:** the handwritten reference named a runtime class that is absent from both the live
  registry and generated runtime reference.

### VDOC-D003 — Lazy, Option, and Result behavior and surface

- **Status:** corrected in `docs/viperlib/functional.md`
- **Mismatch:** the guide called public Lazy values deferred, omitted Option's boolean API and most
  Option/Result combinators, described non-trapping `Value`/`OkValue` accessors as unwrap aliases,
  and omitted payload-type restrictions and exact rendering/equality behavior.

### VDOC-D004 — Input reference drift

- **Status:** correction in progress in `docs/viperlib/input.md`
- **Mismatch:** the guide omitted smooth wheel accessors and the `fps3d` action preset, treated
  boxed key-code sequence elements as integers, overstated mouse capture/relative-mode behavior,
  misstated KeyChord limits/timing, generalized `-1` gamepad handling to methods that reject it,
  and described InputManager debounce as held-key repeat.

## Validation Notes

- `docs/viperlib/functional.md`: 5 examples pass; 2 contextual pseudocode snippets intentionally
  skip in `scripts/audit_bible_examples.py`.
- Runtime registry validation at this review point reports 7,363 functions and 513 classes.
- Generated runtime documentation is current according to `rtgen --docs --check`.
- No build or CTest invocation is used for this review.
