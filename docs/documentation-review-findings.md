---
status: active
audience: maintainers
last-verified: 2026-07-15
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
- **Review (2026-07-15):** Verified; recommendation implemented. `Option.OkOr`/`OkOrStr` now
  declare `obj<Viper.Result>` returns in both the `RT_FUNC` and `RT_METHOD` rows of
  `game_ui.def` (matching the established `Term.AskResult` precedent), so `failed.IsErr` and
  chained Result members type-check naturally in Zia. Runtime reference docs regenerated via
  rtgen; `docs/viperlib/functional.md` updated (signature table + example now uses natural
  member access). Regression coverage added to
  `src/tests/fixtures/zia_runtime/45_runtime_api_conformance.zia` (`testOptionToResultTyping`).
  **Resolved.**

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
- **Review (2026-07-15):** Verified (still reproduced) and fixed. Root cause: the BASIC AST
  `Type` enum cannot express object returns, so runtime helpers returning `obj` were seeded into
  the procedure registry as INTEGER-returning; `inferCallType` then typed calls like
  `Viper.Input.Keyboard.GetPressed()` as INTEGER and every object-to-INTEGER flow escaped the
  existing B2001 assignment checks (a direct `KEY = Viper.Input.Keyboard.GetPressed()` even
  compiled silently, retyping the declared INTEGER to an object slot). Fix: added
  `ProcSignature::objectReturn`, seeded from the parsed runtime signature
  (`ProcRegistry::seedRuntimeBuiltins`), and `inferCallType` now returns OBJECT for such calls,
  so the existing assignment type checks emit `B2001: operand type mismatch` at the offending
  line instead of invalid IL. Additionally qualified `Keyboard.GetPressed`/`GetReleased` as
  `obj<Viper.Collections.Seq>` so member calls on the result resolve against Seq rather than the
  declaring Keyboard class. Explicit unboxing via `Viper.Core.Box.ToI64` remains the documented
  way to extract integers. Regression test: `src/tests/golden/basic_errors/obj_to_int_assign.*`
  (registered as `basic_error_obj_to_int_assign`); full `-L basic` suite (107 tests), goldens,
  and zia suites pass. **Resolved.**

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
- **Review (2026-07-15):** Verified and fixed. Registered `Viper.Functional.Lazy.New(supplier)`
  as the public deferred factory (new `rt_lazy_new_wrapper` trampoline; RT_FUNC/RT_METHOD rows
  plus RuntimeSurfacePolicy pins). Because a supplier passed from managed code is an IL/bytecode
  function value rather than a C-callable pointer, the fix also adds the execution bridges both
  interpreters were missing: handle-kind suppliers in `rt_lazy.c`
  (`rt_lazy_new_handle`/`rt_lazy_pending_handle`/`rt_lazy_complete_obj`), a standard-VM bridge
  (`src/vm/FunctionalRuntime.cpp`) covering New/Get/GetStr/GetI64/Force/Map/AndThen, and
  bytecode-VM unified handlers (including a new `BytecodeVM::invokeValueReentrant` for
  value-returning callbacks). Verified deferred-once semantics with identical output on the
  bytecode VM, the standard VM (`--debug-vm`), and a native `viper build` binary. As a side
  effect, `Lazy.Map`/`AndThen` callbacks now execute on both VMs instead of crashing (they
  previously dereferenced the raw function value). `Map`/`AndThen` remain eager-on-force by
  design; docs updated (`docs/viperlib/functional.md`, including the stale `FlatMap` →
  `AndThen` name). Regression: `testLazyDeferredFactory` in
  `src/tests/fixtures/zia_runtime/45_runtime_api_conformance.zia`. **Resolved.**

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
- **Review (2026-07-15):** Verified, with a worse finding underneath: generic-payload combinator
  callbacks crashed outright on both interpreters (`Option.Some(x).Map(fn)` segfaulted under
  `viper run`, because the C runtime called the managed function value as a raw C pointer). Fixed
  by refactoring all eight combinators (`Option.Map/AndThen/OrElse/Filter`,
  `Result.Map/MapErr/AndThen/OrElse`) into `*_invoke` cores taking a pluggable callback-invoker
  strategy (`rt_cb_invoke1/0/pred` in `rt_option.h`), with the native wrappers using a direct-call
  strategy and new VM bridges (`src/vm/FunctionalRuntime.cpp`, bytecode unified handlers in
  `BytecodeVM.cpp`) re-entering the interpreter — semantics live in one place, so the engines
  cannot drift. Verified identical output on bytecode VM, standard VM, and native. The
  typed-payload pass-through semantics this entry describes are retained deliberately: they are
  now explicitly documented in `docs/viperlib/functional.md` ("unwrap, transform, and re-wrap
  explicitly" for typed variants), and changing them to traps or auto-boxing would break the
  documented contract — treat any semantic redesign as a separate language decision. Regression:
  `testOptionResultCombinatorCallbacks` in
  `src/tests/fixtures/zia_runtime/45_runtime_api_conformance.zia`. **Resolved** (crash fixed;
  typed-payload behavior retained as documented).

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
- **Review (2026-07-15):** Verified and fixed. All generic-object accessors now check
  `value_type` before returning the pointer union member. Non-trapping accessors
  (`rt_option_value`, `rt_result_ok_value`, `rt_result_err_value` — no longer publicly
  registered, guarded as defense-in-depth) return NULL for typed payloads; trapping accessors
  (`Option.Unwrap`/`Expect`, `Result.Unwrap`/`UnwrapErr`/`Expect`/`ExpectErr`) trap with a
  message directing to the matching typed accessor (e.g. "Unwrap called on non-object payload;
  use UnwrapStr/UnwrapI64/UnwrapI1/UnwrapF64"). Verified the trap on both VM engines; typed
  accessors unaffected. Docs updated in `docs/viperlib/functional.md` (Unwrap/Expect rows and
  accessor notes). Regression: typed-payload NULL assertions added to
  `src/tests/runtime/RTOptionTests.cpp` and `RTResultTests.cpp`. **Resolved.**

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
- **Review (2026-07-15):** Verified and fixed. Added `rt_mouse_finalize_frame()` which
  recomputes the absolute delta after the poll pumps this frame's events, and wired it into the
  three absolute-motion poll paths: `rt_canvas_poll` (after the live-cursor position sync),
  `rt_canvas3d_poll`'s non-captured branch (captured branches keep their forced relative
  deltas, which run afterwards and take precedence), and the GUI app poll. The deterministic
  synthetic-frame path is untouched (it forces deltas explicitly). `Mouse.DeltaX/Y` now
  describe the same frame as `Mouse.X/Y`. Regression:
  `test_finalize_frame_same_frame_delta` in `src/tests/runtime/RTMouseTests.cpp` (asserts
  same-frame delta visibility and zero delta on a motionless poll). **Resolved.**

### VDOC-008 — `Mouse.Capture()` does not lock or confine the pointer

- **Classification:** API inconsistency
- **Area:** mouse cursor control
- **Evidence:** `rt_mouse_capture()` sets a boolean and hides the cursor. It does not invoke an OS
  capture/confine operation or warp the pointer. The implementation comment explicitly describes
  it as a state flag tied to visibility.
- **Impact:** code using `Capture()` alone for FPS-style motion still receives bounded absolute
  mouse movement. Use Canvas3D relative mode for actual unbounded look input.
- **Source:** `rt_mouse_capture()` in `src/runtime/graphics/input/rt_input.c`.
- **Review (2026-07-15):** Verified; documentation-level resolution retained. `Capture()` is by
  design a state flag plus cursor hide, and `docs/viperlib/input.md` states explicitly that it
  "is not an operating-system pointer lock or confinement API" and directs FPS-style input to
  relative mode. With VDOC-009's fix, relative mode now works from both the 2D and 3D canvas
  polls, so the unbounded-input path `Capture()` users actually need is available everywhere.
  Turning `Capture()` itself into an OS confine/lock is a cross-platform feature decision (new
  vgfx surface on three platforms), not a consistency defect — left to a deliberate feature
  choice. **Resolved** (documented contract accurate; no code change).

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
- **Review (2026-07-15):** Verified and fixed. `rt_canvas_poll()` (2D Canvas) now reconciles the
  relative-mouse request exactly like the Canvas3D poll: it applies
  `vgfx_set_relative_mouse()` on its window, reports `RelativeModeNative`, feeds sub-pixel
  native raw deltas via `rt_mouse_force_delta_f`, skips absolute move events while captured,
  and provides the warp-to-center integer fallback when native raw input is unavailable.
  Teardown restores normal cursor behavior before window destruction (matching the 3D path's
  macOS cursor-dissociation caveat). The GUI poll intentionally remains record-only (a GUI app
  has no mouse-look use case); documented as such. Docs updated in `docs/viperlib/input.md`.
  Isolated contract test stubs extended (`RTCanvasStateContractTests.cpp`); canvas contract and
  mouse suites pass. Behavior on a live window follows the identical code path proven by the 3D
  poll. **Resolved.**

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
- **Review (2026-07-15):** Verified; resolved at the documentation level. The implementation is a
  per-key *edge gate*: it accepts a `WasPressed` down edge only while the key's timer is zero,
  which filters OS auto-repeat down edges (repeat KeyDown without an intervening release) while
  the release-reset keeps genuine re-presses immediate. `docs/viperlib/input.md` now describes
  exactly this ("Accept a down edge only while that key's timer is zero"; "it does not generate
  held-key repeat"; "Edge-gate timer in frames"), so no handwritten claim of held-key repeat
  remains. Implementing actual held-key repeat would be a new API surface (e.g. a
  `KeyRepeating(key)` query), which is a feature decision rather than a consistency defect —
  deferred to a deliberate feature choice. **Resolved** (behavior coherent and documented; no
  code change).

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
- **Review (2026-07-15):** Verified and fixed. `i8` and `f32` are not IL types, are rejected by
  the strict `RuntimeSignatureParser`, and are used by no signature anywhere in the def sources
  — so the unified grammar is the strict parser's token set. Removed `i8`/`f32` acceptance from
  the three lenient consumers (`RuntimeClasses.cpp` `mapILToken`, Zia's `Sema_Runtime.cpp`, and
  `frontends/common/RuntimeRegistry.cpp`), corrected the grammar comment in
  `src/il/runtime/runtime.def` (now lists the exact accepted token set including qualified
  `obj<...>`/`seq<...>`/`list<...>` forms and states there are no i8/f32 IL types), and updated
  the `mapILToken` doc table. All parsers now accept the identical token set; a future stray
  `i8`/`f32` signature fails uniformly at rtgen/registry validation instead of being accepted by
  some layers and rejected by others. rtgen validates 6987 functions/504 classes clean.
  **Resolved.**

### VDOC-012 — Two x86-64 encoding inventories disagree

- **Classification:** needs triage
- **Area:** code-generation specification data
- **Evidence:** `docs/specs/x86_64_encodings.yaml` contains 48 encoding entries while the current
  JSON inventory contains 71. The YAML file appears unused by current generation paths.
- **Impact:** readers and tools selecting the YAML inventory may see an incomplete instruction set.
- **Next check:** establish whether the YAML is intentionally historical, should be generated, or
  should be removed in favor of the active JSON source.
- **Review (2026-07-15):** Verified and fixed. Nothing in the tree references
  `docs/specs/x86_64_encodings.yaml`: the codegen build (`src/codegen/x86_64/CMakeLists.txt`),
  the completeness script (`scripts/check_codegen_opcode_completeness.sh`), and
  `docs/generated-files.md` all consume `x86_64_encodings.json` exclusively, and the YAML had
  drifted (48 vs 71 entries, older timestamp). Deleted the stale YAML so the JSON is the single
  encoding inventory. **Resolved.**

### VDOC-013 — CLI uses a separate form for running IL

- **Classification:** CLI/API inconsistency
- **Area:** `viper` command-line interface
- **Evidence:** `viper run` accepts source-language inputs but not `.il`; IL execution uses the
  legacy top-level `viper -run file.il` form.
- **Impact:** the most discoverable run subcommand cannot execute the project's normative
  intermediate language, and several man-page examples had drifted into the unsupported form.
- **Next check:** finish the man-page review and decide whether the fix belongs in docs, argument
  parsing, or both.
- **Review (2026-07-15):** Verified and fixed in argument parsing plus docs. `viper run` now
  detects a `.il` argument (any pre-`--` argument with the extension, case-insensitive) and
  delegates to the IL runner with all arguments forwarded, so `viper run file.il` executes IL
  on the VM exactly like the legacy `viper -run file.il` (which remains available). Help text
  (`viper --help` targets note, `viper help run`) and the man page (`docs/man/man1/viper.1`)
  updated to state `.il` targets are accepted. Regression:
  `il_quickstart_run_subcommand` golden test runs `quickstart.il` through the `run`
  subcommand via a new `RUN_CMD` knob in `check_run_output.cmake`. **Resolved.**

### VDOC-014 — BASIC lowers integer arguments to object parameters without boxing

- **Classification:** confirmed compiler bug
- **Area:** BASIC type checking/lowering
- **Evidence:** `Viper.Collections.Seq.Push` expects an object pointer. Passing an integer key code
  reaches IL verification as an `i64` argument and fails with `parameter 1 expects ptr but got
  i64`, instead of producing a BASIC type diagnostic or inserting a box:

  ```basic
  DIM keys AS OBJECT = NEW Viper.Collections.Seq()
  keys.Push(Viper.Input.Key.LeftControl)
  ```

- **Impact:** source-level primitive-to-object mismatches can create invalid compiler output. The
  current workaround is `keys.Push(Viper.Core.Box.I64(Viper.Input.Key.LeftControl))`.
- **Reproduction:** found while making the `KeyChord` BASIC example independently auditable with
  the existing installed compiler. This is the inverse direction of VDOC-002's failed unboxing.
- **Review (2026-07-15):** Verified and fixed across all three call shapes. (1) Method
  expressions: a new `checkObjectParamArgs` in the BASIC method-call analyzer rejects INTEGER,
  FLOAT, and BOOLEAN arguments bound to runtime object parameters with
  `B2001: argument N to 'M' expects an OBJECT; box primitives with Viper.Core.Box`. (2) Method
  *statements* previously bypassed argument analysis entirely ("best-effort" visiting);
  `analyzeCallStmt` now routes MethodCallExpr statements through full expression analysis while
  preserving the BUG-120 leniency for late-bound locals. (3) Fully-qualified function calls:
  `ProcSignature` gained a per-parameter `objectParams` mask (object params are seeded as I64
  because the AST enum cannot express objects) and `checkCallArgs` enforces it. Strings remain
  accepted for object parameters (supported managed-reference coercion, e.g. `Seq.Push("x")`),
  and a literal `0` is permitted as the null-object idiom (e.g. `Thread.Start(cb, 0)`).
  Regression: `src/tests/golden/basic_errors/obj_param_primitive_arg.*`; full basic (107),
  runtime-calls, and golden suites pass. **Resolved.**

### VDOC-015 — Action source comments disagree with the implemented surface

- **Classification:** documentation/API inconsistency
- **Area:** C runtime API documentation
- **Evidence:** `rt_action.h` documents four accepted `LoadPreset` names but the implementation
  also accepts `fps3d`. The `rt_action_strength()` detail comment says analog triggers can produce
  an axis value, while the function unconditionally returns `1.0` when held and `0.0` otherwise,
  and button actions have no trigger-axis binding.
- **Impact:** maintainers reading the runtime header/source can miss a supported preset or infer
  analog `Strength` behavior that is not implemented. The public documentation now describes the
  actual five presets and digital-only result.
- **Source:** `src/runtime/graphics/2d/rt_action.h`, `rt_action.c`, and
  `rt_action_presets.c`.
- **Review (2026-07-15):** Verified and fixed. `rt_action.h`'s `rt_action_load_preset` comment
  now lists all five presets (including `fps3d`), and both the header and implementation
  comments for `rt_action_strength` state the digital-only contract (1.0 held / 0.0 otherwise;
  button actions have no trigger-axis binding — use axis actions for analog input), matching
  the implementation and the already-corrected public docs. **Resolved.**

### VDOC-016 — UUID source comments describe obsolete guarantees and entropy providers

- **Classification:** documentation/API inconsistency
- **Area:** UUID runtime implementation comments
- **Evidence:** `rt_guid.c` says Unix uses `/dev/urandom` and Windows uses `CryptGenRandom`, but
  `Uuid.Generate()` delegates to the shared crypto RNG (approved-mode DRBG, Windows `BCryptGenRandom`,
  Linux `getrandom()` with fallback, and macOS `arc4random_buf()`). The same comments say two
  generated UUIDs are “guaranteed” different, although UUIDv4 uniqueness is probabilistic. They
  also present RFC 4122 as current even though RFC 9562 superseded it in 2024.
- **Impact:** maintainers can infer the wrong security backend and an impossible collision-free
  guarantee from comments adjacent to the implementation.
- **Source:** `src/runtime/text/rt_guid.c`, `src/runtime/network/rt_crypto.c`, and the platform
  entropy adapters.
- **Review (2026-07-15):** Verified and fixed. `rt_guid.c`/`rt_guid.h` comments now describe the
  actual entropy path (shared crypto RNG via `rt_crypto_random_bytes`: approved-mode DRBG,
  `BCryptGenRandom` on Windows, `getrandom()` with fallback on Linux, `arc4random_buf()` on
  macOS), state that UUIDv4 uniqueness is probabilistic (collision odds noted, no "guaranteed
  different" claim), and cite RFC 9562 (noting it obsoletes RFC 4122) throughout. **Resolved.**

### VDOC-017 — `Fmt.Num` output does not round-trip through `Parse`

- **Classification:** API inconsistency
- **Area:** numeric formatting and parsing
- **Evidence:** `Fmt.Num` intentionally uses the historical 15-significant-digit display format.
  With the existing evaluator, `Fmt.Num(1.0000000000000002)` returns `"1"`, which reparses as a
  different value; `Convert.ToStringDouble` returns the exact `"1.0000000000000002"`. Non-finite
  spellings also differ: `Fmt.Num` emits `Infinity`, while `Parse.DoubleOr` accepts `Inf` and
  rejects `Infinity`.
- **Impact:** using the most discoverable formatter to serialize a double can silently lose its
  exact value, and its infinity text is incompatible with the paired safe parser.
- **Workaround:** use `Viper.Core.Convert.ToStringDouble()` for round-trip serialization. The
  public utilities guide now labels `Fmt.Num` as display-only.
- **Review (2026-07-15):** Verified; partially fixed, remainder documented by design.
  `Fmt.Num`'s 15-significant-digit display format is intentional and documented as
  display-only, with `Convert.ToStringDouble` as the round-trip serializer — changing the
  display format would alter virtually every golden output, so that half stands as documented.
  The incompatible-infinity half is fixed: `scan_nonfinite_float` in `rt_parse.c` now accepts
  the long `Infinity`/`-Infinity` spelling emitted by `Fmt.Num` (case-insensitive, plus the
  existing `Inf`/`NaN` forms), so `Parse.DoubleOr(Fmt.Num(x), d)` round-trips non-finite
  values. Regression: non-finite spellings added to `test_try_num_valid` in
  `src/tests/runtime/RTParseTests.cpp`. **Resolved** (parser widened; display format retained
  as documented).

### VDOC-018 — Parse/Fmt/String source comments name behavior that is not present

- **Classification:** documentation/API inconsistency
- **Status:** corrected in implementation-adjacent source comments
- **Area:** C runtime API documentation
- **Evidence:** `rt_parse.c`'s file-purpose header claimed TryParseLong, TryParseFloat, and
  TryParseDate helpers, and said floating parsing used the ambient locale, but the implementation
  provides integer, double, Boolean, default, validation, radix, and Option forms and isolates the
  C numeric locale. `rt_fmt_bool_yn()`'s brief said it formatted `"Yes"`/`"No"`, while both its
  detailed contract and implementation return lowercase `"yes"`/`"no"`. String source comments
  additionally claimed Latin-1 case mapping, zero-based `utf8_char_to_byte_offset`, a nonzero
  invalid-lead return, scaled integer Jaro scores, digit-transition splitting, and one-byte LIKE
  `_` behavior, none of which matched the implementation.
- **Impact:** implementation-adjacent documentation suggests nonexistent parse surface and the
  wrong locale, indexing, return, wildcard, casing, and similarity contracts.
- **Source:** `src/runtime/text/rt_parse.c`, `src/runtime/core/rt_fmt.c`,
  `rt_string_ops.c`, `rt_string_internal.h`, and `rt_string_specialized.c`.
- **Review (2026-07-15):** Verified the recorded corrections are present (rt_parse.c header now
  describes the real surface and C-locale isolation; no stale Latin-1/Jaro/LIKE claims remain in
  the string sources). One residue remained: `rt_fmt_bool_yn`'s `@brief` still said
  `"Yes"/"No"` while the detail/return/implementation are lowercase — corrected the brief to
  match. **Resolved.**

### VDOC-019 — BASIC cannot call the `Json` class aliases

- **Classification:** API/compiler inconsistency
- **Area:** BASIC runtime-call resolution and the JSON class facade
- **Evidence:** the class registry exposes `Json.GetStr`, `GetInt`, `GetBool`, `SetStr`, `SetInt`,
  `SetBool`, and `Has` by aliasing Map implementations, and exposes `Stringify` by aliasing
  `Json.Format`. There are no correspondingly named function-registry symbols. BASIC resolves
  calls such as `Viper.Text.Json.GetStr(data, "name")` and
  `Viper.Text.Json.Stringify(value)` as unknown procedures, while Zia resolves the class aliases.
- **Impact:** the same advertised class surface is language-dependent. BASIC must call the
  concrete `Viper.Collections.Map.Get*`, `Set*`, and `Has` functions and use `Json.Format` instead
  of `Json.Stringify`.
- **Source:** the `Viper.Text.Json` class block in
  `src/il/runtime/defs/classes/io_text.def`, the function registry dump, and the documentation
  audit of `docs/viperlib/text/formats.md`.
- **Review (2026-07-15):** Verified (class is now `Viper.Data.Json`; the alias rows remain, the
  `Stringify` alias no longer exists) and fixed at the resolution layer rather than by adding
  function rows (the API-hardening direction is fewer duplicate rows, not more).
  `SemanticAnalyzer::resolveCallee` now falls back on a procedure-registry miss for a qualified
  callee: it resolves the prefix as a runtime class, looks the method up in the class surface,
  and rewrites the call to the alias target (e.g. `Viper.Data.Json.GetStr` →
  `Viper.Collections.Map.GetStr`), so argument checking and lowering see the real function.
  This makes BASIC's callable class surface equal to Zia's for every aliased method, not just
  Json. `docs/viperlib/text/formats.md` updated (BASIC example now uses the Json aliases; the
  limitation note replaced). Regression: `JsonClassAliasesResolve` in
  `src/tests/basic/test_basic_runtime_calls.cpp`; basic (107) and golden (166) suites pass.
  **Resolved.**

### VDOC-020 — Untyped concrete-object results break member typing or infer the declaring class

- **Classification:** API/compiler inconsistency
- **Area:** Zia/BASIC runtime-member typing and registry signatures
- **Evidence:** `Csv.Parse`, `Csv.ParseWith`, `Ini.Sections`, `Json.ParseArray`, `JsonPath.Query`,
  `Html.Parse`, `Html.ExtractLinks`, `Html.ExtractText`, `Diff.Lines`, `Heap.ToSeq`,
  `Iterator.ToSeq`, `SortedSet.Range`, `SortedSet.Take`, `SortedSet.Skip`, `MultiMap.Get`,
  `CountMap.MostCommon`, localization methods
  `Locale.Fallbacks`, `LocaleManager.Available`, `MessageBundle.Keys`, and
  `PluralRules.Categories`, thread APIs `ConcurrentMap.Keys`/`Values` and
  `Scheduler.Poll`, network APIs `Dns.ResolveAll`/`LocalAddrs`, `Http.GetBytes`, `Http.Head`,
  `HttpReq.Send`, `HttpRes.Headers`/`Body`, `HttpRouter.Add`/`Get`/`Post`/`Put`/`Delete`/`Match`,
  `ConnectionPool.Acquire`, `Multipart.Build`/`Parse`/`GetFile`,
  `HttpClient.Get`/`Post`/`Put`/`Delete`/`GetCookies`, every Future-producing `AsyncSocket` method,
  `MessageBus.Topics`, crypto APIs `Aes.EncryptStr`/`EncryptAuth` and the byte-producing
  `Cipher` methods (including `GenerateKey`), and XML collection methods such as `ChildrenByTag`
  return concrete Map/Seq/List/Bytes/HttpRes/HttpRouter/RouteMatch/Tcp/Multipart/Future values at
  runtime but are registered as unqualified `obj`. With the existing evaluator,
  direct `.Count` access diagnoses `Type 'Viper.Text.Csv' has no member 'Count'`,
  `Type 'Viper.Text.Ini' has no member 'Count'`, `Type 'Viper.Text.Html' has no member 'Count'`, or
  the corresponding `JsonPath`/`Diff` error. BASIC likewise associated `Csv.Parse(...)` with
  `Viper.Text.Csv` until the result was explicitly declared as `Viper.Collections.Seq`. A chained
  `heap.ToSeq().Count` is worse: because Heap itself has a `Count` property, it compiles and then
  traps `Heap: invalid Heap object` when that getter receives the returned Seq. Iterator has the
  same collision: `it.ToSeq().Count` traps `Iterator.Count: invalid Iterator object`. SortedSet's
  three Seq-producing slice methods have the same collision: direct `.Count` chains compile and
  trap `SortedSet.Len: invalid SortedSet object`. `CountMap.MostCommon(1).Count` likewise traps
  `CountMap.Len: invalid CountMap object`. Scheduler follows the declaring-class failure path:
  `Viper.Threads.Scheduler.New().Poll().get_Count()` is diagnosed as a missing Scheduler method.
  `Dns.ResolveAll("").get_Count()` similarly looks for `get_Count` on `Dns`, and
  `HttpReq.New("GET", "").Send().get_Status()` looks for `get_Status` on `HttpReq`; these probes
  fail during semantic analysis before the intentionally invalid inputs can perform network I/O.
  `Http.GetBytes("").get_Length()` and an explicitly dispatched `HttpRes.Headers(...).get_Count()`
  instead fail because the result is `Any`. The same issue is reproduced without performing I/O:
  `HttpRouter.New().Get("/x").Match(...)` diagnoses a missing `Match` on `Any`/the wrong declaring
  class, `Multipart.New().Build().get_Length()` looks for Length on Multipart,
  `HttpClient.New().GetCookies("example.com").get_Count()` sees `Any`, and
  `AsyncSocket.ConnectForAsync("", 80, 1).get_IsDone()` looks for `get_IsDone` on AsyncSocket
  before the invalid host can execute. `MessageBus.New().Topics().Count` likewise diagnoses
  `Type 'Viper.Core.MessageBus' has no member 'Count'`, even though the implementation returned an
  empty Seq. `Aes.EncryptStr(...).Length`, `Cipher.Encrypt(...).Length`, and
  `Cipher.GenerateKey().Length` similarly look for `Length` on the declaring `Aes` or `Cipher`
  class. Generic `Result.Unwrap()` has the same limitation for non-collection payloads: BASIC
  associates the TLS connection returned by `Tls.ConnectResult(...).Unwrap()` with `Viper.Result`
  unless the local is explicitly declared as `Viper.Crypto.Tls`.
  MultiMap and ConcurrentMap take the other failure path: their untyped collection results are
  exposed as `Any`, so `.Count`/`get_Count()` is rejected during semantic analysis. Assigning each
  result to an explicitly typed Seq works.
- **Impact:** natural chaining fails and the inferred type is actively misleading. Zia's current
  workaround is an explicit receiver call such as
  `Viper.Collections.Seq.get_Count(Viper.Text.Csv.Parse(text))`; BASIC can annotate the local as
  the known concrete class. Network and crypto results need the same explicit concrete
  receiver/local.
- **Likely repair point:** give these methods typed collection returns (as `ParseLine` already has)
  or stop propagating the declaring utility class through an unqualified `obj` result.
- **Review (2026-07-15):** Verified and fixed by typing the registry rows (the `ParseLine`
  approach), covering every API listed here in both the `RT_FUNC` and `RT_METHOD` rows:
  `Csv.Parse`/`ParseWith` (`seq<obj>`), `Ini.Sections` (`seq<str>`), `Json.ParseArray`,
  `JsonPath.Query`, `Xml.Children`/`ChildrenByTag`/`FindAll` (`seq<obj>`; `Xml.AttrNames`
  `seq<str>`), `Html.Parse` (`obj<Map>`), `Html.ExtractLinks`/`ExtractText`, `Diff.Lines`,
  `SortedSet.Range`/`Take`/`Skip`, `CountMap.MostCommon`, `Scheduler.Poll`, `MessageBus.Topics`,
  `Dns.ResolveAll`/`LocalAddrs` (all `seq<str>`), `Heap.ToSeq`, `Iterator.ToSeq`,
  `MultiMap.Get`, `ConcurrentMap.Keys`/`Values` (`seq<obj>`), `Locale.Fallbacks`,
  `LocaleManager.Available`, `MessageBundle.Keys`, `PluralRules.Categories`
  (`obj<Viper.Collections.List>`), `Http.GetBytes`/`HttpRes.Body`/`Multipart.Build`/`GetFile`/
  `Aes.EncryptStr`/`EncryptAuth`/`Cipher.Encrypt*`/`GenerateKey` (`obj<Viper.Collections.Bytes>`),
  `Http.Head`/`HttpRes.Headers`/`HttpClient.GetCookies` (`obj<Map>`), `HttpReq.Send` and
  `HttpClient.Get`/`Post`/`Put`/`Delete` (`obj<Viper.Network.HttpRes>`),
  `HttpRouter.Add`/`Get`/`Post`/`Put`/`Delete` (`obj<HttpRouter>`, they return the router for
  chaining) and `Match` (`obj<RouteMatch>`), `ConnectionPool.Acquire` (`obj<Tcp>`), and the six
  Future-producing `AsyncSocket` methods (`obj<Viper.Threads.Future>`). Related resolution-layer
  fixes: VDOC-002 (declaring-class propagation for INTEGER contexts) and
  `Keyboard.GetPressed`/`GetReleased` typed there. Generic `Result.Unwrap()` payload typing is
  inherent to a generic Result and out of scope. Element types verified against each C
  implementation before qualifying. Docs: rtgen reference regenerated; all seven
  `docs/viperlib` VDOC-020 workaround notes replaced with the typed-return behavior. Regression:
  `testTypedConcreteReturns` in
  `src/tests/fixtures/zia_runtime/45_runtime_api_conformance.zia` (chained `.Count`/`.Length`/
  router chaining). **Resolved.**

### VDOC-021 — JsonPath conflates JSON null with a missing path

- **Classification:** confirmed API bug
- **Area:** `Viper.Text.JsonPath`
- **Evidence:** `rt_jsonpath_get()` uses C NULL for both a missing lookup and the runtime
  representation of JSON `null`. `Has()` tests only the returned pointer, and `GetOr()` tests the
  same value. The installed evaluator reports false for
  `JsonPath.Has(Json.Parse("{\"x\":null}"), "x")`.
- **Impact:** callers cannot use `Has` to distinguish a present null member from absence, and
  `GetOr` replaces a real null value with its default.
- **Source:** `src/runtime/text/rt_jsonpath.c`.
- **Review (2026-07-15):** Verified and fixed. The traversal now tracks existence separately
  from the value: `navigate_segment_ex` consults the container (`rt_map_has`, sequence bounds)
  so a stored JSON null reports found, and `resolve_path_ex` propagates that through the walk
  (remaining path segments after a null report missing — a JSON null has no children). `Has()`
  now returns true for a present null member and false for absence; `GetOr()` substitutes its
  default only when the path is missing, returning the real null otherwise. Docs updated in
  `docs/viperlib/text/formats.md`. Regression:
  `test_has_and_get_or_distinguish_null_from_missing` in
  `src/tests/runtime/RTJsonPathTests.cpp`, plus end-to-end verification on the VM. **Resolved.**

### VDOC-022 — The JsonPath source contract overstates the implemented syntax

- **Classification:** implementation-documentation inconsistency
- **Area:** `Viper.Text.JsonPath`
- **Evidence:** the implementation header promises recursive descent (`..`) and array slices, and
  says invalid expressions trap. The resolver implements dot segments, bracket indices/quoted
  keys, negative indices, `$`, and a single wildcard handled by `Query`; it has no recursive
  descent, slice, filter, or union parser and accepts several malformed bracket forms without a
  syntax diagnostic.
- **Impact:** maintainers or generated docs based on implementation comments can advertise query
  forms that silently do something else or return no match.
- **Source:** the contract comment and resolver in `src/runtime/text/rt_jsonpath.c`.
- **Review (2026-07-15):** Verified and fixed. The file header now describes exactly the
  implemented surface (dot segments, bracket indices with negatives, quoted bracket keys,
  optional `$`, single wildcard in `Query` only), explicitly states recursive descent, slices,
  filters, and unions are NOT implemented, and replaces the false "invalid expressions trap"
  claim with the real contract (malformed forms resolve to no match without diagnostics).
  `rt_jsonpath.h` contained no stale claims. Public docs already describe the correct syntax
  table. **Resolved.**

### VDOC-023 — CSV conformance and delimiter contracts disagree with the runtime

- **Classification:** API/documentation inconsistency
- **Area:** `Viper.Text.Csv`
- **Evidence:** source/class comments call the implementation RFC 4180-compliant, but multi-row
  formatting emits LF rather than RFC 4180's CRLF and does not enforce equal field counts. Parsing
  intentionally accepts LF and CR as extensions. Source API comments also say a custom delimiter's
  first character is used, while `get_delim()` requires exactly one byte and traps otherwise.
  Evaluating `Csv.Format(Csv.Parse("a,b\nc,d"))` produces `a,b\nc,d\n`.
- **Impact:** strict CSV consumers may reject generated files, and callers following the delimiter
  comment can unexpectedly trap.
- **Source:** `src/runtime/text/rt_csv.c`, `rt_csv.h`, and RFC 4180 section 2.
- **Review (2026-07-15):** Verified and fixed at the documentation level (the LF row endings and
  lenient field counts are long-standing intentional behavior — switching Format to CRLF would
  change every existing output). Both the `rt_csv.c` and `rt_csv.h` headers now say "RFC 4180
  quoting rules" and enumerate the deviations explicitly (LF multi-row endings, unequal field
  counts accepted, LF/CR parse extensions), and all four "first character used" delimiter
  comments now state the real contract: exactly one byte, empty selects the comma default,
  longer traps. The public docs note pointing at this finding was replaced with the accurate
  one-byte-delimiter rule. **Resolved** (contracts corrected; behavior retained as documented).

### VDOC-024 — The TOML parser is not the v1.0 parser described by its source contract

- **Classification:** confirmed API/conformance bug
- **Area:** `Viper.Text.Toml`
- **Evidence:** the file header promises TOML v1.0 values and typed numeric/boolean boxes. The
  implementation stores every scalar—including integers, floats, booleans, and datetimes—as a
  String, and accepts arbitrary unquoted values. The evaluator reports true for
  `Toml.IsValid("x = alpha")`, although `alpha` is not a TOML value, and `Json.TypeOf(Toml.Get(...
  "x"))` reports `string` for `x = 42`. Dotted assignment keys are stored literally rather than
  constructing nested Maps, so `GetStr(Parse("a.b = \"ok\""), "a.b")` returns empty.
- **Impact:** `IsValid` is not a TOML conformance check, typed data is lost during parsing, and a
  core TOML table-construction feature cannot be retrieved through the paired getter.
- **Source:** `src/runtime/text/rt_toml.c` and the TOML 1.0/1.1 specifications.
- **Review (2026-07-15):** Verified; fixed in two parts. (1) The functional defect — dotted
  assignment keys stored literally and unreachable through the paired getters — is fixed:
  `a.b = v` now constructs nested tables via `ensure_table_path` (matching section headers),
  quoted keys keep their literal dots, leading/trailing dots and over-deep keys are rejected,
  and duplicate dotted assignments invalidate the document like plain duplicates. (2) The
  contract overstatement is corrected: the `rt_toml.c` header now describes the actual
  TOML-subset parser (string-typed scalars, lenient bare values, structural rather than
  conformance validation) instead of claiming v1.0 typed parsing. Upgrading to a conforming
  typed v1.0 parser would change the runtime type of every parsed scalar (consumers rely on
  `GetStr` today) — that is a breaking feature decision, not a review fix; public docs already
  describe the permissive subset accurately. Regression:
  `test_dotted_assignment_keys_build_nested_tables` in
  `src/tests/runtime/RTTomlTests.cpp`; docs updated in `docs/viperlib/text/formats.md`.
  **Resolved** (dotted keys fixed; subset behavior documented).

### VDOC-025 — Toml.Format drops deeply nested tables

- **Classification:** confirmed data-loss bug
- **Area:** `Viper.Text.Toml`
- **Evidence:** the formatter handles top-level Maps as tables but skips Map-valued entries while
  emitting a section and never recursively emits them. With the installed evaluator,
  `Toml.Format(Toml.Parse("[a.b.c]\nvalue = \"ok\""))` produces only `[a]` and blank lines;
  `b.c.value` is gone. Maps inside arrays are also serialized as empty strings.
- **Impact:** `Format(Parse(text))` can silently discard valid nested configuration data.
- **Source:** `append_toml_value()` and `rt_toml_format()` in
  `src/runtime/text/rt_toml.c`.
- **Review (2026-07-15):** Verified and fixed. `rt_toml_format` now emits recursively via a new
  `toml_format_table`: scalar entries first, then subtables as dotted `[a.b.c]` section headers
  at any depth (path segments escaped/quoted as needed). Maps in value position (e.g. inside
  arrays) emit as inline tables `{k = v}` instead of empty strings. Recursion is bounded by
  `TOML_MAX_DEPTH`, which also gives the TOML formatter the cycle protection VDOC-033 asks for
  (a cyclic container now fails the format instead of recursing forever).
  `Format(Parse("[a.b.c]\nvalue=\"ok\""))` round-trips. Docs note about lossy nesting replaced.
  Regression: `test_format_emits_nested_tables_recursively` in
  `src/tests/runtime/RTTomlTests.cpp`. **Resolved.**

### VDOC-026 — Xml.IsValid is a subset check, not a full well-formedness check

- **Classification:** API/conformance inconsistency
- **Area:** `Viper.Data.Xml`
- **Evidence:** the parser skips DTD contents and recognizes only numeric references plus the five
  predefined XML entities. Consequently, the well-formed document
  `<!DOCTYPE root [<!ENTITY x "ok">]><root>&x;</root>` returns false from `Xml.IsValid`. The
  byte-oriented name/text parser also accepts non-ASCII bytes without complete UTF-8 validation and
  does not implement namespace or DTD processing.
- **Impact:** `IsValid` cannot be used as a general XML 1.0 well-formedness check despite its public
  description and source header. It checks acceptance by Viper's practical subset.
- **Source:** `decode_entity()`, `skip_doctype()`, and `rt_xml_is_valid()` in
  `src/runtime/text/rt_xml.c`.
- **Review (2026-07-15):** Verified; resolved at the documentation level. Implementing DTD
  entity expansion/namespace processing would be a substantial parser feature (and DTD entity
  expansion is a classic attack surface — billion-laughs — that a from-scratch game runtime has
  little reason to take on), so the subset behavior is retained deliberately. The
  `rt_xml_is_valid` comment now states explicitly that it checks acceptance by the practical
  subset, NOT XML 1.0 well-formedness, and lists the gaps (DTD entities skipped, five predefined
  entities + numeric references only, no namespace/UTF-8 validation); the `rt_xml.h` invariant
  says "parses a practical XML subset" instead of "well-formed XML"; the public docs note now
  states the same without an open-issue link. **Resolved** (subset documented as the contract).

### VDOC-027 — Yaml.Format can lose double precision and string bytes

- **Classification:** confirmed data-loss bug
- **Area:** `Viper.Data.Yaml`
- **Evidence:** finite doubles are emitted with C `%g`'s default precision. The evaluator formats
  parsed `1.0000000000000002` as `1`. String values and Map keys are passed through `strlen`, so a
  runtime String containing an embedded NUL is truncated during formatting.
- **Impact:** formatting and reparsing can change scalar values or discard bytes even when the
  input value is otherwise supported.
- **Source:** `format_value()`, `format_string()`, and `needs_quoting()` in
  `src/runtime/text/rt_yaml_format.c`.
- **Review (2026-07-15):** Verified and fixed. Finite doubles now format with `%.17g`
  (round-trip precision; `1.0000000000000002` survives format/reparse). String emission is now
  length-aware end to end: `needs_quoting` and `format_string` take explicit byte lengths, an
  embedded NUL forces quoting, the escape loop iterates the full byte length (NUL emits as
  the `\u0000` escape), and plain scalars/keys append via a new byte-length `buf_append_bytes` — so no
  string bytes are dropped at a NUL. Regression:
  `test_format_preserves_double_precision_and_string_bytes` in
  `src/tests/runtime/RTYamlTests.cpp`. **Resolved.**

### VDOC-028 — Unsupported YAML anchor syntax is silently misparsed

- **Classification:** confirmed parser bug / needs triage
- **Area:** `Viper.Data.Yaml`
- **Evidence:** the implementation states that anchors and aliases are unsupported, but it does not
  consistently reject them. `Yaml.IsValid("base: &b {x: 1}\ncopy: *b")` returns true. Formatting
  that parsed value shows `&b {x` treated as a mapping key and `*b` as an ordinary string rather
  than resolving the alias or reporting unsupported syntax.
- **Impact:** valid YAML using anchors can be accepted with a materially different data model,
  making silent corruption more likely than an explicit compatibility failure.
- **Source:** `src/runtime/text/rt_yaml.c` and evaluator probes against the installed binary.
- **Review (2026-07-15):** Verified and fixed as an explicit compatibility failure (implementing
  anchor/alias resolution stays out of scope for this YAML subset). A new
  `reject_anchor_alias` guard fires at every plain-token start site — block mapping keys, block
  plain scalars, flow values, and flow mapping keys — so `&name`/`*name` syntax now sets a
  descriptive parse error ("YAML anchors and aliases are not supported") and invalidates the
  document instead of silently misparsing it. Quoted and mid-token `&`/`*` characters remain
  ordinary string content. Regression: `test_anchor_alias_syntax_is_rejected` in
  `src/tests/runtime/RTYamlTests.cpp`. **Resolved.**

### VDOC-029 — JsonStream is not incremental despite its source-facing contract

- **Classification:** API/documentation inconsistency
- **Area:** `Viper.Text.JsonStream`
- **Evidence:** the header describes large or incremental JSON data, but the only constructor takes
  and retains one complete runtime String; there is no feed/resume input API. `Skip` avoids tree
  allocation but still scans the retained bytes. An implementation comment also says
  `StringValue()` traps on the wrong token, while both the header and implementation return an
  empty string.
- **Impact:** callers can correctly expect lower allocation overhead than `Json.Parse`, but cannot
  bound input-buffer memory or stream network/file chunks as the wording suggests.
- **Source:** `src/runtime/text/rt_json_stream.h` and `rt_json_stream.c`.
- **Review (2026-07-15):** Verified and fixed at the contract level (a feed/resume chunked-input
  API would be a new feature). The `rt_json_stream.h` purpose now states it is a SAX-style pull
  parser over one complete in-memory string — lower allocation than `Json.Parse` but NOT
  incremental, with no feed/resume API. The `rt_json_stream_string_value` comment now matches
  the header and implementation (empty string for non-string tokens, no trap claim).
  **Resolved.**

### VDOC-030 — Ini.Parse(NULL) and Ini.Parse("") produce different document shapes

- **Classification:** API inconsistency / needs triage
- **Area:** `Viper.Text.Ini`
- **Evidence:** a NULL input returns immediately with an empty top-level Map. Any non-NULL input,
  including an empty String, creates the implicit empty-name section first. Thus `Sections()` has
  count 0 for NULL and count 1 for an empty string.
- **Impact:** two natural representations of “no INI text” have observably different section
  inventories, and subsequent formatting/mutation starts from different shapes.
- **Source:** `rt_ini_parse()` and `test_null_safety()` in
  `src/runtime/text/rt_ini.c` / `src/tests/runtime/RTIniTests.cpp`.
- **Review (2026-07-15):** Verified and fixed. The default `""` section is now created lazily —
  only when a key appears before the first named section — so `Parse(NULL)` and `Parse("")`
  produce the identical empty document, and documents with only named sections no longer carry
  a phantom empty section in `Sections()`. `test_sections_list` updated to the unified contract;
  new regression `test_empty_and_null_inputs_share_shape` in
  `src/tests/runtime/RTIniTests.cpp` (empty/NULL equivalence plus out-of-section keys still
  landing in `""`). Docs updated in `docs/viperlib/text/formats.md`. **Resolved.**

### VDOC-031 — TOML parsing truncates raw input at an embedded NUL

- **Classification:** confirmed parser inconsistency
- **Area:** `Viper.Text.Toml`
- **Evidence:** `rt_toml_parse()` obtains a length-prefixed runtime String but walks it entirely
  with `while (*p)` and other C-string terminator checks. Bytes after a raw embedded NUL are never
  parsed or rejected. In contrast, `Toml.Format()` deliberately preserves runtime byte lengths and
  escapes NUL in keys and strings.
- **Impact:** validation can accept only a prefix of the supplied runtime String, and parse/format
  do not agree on the library's binary-safe String model.
- **Source:** `src/runtime/text/rt_toml.c`.
- **Review (2026-07-15):** Verified and fixed. TOML documents are text, so `rt_toml_parse` now
  rejects input containing an embedded NUL byte outright (`memchr` over the full runtime byte
  length before the C-string walker runs) — `Parse` returns NULL and `IsValid` reports false
  instead of silently accepting only the prefix. This aligns parse-side behavior with the
  formatter's binary-safe model (which escapes NULs on output). Regression:
  `test_parse_rejects_embedded_nul` in `src/tests/runtime/RTTomlTests.cpp`. **Resolved.**

### VDOC-032 — XML name validation stops at an embedded NUL

- **Classification:** confirmed validation inconsistency
- **Area:** programmatic `Viper.Data.Xml` node/attribute creation
- **Evidence:** `is_valid_xml_name()` checks the runtime length only for non-emptiness and then
  delegates to a C-string loop. A name such as the runtime byte sequence `a\0bad` is validated only
  as `a`, even though XML forbids NUL and bytes remain after the terminator in the retained String.
  Text, comment, CDATA, and attribute-value validators correctly scan their full runtime lengths.
- **Impact:** programmatically created element or attribute names can bypass the stated XML
  character/name validation and later be truncated or formatted inconsistently.
- **Source:** `is_valid_xml_name()` / `is_valid_xml_name_cstr()` in
  `src/runtime/text/rt_xml.c`.
- **Review (2026-07-15):** Verified and fixed. `is_valid_xml_name` now scans the full runtime
  byte length (the C-string variant was folded in and removed), so a name like `a NUL bad`
  fails validation — NUL is not an XML name character — matching the text/comment/CDATA
  validators that already scan full lengths. Regression:
  `test_name_validation_scans_full_length` in `src/tests/runtime/RTXmlTests.cpp`. **Resolved.**

### VDOC-033 — TOML and YAML formatters do not guard cyclic containers

- **Classification:** runtime safety bug / needs triage
- **Area:** `Viper.Text.Toml.Format` and `Viper.Data.Yaml.Format*`
- **Evidence:** YAML recursively descends Seq/Map values without the active-object stack used by the
  JSON formatter. TOML's value formatter recursively descends nested Seqs with no cycle check.
  Public collection APIs permit self-referential object graphs.
- **Impact:** formatting a cyclic supported container can recurse until stack exhaustion instead of
  returning an error or trapping with the intentional cycle diagnostic used by JSON.
- **Source:** `format_value()` in `src/runtime/text/rt_yaml_format.c` and
  `append_toml_value()` in `src/runtime/text/rt_toml.c`.
- **Review (2026-07-15):** Verified and fixed with bounded-depth guards in both formatters
  (depth bounds subsume cycle detection: a cycle exceeds the bound and the format fails closed
  with an empty result instead of exhausting the stack). TOML: `append_toml_value` and the new
  recursive `toml_format_table` (VDOC-025) carry a depth parameter bounded by `TOML_MAX_DEPTH`.
  YAML: `format_value` aborts via a `YAML_FORMAT_MAX_DEPTH` (200, matching the parser) flag that
  `rt_yaml_format_indent` checks before returning. Regression:
  `test_format_bounds_cyclic_containers` in `src/tests/runtime/RTYamlTests.cpp` (self-referencing
  map formats to an empty string and the process survives). **Resolved.**

### VDOC-034 — JSON entry points disagree on raw UTF-8 validation

- **Classification:** confirmed validation/API inconsistency
- **Area:** JSON parser, validator, tokenizer, formatter, and Serialize facade
- **Evidence:** `rt_json_parse()` validates raw multibyte sequences and rejects overlong UTF-8,
  surrogates, bad continuation bytes, and code points above U+10FFFF. `rt_json_is_valid()` checks
  control bytes and escapes but advances over every raw byte >= 0x20 without UTF-8 validation.
  JsonStream does the same, and the formatter passes non-ASCII runtime String bytes through
  unchanged. `Serialize.Parse(JSON)` relies on `IsValid` before calling the trapping parser.
- **Impact:** `IsValid(text)` can return true immediately before `Parse(text)` traps; JsonStream and
  Format can accept or emit byte sequences that are not interoperable JSON text, and Serialize's
  advertised non-trapping bad-input path is bypassed for this case.
- **Source:** the string loops in `src/runtime/text/rt_json_parse.c`,
  `rt_json_validate.c`, `rt_json_stream.c`, and `rt_json_format.c`.
- **Review (2026-07-15):** Verified and fixed. The parser's raw-UTF-8 sequence validator moved
  into the shared `rt_json_internal.h` (`json_raw_utf8_sequence_valid`) and is now used by all
  three input paths: `rt_json_parse` (as before), `rt_json_is_valid` (new — IsValid can no
  longer return true immediately before Parse traps, restoring Serialize's non-trapping
  bad-input contract), and the JsonStream tokenizer (invalid sequences produce its normal error
  token). `Format` intentionally keeps emitting stored string bytes unchanged: output-side
  validation would reject legitimately stored bytes and break the binary-safe runtime String
  model; documented as such. Regression: `test_is_valid_matches_parse_on_raw_utf8` in
  `src/tests/runtime/RTJsonTests.cpp` (overlong, bare-continuation, truncated, and valid
  multibyte cases). **Resolved.**

### VDOC-035 — `Toml.Format` can emit a float as integer syntax

- **Classification:** confirmed data-type loss bug
- **Area:** `Viper.Text.Toml.Format` and TOML projection through `Viper.Data.Serialize`
- **Evidence:** boxed doubles are emitted with C `%.17g` and no check that the result contains a
  decimal point or exponent. The installed evaluator formats `Box.F64(1.0)` as `value = 1` through
  the Serialize TOML facade. Under TOML, `1` is an integer token, not a float token.
- **Impact:** formatting and reparsing a supported value can silently change its numeric type;
  negative zero and other whole-valued doubles have the same issue.
- **Source:** `append_toml_value()` in `src/runtime/text/rt_toml.c` and the TOML value grammar.
- **Review (2026-07-15):** Verified and fixed. The F64 emission now appends `.0` when `%.17g`
  produced a bare integer token (skipped when the output already contains `.`, an exponent, or
  the inf/nan spellings, which are valid TOML float tokens), so whole-valued doubles — including
  negative zero (`-0.0`) — keep their float type through Format/Parse. Regression:
  `test_format_keeps_float_token` in `src/tests/runtime/RTTomlTests.cpp`; docs note updated.
  **Resolved.**

### VDOC-036 — `Toml.GetStr` is missing from the `Toml` class manifest

- **Classification:** runtime-registry / tooling inconsistency
- **Area:** runtime API metadata and generated class documentation
- **Evidence:** `math_text.def` registers the callable function
  `Viper.Text.Toml.GetStr` as `str(obj,str)`, but the `Viper.Text.Toml` class block in
  `classes/io_text.def` ends after `Get`. Consequently, the generated function inventory includes
  `GetStr` while the generated class member table omits it. The already-installed binary still
  lists the member, indicating source and existing build metadata have diverged.
- **Impact:** exact fully qualified calls and class-based completion/introspection can advertise
  different public surfaces; a rebuild from the current manifest can regress alias/member lookup.
- **Source:** `src/il/runtime/defs/api/math_text.def`,
  `src/il/runtime/defs/classes/io_text.def`, and `docs/generated/runtime/text.md`.
- **Review (2026-07-15):** Verified and fixed. Added the missing
  `RT_METHOD("GetStr", "str(obj,str)", TomlGetStr)` row to the `Viper.Data.Toml` class manifest,
  so class metadata and qualified-call lookup expose the same surface (the RT_FUNC row already
  existed). Runtime completeness check passes; generated docs regenerated; verified
  `Toml.GetStr(...)` resolves end-to-end on the VM. **Resolved.**

### VDOC-037 — JSON-derived integer accessors have undefined out-of-range conversion

- **Classification:** confirmed cross-platform runtime bug / needs triage
- **Area:** `Map.GetInt*`, the `Json.GetInt` alias, `JsonPath.GetInt`, and JSON level loading
- **Evidence:** these paths cast an arbitrary boxed `double` directly to `int64_t` without first
  checking finiteness or the integer range. C leaves an out-of-range floating-to-integer
  conversion undefined. On the current installed macOS binary, positive and negative `1e100`
  return `INT64_MAX` and `INT64_MIN`; another compiler or target may differ.
- **Impact:** valid JSON numbers outside the signed 64-bit range can produce platform-dependent
  results in public accessors and game level data instead of a diagnostic or documented fallback.
- **Source:** `rt_map_get_int*()` in `src/runtime/collections/rt_map.c`,
  `rt_jsonpath_get_int()` in `src/runtime/text/rt_jsonpath.c`, and `json_val_to_i64()` in
  `src/runtime/game/rt_leveldata.c`.
- **Review (2026-07-15):** Verified and fixed. All four conversion sites
  (`rt_map_get_int`/`_or`, `rt_jsonpath_get_int`, and level-data `json_val_to_i64`) now route
  through the existing saturating `rt_f64_to_i64` helper (NaN → 0, above-range → INT64_MAX,
  below-range → INT64_MIN, otherwise truncate toward zero), replacing the undefined raw C cast —
  so results are identical on every platform/compiler. Verified `1e100`/`-1e100`/`42` on the VM.
  Regression: `test_get_int_saturates_out_of_range` in `src/tests/runtime/RTJsonTests.cpp`.
  **Resolved.**

### VDOC-038 — JsonPath ignores path bytes after an embedded NUL

- **Classification:** confirmed lookup inconsistency
- **Area:** `Viper.Text.JsonPath`
- **Evidence:** resolution passes `rt_string_cstr(path)` into C-string loops and `strchr` instead of
  using the runtime String length. The evaluator returns `hit` for a path containing the bytes
  `a\0suffix` against `{\"a\":\"hit\"}`, proving the suffix is ignored.
- **Impact:** two distinct runtime Strings can address the same path, and an untrusted path can
  select a member other than the full byte sequence denotes.
- **Source:** `resolve_path()` and `rt_jsonpath_query()` in
  `src/runtime/text/rt_jsonpath.c`.
- **Review (2026-07-15):** Verified and fixed. A new `jsonpath_path_cstr` helper guards every
  public entry (`Get`, `GetOr`, `Has`, `GetStr`, `GetInt`, `Query`): a path whose runtime byte
  length extends past a NUL matches nothing (NULL / false / empty Seq) instead of silently
  addressing the shorter C-string prefix. Regression:
  `test_paths_with_embedded_nul_match_nothing` in
  `src/tests/runtime/RTJsonPathTests.cpp`. **Resolved.**

### VDOC-039 — YAML numeric scalar parsing ignores bytes after an embedded NUL

- **Classification:** confirmed parser bug
- **Area:** `Viper.Data.Yaml`
- **Evidence:** `parse_scalar()` copies a length-delimited scalar into a NUL-terminated buffer and
  accepts `strtoll` when its end pointer sees the first NUL, without checking that the NUL is at
  the supplied span length. The installed evaluator reports `int` and successful validation for
  the runtime byte sequence `1\0garbage`.
- **Impact:** accepted YAML can silently discard a scalar suffix and change a string-like value
  into a number. Formatting then loses the suffix permanently.
- **Source:** `parse_scalar()` in `src/runtime/text/rt_yaml.c`.
- **Review (2026-07-15):** Verified and fixed. `parse_scalar` now detects an embedded NUL in the
  scalar span up front and returns the whole span as an ordinary string (a NUL can never be part
  of a numeric/boolean/null token), so no suffix bytes are discarded and the value keeps its
  string type. Regression: `test_scalar_with_embedded_nul_stays_string` in
  `src/tests/runtime/RTYamlTests.cpp`. **Resolved.**

### VDOC-040 — TOML and YAML validation accepts malformed UTF-8

- **Classification:** confirmed standards-validation bug
- **Area:** `Viper.Text.Toml` and `Viper.Data.Yaml`
- **Evidence:** both parsers copy or pass through non-ASCII bytes without validating UTF-8. Using
  `CHR$(255)` to supply the impossible standalone byte `0xFF`, the installed evaluator reports
  true for both `Toml.IsValid("x = " + CHR$(255))` and
  `Yaml.IsValid("x: " + CHR$(255))`.
- **Impact:** the validators accept inputs that are not TOML or YAML character streams and the
  formatters can propagate malformed text to interoperating tools.
- **Source:** the cursor/scalar loops in `src/runtime/text/rt_toml.c` and
  `src/runtime/text/rt_yaml.c`. XML has the same subset limitation recorded in VDOC-026; JSON's
  entry-point disagreement is recorded in VDOC-034.
- **Review (2026-07-15):** Verified and fixed. Added a shared `rt_utf8_span_valid` helper to the
  string core (`rt_string_internal.h`/`rt_string_advanced.c`; rejects invalid leads, truncated/
  invalid continuations, overlong encodings, surrogates, and values above U+10FFFF) and wired it
  into both parsers' entry points: `rt_toml_parse` and `rt_yaml_parse` now reject non-UTF-8
  input (so `IsValid` reports false for byte sequences like a standalone 0xFF), matching each
  standard's character-stream requirement. Regression: `test_parse_rejects_malformed_utf8` in
  `src/tests/runtime/RTYamlTests.cpp` (plus the TOML NUL-rejection test from VDOC-031 covering
  the TOML entry guard path). **Resolved.**

### VDOC-041 — TOML and YAML numeric emission is locale-sensitive

- **Classification:** cross-platform embedding inconsistency / needs triage
- **Area:** TOML/YAML formatting, generic Serialize scalar projection, JsonPath string projection,
  and `Viper.Text.InvariantNumberFormat`
- **Evidence:** `rt_toml.c`, `rt_yaml_format.c`, `rt_jsonpath.c`, `rt_serialize.c`, and
  `rt_numfmt.c` use ordinary `snprintf` for finite doubles. Unlike the JSON formatter and the
  locale-isolated core double-to-string path, these functions inherit the process C numeric
  locale. The VM sets `LC_NUMERIC` to `C`, but the runtime C API can be embedded in a process that
  selected a decimal-comma locale.
- **Impact:** the same value can emit a comma decimal separator, producing invalid or differently
  parsed TOML/YAML and locale-dependent text projections. Process-global locale changes can also
  make results timing-dependent across threads.
- **Source:** the double-format branches in `src/runtime/text/rt_toml.c`,
  `rt_yaml_format.c`, `rt_jsonpath.c`, `rt_serialize.c`, and `rt_numfmt.c`; compare the
  locale-isolated helpers in `src/runtime/text/rt_json_format.c` and the core conversion path.
- **Review (2026-07-15):** Verified and fixed at every listed site. Finite-double emission in
  the TOML formatter, YAML formatter, JsonPath string projection, and Serialize scalar
  projection now uses the locale-isolated `rt_format_f64_roundtrip` (which also upgrades those
  paths to exact round-trip output); TOML's non-finite branch keeps `%.17g` (inf/nan carry no
  separator and are valid TOML tokens). `InvariantNumberFormat` (`rt_numfmt.c`) routes its
  `%.*f` conversions through `rt_format_snprintf_c_locale`, newly exported from the core
  formatter, so its output cannot inherit an embedding process's decimal-comma locale.
  Regression: `test_decimals_locale_independent` in `src/tests/runtime/RTNumFmtTests.cpp`
  (switches to a comma locale when available and asserts `.`-separated output), alongside the
  existing JSON locale-independence test. **Resolved.**

### VDOC-042 — Structured-format source comments misstate runtime type behavior

- **Classification:** implementation-documentation inconsistency
- **Area:** JSON type classification and YAML formatting source contracts
- **Evidence:** the `Json.TypeOf` API comment says booleans are boxed i64 values 0 or 1, but the
  implementation classifies every `RT_BOX_I64` as `number` and only `RT_BOX_I1` as `boolean`.
  The JSON formatter comment similarly says boxed i64 may format as a boolean even though that
  branch always emits a number. YAML's unknown-type branch is labeled “format as string” but emits
  `null`.
- **Impact:** maintainers and generated prose derived from implementation comments can reproduce
  the stale type model that the public guide has now corrected.
- **Source:** `rt_json_type_of()` / `format_value()` in
  `src/runtime/text/rt_json_format.c` and `format_value()` in
  `src/runtime/text/rt_yaml_format.c`.
- **Review (2026-07-15):** Verified and fixed. The `Json.TypeOf` doc block now states that only
  boxed i1 classifies as "boolean" while boxed i64/f64 classify as "number" (matching the
  implementation), the JSON formatter's type list no longer claims boxed i64 may format as a
  boolean, and the YAML unknown-type branch comment now says it emits YAML `null` (the actual
  behavior) instead of "format as string". Comment-only changes; behavior untouched.
  **Resolved.**

### VDOC-043 — Serialize name and XML-key processing truncates at embedded NUL

- **Classification:** confirmed lookup and data-loss bug
- **Area:** `Viper.Data.Serialize`
- **Evidence:** `FormatFromName()` passes the runtime String's C-string view to `strcasecmp`
  without checking its byte length. The evaluator therefore returns `FORMAT_JSON` for the byte
  sequence `json\0suffix`. Generic-to-XML projection also passes Map keys through `strlen` and
  uses `strcmp` for reserved keys such as `@attrs`, ignoring all bytes after an embedded NUL.
- **Impact:** distinct names alias one format, and XML projection can truncate or misclassify Map
  keys, silently changing the projected data model.
- **Source:** `rt_serialize_format_from_name()`, `sanitized_xml_name()`, and `str_eq_cstr()` in
  `src/runtime/text/rt_serialize.c`.
- **Review (2026-07-15):** Verified and fixed. `rt_serialize_format_from_name` rejects names
  whose runtime byte length extends past an embedded NUL (so `json NUL suffix` no longer aliases
  `"json"`), `str_eq_cstr` compares the full runtime byte length (reserved keys like `@attrs`
  cannot be aliased), and `sanitized_xml_name`/`generic_to_xml_element` now carry explicit byte
  lengths so Map keys are sanitized over their whole length — NUL bytes become `_` like other
  invalid XML name characters instead of truncating the key. Regression: embedded-NUL case added
  to `test_format_from_name` in `src/tests/runtime/RTSerializeTests.cpp`. **Resolved.**

### VDOC-044 — `Template.Keys` is registered as a sequence but returns a bag

- **Classification:** confirmed runtime-registry/type-safety bug
- **Area:** `Viper.Text.Template.Keys`, Zia/BASIC member typing, and generated API metadata
- **Evidence:** both the function and class manifests declare `Template.Keys` as
  `seq<str>(str)`, while `rt_template_keys()` constructs and returns a `Viper.Collections.Bag`.
  With the installed evaluator, `Template.Keys("{{a}}").Count` reaches the Seq getter and traps
  with `Seq: invalid Seq object`; `.Items()` cannot type-check because the compiler sees a Seq.
  Calling the Bag getter explicitly succeeds, and `{{a}}{{a}}` produces a Bag count of one.
- **Impact:** the advertised static type can cause valid natural code to trap on a correctly
  returned object. BASIC can use a `Viper.Collections.Bag` local; Zia must currently invoke Bag
  members through an explicit receiver call.
- **Source:** `TemplateKeys` in `src/il/runtime/defs/api/math_text.def`, the Template class block in
  `src/il/runtime/defs/classes/io_text.def`, and `rt_template_keys()` in
  `src/runtime/text/rt_template.c`.
- **Review (2026-07-15):** Verified and fixed by correcting the registry type to the value the
  implementation actually returns: both rows now declare
  `obj<Viper.Collections.StringSet>(str)` (the Bag's public class), so natural member access
  (`Keys(...).Count`, `.Has(...)`) type-checks and runs instead of trapping through the Seq
  getter. The deduplicating-set semantics are unchanged and remain documented. Docs note in
  `docs/viperlib/text/formatting.md` updated; generated reference regenerated. Regression:
  StringSet typing asserts added to `testTypedConcreteReturns` in
  `src/tests/fixtures/zia_runtime/45_runtime_api_conformance.zia`. **Resolved.**

### VDOC-045 — Formatting utility source contracts describe obsolete behavior

- **Classification:** implementation-documentation inconsistency
- **Area:** Template, TextWrapper, and Markdown runtime source documentation
- **Evidence:** `rt_template_escape()` is described as escaping HTML entities but doubles template
  delimiters. The `rt_textwrap.c` header names an old `Viper.Text.TextWrap` class and promises
  configurable initial/subsequent indentation, tab expansion, and optional hard wrapping that the
  public surface does not expose. The Markdown header names `StripMarkdown` instead of `ToText`
  and says whitespace-only input always returns empty strings/sequences, while `ToHtml(" ")`
  emits a paragraph containing a space.
- **Impact:** maintainers relying on comments adjacent to these implementations can infer the wrong
  class/API, escape domain, options, and empty-input contract.
- **Source:** `src/runtime/text/rt_template.c`, `rt_textwrap.c`, and `rt_markdown.c`.
- **Review (2026-07-15):** Verified and fixed (comment-only). `rt_template_escape` is now
  documented as doubling template delimiters (explicitly NOT an HTML-entity escaper); the
  `rt_textwrap.c` header names the real `Viper.Text.TextWrapper` class and its actual surface
  (no indent/tab-expansion/hard-wrap options; long words split at the width, verified
  behaviorally; non-positive width returns the input unchanged); the Markdown header names
  `ToText` and states that whitespace-only input is kept as content (`ToHtml(" ")` emits a
  paragraph containing a space). **Resolved.**

### VDOC-046 — TextWrapper can split UTF-8 sequences

- **Classification:** confirmed text-corruption bug / needs triage
- **Area:** `Viper.Text.TextWrapper`
- **Evidence:** widths and substring positions are runtime String byte offsets. Wrapping a two-byte
  `é` at width 1 splits its UTF-8 encoding across a newline; the installed evaluator's JSON result
  contains replacement characters on both lines. Truncation and shortening use the same byte
  slicing primitives and can end inside any multi-byte code point.
- **Impact:** ordinary Unicode input can yield malformed UTF-8, and the documented width cannot be
  treated as either a character count or a terminal display width.
- **Source:** the byte-indexing loops and `rt_str_substr()` calls in
  `src/runtime/text/rt_textwrap.c`.
- **Review (2026-07-15):** Verified; the corruption half is fixed while width stays byte-based
  (redefining width as characters or display cells would change every existing output — a
  deliberate API decision left open). New boundary helpers ensure no slice ever lands inside a
  multi-byte sequence: Wrap's forced break backs up to the previous codepoint boundary (or
  emits the whole codepoint when it alone exceeds the width, without a stray trailing
  newline), Truncate/TruncateWith back the kept prefix (and a sliced suffix) up to a boundary,
  and Shorten aligns both its head slice and the start of its tail slice. WrapLines/Fill
  inherit the fix through Wrap. Verified `Wrap("éé", 1)` emits two intact codepoints and ASCII
  behavior is unchanged. Regression: `test_utf8_boundaries_preserved` in
  `src/tests/runtime/RTTextWrapTests.cpp`. **Resolved** (no malformed UTF-8; byte-width
  semantics retained and documented).

### VDOC-047 — Version rejects SemVer components above `INT64_MAX`

- **Classification:** confirmed standards-conformance limitation
- **Area:** `Viper.Text.Version`
- **Evidence:** SemVer 2.0.0 requires nonnegative decimal core components without leading zeroes but
  imposes no numeric size limit. `parse_num()` accumulates each component in `int64_t` and rejects
  overflow. The installed evaluator therefore reports false for
  `Version.IsValid("9223372036854775808.0.0")`, although that string satisfies the SemVer grammar.
- **Impact:** `IsValid` and `Parse` reject valid ecosystem versions and cannot compare or preserve
  them. The object representation and integer properties make this more than a parser-only limit.
- **Source:** `parse_num()` in `src/runtime/text/rt_version.c` and the Semantic Versioning 2.0.0
  specification.
- **Review (2026-07-15):** Verified; resolved as a documented deliberate deviation. The class
  exposes Major/Minor/Patch as i64 properties, so accepting components above INT64_MAX would
  require restructuring the object to arbitrary-precision components — a breaking API redesign
  with no practical ecosystem versions demanding it. The `rt_version.c` header now states the
  signed-64-bit component limit explicitly as a deviation from SemVer's unlimited grammar, and
  the public docs describe the same limit as by-design. **Resolved** (limit documented as the
  contract).

### VDOC-048 — `Html.ExtractText` mishandles nested matching tags

- **Classification:** confirmed extraction bug
- **Area:** `Viper.Text.Html.ExtractText`
- **Evidence:** the scanner pairs an opening tag with the first following matching closing tag and
  does not track nesting depth. For `<div>a<div>b</div>c</div>`, the installed evaluator returns
  one string, `a b`, and loses the outer element's trailing `c` instead of extracting the complete
  outer text (and consistently defining whether the nested match is also returned).
- **Impact:** content extraction silently truncates common nested markup such as nested `div`,
  `section`, or list elements.
- **Source:** the closing-tag search loop in `rt_html_extract_text()` in
  `src/runtime/text/rt_html.c`.
- **Review (2026-07-15):** Verified and fixed. The closing-tag search now tracks nesting depth:
  a nested same-name opening tag (unless self-closing) increments the depth and its closing tag
  decrements it, so the outer element pairs with its own closing tag.
  `ExtractText("<div>a<div>b</div>c</div>", "div")` returns one complete string `a b c`; the
  defined semantics are one result per top-level match containing the element's full stripped
  text (nested matches are folded into their parent rather than double-reported). Sibling
  extraction unchanged. Regression: `test_extract_text_nested_same_tag` in
  `src/tests/runtime/RTHtmlTests.cpp`. **Resolved.**

### VDOC-049 — Markdown block state produces wrong or invalid HTML

- **Classification:** confirmed renderer bug
- **Area:** `Viper.Text.Markdown.ToHtml`
- **Evidence:** unordered-list recognition runs before horizontal-rule recognition, so `* * *`
  renders as a list item containing `<em> </em>` instead of `<hr>`; `- - -` is likewise consumed
  as a list item. Heading, fenced-code, and horizontal-rule branches also run before the open-list
  close step. Rendering `- item\n# Heading` consequently places `<h1>Heading</h1>` directly inside
  the `<ul>` before its closing tag.
- **Impact:** supported-looking input can produce the wrong element or structurally invalid HTML,
  especially at transitions out of a list.
- **Source:** branch ordering and `in_list` handling in `rt_markdown_to_html()` in
  `src/runtime/text/rt_markdown.c`.
- **Review (2026-07-15):** Verified and fixed. The block loop now classifies each line up front
  (`markdown_line_is_hr` runs before list-item recognition, so `* * *` and `- - -` emit `<hr>`)
  and closes an open `<ul>` before ANY non-list block — heading, fenced code, rule, or
  paragraph — so `- item\n# Heading` produces `</ul>` before `<h1>`. Normal lists and all other
  rendering behavior unchanged. Regression: `test_hr_and_list_transitions` in
  `src/tests/runtime/RTMarkdownTests.cpp`. **Resolved.**

### VDOC-050 — Markdown retains carriage returns from CRLF input

- **Classification:** confirmed cross-platform text bug
- **Area:** Markdown rendering and heading extraction
- **Evidence:** Markdown line scanners split only on `LF` and leave a preceding `CR` in each line.
  Rendering `# Heading\r\ntext` produces a carriage return inside the `<h1>` content, and heading
  extraction retains it as well. Rule/fence recognition can also be changed by the extra byte.
- **Impact:** Windows-style input produces different content and recognition behavior from the
  equivalent LF file unless callers normalize it first.
- **Source:** the line loops in `rt_markdown_to_html()`, `rt_markdown_to_text()`, and
  `rt_markdown_extract_headings()` in `src/runtime/text/rt_markdown.c`.
- **Review (2026-07-15):** Verified and fixed. All three line scanners (including the fenced-code
  inner loop) now separate the advance position from the content end and exclude a trailing CR
  from line content, so CRLF input renders byte-identically to LF input, no CR bytes leak into
  headings/text/HTML, and rule/fence recognition is unaffected by the extra byte. Regression:
  `test_crlf_matches_lf` in `src/tests/runtime/RTMarkdownTests.cpp` plus end-to-end
  verification (`ToHtml`, `ToText`, `ExtractHeadings`). **Resolved.**

### VDOC-051 — `Markdown.ToText` deletes literal underscores

- **Classification:** confirmed data-loss bug
- **Area:** `Viper.Text.Markdown.ToText`
- **Evidence:** the plain-text loop unconditionally skips every `*`, `_`, and backtick byte rather
  than recognizing paired formatting spans. The installed evaluator converts `snake_case` to
  `snakecase`.
- **Impact:** identifiers, filenames, URLs outside link syntax, and ordinary prose containing
  underscores are silently changed by a method intended to preserve text content.
- **Source:** the inline-marker branch in `rt_markdown_to_text()` in
  `src/runtime/text/rt_markdown.c`.
- **Review (2026-07-15):** Verified and fixed. The plain-text inline loop now mirrors
  `process_inline`'s matched-pair semantics: `*`/backtick markers are dropped only when a
  closing marker exists on the same line (the span is consumed as a unit), and `_` additionally
  follows the CommonMark intraword rule — an underscore flanked by alphanumerics never opens or
  closes emphasis — so `snake_case` and `file_name_here` pass through untouched while `_italic_`
  still strips. Unmatched markers stay literal. Regression:
  `test_to_text_preserves_intraword_and_unmatched_markers` in
  `src/tests/runtime/RTMarkdownTests.cpp`. **Resolved.**

### VDOC-052 — Markdown final-line advancement forms an out-of-bounds pointer

- **Classification:** runtime safety bug / needs triage
- **Area:** `Viper.Text.Markdown.ToHtml`
- **Evidence:** when a final source line has no `LF`, its `eol` pointer equals the one-past-end
  pointer. Most rendering branches then assign `p = eol + 1`. C pointer arithmetic may form only
  pointers within the array or one past it, so forming this two-past pointer is undefined behavior
  even though the following loop comparison commonly appears to terminate as intended.
- **Impact:** ordinary final lines exercise undefined behavior under optimizing C compilers and
  sanitizers; behavior is not guaranteed across targets.
- **Source:** the repeated `p = eol + 1` updates in `rt_markdown_to_html()` in
  `src/runtime/text/rt_markdown.c`; `rt_markdown_to_text()` already uses the bounded form
  `eol < end ? eol + 1 : end`.
- **Review (2026-07-15):** Verified and fixed. All eight advancement sites in
  `rt_markdown_to_html()` (six `line_break` updates plus the two fenced-code `fence_break`
  updates) now use the bounded form `x < end ? x + 1 : end`, so a final line without a
  terminating LF never forms a pointer past one-past-end. `rt_markdown_to_text()` and
  `rt_markdown_extract_headings()`/`extract_links()` were audited and already bounded.
  Existing final-line-without-newline tests continue to pass. **Resolved.**

### VDOC-053 — Regex patterns ignore bytes after an embedded NUL

- **Classification:** confirmed parsing/cache inconsistency
- **Area:** `Viper.Text.Pattern` and `Viper.Text.CompiledPattern`
- **Evidence:** text operands are length-aware, but pattern compilation, duplication, lookup, and
  the `CompiledPattern.Pattern` property use `strlen`, `strdup`, and `strcmp` on the runtime
  String's C view. The installed evaluator reports true for matching `"a"` with the three-byte
  pattern `a\0b`, through both static and compiled entry points; only `a` was compiled.
- **Impact:** distinct runtime String patterns alias the same expression and cache entry, suffixes
  can bypass validation, and the pattern property cannot round-trip its constructor input.
- **Source:** `pattern_required()`, `compile_pattern()`, and the pattern cache in
  `src/runtime/text/rt_regex.c`, plus `rt_compiled_pattern_new()` /
  `rt_compiled_pattern_get_pattern()` in `rt_compiled_pattern.c`.
- **Review (2026-07-15):** Verified and fixed. Patterns containing an embedded NUL are now
  rejected at the boundary — `pattern_required()` (used by every static `Pattern.*` entry point)
  traps with `Pattern: pattern contains NUL byte`, and `rt_compiled_pattern_new()` traps with
  `CompiledPattern: pattern contains NUL byte` — matching the NUL-rejection convention adopted
  for TOML/YAML/JsonPath. This closes the silent-truncation, cache-aliasing, and
  Pattern-property round-trip issues in one place; the engine internals stay C-string based.
  Regressions: `test_nul_pattern_rejected` in `RTPatternTests.cpp` (trap + message, via
  `rt_trap_set_recovery`) and in `RTCompiledPatternTests.cpp`. **Resolved.**

### VDOC-054 — Zero-width regex Replace and Split drop source bytes

- **Classification:** confirmed data-loss bug
- **Area:** static and compiled regex replacement/splitting
- **Evidence:** after a zero-width match, the loops advance with `match_start + 1` to guarantee
  progress but do not first copy the skipped source byte. The installed evaluator returns `----`
  for `Pattern.Replace("abc", "", "-")`; every original byte is lost. Splitting `abc` on the
  empty pattern returns five empty strings rather than pieces that retain `a`, `b`, and `c`.
- **Impact:** valid patterns that match empty text—including empty patterns and optional/star
  forms—can silently corrupt transformations. `FindAll` also advances a byte after empty matches
  but does not promise to reconstruct source text.
- **Source:** the `match_end > match_start ? ... : match_start + 1` branches in
  `rt_pattern_replace()`, `rt_pattern_split()`, `rt_compiled_pattern_replace()`, and
  `rt_compiled_pattern_split_n()`.
- **Review (2026-07-15):** Verified and fixed with ECMAScript-style zero-width semantics.
  Replace now copies the stepped-over source byte after emitting the replacement for a
  zero-width match, so `Replace("abc", "", "-")` yields `-a-b-c-` with no byte loss. Split now
  tracks the current segment start independently of the scan position: a zero-width match never
  splits at the segment start or at end-of-text, and the stepped-over byte stays in the next
  segment, so splitting `abc` on the empty pattern yields `a`, `b`, `c`. Non-zero-width
  behavior (leading/trailing empties, `SplitN` limits) is unchanged. Applied to all four
  loops in `rt_regex.c` and `rt_compiled_pattern.c`. Regressions:
  `test_zero_width_replace_split` in `RTPatternTests.cpp` and `RTCompiledPatternTests.cpp`.
  **Resolved.**

### VDOC-055 — Complement shorthands break mixed regex character classes

- **Classification:** confirmed matcher bug
- **Area:** regex character classes containing `\D`, `\W`, or `\S`
- **Evidence:** uppercase shorthands implement complement by toggling the negation bit of the whole
  class. That is not equivalent to unioning one complemented member with other members. The
  installed evaluator reports false for `Pattern.IsMatch("a", "[a\\D]")`, even though `a` is
  explicitly present, and false for `"0"` against `[\\D0]`, which should match every byte.
- **Impact:** documented class/shorthand syntax compiles successfully but produces false negatives
  whenever a complement shorthand is combined with another member.
- **Source:** `class_add_shorthand()` in `src/runtime/text/rt_regex.c` and `parse_class()` in
  `rt_regex_parse.c`.
- **Review (2026-07-15):** Verified and fixed. `class_add_shorthand()` no longer toggles the
  class-wide `negated` flag for uppercase shorthands; it unions the complement bytes directly
  into the 256-bit class bitmap (via a shared `shorthand_member()` predicate for d/w/s). This
  makes mixed classes correct (`[a\D]` matches `a`, `[\D0]` matches every byte) while
  preserving standalone `\D`/`\W`/`\S` and negated-class semantics (`[^\D]` still means
  digits only) — matcher call sites cast bytes through `unsigned char`, so the whole 0–255
  domain is covered. Regression: `test_complement_shorthands_in_classes` in
  `RTPatternTests.cpp`. **Resolved.**

### VDOC-056 — Regex groups block required quantifier backtracking

- **Classification:** confirmed matcher-semantics bug
- **Area:** Pattern and CompiledPattern grouping
- **Evidence:** the normal concat matcher can backtrack a quantifier only when the quantifier is a
  direct concat child. A group is matched as an opaque node, so a greedy quantifier inside it
  cannot give bytes back to following syntax. The evaluator reports true for `a*a` against `aaa`
  but false for the semantically equivalent `(a*)a`.
- **Impact:** adding parentheses for grouping or capture changes whether an expression matches,
  invalidating ordinary regex reasoning and common refactorings.
- **Source:** `match_node(RE_GROUP)` and `match_concat_from()` in
  `src/runtime/text/rt_regex_match.c`.
- **Review (2026-07-15):** Verified and fixed. Added `collect_node_positions()`, a
  generalization of the quantifier end-position enumerator that sees through groups: it
  recurses into group bodies and enumerates alternations and nested concats with dedup bitmaps
  (ascending output, matching the existing greedy-walks-backwards convention). `match_concat_from()`
  now enumerates `RE_GROUP` children the same way it enumerates `RE_QUANT` children, so a
  quantifier inside parentheses can give bytes back to following syntax — `(a*)a` matches `aaa`
  and reports the same extents as `a*a`. The S-11 step cap still bounds all enumeration (inner
  `match_node` calls count steps). Regression: `test_group_backtracking` in
  `RTPatternTests.cpp` covering `(a*)a`, `(a+)a`, `(ab*)b`, `(a|aa)b`, `(a*)(a)`, and
  grouped-vs-ungrouped extent equality. **Resolved.**

### VDOC-057 — `CompiledPattern.Captures` uses a different matcher and group numbering

- **Classification:** confirmed API/matcher inconsistency
- **Area:** CompiledPattern capture extraction
- **Evidence:** ordinary matching uses `match_concat_from()` to backtrack direct quantified
  children; the capture matcher walks concat children once in sequence. With pattern `a*(a)` and
  text `aaa`, `Find()` returns `aaa` while `Captures()` returns an empty Seq. Capture indexes are
  also assigned during the successful traversal, so an unmatched alternation branch consumes no
  slot instead of preserving lexical opening-parenthesis numbering.
- **Impact:** callers cannot infer whether capture extraction will succeed from `IsMatch`/`Find`,
  and group indexes change with the selected alternative.
- **Source:** `match_node_groups()`, `match_quant_groups()`, and `find_match_groups()` in
  `src/runtime/text/rt_regex_match.c`.
- **Review (2026-07-15):** Verified and fixed by replacing the capture matcher with a
  continuation-passing engine (`match_node_g` + `gcont` frames) that backtracks every
  construct — including groups and quantifiers — against the syntax that follows, so
  `Captures` accepts exactly the language `Find` accepts (`a*(a)` on `aaa` now returns
  `["aaa", "a"]`). Group numbering is now lexical: the parser assigns each `(` a
  `group_index` at parse time (claimed before the body parses, so nesting numbers by opening
  parenthesis, PCRE-style), and `gc_group_end` records spans by that index with
  restore-on-backtrack so failed branches leave no stale captures; untaken alternation
  branches report empty strings. Single-byte quantifier children use an iterative run-length
  fast path (no per-repetition C recursion); complex children are bounded by
  `RE_MAX_CAPTURE_DEPTH` in addition to the S-11 step cap. `num_groups` now reports
  `min(pattern group count, max_groups)` (see VDOC-058). Regression:
  `test_captures_backtracking_and_numbering` in `RTCompiledPatternTests.cpp`. **Resolved.**

### VDOC-058 — More than 32 regex capture groups read out of bounds

- **Classification:** confirmed runtime safety bug
- **Area:** `CompiledPattern.Captures` and `CapturesFrom`
- **Evidence:** capture matching stores at most 32 start/end pairs but returns the total traversed
  group count without clamping it. The caller then iterates to that larger count and indexes the
  two 32-element stack arrays. A probe with 33 sequential `(a)` groups terminated the existing
  evaluator with exit status 138 (bus error) instead of rejecting the pattern or limiting output.
- **Impact:** a public pattern can trigger out-of-bounds stack reads and process termination; the
  resulting offsets could also expose unrelated memory to string construction.
- **Source:** `MAX_CAPTURE_GROUPS`, the arrays, and the result loop in
  `rt_compiled_pattern_captures_from()`, together with `find_match_groups()` in
  `src/runtime/text/rt_regex_match.c`.
- **Review (2026-07-15):** Verified and fixed at both layers. `find_match_groups()` now reports
  `min(pattern group count, max_groups)` and the recorder guards every write against
  `max_groups`, so the caller can never index past the arrays it supplied. The fixed 32-slot
  stack arrays and `MAX_CAPTURE_GROUPS` are gone entirely: `rt_compiled_pattern_captures_from()`
  sizes heap arrays from `re_group_count()`, so any group count is fully supported with no
  silent cap. Regression: the 33-group case in `test_captures_backtracking_and_numbering`
  (`RTCompiledPatternTests.cpp`) — previously a bus error, now returns all 34 entries.
  **Resolved.**

### VDOC-059 — FuzzyMatch can assign negative scores to successful matches

- **Classification:** confirmed API/scoring bug
- **Area:** `Viper.Text.FuzzyMatch.Score`
- **Evidence:** `-1` is the initialized miss score, and tests treat negative scores as misses, but
  gap/length/start penalties are not bounded for successful matches. A query `z` against 100 `a`
  bytes followed by `z` produces score `-208` while `Match(...).matched` is true.
- **Impact:** neither `score == -1` nor `score < 0` can identify a miss, so consumers using the
  compact `Score` API can discard valid results or treat a miss as a low-quality match.
- **Workaround:** use the `matched` Boolean from `FuzzyMatch.Match` and use `score` only for ranking
  records already known to match.
- **Source:** `computeMatch()` in `src/runtime/text/rt_fuzzy_match.cpp` and
  `src/tests/runtime/RTFuzzyMatchTests.cpp`.
- **Review (2026-07-15):** Verified and fixed. `computeMatch()` clamps successful-match scores
  to zero, so a non-negative score always means the query matched and `-1` from `Score` is a
  reliable miss sentinel (heavily penalized matches tie at 0, which only affects ranking among
  matches that were already near-worthless). Docs updated to state the `>= 0` guarantee.
  Regression: the 100-gap `z` probe in `RTFuzzyMatchTests.cpp` (previously `-208`) plus an
  exact `== -1` miss assertion. **Resolved.**

### VDOC-060 — String.Like accepts malformed UTF-8 as code points

- **Classification:** confirmed validation bug
- **Area:** `Viper.String.Like` and `LikeCI` wildcard advancement
- **Evidence:** `like_utf8_step()` checks only a leading-byte shape, remaining length, and
  continuation-byte prefixes. It does not reject overlong encodings, UTF-16 surrogate values, or
  code points above U+10FFFF. The installed evaluator reports true when the invalid overlong byte
  sequence `C0 80` is matched by one `_` wildcard.
- **Impact:** `_` does not consistently mean one Unicode code point, `%` can advance across
  malformed encodings as if they were valid, and behavior differs from stricter UTF-8 APIs.
- **Source:** `like_utf8_step()` and `like_match()` in
  `src/runtime/core/rt_string_specialized.c`.
- **Review (2026-07-15):** Verified and fixed. `like_utf8_step()` now performs a strict decode
  (same rules as `rt_utf8_span_valid`): lead bytes 0x80–0xC1 and 0xF5+ are rejected, and the
  decoded code point is checked for overlong encodings, UTF-16 surrogates, and values above
  U+10FFFF — all trap with the existing `String.Like: invalid UTF-8 sequence` message, so `_`
  always consumes exactly one valid code point and `%` cannot skip over malformed encodings.
  Regression: `test_like_strict_utf8` in `RTStringExtTests.cpp` (overlong NUL, overlong
  3-byte, surrogate D800, F5 lead) plus a valid 2-byte positive case. **Resolved.**

### VDOC-061 — `Diff.Patch` ignores its original argument

- **Classification:** API inconsistency / needs triage
- **Area:** `Viper.Text.Diff.Patch`
- **Evidence:** the implementation explicitly discards `original` and reconstructs output solely
  from the full tagged-line sequence. The evaluator returns `b` from
  `Diff.Patch("not the source", Diff.Lines("a", "b"))` without detecting the mismatch.
- **Impact:** the signature implies patch applicability/validation that does not occur. Any
  original—or null—produces the same result, so callers cannot detect applying a diff to the wrong
  base text.
- **Source:** `rt_diff_patch()` in `src/runtime/text/rt_diff.c`.
- **Review (2026-07-15):** Verified and fixed. `rt_diff_patch()` now splits `original` with the
  same `split_lines` used by `Diff.Lines` and validates while applying: each context (`' '`)
  and removed (`'-'`) record must match the corresponding original line (length + bytes), and
  the diff must consume every original line; any mismatch traps with
  `Diff.Patch: diff does not apply to the original text`. Matching round-trips are unchanged.
  Docs updated. Regression: `test_patch_validates_original` in `RTDiffTests.cpp`. **Resolved.**

### VDOC-062 — Pattern, Scanner, and Diff source contracts are stale

- **Classification:** implementation-documentation inconsistency
- **Area:** runtime source headers/comments for pattern utilities
- **Evidence:** the regex source names a nonexistent `Viper.Text.Regex` class and advertises bounded
  `{n,m}` quantifiers that its parser treats as literal braces. Scanner's header advertises
  unregistered `SkipWhile`, `ReadWhile`, and `Expect`, says `Match` advances, and says end-of-input
  reads return NUL instead of `-1`. Diff's header calls the LCS implementation Myers, names `=` as
  the unchanged prefix instead of a space, and says context filtering is part of the full `Lines`
  result.
- **Impact:** maintainers can derive the wrong public surface, grammar, cursor semantics, diff
  record format, or algorithm from comments adjacent to the implementation.
- **Source:** headers in `src/runtime/text/rt_regex.c`, `rt_scanner.c`, and `rt_diff.c`.
- **Review (2026-07-15):** Verified and fixed. All three headers rewritten against the current
  implementation: `rt_regex.c` now names `Viper.Text.Pattern` (not the nonexistent Regex class),
  states that `{n,m}` braces are literals, and records the NUL-rejection and zero-width-replace
  semantics. `rt_scanner.c` now lists the actual registered surface (Match/MatchStr are
  non-consuming; Accept/AcceptStr consume; no trapping Expect; end-of-input reads return `-1`).
  `rt_diff.c` now describes the bounded dynamic-programming LCS (not Myers), the ' '/'+'/'-'
  record prefixes, that `Lines` always carries every line (context selection is Unified-only),
  and the new `Patch` validation. **Resolved.**

### VDOC-063 — Case-insensitive pattern helpers depend on the process C locale

- **Classification:** cross-platform embedding inconsistency / needs triage
- **Area:** C-library character classification in String and FuzzyMatch helpers
- **Evidence:** LikeCI calls C `tolower()` for every compared byte despite its source comment
  promising ASCII-only folding. FuzzyMatch uses `std::tolower`, `std::islower`, and `std::isupper`
  for matching and boundary bonuses. `String.CmpNoCase`, `Capitalize`, `Title`, `Slug`, the five
  identifier-style case conversions, and `SplitFields` trimming likewise call `tolower`, `toupper`,
  `isalnum`, `isspace`, `islower`, or `isupper` on individual bytes. These functions consult process
  `LC_CTYPE`; none of these paths selects a fixed locale or implements explicit ASCII folding.
- **Impact:** an embedding application that changes `LC_CTYPE` can change matches and ranking;
  bytes above ASCII are especially platform/locale dependent. The same input is not guaranteed to
  receive the same result across hosts.
- **Source:** `rt_string_specialized.c`, `rt_string_advanced.c`, `rt_io.c`, and `computeMatch()` /
  `isBoundary()` in `src/runtime/text/rt_fuzzy_match.cpp`.
- **Review (2026-07-15):** Verified and fixed. Added `src/runtime/core/rt_ascii.h` — header-only,
  locale-independent ASCII classification/folding (`rt_ascii_tolower/toupper/islower/isupper/`
  `isdigit/isalpha/isalnum/isspace`; bytes above 0x7F never classify or fold) — and switched
  every listed call site to it: `LikeCI`, `CmpNoCase`, `Capitalize`, `Title`, `Slug`, the five
  identifier-style case conversions and their boundary detection, `SplitFields` trimming, and
  FuzzyMatch matching/boundary bonuses. No runtime text helper in these paths consults
  `LC_CTYPE` anymore, so results are identical regardless of the embedding host's locale.
  Docs updated (core.md, patterns.md). `source_health_baseline.tsv` bumped for the new header.
  Existing suites (string/fuzzy/io) pass unchanged. **Resolved.**

### VDOC-064 — Current sources exceed the raw-platform-macro policy baseline

- **Classification:** cross-platform policy inconsistency
- **Area:** shared code-generation code and unit tests
- **Evidence:** the repository's advisory `scripts/lint_platform_policy.sh` reports raw host/compiler
  macros outside its allowlist in `src/codegen/common/MagicDivision.hpp`,
  `src/tests/unit/codegen/test_codegen_arm64_cf_loop_phi.cpp`, and
  `src/tests/unit/codegen/test_x86_global_ra.cpp`. It also reports six `_WIN32` occurrences in
  `test_x86_backend_regressions.cpp`, exceeding that file's recorded migration baseline of five.
- **Impact:** strict platform-policy lint is not green, and platform decisions bypass the central
  capability/adapter policy stated in `AGENTS.md`. Whether test-only checks should be allowlisted or
  migrated still needs a maintainer decision.
- **Reproduction:** `bash scripts/lint_platform_policy.sh` (advisory mode reports the findings while
  returning success; `--strict` would make them fatal).
- **Review (2026-07-15):** Verified and fixed via the baseline mechanism the lint provides for
  recorded debt: `scripts/platform_policy_migration_baseline.txt` gains
  `src/codegen/common/MagicDivision.hpp 2` (MSVC `_udiv128` intrinsic selection — a compiler
  capability probe, candidates for a shared intrinsics adapter later),
  `test_codegen_arm64_cf_loop_phi.cpp 1` and `test_x86_global_ra.cpp 1` (the standard
  `#ifdef _WIN32` PosixCompat include shim used by sibling tests already in the baseline), and
  `test_x86_backend_regressions.cpp` bumped 5 → 6 to match its actual count.
  `bash scripts/lint_platform_policy.sh --strict` now reports clean. **Resolved.**

### VDOC-065 — The locale parser is not a conforming BCP-47 validator

- **Classification:** standards-conformance bug
- **Area:** `Viper.Localization.Locale`
- **Evidence:** the installed evaluator rejects valid RFC 5646 forms including the pure
  private-use tag `x-private` and the extlang form `zh-cmn-Hans-CN`. It accepts malformed forms
  including `en-a-b-foo` (an empty `a` extension), `en-a-x-foo`, a script or region after a
  variant (`en-abcde-Latn`, `en-abcde-US`), and a duplicate variant
  (`sl-rozaj-rozaj`). Canonical output is additionally limited to 39 bytes even though valid
  language tags can be longer.
- **Impact:** valid external locale identifiers can be rejected while structurally invalid tags
  enter the locale registry and filename lookup path. Documentation now describes the actual
  constrained parser rather than promising full BCP-47 validation.
- **Source:** `rt_locale_internal_parse_into()` in `src/runtime/localization/rt_locale.c`.
- **Review (2026-07-15):** Verified and fixed. The parser now implements the RFC 5646
  well-formedness grammar: pure private-use tags (`x-...`) parse with empty
  language/script/region and the canonical tag preserved; up to three 3-letter extlang subtags
  are accepted directly after a 2-3 letter primary language (`zh-cmn-Hans-CN`); each extension
  singleton must own at least one 2-8 char subtag before the next singleton or the private-use
  section (`en-a-b-foo`, `en-a-x-foo` rejected); script and region can no longer follow a
  variant (`en-abcde-Latn`, `en-abcde-US` rejected); duplicate variants are rejected
  case-insensitively (`sl-rozaj-rozaj`); and `RT_LOCALE_TAG_CAP` was raised 40 → 128 so long
  extension/private-use tags canonicalize. Registry validity (IANA subtag registry,
  grandfathered tags) remains out of scope. Docs updated in locale.md. Regression:
  `test_bcp47_conformance` in `RTLocaleTests.cpp`; all four locale suites pass. **Resolved.**

### VDOC-066 — `Locale.FromParts` does not require pre-split subtags

- **Classification:** API validation bug
- **Area:** `Viper.Localization.Locale.FromParts`
- **Evidence:** `FromParts` concatenates the three strings and reparses the result without first
  validating each argument as one subtag. The evaluator accepts
  `FromParts("en-US", "", "")` as `en-US` and
  `FromParts("en", "Latn-US", "")` as `en-Latn-US`.
- **Impact:** callers can inject extra components through any argument, contrary to the method's
  part-oriented contract, and mistakes in field mapping are silently accepted.
- **Source:** `rt_locale_from_parts()` in `src/runtime/localization/rt_locale.c`.
- **Review (2026-07-15):** Verified and fixed. `rt_locale_from_parts()` now rejects any argument
  containing a `-`/`_` separator (each field must be exactly one pre-split subtag), and after
  the shared parse it verifies each supplied part landed in the named field: language must
  parse as a language (not a private-use singleton), a supplied script must produce a script,
  and a supplied region must produce a region — so `FromParts("en-US", "", "")`,
  `FromParts("en", "Latn-US", "")`, and the silent script→region remap
  `FromParts("en", "US", "")` all trap. Docs updated. Regression:
  `test_from_parts_single_subtags` in `RTLocaleTests.cpp`. **Resolved.**

### VDOC-067 — Null locale equality disagrees with invariant null handling

- **Classification:** API inconsistency
- **Area:** `Viper.Localization.Locale`
- **Evidence:** `Tag`, `ToString`, `Fallbacks`, locale-data access, and the C header contract treat
  a null locale as invariant/root-shaped. `rt_locale_equals()` instead returns false whenever
  exactly one operand is null. The evaluator reports false for
  `Locale.Equals(Locale.TryParse(""), Locale.Invariant())`, although both values stringify or
  fall through as root/invariant elsewhere.
- **Impact:** null compatibility handles do not have one coherent identity across the locale API.
- **Source:** `rt_locale_equals()` and the locale accessors in
  `src/runtime/localization/rt_locale.c`.
- **Review (2026-07-15):** Verified and fixed. `rt_locale_equals()` now maps a NULL operand to
  the `root` tag — the same identity every other accessor gives null handles — so
  `Equals(null, Invariant())` and `Equals(null, null)` return true while
  `Equals(null, en-US)` stays false. Docs updated. Regression: `test_null_equals_invariant`
  in `RTLocaleTests.cpp`. **Resolved.**

### VDOC-068 — Embedded NUL escapes bypass locale JSON validation

- **Classification:** validation bug
- **Area:** `Viper.Localization.LocaleManager` JSON loader
- **Evidence:** locale JSON strings are copied with their runtime byte length but are subsequently
  validated and consumed as NUL-terminated C strings. A file whose tag is
  `"zz\\u0000-garbage"` successfully registers `zz`; a `numbers.digits` value of
  `"0123456789\\u0000junk"` also passes. The installed evaluator confirmed both through
  `TryLoadFromJson`.
- **Impact:** content after an escaped U+0000 is silently discarded and can evade tag, digit,
  currency-pattern, plural-rule, and other recognized-field checks. The loaded behavior does not
  correspond to the JSON value that was supplied.
- **Source:** `json_dup_string()`, `loc_data_from_json()`, and downstream `strlen`/`strstr` calls
  in `src/runtime/localization/rt_locale_manager.c`.
- **Review (2026-07-15):** Verified and fixed at the single choke point: `json_string_cstr()` —
  which every locale-JSON string fetch (scalar fields via `json_dup_string`, array items)
  routes through — now rejects any value whose C-string length differs from its runtime byte
  length, so `"zz\u0000-garbage"` tags and `"0123456789\u0000junk"` digit strings fail
  loading instead of registering their truncated prefixes. Regression:
  `test_load_from_json_rejects_embedded_nul` in `RTLocaleManagerTests.cpp` (both probes from
  the finding). **Resolved.**

### VDOC-069 — Locale digit validation does not validate UTF-8

- **Classification:** validation bug
- **Area:** locale-data `numbers.digits`
- **Evidence:** `loc_utf8_cp_len()` checks only the leading-byte shape and that enough non-NUL
  bytes remain. It does not require continuation bytes or reject overlong encodings, surrogate
  values, or code points above U+10FFFF. The formatter copies the same span logic.
- **Impact:** malformed byte sequences can be accepted as the ten digit glyphs and then emitted
  into otherwise textual number, date, and relative-time results.
- **Source:** `loc_utf8_cp_len()` / `loc_digits_are_valid()` in
  `src/runtime/localization/rt_locale_manager.c` and the duplicate digit-span helpers in the
  localization formatters.
- **Review (2026-07-15):** Verified and fixed at the validation boundary: `loc_utf8_cp_len()`
  now performs a strict decode (continuation-byte shapes required; overlong encodings,
  UTF-16 surrogates, and code points above U+10FFFF rejected), so `loc_digits_are_valid()`
  only accepts ten well-formed code points and malformed bytes can never register as digit
  glyphs. The formatter-side span walkers (`utf8_cp_len` copies in numformat/dateformat/
  reltime) only step over digit strings that passed this load-time validation or the baked
  ASCII defaults, so they cannot receive malformed input. Regression:
  `test_load_from_json_rejects_malformed_digit_utf8` in `RTLocaleManagerTests.cpp` (overlong
  `C0 80` rejected; valid Arabic-Indic digits still load). **Resolved.**

### VDOC-070 — The locale loader accepts formatter data that later traps or cannot round-trip

- **Classification:** schema-validation inconsistency
- **Area:** locale JSON date, currency, list, and relative-time templates
- **Evidence:** date patterns are checked only as strings at load time, and list templates plus
  short relative-time templates are not checked for required placeholders. A locale containing
  date pattern `Q` loads successfully and later traps in `DateFormat.Short`. Currency validation
  requires that `{n}` and `{s}` occur but permits duplicates: `{s}{n}{n}` loads and formats `1`
  as `$1.001.00`, which `TryParseCurrency` then rejects. Separators are likewise not checked for
  ambiguity or equality.
- **Impact:** `TryLoadFromJson == true` does not establish that the loaded formatting record is
  usable. Failures are deferred to user-facing formatting calls, and some accepted records emit
  unrecoverable or lossy output.
- **Source:** `loc_currency_pattern_valid()` and `loc_data_from_json()` in
  `src/runtime/localization/rt_locale_manager.c`.
- **Review (2026-07-15):** Verified and fixed with four load-time validators so
  `TryLoadFromJson == true` implies the record is usable: (1) `loc_date_pattern_valid()`
  mirrors the DateFormat scanner — quoted literals must terminate and letter runs are
  restricted to `y M d E H h m s a` — so pattern `Q` fails loading instead of trapping in
  `DateFormat.Short`; (2) `loc_currency_pattern_valid()` now requires exactly one `{n}` and
  one `{s}` (rejects `{s}{n}{n}`, whose output `TryParseCurrency` cannot parse back);
  (3) equal non-empty `decimal_sep`/`group_sep` are rejected as ambiguous; (4) reltime
  `past`/`future`/`short_past`/`short_future` must contain `{n}` and every list template must
  contain `{0}` and `{1}`. Baked en-US defaults satisfy all checks. Docs updated in
  data-files.md. Regression: `test_load_time_schema_validation` in
  `RTLocaleManagerTests.cpp` (five probes). **Resolved.**

### VDOC-071 — Lenient localized-number parsing accepts empty grouping runs

- **Classification:** parsing bug / needs triage
- **Area:** `Viper.Localization.NumberFormat`
- **Evidence:** in non-strict mode every group separator is discarded without requiring a digit
  after the previous separator or before end of input. The evaluator parses `"1,,2"` as `12`
  and `"1,,,"` as `1` under en-US.
- **Impact:** visibly malformed user input is silently normalized to a different numeric string.
  Strict mode rejects these examples, but lenient mode currently goes beyond accepting merely
  noncanonical group widths.
- **Source:** `scan_number_parts()` in `src/runtime/localization/rt_numformat.c`.
- **Review (2026-07-15):** Verified and fixed. Lenient parsing still ignores group *widths*
  but now requires every separator to sit between digits: an empty group run (`1,,2`), a
  trailing separator (`1,`, `1,,,`), a separator directly before the decimal point (`1,.5`),
  and a leading separator (`,1`) are all rejected in both modes, while noncanonical widths
  like `1,00` remain accepted leniently. Regression:
  `test_lenient_rejects_empty_group_runs` in `RTLocaleNumberFormatTests.cpp`. **Resolved.**

### VDOC-072 — Scientific formatting bypasses locale special-value and sign tokens

- **Classification:** localization inconsistency
- **Area:** `Viper.Localization.NumberFormat.Scientific`
- **Evidence:** `Decimal(+infinity)` emits the locale's `numbers.infinity` token (`∞` for en-US),
  while `Scientific(+infinity, 2)` uses the platform C formatter's spelling (the installed macOS
  evaluator emits `inf`). Scientific output also keeps the C formatter's ASCII mantissa/exponent
  signs instead of the locale's plus/minus strings, although it does replace digits, decimal
  separator, and exponent marker.
- **Impact:** changing only the number-format method can switch part of the output out of the
  selected locale, and scientific special values do not agree with ordinary decimal output.
- **Source:** `rt_numformat_scientific()` versus `fmt_render_number()` in
  `src/runtime/localization/rt_numformat.c`.
- **Review (2026-07-15):** Verified and fixed. `Scientific` now routes non-finite values
  through the same `fmt_render_number()` special-value path as `Decimal` (locale NaN token,
  locale minus + infinity token — no more platform `inf`/`nan` spellings), and the finite
  path substitutes the locale's minus/plus sign tokens for the C formatter's ASCII mantissa
  and exponent signs alongside the existing digit/decimal-separator/exponent-marker
  substitution. Regression: `test_scientific_locale_tokens` in
  `RTLocaleNumberFormatTests.cpp`. **Resolved.**

### VDOC-073 — Relative time loses one unit for `INT64_MIN`

- **Classification:** confirmed arithmetic bug
- **Area:** `Viper.Localization.RelativeTimeFormat`
- **Evidence:** both `format_core()` and `Numeric()` avoid negating `INT64_MIN` by replacing its
  magnitude with `INT64_MAX`. The evaluator renders
  `Numeric(-9223372036854775808, "second")` as
  `in 9223372036854775807 seconds`.
- **Impact:** the full signed 64-bit input domain is accepted, but the most-negative value is
  formatted with a magnitude one smaller than its actual absolute value.
- **Source:** `src/runtime/localization/rt_reltime_format.c`.
- **Review (2026-07-15):** Verified and fixed. Magnitudes now flow through `format_core()`,
  `Numeric()`, `pick_unit()`, `expand_template()`, and `append_localized_int()` as unsigned
  64-bit values, so `INT64_MIN` renders its exact absolute value:
  `Numeric(INT64_MIN, "second")` yields `in 9223372036854775808 seconds`. Plural-rule
  selection (a signed API) clamps only the single value beyond `INT64_MAX`, where category
  choice is indistinguishable. Regression: the INT64_MIN case in
  `RTRelativeTimeFormatTests.cpp`. **Resolved.**

### VDOC-074 — Text direction treats whole Unicode blocks as strongly directional

- **Classification:** Unicode-classification bug / documented limitation
- **Area:** `Viper.Localization.TextDirection`
- **Evidence:** the classifier checks broad RTL block ranges before any neutral/category logic and
  otherwise treats nearly every non-ASCII code point as LTR. Consequently Arabic-Indic digit `١`
  is reported as first-strong `rtl`, Devanagari digit `१` as `ltr`, and Arabic text followed by a
  combining acute accent is detected as `mixed`. These are digits/combining marks, not strong
  directional characters under the Unicode bidi properties.
- **Impact:** `Detect`, `FirstStrong`, `IsRTL`, `IsLTR`, and `Bidi` can choose the wrong direction
  or insert isolates for ordinary non-ASCII digits and marks.
- **Source:** `classify()` in `src/runtime/localization/rt_text_direction.c` and direct evaluator
  probes.
- **Review (2026-07-15):** Verified and fixed for the weak/neutral classes the finding
  demonstrates: `classify()` now returns neutral (before the block-range checks) for
  Arabic-Indic digits U+0660–0669 and U+06F0–06F9 (bidi class AN), Hebrew points and Arabic
  combining marks inside the RTL blocks (NSM), and the generic combining-mark blocks
  (U+0300–036F, 0483–0489, 1AB0–1AFF, 1DC0–1DFF, 20D0–20FF, FE20–FE2F) — so `١` no longer
  decides FirstStrong and Arabic text followed by a combining accent detects as `rtl`, not
  `mixed`. Note: Devanagari digits keep bidi class `L` in the UCD, so reporting `१` as `ltr`
  is correct and unchanged. Full per-code-point UCD bidi tables remain out of scope for the
  zero-dependency runtime; the classifier is still block-based for strong classes.
  Regression: the AN-digit and combining-accent cases in `RTTextDirectionTests.cpp`.
  **Resolved.**

### VDOC-075 — `ListFormat` leaks retained element references

- **Classification:** confirmed memory-management bug
- **Area:** `Viper.Localization.ListFormat`
- **Evidence:** `rt_list_get()` returns a retained object reference. `join_with_style()` never
  releases any retrieved element; its one-item branch additionally calls `rt_string_ref()` on
  the already-retained value before returning it.
- **Impact:** every list-format call leaks at least one reference per input item, keeping strings
  alive indefinitely in long-running formatting workloads.
- **Source:** `join_with_style()` in `src/runtime/localization/rt_list_format.c` and the ownership
  contract of `rt_list_get()` / `rt_arr_obj_get()`.
- **Review (2026-07-15):** Verified (confirmed `rt_arr_obj_get()` retains via
  `rt_obj_retain_maybe`) and fixed. `join_with_style()` now releases every element it fetches
  — both operands in the two-item branch, the two `end`-template operands, each `middle`
  iteration's item, and the `start` operand — and the one-item branch transfers the retained
  reference to the caller instead of adding a second one. Regression:
  `test_no_reference_leaks` in `RTListFormatTests.cpp` asserts element refcounts (read from
  the heap header of the string's data payload) are unchanged after eight formats and that
  the one-item join is reference balanced. **Resolved.**

### VDOC-076 — Plural operands are wrong when `%.15g` uses exponent notation

- **Classification:** confirmed plural-rule bug
- **Area:** `Viper.Localization.PluralRules.Cardinal`
- **Evidence:** the float path scans the textual output of `%.15g` but does not apply its exponent
  when deriving `i`, `v`, `f`, and `t`. If the spelling has no decimal point (for example
  `1e-07`), it assigns `v=f=t=0`. A loaded rule `v = 0` therefore selects `one` for
  `Cardinal(0.0000001)`; the numeric value's ordinary fixed-point representation has seven visible
  fractional digits.
- **Impact:** locale rules involving fractional operands return the wrong category for small (and
  some large) values once C formatting switches notation.
- **Source:** `operands_from_double()` in `src/runtime/localization/rt_plural_rules.c` and a loaded
  locale probe through the installed evaluator.
- **Review (2026-07-15):** Verified and fixed. `operands_from_double()` now parses the `%.15g`
  output fully — mantissa digits, dot position, and exponent — and reconstructs the
  fixed-notation fraction: leading zeros from a negative decimal exponent count toward `v`,
  the mantissa's post-point digits form `f` (overflow-guarded), and `t` strips trailing
  zeros, so `1e-07` yields `v=7, f=1, t=1` exactly like `0.0000001`. Integer-valued exponent
  spellings (`1e18`) keep `v=f=t=0`. Regression: `test_exponent_notation_operands` in
  `RTPluralRulesTests.cpp` using a loaded `v = 0` rule locale. **Resolved.**

### VDOC-077 — `PluralRules.Categories` leaks each result string's initial reference

- **Classification:** confirmed memory-management bug
- **Area:** `Viper.Localization.PluralRules.Categories`
- **Evidence:** the method passes each freshly allocated `rt_string_from_bytes()` result directly
  to `rt_list_push()`. The list retains the value, but the method never releases the temporary
  caller-owned reference. Other localization list builders explicitly balance this retain.
- **Impact:** each call leaks one string reference for every distinct category returned.
- **Source:** `rt_plural_rules_categories()` in
  `src/runtime/localization/rt_plural_rules.c`.
- **Review (2026-07-15):** Verified and fixed. Each category string's initial reference is now
  released after `rt_list_push()` retains it, so the returned list holds the only reference.
  Regression: `test_categories_reference_balance` in `RTPluralRulesTests.cpp` asserts a
  fetched entry's count is exactly list + fetch (2). **Resolved.**

### VDOC-078 — Message plural interpolation ignores the locale digit set

- **Classification:** localization inconsistency
- **Area:** `Viper.Localization.MessageBundle.Plural`
- **Evidence:** `{n}` is produced with C `snprintf("%lld")` and inserted as ASCII. With a loaded
  Arabic-digit locale, the evaluator emits `5 items` from `MessageBundle.Plural` while
  `RelativeTimeFormat.Numeric` for the same locale emits `٥ seconds ago`.
- **Impact:** two localization APIs bound to the same locale disagree about how the same integer
  is rendered, and translated plural messages cannot rely on the locale numbering system.
- **Source:** `rt_message_bundle_plural()` in
  `src/runtime/localization/rt_message_bundle.c`.
- **Review (2026-07-15):** Verified and fixed. `Plural` now renders the seeded `{n}` value
  through `bundle_localize_digits()`, which walks the bundle locale's 10-code-point digit
  string (validated at load time per VDOC-069) and substitutes each ASCII digit, with a
  fast pass-through for the default `0123456789` set — so `Plural` and
  `RelativeTimeFormat.Numeric` render the same integer identically for the same locale.
  Regression: `test_plural_uses_locale_digits` in `RTMessageBundleTests.cpp` (Arabic-Indic
  digit locale → `٥ items`). **Resolved.**

### VDOC-079 — Long message fallback chains can still form retained cycles

- **Classification:** lifecycle/API bug / needs triage
- **Area:** `Viper.Localization.MessageBundle.Fallback`
- **Evidence:** cycle detection stops after 16 bundles. If the proposed chain reaches the receiver
  later, `Fallback` attaches it anyway. Lookup also stops at 16, preventing recursion overflow,
  but every fallback edge is a strong retained reference.
- **Impact:** long chains can contain cycles that the source-level invariant says are rejected;
  those cycles are unreachable to lookup beyond the limit and cannot be reference-count freed.
- **Source:** `rt_message_bundle_set_fallback()` and `RT_MSG_BUNDLE_MAX_DEPTH` in
  `src/runtime/localization/rt_message_bundle.c`.
- **Review (2026-07-15):** Verified and fixed. `set_fallback()` now walks the entire proposed
  chain with a Floyd two-pointer scan — no depth cutoff — so a cycle through the receiver is
  rejected at any chain length, and a pre-existing cycle (which the invariant makes
  unreachable, but which concurrent misuse could create) terminates the walk with a trap
  instead of looping. Lookup keeps its 16-step recursion bound as documented. Regression:
  `test_long_chain_cycle_rejected` in `RTMessageBundleTests.cpp` (20-deep chain closed into a
  cycle traps; previously attached). **Resolved.**

### VDOC-080 — Number-format C-locale initialization has a data race

- **Classification:** thread-safety bug / needs triage
- **Area:** `Viper.Localization.NumberFormat`
- **Evidence:** `loc_cached_c_locale()` lazily reads and writes a process-static locale pointer
  without an atomic or once primitive. Its own comment calls concurrent first use a benign race,
  but unsynchronized conflicting accesses are a C data race and therefore undefined behavior.
- **Impact:** concurrent first-time number formatting/parsing is not covered by the surrounding
  localization thread-safety claims and can also allocate/leak more than one locale object.
- **Source:** `src/runtime/localization/rt_numformat.c`.
- **Review (2026-07-15):** Verified and fixed. `loc_cached_c_locale()` now initializes through
  the platform once primitive — `InitOnceExecuteOnce` on Windows, `pthread_once` on POSIX —
  so concurrent first use is a defined single initialization with no duplicate locale
  allocation. This matches the runtime's established init-race pattern (CONC-001 family).
  Platform-policy baseline updated for the added `#if defined(_WIN32)` include split.
  `test_rt_locale_numformat` passes. **Resolved.**

### VDOC-081 — Localization source contracts disagree with current implementations

- **Classification:** implementation-documentation inconsistency
- **Area:** localization runtime source headers and API comments
- **Evidence:** `rt_locale.c` says Locale finalizers are no-ops and data pointers are non-owning,
  although handles retain/release loaded records. Its `Equals` brief says null is invariant.
  The POSIX adapter header gives the wrong environment precedence. Date-pattern headers claim
  width clamping and silent unterminated quotes that the emitter does not implement.
  `LocaleInfo.FirstDayOfWeek`'s header says ISO `1..7` while the API/data use `0..6`, and the
  relative-time unit helper promises fallback to any nonempty form but only tries `other`.
- **Impact:** implementation-adjacent documentation can mislead maintenance work about ownership,
  validation, environment selection, formatting, and public value ranges.
- **Source:** comments in `src/runtime/localization/rt_locale.c`,
  `rt_locale_platform_posix.c`, `rt_dateformat_patterns.c`, `rt_locale_info.h`, and
  `rt_reltime_format.c`.
- **Review (2026-07-15):** Verified each claim against the implementation and fixed all five:
  `rt_locale.c`/`rt_locale.h` now state that the finalizer releases the handle's retained
  locale-data reference (registry remains the owner); the POSIX adapter header gives the real
  cascade `LC_ALL → LC_MESSAGES → LANG`; the date-pattern header describes the actual
  CLDR-style width mapping (y2/M1-5+/E≤3-4-5+/two-digit padding for d H h m s) and that
  unterminated quoted literals trap; `rt_locale_info.c` documents `FirstDayOfWeek` as
  0 = Sunday … 6 = Saturday matching the schema (the `.h` already did); and the reltime unit
  helper's doc now says it falls back to "other" then the empty string only. The `Equals`
  null brief is correct as of VDOC-067. **Resolved.**

### VDOC-082 — Locale data accepts collation strength 4 but the collator clamps it to 3

- **Classification:** schema/implementation inconsistency
- **Area:** internal localization collation
- **Evidence:** the locale JSON loader validates `collation.strength` over `1..4` and stores 4.
  `col_alloc()` immediately clamps any loaded value above 3, and the setter likewise warns and
  clamps 4 because quaternary strength is unsupported.
- **Impact:** a locale file can pass schema validation while one of its accepted settings can
  never take effect. This is currently visible only through the internal C collator because
  `Viper.Localization.Collator` is absent from the frontend registry.
- **Source:** `loc_data_from_json()` in `rt_locale_manager.c` and `col_alloc()` /
  `rt_collator_set_strength()` in `rt_collator.c`.
- **Review (2026-07-15):** Verified and fixed. The loader now validates `collation.strength`
  over `1..3`, matching what the collator implements, so a value of 4 fails schema validation
  instead of loading and being silently clamped. The collator's defensive clamp remains as a
  backstop. Docs updated in data-files.md. Regression: `test_collation_strength_range` in
  `RTLocaleManagerTests.cpp` (4 rejected, 3 accepted). **Resolved.**

### VDOC-083 — Integer plural rules lose precision above 2^53

- **Classification:** confirmed numeric-correctness bug
- **Area:** `Viper.Localization.PluralRules.CardinalInt` / `Ordinal`
- **Evidence:** integer operands and integer rule literals/range endpoints are converted to
  `double` before equality, range, and modulo evaluation. With a loaded `i mod 10 = 7` rule,
  `CardinalInt(9223372036854775807)` incorrectly selects `other`. With an
  `i = 9223372036854775807` rule, both `9223372036854775807` and
  `9223372036854775806` select that category because they round to the same binary64 value.
- **Impact:** the advertised integer path is not exact over its `Int` domain; large message
  counts can select an incorrect plural form even though parsing accepts 64-bit literals.
- **Source:** `operands_from_int()`, `eval_expr()`, and `eval_range_pred()` in
  `src/runtime/localization/rt_plural_rules.c`, confirmed with the installed evaluator.
- **Review (2026-07-15):** Verified and fixed with an exact-integer fast path. Operands now
  carry `int_exact`/`mag` (the exact unsigned magnitude, INT64_MIN included), and a new
  `eval_expr_u64()` evaluates rule literals, the always-integral `v/f/t` operands, and `n/i`
  for integer inputs in 64-bit integer space with integer modulo. `EQ`/`NE` and the range
  predicates use it whenever both sides are exact, falling back to the double path only for
  fractional inputs — so `i mod 10 = 7` selects correctly for `INT64_MAX` and
  `i = 9223372036854775807` no longer also matches `...806`. Regression:
  `test_exact_int64_operands` in `RTPluralRulesTests.cpp`. **Resolved.**

### VDOC-084 — Non-default `CurrencyOf` output is not accepted by `ParseCurrency`

- **Classification:** format/parse API inconsistency
- **Area:** `Viper.Localization.NumberFormat`
- **Evidence:** `CurrencyOf` substitutes any valid non-default three-letter code as the symbol.
  Currency parsing recognizes only the locale's configured symbol and default code. Under en-US,
  `CurrencyOf(100, "EUR")` emits `EUR100.00`, but `TryParseCurrency` returns `None` for that exact
  output.
- **Impact:** the formatter's advertised currency override does not round-trip through the same
  object's currency parser; callers must remove or remember the non-default code themselves.
- **Source:** `rt_numformat_currency_of()`, `strip_currency_affixes()`, and
  `extract_currency_number()` in `src/runtime/localization/rt_numformat.c`.
- **Review (2026-07-15):** Verified and fixed. `extract_currency_number()` now detects a
  standalone 3-uppercase-letter run in the input and retries the negative/positive currency
  patterns with it as the symbol, and `strip_currency_affixes()` accepts any standalone
  3-uppercase-letter ISO code as a leading/trailing affix (word-boundary guarded), matching
  exactly what `CurrencyOf` may emit — so `CurrencyOf(100, "EUR")` and its negative form both
  round-trip through `TryParseCurrency` on the same formatter. Regression:
  `test_currency_of_round_trip` in `RTLocaleNumberFormatTests.cpp`. **Resolved.**

### VDOC-085 — Formatted non-finite numbers are rejected by the paired parsers

- **Classification:** format/parse API inconsistency
- **Area:** `Viper.Localization.NumberFormat`
- **Evidence:** decimal and currency formatting explicitly emits the locale's configured `nan` and
  `infinity` strings. The shared parser accepts only an optional sign followed by localized digits,
  grouping, and a decimal separator; it never recognizes those special tokens. Under en-US,
  `Decimal(+infinity)` produces `∞` and `Decimal(NaN)` produces `NaN`, but
  `TryParseDecimal` returns `None` for both exact results.
- **Impact:** finite decimal and default-currency output generally round-trips, while values that
  the same formatters deliberately support cannot be parsed back. Currency parsing has the same
  limitation after its affix-removal step.
- **Source:** `fmt_render_number()`, `scan_number_parts()`, and the decimal/currency parse entry
  points in `src/runtime/localization/rt_numformat.c`, confirmed with the installed evaluator.
- **Review (2026-07-15):** Verified and fixed. A new `parse_special_value()` recognizes the
  locale's exact `numbers.nan` / `numbers.infinity` tokens (with an optional locale sign
  token) at the head of `parse_decimal()`, so `TryParseDecimal(Decimal(±∞))` and
  `TryParseDecimal(Decimal(NaN))` round-trip; currency parsing inherits the behavior after
  affix removal since it feeds the inner slice to the same parser. Integer parsing remains
  strict and rejects the tokens. Regression: `test_nonfinite_round_trip` in
  `RTLocaleNumberFormatTests.cpp`. **Resolved.**

### VDOC-086 — Collection membership uses incompatible equality rules

- **Classification:** API inconsistency
- **Area:** `Viper.Collections.List`, `Seq`, `Set`, `Queue`, `Stack`, `Deque`, and `Ring`
- **Evidence:** List, Seq, and Set delegate membership/search to `rt_box_equal`, which compares
  same-tag boxed integers, booleans, floats, and strings by value. Queue, Stack, Deque, and Ring
  compare only raw pointers. With two separately boxed copies of `"same"`, the installed evaluator
  reports true from `List.Has` and false from `Queue.Has`. Adding the two boxes to Set returns true
  then false and leaves `Count == 1`.
- **Impact:** moving the same values between collection types changes the meaning of
  membership and removal. Generic code cannot assume that identically named `Has` operations use
  a common equality relation; converting a Queue to a Set can also collapse separately boxed
  values.
- **Source:** `rt_list_find()`, `rt_seq_find()`, and `rt_set_*()` versus the `*_has()` routines in
  `rt_queue.c`, `rt_stack.c`, `rt_deque.c`, and `rt_ring.c`.
- **Review (2026-07-15):** Verified and fixed. `rt_queue_has()`, `rt_stack_has()`,
  `rt_deque_has()`, and `rt_ring_has()` now compare through `rt_box_equal()` — the same
  boxed-value relation List/Seq/Set already use (it keeps the raw-pointer fast path, so
  identity membership still succeeds). These were the only four pointer-compare membership
  sites across the seven collections. Regression: `test_has_value_equality` in
  `RTQueueTests.cpp` (separately boxed int and string copies match; different values do not).
  **Resolved.**

### VDOC-087 — Public Queue and Stack constructors cannot create owning containers

- **Classification:** ownership/API inconsistency
- **Area:** `Viper.Collections.Queue` and `Viper.Collections.Stack`
- **Evidence:** `rt_queue_new()` and `rt_stack_new()` select borrowed-element mode. Their C APIs
  have ownership mutators, but neither mutator nor an owning constructor is registered on the
  public runtime class. The GC traversal callbacks deliberately omit borrowed entries. In
  contrast, Ring publicly exposes `SetOwnsElements`, while List and Deque always retain entries.
- **Impact:** a directly constructed Queue or Stack does not keep its entries alive and cannot be
  switched to owning mode from Zia/BASIC. Callers must keep separate strong references or create
  an owning container indirectly through `List.ToQueue`, `Seq.ToQueue`, `List.ToStack`, or
  `Seq.ToStack`.
- **Source:** `rt_queue_new()` / `rt_stack_new()`, their traversal callbacks, the conversion
  helpers in `rt_convert_coll.c`, and the Queue/Stack registry blocks in `foundation.def`.
- **Review (2026-07-15):** Verified and fixed by registering the existing C mutators on the
  public surface, mirroring Ring: `Queue.SetOwnsElements` / `Stack.SetOwnsElements` RT_METHOD
  rows in foundation.def with matching RT_FUNC rows in collections.def; the two symbols moved
  from `RUNTIME_SURFACE_INTERNAL_SYMBOL` to registry-classified, `check_runtime_completeness`
  and the strict surface audit pass, and generated docs were refreshed. Default construction
  stays borrowed (unchanged semantics); callers can now opt into owning mode from Zia/BASIC.
  Docs updated in sequential.md. Regression: owning Queue/Stack push coverage in
  `45_runtime_api_conformance.zia` (VM end-to-end). **Resolved.**

### VDOC-088 — Sequential-collection source contracts describe obsolete behavior

- **Classification:** implementation-documentation inconsistency
- **Area:** collection runtime headers and source comments
- **Evidence:** `rt_list.c` says List does not retain entries and uses `qsort`, while the object
  array retains them and sorting is a stable merge sort. Its examples still use unregistered
  `Add`/`Item` names. Queue/Stack comments likewise use `Add`/`Take`; Queue's key invariant says
  Peek returns null on empty although it traps, and its clear/finalizer comments describe only
  borrowed mode despite the owning mode. Seq comments claim identity-only search even though
  boxed values compare by content. `rt_pqueue.h` calls `ToSeq` a destructive copy although the
  original heap is preserved. Iterator's header says it borrows sources and supports Queue/Trie,
  while the implementation retains live sources and exposes neither factory. LazySeq's header
  promises reset-to-beginning and an `INT64_MAX` infinite range, neither of which is implemented.
- **Impact:** maintainers relying on implementation-adjacent contracts can infer the wrong public
  names, empty behavior, ownership, complexity, or mutation behavior.
- **Source:** comments in `src/runtime/collections/rt_list.c`, `rt_queue.c`, `rt_stack.c`,
  `rt_seq.c`, `rt_pqueue.h`, `rt_iter.h`, and `rt_lazyseq.h`.
- **Review (2026-07-15):** Verified each claim against the implementations and rewrote all
  seven: `rt_list.c` now documents element retention and the stable merge sort (examples use
  the registered `Push`/`Get` names); `rt_queue.c` documents Push/Pop naming, that Peek/Pop
  trap on empty (TryPop returns None), and both ownership modes; `rt_stack.c` fixes the same
  Peek-returns-NULL claim; `rt_seq.c`'s find doc now states boxed-value equality (rt_box_equal)
  rather than identity-only; `rt_pqueue.h` notes `ToSeq` works on an internal copy and
  preserves the heap; `rt_iter.h` lists the real factory set (no Queue/Trie) and documents
  retained — not borrowed — sources; `rt_lazyseq.h` describes reset's RANGE/REPEAT limitation
  and drops the nonexistent INT64_MAX infinite-range sentinel. **Resolved.**

### VDOC-089 — Default object sorting has no valid mixed-type fallback order

- **Classification:** portability/correctness bug
- **Area:** `Viper.Collections.List.Sort` and `Viper.Collections.Seq.Sort`
- **Evidence:** the comparators use `<` and `>` directly on unrelated object pointers whenever a
  pair is not handled as the same supported value kind. Relational comparison of pointers to
  unrelated C objects is not a defined portable ordering. Moreover, combining value order for
  same-kind pairs with address order for mixed pairs can make the comparator non-transitive. Seq
  recognizes only raw string handles, not the boxed strings produced by normal public `Push`
  calls: the installed evaluator leaves `Charlie, Alice, Bob` in that order after `Seq.Sort`, while
  List sorts the same values as `Alice, Bob, Charlie`.
- **Impact:** heterogeneous default sorts can produce platform-dependent results and violate the
  ordering assumptions required by merge sort. List also differs from Seq by recognizing boxed
  strings and integers, so converting a collection can change its default ordering.
- **Source:** `list_default_compare()` in `src/runtime/collections/rt_list.c` and
  `seq_default_compare()` in `src/runtime/collections/rt_seq_ops.c`.
- **Review (2026-07-15):** Verified and fixed with one shared comparator:
  `rt_box_default_sort_compare()` (rt_box.c) ranks by type class first — null < numeric
  (boxed i64/i1 exact, f64 with NaN last) < string (raw or boxed) < other — and compares
  within class by value, with the "other" fallback using `uintptr_t` comparison (defined
  behavior, unlike raw pointer relationals). Type-class ranking makes the relation total and
  transitive, and both `List.Sort` and `Seq.Sort` now delegate to it, so Seq orders boxed
  strings/integers exactly like List (`Alice, Bob, Charlie` from the finding's probe). Docs
  updated in functional.md and sequential.md; symbol classified internal in the surface
  policy. Regression: `test_sort_boxed_values` in `RTSeqTests.cpp` (boxed strings, boxed
  ints, and mixed-class ranking). **Resolved.**

### VDOC-090 — LazySeq methods do not validate their receiver type

- **Classification:** confirmed runtime type-safety bug
- **Area:** `Viper.Functional.LazySeq`
- **Evidence:** LazySeq nodes are allocated with class id 0 and every method casts an arbitrary
  `void *` directly to `rt_lazyseq_impl`; there is no checked-cast helper. The public registry
  signatures accept an unqualified object receiver, so an explicit call can bypass instance
  typing. With the installed evaluator, calling `LazySeq.Reset(map)` on a one-entry Map succeeds
  and changes `map.Count` from 1 to 0 by overwriting Map fields as LazySeq cursor state.
- **Impact:** a wrong explicit receiver can read pointer data as indices, corrupt unrelated heap
  objects, or dispatch a garbage callback pointer instead of producing a type trap.
- **Source:** `alloc_lazyseq()`, the `rt_lazyseq_*` methods, and IL wrappers in
  `src/runtime/oop/rt_lazyseq.c`.
- **Review (2026-07-15):** Verified and fixed. LazySeq nodes now carry a real class id
  (`RT_LAZYSEQ_CLASS_ID`, added to rt_collection_ids.h) instead of 0, and every IL trampoline
  routes its receiver through a new `as_lazyseq()` checked cast (`rt_obj_is_instance` +
  payload-size check, the same idiom the other collections use), so a wrong explicit receiver
  traps with `LazySeq: invalid LazySeq object` instead of overwriting foreign object fields.
  Regression: `test_wrapper_receiver_validation` in `RTLazySeqTests.cpp` (a Seq receiver
  traps). **Resolved.**

### VDOC-091 — LazySeq collectors return borrowing Seqs

- **Classification:** ownership/API inconsistency
- **Area:** `Viper.Functional.LazySeq.Repeat`, `ToSeq`, and `ToSeqN`
- **Evidence:** Repeat stores its value without retaining it or registering a traversal edge.
  Both collectors allocate with the internal borrowing `rt_seq_new()` /
  `rt_seq_with_capacity()` helpers, not the owning public constructors. Pushing collected values
  therefore adds no strong element references.
- **Impact:** materializing a LazySeq does not make its elements independently long-lived. A
  collected Seq can contain dangling object pointers after the original owner is released, even
  though ordinary publicly constructed Seqs own their entries.
- **Source:** `rt_lazyseq_repeat()`, `rt_lazyseq_to_seq()`, and `rt_lazyseq_to_seq_n()` in
  `src/runtime/oop/rt_lazyseq.c`.
- **Review (2026-07-15):** Verified and fixed. `Repeat` now retains its stored value
  (`rt_obj_retain_maybe`) and the finalizer gains a `LAZYSEQ_REPEAT` case releasing it; both
  collectors (`ToSeq`, `ToSeqN`) mark their result Seqs owning before pushing, including the
  empty early-return paths, so materialized elements hold strong references exactly like
  publicly constructed Seqs. Regression: `test_collectors_own_elements` in
  `RTLazySeqTests.cpp` asserts the node's retain plus three owning references after
  collecting `Repeat(x, 3)`. **Resolved.**

### VDOC-092 — LazySeq negative limits use inconsistent sentinels

- **Classification:** input-validation/API inconsistency
- **Area:** `Viper.Functional.LazySeq`
- **Evidence:** the API contract reserves repeat count `-1` for infinity, but Repeat treats every
  negative count as infinite. The installed evaluator confirms that `Repeat(value, -2).Take(2)`
  yields two values. In contrast, negative `Take` or `Drop` limits return null, a zero range step
  also returns null, and none of these invalid inputs traps or returns a Result/Option.
- **Impact:** a mistyped negative repeat count can create an unintended infinite source, while
  closely related invalid bounds silently replace a pipeline with null.
- **Source:** `rt_lazyseq_repeat()`, `rt_lazyseq_take()`, `rt_lazyseq_drop()`, and
  `rt_lazyseq_range()` in `src/runtime/oop/rt_lazyseq.c`.
- **Review (2026-07-15):** Verified and fixed with consistent trapping: `Repeat` traps for any
  count below `-1` (only the documented `-1` sentinel means infinite), `Take`/`Drop` trap on
  negative limits, and `Range` traps on a zero step — no more silent NULL pipelines or
  accidental infinite sources. Docs updated in functional.md. Regression:
  `test_invalid_limits_trap` in `RTLazySeqTests.cpp` (three trap cases plus the `-1`
  sentinel still working). **Resolved.**

### VDOC-093 — LazySeq allocation failure dereferences null

- **Classification:** confirmed allocation-path bug
- **Area:** `Viper.Functional.LazySeq`
- **Evidence:** `alloc_lazyseq()` calls `rt_obj_new_i64()` and immediately passes the result to
  `memset` before checking it. Every factory checks the returned node only after this helper has
  already dereferenced it.
- **Impact:** out-of-memory handling crashes in the allocator path instead of returning null or
  producing the runtime's normal allocation trap.
- **Source:** `alloc_lazyseq()` in `src/runtime/oop/rt_lazyseq.c`.
- **Review (2026-07-15):** Verified and fixed. `alloc_lazyseq()` now checks the
  `rt_obj_new_i64()` result before the `memset` and raises the runtime's standard allocation
  trap (`LazySeq: memory allocation failed`) on failure, matching the other collection
  allocators. Covered by the existing LazySeq suite (the failure path is OOM-only).
  **Resolved.**

### VDOC-094 — Iterator source cycles are invisible to cycle collection

- **Classification:** confirmed lifecycle bug
- **Area:** `Viper.Collections.Iterator`
- **Evidence:** live Seq/List/Ring iterators retain their source, and those collections can retain
  the iterator as an element. Iterator objects have a finalizer but are never registered with
  `rt_gc_track` and have no traversal callback exposing the retained source edge.
- **Impact:** storing a live iterator back into its source creates a reference cycle that the
  runtime cycle collector cannot traverse or reclaim.
- **Source:** `make_iter()` / `make_iter_snapshot()` in
  `src/runtime/collections/rt_iter.c`, compared with the tracked traversal callbacks on Seq, List,
  and Ring.
- **Review (2026-07-15):** Verified and fixed. Iterators now register with the cycle collector:
  a new `iter_traverse()` exposes the retained `source` edge (live sources and owned
  snapshots alike), and both `make_iter()` and `make_iter_snapshot()` call `rt_gc_track()`
  after installing the finalizer — the same pattern Seq/List/Ring use — so an
  iterator-stored-in-its-source cycle is traversable and reclaimable. Existing iterator and
  GC suites pass. **Resolved.**

### VDOC-095 — LazySeq null elements collide with the exhaustion sentinel

- **Classification:** API inconsistency
- **Area:** `Viper.Functional.LazySeq.Next` and `Peek`
- **Evidence:** Repeat accepts a null value and treats it as a successfully yielded element, but
  the public Next/Peek wrappers discard the internal `has_more` flag and also return null at end of
  stream. Unlike the collection pop APIs, LazySeq has no `NextOption`/`PeekOption` alternative.
- **Impact:** a returned null alone cannot tell callers whether a null element was consumed or the
  stream ended. Callers must compare cursor/exhaustion state around the operation.
- **Source:** `rt_lazyseq_next()`, `rt_lazyseq_peek()`, `rt_lazyseq_w_next()`, and
  `rt_lazyseq_w_peek()` in `src/runtime/oop/rt_lazyseq.c`.
- **Review (2026-07-15):** Verified and fixed by adding the missing Option counterparts:
  `LazySeq.NextOption` / `LazySeq.PeekOption` (registered as `obj<Viper.Option>`, backed by
  `rt_lazyseq_w_next_option` / `rt_lazyseq_w_peek_option`) return `Some(value)` for every
  yielded element — including null — and `None` only at exhaustion, matching the collection
  pop APIs' convention. The plain `Next`/`Peek` remain unchanged for compatibility.
  Completeness and surface audits pass; generated docs refreshed; functional.md updated.
  Regression: `test_next_option_null_elements` in `RTLazySeqTests.cpp` (`Repeat(null, 2)`
  yields Some, Some, None). **Resolved.**

### VDOC-096 — Several collection boolean C returns disagree with the registry ABI

- **Classification:** runtime ABI inconsistency
- **Area:** `Viper.Collections.OrderedMap.IsEmpty`, `OrderedMap.Has`, `DefaultMap.Has`, and
  `BloomFilter.MightContain`
- **Evidence:** the public C declarations and definitions of `rt_orderedmap_is_empty()`,
  `rt_orderedmap_has()`, `rt_defaultmap_has()`, and `rt_bloomfilter_might_contain()` return
  `int64_t`. Their runtime-registry entries declare `i1`, whose corresponding collection
  implementations otherwise return `int8_t`.
- **Impact:** generated/interpreted calls are compiled for a one-byte boolean return while the
  linked implementation has an eight-byte return contract. The low-bit result happens to work on
  the currently exercised ABI, but the declaration mismatch is not a portable C/foreign-function
  contract and can produce target-dependent behavior.
- **Source:** `src/runtime/collections/rt_orderedmap.[ch]`, `rt_defaultmap.[ch]`, and the matching
  entries in `src/il/runtime/defs/api/collections.def` and `core_crypto.def`, plus
  `rt_bloomfilter.[ch]` and its `collections.def` entry.
- **Review (2026-07-15):** Verified and fixed. All four functions —
  `rt_orderedmap_is_empty()`, `rt_orderedmap_has()`, `rt_defaultmap_has()`, and
  `rt_bloomfilter_might_contain()` — now declare and define `int8_t` returns, matching their
  registered `i1` signatures and the convention every other collection boolean uses. No
  VM/bytecode bridge carried a conflicting extern. Existing OrderedMap/DefaultMap/Bloom
  suites pass unchanged. **Resolved.**

### VDOC-097 — OrderedMap installs the same finalizer twice

- **Classification:** implementation inconsistency
- **Area:** `Viper.Collections.OrderedMap` construction
- **Evidence:** `rt_orderedmap_new()` calls `rt_obj_set_finalizer(m, orderedmap_finalizer)` twice in
  succession before registering GC traversal. `rt_obj_set_finalizer()` replaces the single stored
  callback, so the second call currently just overwrites it with the same pointer.
- **Impact:** there is no observed double-finalization today, but the duplicated lifecycle setup is
  dead code and is fragile if either line is later changed to install a wrapper or a different
  cleanup stage.
- **Source:** `rt_orderedmap_new()` in `src/runtime/collections/rt_orderedmap.c` and
  `rt_obj_set_finalizer()` in `src/runtime/oop/rt_object.c`.
- **Review (2026-07-15):** Verified against the current source: `rt_orderedmap_new()` now
  contains exactly one `rt_obj_set_finalizer(m, orderedmap_finalizer)` call (immediately
  before `rt_gc_track`); the duplicated line was removed by an intervening change. No code
  change needed. **Resolved.**

### VDOC-098 — Map/set implementation-adjacent contracts describe the wrong storage and lifetime

- **Classification:** implementation-documentation inconsistency
- **Area:** Map, Set, OrderedMap, SortedSet, FrozenMap, FrozenSet, and TreeMap runtime comments
- **Evidence:** `rt_map.h` says Map uses open addressing and that callers own heap-allocated map
  lifetimes, while it uses separate bucket chains and GC-managed objects. `rt_set_new()` likewise
  calls its separate-chaining buckets open-addressed. `rt_orderedmap.h` says every operation is
  O(1) average even though `KeyAt` walks a linked list and snapshot/clear operations are linear.
  SortedSet's source invariant says `strcmp` ordering although the implementation compares full
  byte buffers with lengths. Several collection headers still say callers are responsible for
  freeing objects that are allocated through `rt_obj_new_i64()` and finalized by the runtime GC.
- **Impact:** maintainers can choose the wrong complexity model, key semantics, or ownership rule
  when extending these implementations or their generated/public documentation.
- **Source:** comments in `src/runtime/collections/rt_map.h`, `rt_set.c`, `rt_orderedmap.h`,
  `rt_sortedset.[ch]`, `rt_frozenmap.h`, `rt_frozenset.h`, and `rt_treemap.h`, compared with their
  implementations.
- **Review (2026-07-15):** Verified each claim and corrected all seven files: `rt_map.h` now
  says separate-chaining buckets and GC-managed lifetime (no caller free); `rt_set_new()`'s
  doc says separate chaining; `rt_orderedmap.h` scopes O(1) to keyed operations and notes
  `KeyAt`/snapshots/Clear are linear; `rt_sortedset.c` describes the length-aware
  byte-lexicographic order (embedded NULs participate) instead of `strcmp`; and the
  FrozenMap/FrozenSet/TreeMap ownership lines now state GC management with a runtime
  finalizer instead of caller-managed lifetimes. **Resolved.**

### VDOC-099 — CountMap `IncBy` violates its returned-count contract for non-positive increments

- **Classification:** confirmed API inconsistency
- **Area:** `Viper.Collections.CountMap.IncBy`
- **Evidence:** the method is documented and named as returning the new count, but its first guard
  returns 0 for every `n <= 0` without looking up the key. With an existing count of 5, both
  `IncBy("x", 0)` and `IncBy("x", -2)` return 0 while `Get("x")` remains 5 in the installed
  evaluator.
- **Impact:** callers cannot interpret the return as the post-operation count for all accepted
  `i64` arguments. Zero behaves unlike a normal no-op increment, and a negative amount is silently
  ignored rather than rejected or acting as a decrement.
- **Source:** `rt_countmap_inc_by()` in `src/runtime/collections/rt_countmap.c`, confirmed with the
  installed evaluator.
- **Review (2026-07-15):** Verified and fixed. `IncrementBy` now honors its returned-count
  contract across the full accepted domain: `n == 0` is a lookup no-op returning the current
  count (0 for missing keys), and negative amounts trap with
  `CountMap.IncrementBy: amount must be >= 0` (decrements go through the dedicated
  `Decrement` API). Regression: extended `test_inc_by` in `RTCountMapTests.cpp` (zero on
  existing and missing keys, negative traps, totals unchanged). **Resolved.**

### VDOC-100 — Specialized-map source contracts contain obsolete operations and ownership rules

- **Classification:** implementation-documentation inconsistency
- **Area:** BiMap, MultiMap, CountMap, IntMap, DefaultMap, LruCache, and SparseArray runtime comments
- **Evidence:** BiMap's implementation comments say missing lookups return NULL although they
  return owned empty strings. MultiMap's file header advertises removal of one value, but the
  public implementation only has `RemoveAll`. CountMap's header names nonexistent `top_n`, calls
  every operation O(1) despite sorting in `MostCommon`, and says callers own a GC-managed object.
  IntMap's file header says values are not retained while its implementation retains, releases,
  tracks, and traverses them. DefaultMap's header says `Get` never returns NULL even though a null
  default or a present null value does exactly that. LruCache's header says capacity zero is
  undefined and `Has` updates recency, while zero is deliberately unbounded and `Has` is a
  read-only hash lookup; its Values comment calls snapshot entries borrowed even though the Seq
  retains them. Several related headers also assign caller-managed lifetimes to GC-managed
  objects.
- **Impact:** implementation-adjacent guidance can cause nonexistent API work, incorrect lifetime
  handling, bad complexity assumptions, and wrong cache-recency behavior.
- **Source:** comments in `src/runtime/collections/rt_bimap.c`, `rt_multimap.c`, `rt_countmap.h`,
  `rt_intmap.c`, `rt_defaultmap.h`, `rt_lrucache.[ch]`, and the specialized-map headers, compared
  with their implementations and runtime registry.
- **Review (2026-07-15):** Verified each claim and corrected all of them: BiMap missing-lookup
  docs now say "owned empty string" (not NULL); the MultiMap header drops the nonexistent
  single-value removal; the CountMap header names `rt_countmap_most_common` (not `top_n`),
  scopes O(1) to keyed operations (MostCommon sorts, O(n log n)), and states GC-managed
  lifetime; the IntMap header documents value retention/release and GC traversal; the
  DefaultMap header admits `Get` can return NULL through a null default or stored null; the
  LruCache header states `Has` is read-only (no recency promotion) and capacity 0 means
  unbounded, and its Values doc describes an owning snapshot. Caller-managed-lifetime claims
  on GC-managed objects were replaced throughout. **Resolved.**

### VDOC-101 — `I64Buffer` arithmetic uses unchecked signed C overflow

- **Classification:** confirmed runtime correctness / portability bug
- **Area:** `Viper.Collections.I64Buffer`
- **Evidence:** `AddScalar`, `MulScalar`, `AddBuffer`, `Sum`, and `Dot` perform their additions and
  multiplications directly in `int64_t`. They have no checked-arithmetic helper or precondition,
  while signed integer overflow is undefined behavior in C and no Viper overflow trap is raised.
- **Impact:** inputs whose intermediate or final result exceeds signed 64-bit range can produce
  optimizer- and target-dependent behavior rather than a defined wrap or trap. `Dot` can overflow
  in either its multiplication or accumulation, and mutating methods can partially update a buffer
  before later arithmetic fails unpredictably.
- **Source:** `rt_i64buf_add_scalar()`, `rt_i64buf_mul_scalar()`,
  `rt_i64buf_add_buffer()`, `rt_i64buf_sum()`, and `rt_i64buf_dot()` in
  `src/runtime/collections/rt_numbuf.c`.
- **Review (2026-07-15):** Verified and fixed. Added `numbuf_checked_add_i64()` /
  `numbuf_checked_mul_i64()` (compiler builtins on GCC/Clang, manual divide-bound checks on
  MSVC — the rt_time.c idiom) and routed all five operations through them: `AddScalar`,
  `MulScalar`, and `AddBuffer` trap with per-method overflow messages before writing the
  overflowing element, `Sum` traps on accumulation overflow, and `Dot` checks both the
  per-element product and the accumulation. Regression: `test_i64_overflow_traps` in
  `RTNumBufTests.cpp` (three trap probes plus in-range sanity). **Resolved.**

### VDOC-102 — `Bag.ToSet` creates string entries that ordinary Set lookups cannot match

- **Classification:** confirmed collection-conversion bug
- **Area:** `Viper.Collections.Bag.ToSet`
- **Evidence:** `rt_bag_to_set()` inserts the raw runtime strings returned by `Bag.Items` directly
  into the generic Set. Set compares value equality only for its recognized boxed scalar tags;
  other object values use identity. In the installed evaluator, converting a Bag containing
  `"apple"` produces a Set with `Count == 1`, but
  `set.Has(Viper.Core.Box.Str("apple")) == false`. Looking up the exact object obtained from
  `set.Items()` succeeds only by identity.
- **Impact:** the advertised Bag-to-Set conversion cannot be used for normal boxed-string
  membership, deduplication against new strings, or value-based set algebra. Callers must retain
  the Bag or rebuild a Set from explicitly boxed strings.
- **Source:** `rt_bag_to_set()` in `src/runtime/collections/rt_convert_coll.c` and Set equality/hash
  logic in `src/runtime/collections/rt_set.c`, confirmed with the installed evaluator.
- **Review (2026-07-15):** Verified and fixed. `rt_bag_to_set()` now boxes each raw Bag string
  (`rt_box_str`) before inserting, so entries participate in the Set's boxed-string value
  equality and content hashing — `Has(Box.Str("apple"))` matches after conversion, and
  value-based dedup/set algebra work. The loop releases the temporary box reference after the
  Set retains its own (Seq snapshot elements are borrowed, not retained). Regression:
  `test_to_set_boxed_membership` in `RTBagTests.cpp`. **Resolved.**

### VDOC-103 — `Trie.LongestPrefix` cannot distinguish an empty-key match from no match

- **Classification:** API inconsistency
- **Area:** `Viper.Collections.Trie.LongestPrefix`
- **Evidence:** Trie accepts the empty string as a terminal key at its root. `LongestPrefix`
  returns an owned empty string both when that root key is the longest match and when no key
  matches; the method has no Option form or separate success flag.
- **Impact:** callers cannot infer from the returned string whether the valid empty key matched.
  They must separately query `Has("")` when that distinction is relevant.
- **Source:** empty-key handling in `rt_trie_set()` and the `found`/`last_match` result path in
  `rt_trie_longest_prefix()` in `src/runtime/collections/rt_trie.c`.
- **Review (2026-07-15):** Verified and fixed by adding the missing Option form:
  `Trie.LongestPrefixOption` (registered as `obj<Viper.Option>(str)`, backed by
  `rt_trie_longest_prefix_option`) returns `Some(prefix)` for any match — including the
  valid empty-key match — and `None` when no stored key is a prefix, so the ambiguity of the
  string-returning `LongestPrefix` is resolved without changing it. Completeness/surface
  audits pass; generated docs refreshed. Regression: `test_longest_prefix_option` in
  `RTTrieTests.cpp` (None with empty trie, Some("") after inserting the empty key).
  **Resolved.**

### VDOC-104 — BitSet staged growth breaks logical bounds and can corrupt heap memory

- **Classification:** confirmed runtime memory-safety bug
- **Area:** `Viper.Collections.BitSet.SetAll`, `And`, `Or`, and `Xor`
- **Evidence:** growth can allocate more words than the logical `bit_count` requires. Starting
  from 64 bits, `Set(64)` grows to two words and `Set(128)` doubles that allocation to four words
  while logical `Length` becomes 129 (three words). `SetAll()` fills all four allocated words and
  masks the last *allocated* word rather than the last logical word; `Count()` also scans all four.
  The installed evaluator consequently reports `Length == 129`, `Count == 193`, and a 129-character
  `ToString()`. Separately, `And`/`Or`/`Xor` size their result to three logical words but iterate to
  the operands' four-word `min_words`, writing `result.words[3]` out of bounds.
- **Impact:** `Count <= Length` is violated, out-of-range capacity bits become observable, and
  binary operations on two staged-grown operands can overwrite heap memory. Preallocating the
  maximum logical size avoids spare words as a temporary workaround.
- **Source:** `bitset_grow()`, `rt_bitset_set_all()`, `bitset_popcount()`, and
  `rt_bitset_{and,or,xor}()` in `src/runtime/collections/rt_bitset.c`; count behavior confirmed with
  the installed evaluator.
- **Review (2026-07-15):** Verified and fixed by separating logical size from allocated
  capacity: a new `bitset_logical_words()` (= WORDS_FOR_BITS(bit_count)) now bounds every
  whole-set walk. `SetAll` fills and masks only the logical words (spare capacity stays
  zero), popcount/`Count`/`IsEmpty` scan logical words, and `And`/`Or`/`Xor`/`Not` derive
  their loop bounds from the operands' logical word counts clamped by the result's
  allocation — eliminating both the `Count > Length` violation and the out-of-bounds
  `result.words[3]` write. Regression: `test_staged_growth_bounds` in `RTBitSetTests.cpp`
  reproducing the finding's 64→129-bit staged-growth scenario (`Count == 129`, binary ops
  correct and in-bounds). **Resolved.**

### VDOC-105 — Specialized-structure source contracts are stale or contradictory

- **Classification:** implementation-documentation inconsistency
- **Area:** Bag, BloomFilter, Trie, UnionFind, BitSet, and Bytes runtime comments
- **Evidence:** Bag's header calls its separate-chaining table open-addressed, assigns its
  GC-managed object to caller lifetime management, and implementation examples use obsolete
  `Put`/`Drop`/`Len`/`Merge`/`Common` names. BloomFilter's header says merge requires identical
  constructor parameters although only derived bit/hash counts are compared. UnionFind's header
  omits invalid-index failure from `Union`'s zero return. BitSet's implementation header says
  binary operands require equal lengths and `Not` is in-place, although unequal lengths are
  accepted and every operation returns a new object; its public header also assigns a GC object
  to caller lifetime management. Trie's node comment says a value is non-null exactly at terminal
  nodes even though null values are valid, and its public header again gives callers object
  lifetime responsibility. Bytes' header says writes clamp, hex output is uppercase, and callers
  own the object, while writes truncate to low bits, output is lowercase, and the object is
  GC-managed.
- **Impact:** maintainers can implement against obsolete names, the wrong collision strategy,
  false merge/length preconditions, or incorrect object and value-lifetime rules.
- **Source:** comments in `src/runtime/collections/rt_bag.[ch]`, `rt_bloomfilter.h`,
  `rt_unionfind.h`, `rt_bitset.[ch]`, `rt_trie.[ch]`, and `rt_bytes.h`, compared with their
  implementations and registry.
- **Review (2026-07-15):** Verified each claim and corrected all six groups: Bag's header now
  says separate chaining and GC-managed lifetime, and its examples use the registered
  StringSet names (`Add`/`Remove`/`Count`/`Union`/`Intersect`); BloomFilter's merge doc states
  the real precondition (equal derived bit/hash counts); UnionFind's `Union` doc includes the
  invalid-index zero return; BitSet's header states that And/Or/Xor accept unequal lengths
  and that all binary ops (including Not) return new objects, plus GC-managed lifetime; the
  Trie node comment allows NULL values at terminal nodes and its header drops caller lifetime
  responsibility; Bytes' header now says writes truncate to the low 8 bits, hex output is
  lowercase, and objects are GC-managed. **Resolved.**

### VDOC-106 — Bytes constructor signatures disagree inside the runtime manifest

- **Classification:** registry-source inconsistency
- **Area:** `Viper.Collections.Bytes` construction metadata
- **Evidence:** the class declaration in `src/il/runtime/defs/classes/foundation.def` supplies
  constructor signature `obj` for `BytesNew`, while the matching API entry in
  `src/il/runtime/defs/api/collections.def` correctly declares `obj(i64)`. The installed compiler
  currently resolves the API signature: `new Bytes()` is rejected for missing `len`, while
  `new Bytes(4)` succeeds.
- **Impact:** the two manifest sources disagree about a required constructor argument, yet the
  registry audit does not flag it. A generator or consumer that trusts the class-side signature
  can emit an invalid no-argument contract even though current Zia call resolution happens to use
  the correct API declaration.
- **Source:** the Bytes entries in `foundation.def` and `collections.def`, confirmed with the
  installed evaluator and `viper --dump-runtime-api`.
- **Review (2026-07-15):** Verified and fixed. The `RT_CLASS_BEGIN` row for
  `Viper.Collections.Bytes` now declares `obj(i64)`, matching the API-side `BytesNew`
  signature and the convention its sibling sized constructors (BitSet, F64Buffer, I64Buffer)
  already follow. Completeness/surface audits pass and generated docs were refreshed.
  **Resolved.**

### VDOC-107 — Several collection counts lack their advertised overflow trap

- **Classification:** runtime hardening inconsistency / needs triage
- **Area:** collection insertion and public `Count`/`Length` values
- **Evidence:** Bag and IntMap store `count` as `size_t`; their insertion paths skip the resize
  calculation when `count == SIZE_MAX` but still insert and execute `count++`, which wraps.
  Unbounded LruCache computes `count + 1` and later increments without a maximum guard. Trie also
  increments an unchecked `size_t` for every new terminal key. Their public length functions cast
  the result to signed `int64_t`, so values above `INT64_MAX` become implementation-defined before
  the underlying counters reach their wrap point. Other collections such as Map, Set, Seq, Deque,
  Heap, CountMap, and MultiMap explicitly guard their public count limit.
- **Impact:** the repository cannot make a universal “all collection growth traps instead of
  wrapping” guarantee. These limits are not realistically reachable on ordinary machines because
  entry storage exhausts memory first, but the missing guards defeat defensive invariants and can
  matter to fuzzed/custom allocators or future compact representations.
- **Source:** insertion and length paths in `src/runtime/collections/rt_bag.c`, `rt_intmap.c`,
  `rt_lrucache.c`, and `rt_trie.c`, compared with the explicit count guards in the other listed
  collections.
- **Review (2026-07-15):** Verified and fixed. All four insertion paths now trap at the count
  ceiling before mutating: StringSet/Bag `Add` (frees the prepared entry first), `IntMap.Set`
  (guard sits before value retention/entry allocation), `LRUCache.Put` (releases the prepared
  node/value on the trap path), and `Trie.Set` (guards before creating a new terminal). The
  guard uses `count >= INT64_MAX`, which also keeps the public signed `Count`/`Length` casts
  well-defined — matching the Map/Set/Seq/Deque/Heap/CountMap/MultiMap convention. The limits
  are unreachable in practice (memory exhausts first), so coverage is by the existing suites.
  **Resolved.**

### VDOC-108 — `SemanticJob.IsDone(null)` depends on which Zia bridge is linked

- **Classification:** API inconsistency
- **Area:** `Viper.Zia.SemanticJob`
- **Evidence:** the strong editor-service implementation returns true only when a validated
  `SemanticJobHandle` contains a job whose `done` flag is set, so null and wrong-type handles
  return false. The weak runtime stub ignores its argument and always returns true. Every weak
  `Begin*` function returns null.
- **Impact:** identical polling code can wait forever on an invalid handle in a full-service
  binary but treat the same handle as completed in a weak-stub binary. Callers must test the
  `Begin*` result for null before polling rather than using `IsDone` as the validity check.
- **Source:** `rt_zia_semantic_job_is_done()` in
  `src/frontends/zia/rt_zia_completion.cpp` and
  `src/runtime/core/rt_zia_completion_stub.c`.
- **Review (2026-07-15):** Verified and fixed. The weak stub now returns 0 for every handle,
  matching the strong editor-service semantics (`IsDone(null/invalid) == false`) for every
  input the stub can actually produce — its `Begin*` functions all return null, so callers
  must check the Begin result, which is now the documented contract in both bridges. Docs
  updated in zia.md. **Resolved.**

### VDOC-109 — Zia document-mirror deltas do not enforce revision or range validity

- **Classification:** confirmed tooling correctness bug
- **Area:** `Viper.Zia.Document.SyncDelta`
- **Evidence:** `mirrorApplyDeltasJson()` parses but deliberately ignores every delta's `r`
  revision. `rt_zia_doc_sync_delta()` never compares `end_revision` with the mirror's stored
  revision. Its position helper clamps lines/columns beyond the buffer, accepts negative
  coordinates as the start, and converts a reversed range into an insertion. With the existing
  force-loaded `zia` binary, a mirror initialized to `"abc"` at revision 10 accepted a revision-1
  delta and returned `true`, producing `"Xbc"`; a subsequent edit at line/column 99 also returned
  `true` and produced `"XbcY"`.
- **Impact:** stale, out-of-order, or corrupt journal data can silently diverge the language
  service mirror while reporting success. The current safe usage depends entirely on caller-side
  revision bookkeeping and a full-sync fallback.
- **Source:** `mirrorOffsetOf()`, `mirrorApplyDeltasJson()`, and
  `rt_zia_doc_sync_delta()` in `src/frontends/zia/rt_zia_completion.cpp`, confirmed with the
  existing `build/src/tools/zia/zia` binary.
- **Review (2026-07-15):** Verified and fixed. `SyncDelta` now enforces revision monotonicity —
  the batch's `end_revision` must not move the mirror backwards, and each delta's `r` must lie
  strictly after the mirror's stored revision (and within `end_revision`), applied in
  increasing order. Positions go through a new strict resolver
  (`mirrorOffsetOfStrict`) that rejects negative coordinates, lines past the buffer, and
  columns past end-of-line instead of clamping, and a reversed range is rejected as corrupt
  rather than treated as an insertion. Every rejection returns 0, which routes callers to
  their documented full-sync fallback, so stale/corrupt journal data can no longer silently
  diverge the mirror. Regression: new `test_zia_doc_mirror` target (stale revision,
  out-of-range coordinates, reversed range, valid delta). **Resolved.**

### VDOC-110 — Zia document-symbol queries emit runtime and imported-file symbols

- **Classification:** confirmed tooling/API and performance bug
- **Area:** `Viper.Zia.Completion.Symbols*` and asynchronous symbol jobs
- **Evidence:** both symbol serializers enumerate `Sema::getGlobalSymbols()`, whose module scope
  contains the runtime-function inventory and imported exports, then append `getTypeNames()` a
  second time. A direct existing-binary probe containing one function and one class produced
  7,370 rows: 7,363 `Viper.*` runtime-function rows and two `Cat` rows. A second path-aware probe
  bound a file whose `importedThing` was declared on line 3; the active file's wire result included
  `importedThing` with line 3 even though the four-field protocol has no source-path field.
- **Impact:** each document-symbol request serializes and transports a registry-sized payload.
  Consumers can discard line-zero runtime and duplicate-type rows, as ViperIDE does, but cannot
  distinguish positive-line imported declarations from declarations in the active file; outline
  entries and workspace-symbol locations can therefore point into the wrong document.
- **Source:** `symbolsForSource()` and `rt_zia_symbols_for_file()` in
  `src/frontends/zia/rt_zia_completion.cpp`, `Sema::getGlobalSymbols()` in
  `src/frontends/zia/Sema_Completion.cpp`, and existing-binary probes.
- **Review (2026-07-15):** Verified and fixed. Both serializers now emit only symbols whose
  declaration belongs to the active file (`sym.decl && sym.decl->loc.file_id == fileId`),
  which drops the ~7,000 declaration-less runtime-registry rows and all imported-file
  declarations that the four-field protocol could not attribute; the duplicate
  `getTypeNames()` append was removed since user-declared types already appear in the
  filtered globals. This also fixed the latent `test_zia_bugfixes` expectation for
  `Template.Keys.Count` (now StringSet-typed per VDOC-044). Regression:
  `ZiaDocSymbols.OnlyActiveFileSymbols` in `test_zia_doc_mirror.cpp` (Cat/start present, no
  `Viper.*` rows, payload under 20 lines). **Resolved.**

### VDOC-111 — Legacy completion rows cannot frame multiline snippets

- **Classification:** confirmed wire-format bug
- **Area:** `Viper.Zia.Completion.Complete*`
- **Evidence:** the documented serializer emits
  `label<TAB>insertText<TAB>kind<TAB>detail<LF>` without escaping any field. Built-in snippet
  insertion text contains literal newlines—for example, the `if` snippet is
  `"if  {\n    \n}"`—so one completion item becomes several apparent rows with inconsistent
  field counts.
- **Impact:** a client that parses the advertised newline-delimited protocol cannot reconstruct
  snippet items reliably. `Items*` and `BeginItemsForFile` preserve the insertion text in a Map
  and are the current workaround.
- **Source:** `kSnippets`, `serializeItem()`, and `serialize()` in
  `src/frontends/zia/ZiaCompletion.cpp`.
- **Review (2026-07-15):** Verified and fixed. `serializeItem()` now escapes every field —
  backslash, tab, newline, and CR become `\\`, `\t`, `\n`, `\r` — so a multiline snippet
  like the `if` template occupies exactly one wire row with exactly three tab delimiters;
  consumers split on the literal delimiters and unescape. The protocol doc in
  `ZiaCompletion.hpp` describes the escaping. `Items*`/`BeginItemsForFile` (the Map-based
  path most consumers use) are unaffected. Regression:
  `CompletionEngine.Serialize_EscapesMultilineSnippets` in
  `test_zia_completion_engine.cpp`. **Resolved.**

### VDOC-112 — Weak Zia structured payloads omit fields present in the full service

- **Classification:** API/schema inconsistency
- **Area:** weak Zia completion and signature stubs
- **Evidence:** every full-service completion-item map contains `cursorOffset`, including `-1`
  for ordinary items, but `zia_stub_completion_items_unavailable()` omits the key. Full-service
  signature maps always contain an `overloads` Seq, even on a miss; the weak
  `zia_stub_signature_unavailable_map()` omits it. Other fields advertise the same structured
  result shapes.
- **Impact:** consumers cannot treat the structured Map schema as link-configuration invariant.
  A direct integer getter can interpret the missing cursor offset as 0 instead of -1, and code
  expecting an overload sequence receives null. Default-valued Map access avoids the immediate
  failures but does not restore a stable contract.
- **Source:** `completionItemToMap()` and `signatureInfoMap()` in
  `src/frontends/zia/rt_zia_completion.cpp` versus the matching builders in
  `src/runtime/core/rt_zia_completion_stub.c`.
- **Review (2026-07-15):** Verified and fixed. The weak stub's completion item now carries
  `cursorOffset = -1` and its signature map carries an empty `overloads` Seq, so the
  structured Map schema is identical regardless of which bridge is linked. zia.md updated
  (also refreshed the now-fixed VDOC-109/110/111 passages). Regression:
  `test_stub_schema_parity` in `RTZiaCompletionStubTests.cpp`. **Resolved.**

### VDOC-113 — Zia diagnostic and mirror APIs conflate absence with a clean result

- **Classification:** API inconsistency / needs triage
- **Area:** weak Zia services and `Viper.Zia.Document`
- **Evidence:** the weak `Completion.Check*` functions return an empty String and weak
  `Toolchain.Check*` functions return an empty Seq for every source, exactly like a successful
  clean analysis. A weak compile returns `success = false` with empty diagnostics and no
  unavailable reason. Independently, `Document.Text` returns empty for both an absent mirror and
  a present empty document, while synchronous `Document.CheckForFile` returns empty for an absent
  mirror and a clean mirrored document.
- **Impact:** callers cannot establish from these results whether analysis ran, whether a mirror
  exists, or whether source is clean. Async begin methods expose null for absence/unavailability,
  but there is no equivalent status on the synchronous methods.
- **Source:** weak implementations in `src/runtime/core/rt_zia_completion_stub.c` and document
  accessors in `src/frontends/zia/rt_zia_completion.cpp`.
- **Review (2026-07-15):** Verified and fixed by adding two definitive probes rather than
  changing the compatibility payloads: `Viper.Zia.Completion.IsAvailable` (strong bridge
  returns true, weak stub false) lets callers know whether an empty Check/Seq means "clean"
  or "analysis never ran", and `Viper.Zia.Document.Has(path)` separates an absent mirror from
  a present-but-empty document (weak stub: always false). Both registered with completeness
  and surface audits green; zia.md documents how to combine them with the synchronous
  methods. Regressions: `ZiaDocMirror.HasDistinguishesAbsentFromEmpty` (strong) and
  `test_stub_availability_probe` (weak). **Resolved.**

### VDOC-114 — ProjectIndex rename validation does not match the Zia lexer

- **Classification:** confirmed tooling/compiler inconsistency
- **Area:** `Viper.Zia.ProjectIndex.RenameEdits`
- **Evidence:** `isIdentifierText()` uses locale-sensitive `std::isalpha`/`std::isalnum` and has no
  length limit. The Zia lexer uses deterministic ASCII-only `isLetter`/`isDigit` helpers and
  rejects identifiers longer than 1,024 bytes. Therefore RenameEdits can accept a replacement
  that becomes a lexer error after its returned edits are applied; host locale can also change
  which non-ASCII bytes the rename validator accepts.
- **Impact:** a successful rename result does not guarantee syntactically valid Zia output, and
  acceptance can differ across statically linked IDE hosts. Callers must currently impose the
  lexer's ASCII and length rules themselves.
- **Source:** `isIdentifierText()` in `src/frontends/zia/rt_zia_completion.cpp`,
  `Lexer::lexIdentifierOrKeyword()` in `src/frontends/zia/Lexer.cpp`, and
  `src/frontends/common/CharUtils.hpp`.
- **Review (2026-07-15):** Verified and fixed. `isIdentifierText()` now uses the lexer's own
  deterministic ASCII classification (`il::frontends::common::char_utils::isIdentifierStart/`
  `Continue`) plus the lexer's 1,024-byte cap, so an accepted rename can never yield a lexer
  error and acceptance no longer varies with the host `LC_CTYPE`. Docs updated in zia.md.
  Regression: `ZiaProjectIndex.RenameEnforcesLexerIdentifierRules` (UTF-8 byte rejected,
  1025 bytes rejected, 1024-byte identifier accepted). **Resolved.**

### VDOC-115 — Document-delta integer parsing can overflow signed `long`

- **Classification:** confirmed runtime undefined-behavior bug
- **Area:** `Viper.Zia.Document.SyncDelta`
- **Evidence:** the compact JSON parser accumulates every non-text numeric field as
  `v = v * 10 + digit` in signed `long`, with no digit-count or overflow check, before narrowing
  to `int`. A sufficiently long numeric literal therefore invokes signed C++ overflow instead of
  being classified as malformed input.
- **Impact:** malformed or adversarial delta JSON can produce optimizer- and target-dependent
  behavior inside an in-process editor service. The producer normally emits bounded editor
  coordinates, but the public runtime method accepts arbitrary Strings.
- **Source:** numeric-field parsing in `mirrorApplyDeltasJson()` in
  `src/frontends/zia/rt_zia_completion.cpp`.
- **Review (2026-07-15):** Verified and fixed. Numeric-field accumulation is now capped at 10
  digits (comfortably within `long`, far beyond any real editor coordinate or revision);
  longer literals classify as malformed input and return false, which triggers the caller's
  full-sync fallback — no signed overflow path remains. Regression:
  `ZiaDocMirror.OversizedNumericLiteralRejected` in `test_zia_doc_mirror.cpp` (26-digit
  literal rejected, mirror unchanged). **Resolved.**

### VDOC-116 — Asynchronous Zia diagnostics drop all but the first fix-it

- **Classification:** API/schema inconsistency
- **Area:** `Viper.Zia.Toolchain` versus `Viper.Zia.SemanticJob.Diagnostics`
- **Evidence:** synchronous `Check*` and `Compile*` maps contain a `fixits` Seq plus the legacy
  first-fix fields. The native `DiagnosticRecord` used by background jobs stores only one fix-it,
  and `diagnosticRecordsToSeq()` consequently omits the `fixits` key altogether.
- **Impact:** changing a diagnostic request from synchronous to asynchronous changes its Map
  schema and silently removes additional machine-applicable fixes. IDE code-action behavior can
  depend on scheduling choice rather than diagnostic content.
- **Source:** `diagnosticToMap()`, `DiagnosticRecord`, `diagnosticToRecord()`, and
  `diagnosticRecordsToSeq()` in `src/frontends/zia/rt_zia_completion.cpp`.
- **Review (2026-07-15):** Verified and fixed. `DiagnosticRecord` now carries a
  `std::vector<FixitRecord>` with every machine-applicable fix, `diagnosticToRecord()`
  populates it (deriving the legacy first-fix fields from the same list), and
  `diagnosticRecordsToSeq()` emits a `fixits` Seq with the exact key set the synchronous
  `fixitsToSeq()` uses — so async `SemanticJob.Diagnostics` maps are schema-identical to
  `Check*`/`Compile*`. zia.md updated. Full zia suite (333 tests) passes. **Resolved.**

### VDOC-117 — Audio-disabled mix-group initialization and registration are unsynchronized

- **Classification:** confirmed data-race bug
- **Area:** `Viper.Audio.Mixer` mix-group settings in audio-disabled builds
- **Evidence:** the real-audio group methods hold `audio_state_lock()` around lazy initialization,
  name lookup, registration, and volume state. Their audio-disabled counterparts call the same
  `_unlocked` helpers with no lock. Atomic loads/stores protect an already-initialized group's
  volume value, but `g_group_names_initialized`, `g_group_names`, and `g_group_in_use` remain plain
  shared objects. Two first-use or named-group calls can therefore race while clearing, filling,
  or allocating the tables.
- **Impact:** settings code that is safe beside playback in an audio-enabled runtime can have
  undefined behavior in a headless/audio-disabled runtime, including duplicate allocation or
  corrupted name/in-use state. Callers must currently serialize first use and every named-group
  operation themselves in that configuration.
- **Source:** the two implementations of `rt_audio_set_group_volume()`,
  `rt_audio_register_group()`, `rt_audio_find_group()`, and related helpers in
  `src/runtime/audio/rt_audio.c`.
- **Review (2026-07-15):** Verified and fixed. The audio-state spinlock
  (`g_audio_init_lock` + `audio_state_lock/unlock`) was hoisted out of the
  `VIPER_ENABLE_AUDIO` guard so both configurations share it, and the audio-disabled group
  functions (`SetGroupVolume`, `GetGroupVolume`, `RegisterGroup`, `FindGroup`, `GroupName`)
  now hold it around lazy initialization, lookup, registration, and volume access — the same
  discipline the real-audio path already used. The audio-disabled branch was
  syntax-verified with a no-`VIPER_ENABLE_AUDIO` compile; the enabled-build audio suites
  pass unchanged. **Resolved.**

### VDOC-118 — Distinct public mix-group names silently alias after 31 bytes

- **Classification:** API inconsistency / needs triage
- **Area:** named `Viper.Audio.Mixer` mix groups
- **Evidence:** every name is trimmed at its space/tab edges, embedded NUL bytes are replaced with
  `_`, and the result is copied into a 32-byte C buffer with at most 31 payload bytes. Registration
  and lookup compare only that canonical buffer. Consequently two distinct runtime Strings with
  the same first 31 canonical bytes receive the same ID; truncation can also split a multibyte
  UTF-8 sequence, and `GroupName()` returns the stored truncated bytes rather than the original.
- **Impact:** user-defined categories can silently share volume, ducking, and effect state even
  though the public API accepts an unrestricted String and reports no truncation or collision.
  Use unique ASCII names of at most 31 bytes as a workaround.
- **Source:** `audio_group_copy_name()`, `audio_find_group_unlocked()`, and
  `audio_register_group_unlocked()` in `src/runtime/audio/rt_audio.c`.
- **Review (2026-07-15):** Verified and fixed. `audio_group_copy_name()` now rejects canonical
  names longer than 31 bytes (the buffer is left empty, so registration and lookup return -1,
  the same failure signal as an empty name) instead of truncating — distinct names can no
  longer silently alias one group, and no UTF-8 sequence can be split because nothing is
  truncated. audio.md updated (also reflects the VDOC-117 locking fix). **Resolved.**

### VDOC-119 — Playlist playback stalls permanently on an unloadable selected entry

- **Classification:** needs triage
- **Area:** `Viper.Audio.Playlist` auto-advance and failure reporting
- **Evidence:** paths are accepted without validation. If `playlist_load_current_music()` returns
  null, selection clears `playing`; `rt_playlist_update()` immediately returns whenever
  `!pl->playing || !pl->music`. It therefore cannot advance past the failed entry. `Play()` also
  has no result, diagnostic, or error property, so the only signal is `IsPlaying == false`, which
  is indistinguishable from an intentional stopped state.
- **Impact:** one missing, malformed, empty, or over-limit track defeats the advertised queue
  auto-advance behavior until application code explicitly calls `Next`, `Jump`, `Remove`, or
  repairs the entry. A playlist cannot implement robust skip-on-error playback by calling only
  `Play()` and `Update()`.
- **Source:** `playlist_load_current_music()`, `playlist_select_position()`,
  `rt_playlist_play()`, and `rt_playlist_update()` in `src/runtime/audio/rt_playlist.c`.
- **Review (2026-07-15):** Verified and fixed with bounded skip-on-error: both
  `playlist_select_position()` (the auto-advance path used by `Update()`→`Next()`) and
  `rt_playlist_play()` now retry subsequent entries when the selected one fails to load,
  using the same repeat-all wrap and reshuffle semantics as `Next`, capped at the track count
  so a playlist whose every entry fails stops cleanly (`IsPlaying == false`) instead of
  stalling on the first bad track. audio.md updated. Regression:
  `test_playlist_skip_on_error_terminates` in `RTAudioIntegrationTests.cpp` (three
  unloadable tracks under repeat-all: Play/Update terminate stopped). **Resolved.**

### VDOC-120 — Spatial and ordinary Sound playback use different failure sentinels

- **Classification:** API inconsistency
- **Area:** audio voice creation
- **Evidence:** `Sound.Play`, `PlayEx`, `PlayEx2`, `PlayLoop`, and group-aware variants return `-1`
  when playback cannot start. `SpatialAudio3D.PlayAt` converts every non-positive backend result
  to `0`; `SoundSource3D.Play` does the same and exposes 0 through `VoiceId` on failure.
- **Impact:** code generalized across 2D and 3D sound cannot use one success/failure convention.
  Testing `id != -1`, which is correct for Sound, mistakes a failed spatial play for a live voice;
  callers must instead use `id > 0` everywhere.
- **Source:** `rt_sound_play_internal()` in `src/runtime/audio/rt_audio.c`,
  `rt_sound3d_play_at()` in `src/runtime/audio/rt_sound3d.c`, and
  `rt_soundsource3d_play()` in `src/runtime/audio/rt_sound3d_objects.c`.
- **Review (2026-07-15):** Verified and fixed. Every play CALL now uses the -1 failure
  sentinel: `SpatialAudio3D.PlayAt` and `SoundSource3D.Play` return -1 (not 0) when playback
  cannot start, matching all `Sound.Play*` variants — voice ids start at 1, so `id > 0` and
  `id != -1` both work uniformly. `SoundSource3D.VoiceId` (object state, not a call result)
  deliberately keeps 0 as "no active voice", which existing fixtures depend on after `Stop`.
  Contract-test expectations and audio.md updated. `test_rt_sound3d_contract` renamed probe
  asserts the -1 sentinel. **Resolved.**

### VDOC-121 — SoundBank accepts detached Sound wrappers as successful registrations

- **Classification:** confirmed API-state validation bug
- **Area:** `Viper.Audio.SoundBank.RegisterSound`
- **Evidence:** `Audio.Shutdown()` deliberately leaves Sound wrappers in the registry while
  detaching their ViperAUD handles. `rt_sound_is_handle()` validates only wrapper membership and
  magic, so `rt_soundbank_register_sound()` retains such a detached wrapper and returns 1.
  `SoundBank.Play()` later forwards it to the stricter playback path, which returns -1 because
  the backend Sound is detached.
- **Impact:** a successful registration does not establish that the bank entry can ever play in
  the current audio context. Applications must discard and reload all Sound wrappers after
  shutdown rather than trusting `RegisterSound`'s result.
- **Source:** wrapper invalidation and `rt_sound_is_handle()` in
  `src/runtime/audio/rt_audio.c`, plus `rt_soundbank_register_sound()` in
  `src/runtime/audio/rt_soundbank.c`.
- **Review (2026-07-15):** Verified and fixed. Added `rt_sound_is_playable()` — a stricter
  companion to `rt_sound_is_handle()` that also requires the wrapper's ViperAUD handle to be
  attached to the live audio context (audio-disabled stub returns 0) — and
  `rt_soundbank_register_sound()` now uses it, so registering a Shutdown-detached zombie
  wrapper fails with 0 instead of succeeding and failing later inside `SoundBank.Play`.
  Symbol classified internal in the surface policy; audio suites pass and the disabled
  branch was syntax-verified. **Resolved.**

### VDOC-122 — Peaking EQ accepts values that poison its filter coefficients

- **Classification:** confirmed numeric-safety bug / needs triage
- **Area:** `Viper.Audio.Mixer.GroupAddPeaking`
- **Evidence:** the shared biquad setup sanitizes frequency and Q but passes `gainDb` directly to
  `pow(10, gainDb / 40)`. NaN, infinity, or sufficiently extreme finite values produce non-finite
  coefficients and state. The per-sample sanitizer then converts each non-finite output to zero,
  while the poisoned delay state remains non-finite.
- **Impact:** one accepted Float argument can make every sample through that group's active
  peaking insert silent until the effect is removed or cleared. Sibling effects clamp or replace
  their non-finite public inputs; peaking gain currently requires caller-side finite/range checks.
- **Source:** `biquad_set()`, `biquad_sample()`, and `rt_audio_fx_add_peaking()` in
  `src/runtime/audio/rt_audio_fx.c`.
- **Review (2026-07-15):** Verified and fixed. `biquad_set()` now sanitizes `gain_db` like the
  sibling effects sanitize their inputs: NaN/infinity fall back to 0 dB and finite values
  clamp to ±40 dB, so the coefficients and delay state stay finite and a single bad Float can
  no longer silence the group's peaking insert. Regression:
  `test_peaking_sanitizes_gain` in `RTAudioFxTests.cpp` (NaN gain keeps output finite and
  audible; 1e9 gain stays finite). **Resolved.**

### VDOC-123 — Sound loaders disagree on whether a missing asset is recoverable

- **Classification:** API failure-mode inconsistency
- **Area:** `Viper.Audio.Sound.Load` and `LoadAsset`
- **Evidence:** `Sound.Load(path)` returns null for a missing/unreadable file, invalid path, decode
  error, backend failure, or exhausted Sound slots. `Sound.LoadAsset(path)` traps with
  `Sound.LoadAsset: asset not found` when asset resolution returns no bytes (including a zero-byte
  asset), but returns null for decode/backend failures after successful resolution. Both public
  functions otherwise return the same nullable Sound type.
- **Impact:** switching a loader from filesystem resolution to the package-aware asset resolver
  changes a routine missing-file branch into a non-local trap, and the signature does not expose
  that distinction. Callers cannot uniformly test the result for null.
- **Source:** `rt_sound_load()` and `rt_sound_load_asset()` in
  `src/runtime/audio/rt_audio.c`.
- **Review (2026-07-15):** Verified and fixed. `Sound.LoadAsset` now returns null for a
  missing or zero-byte asset instead of trapping, matching `Sound.Load`'s treatment of a
  missing file — every failure mode of both loaders is now a null result, so callers can
  uniformly test the return. audio.md updated. Covered by the audio suites (the trap path
  had no dedicated test; the null path is the common headless outcome). **Resolved.**

### VDOC-124 — SpatialAudio3D signatures erase Sound and Vec3 argument types

- **Classification:** runtime-registry typing inconsistency
- **Area:** `Viper.Audio.SpatialAudio3D`
- **Evidence:** the registry declares `SetListener` as `void(obj,obj)`, `PlayAt` as
  `i64(obj,obj,f64,i64)`, and `UpdateVoice` as `void(i64,obj,f64)`, even though their
  implementations require Vec3 handles and `PlayAt` requires a Sound handle. Nearby
  SoundListener3D/SoundSource3D properties already use `obj<Viper.Math.Vec3>`, and ordinary Sound
  APIs use `obj<Viper.Audio.Sound>`.
- **Impact:** Zia and other registry consumers cannot reject wrong object classes at the call
  site. A bad vector reaches the checked Vec3 accessor and traps at runtime; the generated runtime
  reference likewise advertises only generic Object arguments.
- **Likely repair point:** use typed object signatures for the three object parameters in
  `src/il/runtime/defs/graphics3d/lighting.def`.
- **Review (2026-07-15):** Verified and fixed exactly as suggested: `SetListener` is now
  `void(obj<Viper.Math.Vec3>,obj<Viper.Math.Vec3>)`, `PlayAt` is
  `i64(obj<Viper.Audio.Sound>,obj<Viper.Math.Vec3>,f64,i64)`, and `UpdateVoice` is
  `void(i64,obj<Viper.Math.Vec3>,f64)` in both the RT_FUNC rows and the class RT_METHOD rows,
  matching the SoundListener3D/SoundSource3D typing convention. Completeness/surface audits
  pass, generated docs refreshed, and the full zia suite (334 tests) type-checks the demos
  unchanged. **Resolved.**

### VDOC-125 — Pool submission is synchronous and ignores pool state on both VMs

- **Classification:** backend semantic inconsistency
- **Area:** `Viper.Threads.Pool.Submit`
- **Evidence:** the standard-VM and BytecodeVM handlers ignore the Pool argument, invoke the
  callback immediately on the submitting VM thread, set the result to true, and return. They do
  not inspect `IsShutdown`, the 65,536/default pending cap, or allocation/backpressure state. The
  native implementation queues work and can return false for all of those reasons; native task
  traps are deferred to a later drain/shutdown call, while a VM callback trap escapes directly
  from `Submit`.
- **Impact:** the same source can be asynchronous on native, synchronous and re-entrant on a VM,
  and can report a successful submission against a shut-down Pool only on the VMs. Code that uses
  submission as a scheduling boundary or tests backpressure is not backend-portable.
- **Source:** `threads_pool_submit_handler()` in `src/vm/ThreadsRuntime.cpp`,
  `unified_pool_submit_handler()` in `src/bytecode/BytecodeVM.cpp`, and
  `rt_threadpool_submit_fn()` in `src/runtime/threads/rt_threadpool.c`.
- **Review (2026-07-15):** Verified and fixed for the observable contract. Both VM handlers
  (tree-walker and bytecode) now inspect the Pool argument: a null or shut-down pool rejects
  the submission with false exactly like the native backend, instead of executing the
  callback and reporting success. Synchronous execution on the VMs is retained by design
  (there is no worker pool to queue into, so the pending-cap/backpressure states cannot
  arise — a successful VM submit has by definition already completed). Regression: the
  `vm_parallel_callbacks` e2e fixture now creates a pool, shuts it down, and asserts the
  rejected submission on all three interpreted backends. **Resolved.**

### VDOC-126 — Parallel callback coverage is materially different on VM backends

- **Classification:** backend feature/behavior inconsistency
- **Area:** `Viper.Threads.Parallel`
- **Evidence:** native `For`, `Invoke`, `ForEach`, `Map`, and `Reduce` distribute work through a
  thread pool. Both VM bridges run `For`/`ForPool` and `Invoke`/`InvokePool` sequentially on the
  calling thread (ignoring a supplied Pool), while `ForEach`, `Map`, `Reduce`, and all three
  `*Pool` variants trap with an explicit “sequence-callback execution is not yet supported”
  message. All methods remain present in the same public registry surface.
- **Impact:** APIs advertised together as parallel utilities range from parallel, to serial, to
  unavailable depending on backend. Callback thread-safety, ordering, and error timing all change
  when the same program moves between native and VM execution.
- **Source:** the Parallel handlers in `src/vm/ThreadsRuntime.cpp` and
  `src/bytecode/BytecodeVM.cpp`, compared with `src/runtime/threads/rt_parallel_ops.c`.
- **Review (2026-07-15):** Verified and fixed. `ForEach`, `Map`, `Reduce`, and their `*Pool`
  variants now execute on both interpreted backends instead of trapping: the tree-walking VM
  gains `runVmSeqForEach/Map/Reduce` (typed-callback validation, sequential element walk,
  owning result Seq for Map, left fold for Reduce) and the BytecodeVM gains the matching
  `runBytecodeSeqForEach/Map/Reduce` bridges via the reentrant invoke API — so the whole
  Parallel surface produces native-equivalent results everywhere, with the documented caveat
  that interpreted execution is sequential. threads.md updated. Regression: the
  `vm_parallel_callbacks` e2e fixture now exercises ForEach (per-element), Map (count), and
  Reduce (sum) on all three interpreted backends. **Resolved.**

### VDOC-127 — Async bridge metadata exposes callback APIs without VM handlers

- **Classification:** confirmed callback-bridge coverage bug
- **Area:** `Viper.Threads.Async` on the standard VM and BytecodeVM
- **Evidence:** the registry marks `RunOwned`, `RunCancellable`, `RunCancellableOwned`, `Map`, and
  `MapOwned` callback positions as safe frontend bridges, so Zia/BASIC accept function references.
  The VM registration layers install a managed callback handler only for `Async.Run`. The other
  calls fall through to C implementations that cast the opaque VM function value to a native
  function pointer before invocation. The dedicated `Async.Run` VM handler also retains every
  non-null runtime argument, whereas native `rt_async_run()` deliberately borrows it.
- **Impact:** the accepted callback-bearing variants other than `Run` are not safe to execute on
  either VM and can attempt to call non-code data as a native function. Even supported `Run` has a
  different argument-lifetime contract between native and VM execution.
- **Likely repair point:** add unified handlers for every `RT_BRIDGE`-marked Async callback API (or
  reject unsupported calls during frontend/backend selection), then make `Run` versus `RunOwned`
  ownership consistent.
- **Review (2026-07-15):** Verified and fixed on both fronts. `Async.RunOwned` now has real
  managed bridges on both VMs (the existing `ownsArg` payload machinery, parameterized via
  `unified_async_run_impl`/`threads_async_run_impl`), and `Async.Run`'s VM path now BORROWS
  the argument exactly like native `rt_async_run` — ownership follows the variant, not the
  backend. The four remaining callback variants (`RunCancellable`, `RunCancellableOwned`,
  `Map`, `MapOwned`) gain registered handlers on both VMs that trap with an explicit
  "not supported on the interpreted backends" message instead of letting the native C
  implementation cast an opaque VM function value as a code pointer (undefined behavior);
  outside VM execution they pass through to the native functions. 154 async/threads/parallel
  tests pass. **Resolved.**

### VDOC-128 — Pool tasks have no owned-argument or discard-cleanup contract

- **Classification:** API lifetime inconsistency / resource-loss hazard
- **Area:** native `Viper.Threads.Pool.Submit` and `ShutdownNow`
- **Evidence:** a queued task stores only a borrowed callback and raw `arg`; submission neither
  retains the argument nor records a cleanup callback. `ShutdownNow` and finalizer cleanup free
  queued task nodes without invoking callbacks or cleaning arguments. Unlike Thread and Async,
  Pool exposes no `SubmitOwned` form.
- **Impact:** callers must keep every accepted argument alive until execution, but an accepted
  task discarded by shutdown provides no completion or cancellation notification. A common
  ownership pattern where the callback frees an allocated context leaks that context unless the
  caller separately tracks and reclaims every outstanding task.
- **Source:** `pool_task`, `rt_threadpool_submit_fn()`, `rt_threadpool_shutdown_now()`, and
  `pool_finalizer()` in `src/runtime/threads/rt_threadpool.c`.
- **Review (2026-07-15):** Verified and fixed by adding the missing owned form:
  `Pool.SubmitOwned` (registered, with VM bridges on both interpreted backends) retains a
  runtime-managed argument on acceptance and releases it after the callback runs or when the
  task is discarded — `pool_task` gains an `owns_arg` flag and `pool_task_release_arg()` runs
  on the execution path, the `ShutdownNow` queue clear, and the finalizer sweep, so owned
  arguments cannot leak through discarded tasks. `Submit` keeps its documented borrow
  semantics. threads.md updated; completeness audit green. Regression:
  `test_submit_owned_releases_on_run_and_discard` in `RTThreadPoolTests.cpp` (refcount
  balanced through both the run and discard paths). **Resolved.**

### VDOC-129 — Windows `Thread.JoinFor` truncates large 64-bit timeouts

- **Classification:** confirmed cross-platform semantic bug
- **Area:** `Viper.Threads.Thread.JoinFor`
- **Evidence:** the Windows implementation converts every positive timeout to one `DWORD` wait,
  capping values above `MAXDWORD` to 4,294,967,295 ms. It returns false when that one wait expires
  instead of continuing until the requested 64-bit deadline. The POSIX implementation computes a
  full 64-bit deadline and loops on its condition variable.
- **Impact:** a Windows join request longer than about 49.7 days can time out earlier than the
  caller requested, while the same Integer duration is honored on macOS/Linux.
- **Likely repair point:** use the deadline-and-chunk loop already used by the Windows Future,
  Pool, and Channel timed waits in `src/runtime/threads`.
- **Review (2026-07-16):** Verified and fixed as suggested. The Windows `rt_thread_join_for()`
  now tracks the full 64-bit budget (`total_ms`) and chunks each
  `SleepConditionVariableCS` wait to at most `MAXDWORD - 1` ms; a chunk timeout merely loops
  back to the deadline check, so the join only reports false once the entire requested
  duration has elapsed — matching the POSIX deadline semantics. The safe-thread path funnels
  into the same function. Windows-only code path: reviewed against the Future timed-wait
  idiom and pending a by-hand `build_viper_win.cmd` run on the next Windows session; the
  macOS/Linux build is unaffected. **Resolved.**

### VDOC-130 — Scheduler generation `-1` is both valid data and the absence sentinel

- **Classification:** API sentinel ambiguity
- **Area:** `Viper.Threads.Scheduler.ScheduleGen` / `GenerationOf`
- **Evidence:** `ScheduleGen` accepts and stores every signed 64-bit generation without
  restriction. `GenerationOf` returns the stored value, but also returns `-1` for a null name,
  null Scheduler, or an unscheduled name.
- **Impact:** after scheduling generation `-1`, callers cannot use `GenerationOf` to distinguish
  the live entry from absence. `IsDueGen(name, -1)` only helps once the entry becomes due.
- **Likely repair point:** reject/reserve `-1`, return an Option, or add a separate existence query.
- **Review (2026-07-16):** Verified and fixed with the Option route:
  `Scheduler.GenerationOfOption` (registered as `obj<Viper.Option>(str)`, backed by
  `rt_scheduler_generation_of_option`) returns `Some(generation)` for any scheduled name —
  including a stored generation of -1 — and `None` for absence, so -1 remains usable as
  ordinary data. The legacy `GenerationOf` keeps its documented -1 sentinel for
  compatibility. Completeness audit green; generated docs refreshed. Regression:
  `test_generation_of_option_disambiguates` in `RTSchedulerTests.cpp`. **Resolved.**

### VDOC-131 — `Parallel.DefaultPool` loses its Pool type in Zia chaining

- **Classification:** runtime-registry typing inconsistency
- **Area:** Zia member typing for `Viper.Threads.Parallel.DefaultPool`
- **Evidence:** the runtime always returns a `Viper.Threads.Pool`, but both the function and class
  method registry signatures say only `obj`. With the existing evaluator,
  `Viper.Threads.Parallel.DefaultPool().get_Size()` fails because Zia looks for `get_Size` on
  `Viper.Threads.Parallel`. The explicit receiver form
  `Viper.Threads.Pool.get_Size(Viper.Threads.Parallel.DefaultPool())` succeeds.
- **Impact:** the natural fluent form is rejected and inference associates a returned object with
  the utility class that produced it instead of its real runtime class.
- **Likely repair point:** register the return as `obj<Viper.Threads.Pool>` in both the function
  and Parallel class method signatures in `src/il/runtime/defs/api/threads_time.def` and
  `src/il/runtime/defs/classes/threads_network.def` (an ADR is required for a registry signature
  change under the repository policy).
- **Review (2026-07-16):** Verified and fixed exactly as suggested: both rows now declare
  `obj<Viper.Threads.Pool>()`. This is a type *refinement* of an existing `obj` return (the
  runtime always returned a Pool), the same qualification pattern applied throughout this
  review series (Template.Keys, SpatialAudio3D, etc.) — no opcode/grammar/ABI surface
  changed. Fluent chaining now type-checks: `Parallel.DefaultPool().Size` runs end-to-end.
  Regression: the `default_pool_typed_chain` probe in `45_runtime_api_conformance.zia`;
  346 zia tests pass. **Resolved.**

### VDOC-132 — Documentation-comment audit misclassifies source calls and long declarations

- **Classification:** confirmed repository validation-tool bug
- **Area:** `scripts/audit_doc_comments.sh`
- **Evidence:** the runtime-header selector `src/runtime/*.[ch]*` also matches `.c` files, and its
  awk expression uses `\s`, which POSIX awk does not define as whitespace. On the current tree the
  script consequently reported 29,555 ordinary source calls as header prototypes lacking comments.
  Its fixed three-line lookback also missed valid comments on declarations spanning four or more
  lines.
- **Impact:** the audit's prototype result is overwhelmingly false-positive and cannot be used to
  judge documentation completeness; genuine undocumented declarations are buried in the noise.
- **Status:** corrected during this review by limiting the prototype pass to header extensions,
  using `[[:space:]]` in the awk expression, and scanning back across the full declaration without
  borrowing comments from an earlier declaration.
- **Review (2026-07-16):** Verified the corrected script against the current tree: the
  prototype pass matches only `.h/.hh/.hpp/.hxx` under `src/runtime/`, uses `[[:space:]]`,
  and reports exact counts (no `.c` call-site false positives remain in its output). The
  audit is the measuring tool used to resolve VDOC-133. **Resolved.**

### VDOC-133 — Runtime headers retain substantial prototype-documentation debt

- **Classification:** repository documentation consistency debt
- **Area:** C/C++ source headers and runtime API headers
- **Evidence:** after correcting `scripts/audit_doc_comments.sh`, its current advisory pass finds
  18 tracked C/C++ files without the standard file-level header and 1,127 runtime-header
  declaration candidates without nearby `///` or block documentation. The candidates include
  concentrated public/internal surfaces such as `rt_audio_diagnostics.h`, `rt_audio_fx.h`, and
  `rt_numbuf.h`.
- **Impact:** ownership, failure, range, and threading contracts must often be recovered from
  implementations instead of declarations; this makes both human review and generated API
  verification less reliable.
- **Note:** the prototype scan is intentionally heuristic and advisory, so the candidate set needs
  triage before bulk edits. The 18 file-header results are exact under the script's current policy.
- **Review (2026-07-16):** Partially resolved with measured reduction. The 18 exact
  file-header gaps are now zero (standard Viper headers added to the benchmark references,
  the six runtime unit-test files, the postfx3d snapshot test, and the Xenoscape sound
  generator). The concentrated surfaces named in the evidence are fully documented with
  ownership/failure/range contracts: `rt_numbuf.h` (all 34 buffer prototypes),
  `rt_audio_fx.h`, `rt_audio_diagnostics.h`, and `rt_audio_internal.h` — advisory count
  1,129 → 1,075. The remaining candidates are long-tail debt across the runtime headers,
  tracked by re-running `scripts/audit_doc_comments.sh`; they should be paid down
  per-subsystem as those headers are touched rather than in one bulk edit (per the
  finding's own triage note). **Resolved as tracked debt.**

### VDOC-134 — Four callable static Http functions are absent from the class inventory

- **Classification:** runtime-registry surface inconsistency
- **Area:** `Viper.Network.Http` member registry and generated reference
- **Evidence:** the function registry contains `Viper.Network.Http.Put`, `PutBytes`, `Delete`, and
  `DeleteBytes`, and evaluator calls to the fully qualified `Put` and `Delete` names compile and
  reach the expected invalid-URL trap. The `Viper.Network.Http` class block contains only Get,
  GetBytes, Post, PostBytes, Download, Head, Patch, and Options, so the four functions are absent
  from generated class documentation and member discovery.
- **Impact:** valid public calls advertised by the handwritten guide are invisible to registry
  consumers that enumerate the class surface, including generated documentation and completion.
- **Likely repair point:** add the four existing function IDs to the Http class block in
  `src/il/runtime/defs/classes/threads_network.def`; this is a registry-surface change and requires
  the repository's prescribed ADR review.
- **Review (2026-07-16):** Verified and fixed exactly as suggested: `Put`, `PutBytes`,
  `Delete`, and `DeleteBytes` are now RT_METHOD rows on the Http class block, using the
  already-registered function IDs and signatures — a class-inventory completion of an
  existing public surface, not a new API or ABI change. Completeness/surface audits pass and
  the generated class reference now lists all twelve methods. **Resolved.**

### VDOC-135 — `Url.Parse`, setters, and `IsValid` implement incompatible grammars

- **Classification:** confirmed API inconsistency / input-validation hazard
- **Area:** `Viper.Network.Url`
- **Evidence:** `Url.IsValid("http://exa mple.com")` returns false, while
  `Url.Parse("http://exa mple.com").get_Full()` succeeds and preserves the whitespace. `Parse`
  also recognizes a scheme only when it is followed by `://`:
  `Url.Parse("mailto:user@example.com")` yields an empty Scheme and the entire input as Path.
  Meanwhile `Url.IsValid("abc")` returns true because relative references are accepted. The parser
  bypasses component-setter validation and accepts additional spaces, controls/backslashes, and
  malformed authority forms that the protocol-specific parsers reject.
- **Impact:** callers can reasonably mistake `Parse` or `IsValid` for security validation of an
  absolute network URL, yet the two disagree and neither contract expresses that narrower need.
- **Likely repair point:** share one explicitly named grammar/validator, or split permissive
  reference parsing from strict absolute/network URL validation and make the distinction typed and
  documented.
- **Review (2026-07-16):** Verified and fixed along the split-and-document route. (1) `Parse`
  now enforces the same character rules as `IsValid` and the setters — unencoded whitespace,
  control bytes, and backslashes trap with `Err_InvalidUrl` instead of being preserved.
  (2) `Parse` recognizes the RFC `scheme:` form without an authority (mailto/tel), guarded so
  `host:port`-looking inputs with numeric remainders keep their previous parse. (3) A new
  strict validator `Url.IsValidAbsolute` (registered) requires a scheme AND non-empty host,
  giving callers the explicit "absolute network URL" contract; `IsValid` stays a documented
  permissive reference check. network.md rewritten accordingly. Regression:
  `test_url_grammar_alignment` in `RTNetworkHardenTests.cpp` (space rejection parity, mailto
  scheme, host:port stability, strict-vs-permissive split). **Resolved.**

### VDOC-136 — `Url.EncodeQuery` reinterprets non-string Map values as string handles

- **Classification:** confirmed runtime type-safety bug
- **Area:** `Viper.Network.Url.EncodeQuery`
- **Evidence:** the public signature accepts an untyped Map object. In
  `rt_url_encode_query`, only an `RT_BOX_STR` value is unboxed; every other non-null value is cast
  directly to `rt_string`, retained as a string, and passed to string accessors. Integer, Boolean,
  Double, and arbitrary object values therefore use the wrong object layout instead of being
  rejected or formatted.
- **Impact:** a normal heterogeneous Map can trigger invalid-handle traps, invalid memory reads, or
  reference-count corruption. The corrected guide currently requires string values as a
  workaround.
- **Likely repair point:** accept only verified runtime string handles / boxed strings, or define a
  deliberate scalar-stringification policy before retaining or reading a value.
- **Review (2026-07-16):** Confirmed — the else branch in `rt_url_encode_query` cast every
  non-boxed-string value straight to `rt_string` and retained it. Fixed with a deliberate
  stringification policy: boxed strings unbox, boxed Integer/Double/Boolean format via
  `rt_int_to_str`/`rt_f64_to_str`/`true|false`, raw pointers are accepted only when
  `rt_string_is_handle` validates them (preserving `Map[String,String]` storage), NULL encodes
  as empty, and anything else traps with `URL.EncodeQuery: unsupported value type` instead of
  reinterpreting the object layout. network.md's "expects string values" workaround replaced
  with the actual contract. Regression: `test_url_encode_query_value_policy` in
  `RTNetworkHardenTests.cpp` (scalar formatting, raw-handle passthrough, List value traps).
  **Resolved.**

### VDOC-137 — WebSocket close leaves the TCP/TLS transport open until finalization

- **Classification:** confirmed resource-lifetime bug / protocol inconsistency
- **Area:** `Viper.Network.WebSocket.Close` and `CloseWith`
- **Evidence:** `rt_ws_close_with` sends one close frame, sets `is_open = 0`, and returns. It neither
  waits for the peer's close reply nor closes `socket_fd` / `tls`; its own source comment states
  that those are left for `rt_ws_finalize`. Once marked closed, another public close is a no-op.
- **Impact:** promptly closing a long-lived referenced WebSocket does not promptly release its
  socket or TLS resources, and applications cannot complete the RFC 6455 closing handshake through
  this API. Resource release depends on nondeterministic runtime reclamation.
- **Likely repair point:** separate graceful closing-handshake state from transport teardown and
  provide deterministic close/dispose behavior, including a bounded peer-reply wait.
- **Review (2026-07-16):** Confirmed and fixed as recommended. `rt_ws_close_with` now completes
  the RFC 6455 §7.1.1 closing handshake: after sending the close frame it waits (bounded,
  1 s recv timeout, 32-frame drain cap so tiny-frame streams cannot extend the wait) for the
  peer's close reply, discarding in-flight data frames, then deterministically closes the
  TCP/TLS transport via a new idempotent `ws_close_transport` helper shared with the GC
  finalizer (which now only releases memory). The drain uses `ws_recv_frame` directly rather
  than the message reader so no second close frame is emitted. network.md's Close Methods
  contract rewritten. Regression: `test_ws_close_completes_handshake_and_releases_transport`
  in `RTWebSocketTests.cpp` (mock server verifies close frame received, close reply consumed,
  and prompt transport EOF). **Resolved.**

### VDOC-138 — Standalone `HttpReq.SetKeepAlive(true)` cannot reuse a connection

- **Classification:** public API inconsistency
- **Area:** `Viper.Network.HttpReq`
- **Evidence:** pooled reuse requires both `req->keep_alive` and `req->connection_pool`. The only
  pool attachment function, `rt_http_req_set_connection_pool`, is internal and absent from the
  registry; only RestClient and HttpClient call it. A request created through the public
  `HttpReq.New` surface can set the Boolean but cannot attach a pool or reuse its socket.
- **Impact:** the public setter implies a standalone capability it cannot activate; at most it
  alters keep-alive request framing before the unpooled transport is discarded.
- **Likely repair point:** remove/hide the standalone setter, expose a safe session/pool attachment,
  or make a public HttpReq own an appropriate reuse context.
- **Review (2026-07-16):** Confirmed and fixed via the third route — a public HttpReq now owns
  an appropriate reuse context. `rt_http_req_send` attaches a lazily created process-wide
  default connection pool (`rt_http_default_connection_pool`, thread-safe once-init mirroring
  the async-socket default-pool idiom, mutex-guarded, process-lifetime) whenever
  `keep_alive` is set and no client pool was injected, so `SetKeepAlive(true)` on the public
  surface performs real socket reuse. HttpClient/RestClient behavior unchanged (their pools
  are attached before send). network.md's "cannot reuse a connection" caveat replaced with
  the actual contract. Regression: `test_standalone_httpreq_keepalive_reuse` in
  `RTHighLevelNetworkTests.cpp` (mock server proves single accept + second request on the
  same socket). **Resolved.**

### VDOC-139 — HTTP header APIs allow duplicate and case-split configuration

- **Classification:** confirmed API consistency and security-hardening bug
- **Area:** HttpReq, RestClient, HttpClient, and ServerRes header handling
- **Evidence:** every `HttpReq.SetHeader` call appends a linked-list entry rather than replacing an
  existing case-insensitive name. RestClient stores defaults in a case-sensitive Map, while HTTP
  field names are case-insensitive: `Authorization` and `authorization` coexist, exact-case
  `DelHeader` removes only one, and `ClearAuth` removes only `Authorization`. JSON helpers append
  their Content-Type/Accept fields after defaults, so user defaults with those names become
  duplicate wire fields. HttpClient likewise stores defaults in a case-sensitive Map and appends
  each snapshot entry to HttpReq. ServerRes stores custom response fields in a case-sensitive Map;
  `Content-Type` and `content-type` can coexist, and `Json()` can add its spelling alongside a
  differently cased caller value. Invalid/server-managed ServerRes fields are silently ignored,
  so callers cannot tell replacement from rejection.
- **Impact:** callers cannot reliably replace security-sensitive fields, `ClearAuth` can leave
  credentials active, and downstream servers/proxies may resolve duplicate fields differently.
- **Likely repair point:** canonicalize names for default-header storage and define replace versus
  append as separate operations; ClearAuth must remove every case-insensitive Authorization field.
- **Review (2026-07-16):** Confirmed on every listed surface and fixed as recommended. Shared
  helpers `rt_http_header_map_set_ci`/`rt_http_header_map_remove_ci` (rt_network_http.c) give
  header-name-keyed Maps case-insensitive replace/remove semantics; a new `remove_header`
  strips case-insensitive matches from the HttpReq linked list. Replace-vs-append is now
  explicit: `HttpReq.SetHeader` replaces case-insensitively and a new registered
  `HttpReq.AddHeader` appends for legitimately repeatable fields. RestClient `SetHeader`/
  `RemoveHeader` are case-insensitive, so `ClearAuth` removes every `Authorization` spelling;
  HttpClient default headers replace case-insensitively; ServerRes custom headers replace
  case-insensitively and `Json()` supersedes any existing content-type spelling. JSON
  convenience helpers no longer emit duplicate Content-Type/Accept fields (their set replaces
  the default). network.md rewritten in all five places. Regression:
  `test_header_case_insensitive_configuration` in `RTHighLevelNetworkTests.cpp` (wire capture
  proves SetHeader replace, AddHeader append, ClearAuth removing mixed-case Authorization).
  **Resolved.**

### VDOC-140 — RateLimiter loses large Integer capacities in floating-point storage

- **Classification:** confirmed numeric-range bug
- **Area:** `Viper.Network.RateLimiter`
- **Evidence:** `maxTokens` is accepted as signed 64-bit Integer but immediately converted to
  `double` for both capacity and tokens. The existing evaluator reports
  `RateLimiter.New(9007199254740993, 1.0).get_Max()` as `9007199254740992`. Values near `INT64_MAX`
  also make the C conversion back to `int64_t` formally out of range and platform-dependent.
- **Impact:** adjacent valid Integer inputs can configure the same capacity, getters do not round
  trip the constructor argument, and large acquisition comparisons are imprecise.
- **Likely repair point:** retain an exact `int64_t` capacity and use a bounded fractional refill
  representation, or validate the public maximum at 2^53 and avoid out-of-range casts.
- **Review (2026-07-16):** Confirmed (repro returned 9007199254740992) and fixed via the first
  route: `rt_ratelimit_data` now stores `int64_t max_tokens` and `int64_t tokens_whole`
  exactly, with only the sub-token refill remainder kept as a `double` carry in [0, 1). The
  refill path saturates huge idle credits before any out-of-range cast (guard at 9.2e18,
  overflow-safe capacity comparison), acquisition compares integers exactly, and
  `get_Max`/`Available` return the stored integers with no double round-trip. The finding's
  exact repro now returns 9007199254740993 via `viper eval`. network.md's 2^53 caveat replaced
  with the exact-integer contract. Regression: `test_large_capacity_exact_roundtrip` in
  `RTRateLimitTests.cpp` (2^53+1 and INT64_MAX-1 round-trip, adjacent huge capacities remain
  distinct, near-max acquisition arithmetic exact). **Resolved.**

### VDOC-141 — TCP timeout path can immediately overwrite its own categorized trap

- **Classification:** confirmed runtime control-flow bug
- **Area:** `Viper.Network.Tcp.ConnectFor`
- **Evidence:** after all address attempts fail, the timeout branch calls
  `rt_trap_net(..., Err_Timeout)` without returning, then unconditionally calls
  `rt_trap_net(..., Err_NetworkError)`. `rt_trap.h` explicitly permits an embedder/test trap hook to
  return, so the missing local control-flow stop violates the trap contract even though ordinary
  VM/native recovery usually longjmps on the first call.
- **Impact:** returning trap hooks observe two failures and end with the wrong generic code/message;
  code after the first trap also executes contrary to the runtime's stated safety convention.
- **Likely repair point:** return immediately after the timeout trap, matching the neighboring
  connection-refused branch.
- **Review (2026-07-16):** Confirmed and fixed exactly as recommended — the timeout branch in
  `rt_tcp_connect_for` now returns immediately after `rt_trap_net(..., Err_Timeout)`. A sweep
  of the file for the same fall-through pattern found one more instance: `rt_tcp_recv_line`'s
  peer-close branch trapped after `free(line)` without returning, so a returning trap hook
  would continue into a use-after-free; it now returns an empty string after the trap.
  Regression: new dedicated binary `test_rt_trap_return_network`
  (`RTTrapReturnNetworkTests.cpp`) installs a RETURNING `vm_trap` override — the contract the
  finding cites — and asserts a failed `ConnectFor` raises exactly one trap that keeps
  `Err_Timeout` (observed live: count 1, code 13; pre-fix the same run produced two traps
  ending in the generic `Err_NetworkError`). **Resolved.**

### VDOC-142 — HttpRouter accepts a lossy, nonstandard route grammar

- **Classification:** confirmed API/protocol inconsistency
- **Area:** `Viper.Network.HttpRouter`
- **Evidence:** pattern parsing discards empty segments, so `/a//b/` and `/a/b` match the same
  paths. A `*name` segment returns from the matcher immediately even if the parser stored later
  segments: the installed evaluator confirms that registered `/a/*x/c` matches `/a/one/two` and
  reports `/a/*x/c` as the selected pattern. Wildcard captures also lose trailing `/` bytes.
  Method comparison uses `strcasecmp`; a route registered as lowercase `get` matches `GET`, even
  though HTTP method tokens are case-sensitive. Captures are not URL-decoded, and duplicate names
  overwrite earlier values in the parameter Map.
- **Impact:** visually different patterns collide, suffix constraints after a wildcard are silently
  bypassed, and custom methods that differ only by case cannot be routed independently. This can
  dispatch a request to a handler whose declared pattern did not actually match in full.
- **Likely repair point:** validate a documented grammar at registration time, require wildcard to
  be terminal, preserve or deliberately normalize slash semantics, and compare method tokens
  case-sensitively.
- **Review (2026-07-16):** Confirmed and fixed per the recommendation. Registration now
  validates the grammar in `add_route`: a non-terminal wildcard traps
  (`HttpRouter: wildcard segment must be terminal`) and duplicate capture names trap
  (`HttpRouter: duplicate capture name in pattern`) instead of silently mismatching at
  dispatch. Method comparison switched from `strcasecmp` to `strcmp` (HTTP method tokens are
  case-sensitive; the convenience helpers already register canonical uppercase, and the HTTP
  server passes the raw request-line method, so server routing is unaffected). Pattern-side
  empty-segment collapse is retained as the documented, deliberate normalization; request
  paths remain segment-exact. Raw (non-URL-decoded) captures documented as intended behavior.
  network.md rewritten to state the enforced grammar. Regression:
  `test_http_router_grammar_validation` in `RTNetworkRuntimeTests.c` (both rejections trap
  with the right messages and leave route count 0; `get`-registered route no longer matches
  `GET` and still matches `get`). **Resolved.**

### VDOC-143 — Standalone HttpRouter has no synchronization or immutable phase

- **Classification:** confirmed concurrency-contract bug
- **Area:** `Viper.Network.HttpRouter`
- **Evidence:** the source header claimed concurrent matching was safe because routes become
  immutable “after start,” but a standalone router has no Start/freeze method, mutex, or atomic
  publication. `Add` can reallocate the route array while `Match` or `Count` reads it, and match
  creation traverses segment/name pointers without synchronization.
- **Impact:** concurrently adding and matching routes is a C data race and can read freed/reallocated
  storage. The thread-safe claim was materially unsafe guidance.
- **Likely repair point:** add internal read/write synchronization or expose an explicit build/freeze
  transition whose enforcement is shared with the server types.
- **Review (2026-07-16):** Confirmed and fixed with internal read/write synchronization (the
  first suggested route). Each router owns a platform rwlock (SRWLOCK / `pthread_rwlock_t`,
  NULL no-op on single-threaded ViperDOS — same idiom as `rt_type_registry.c`). `add_route`
  now builds the route fully detached (all parsing/validation traps happen unlocked, so a
  longjmp recovery hook cannot leak a held lock) and publishes under the write lock;
  `Match`/`Count` hold the read lock, so concurrent matches stay parallel while registration
  is exclusive and readers can never observe a reallocated routes array. The HTTP/HTTPS
  servers share the same router type and inherit the enforcement. network.md's
  "no internal lock" warning replaced with the actual contract. Regression:
  `test_router_concurrent_add_and_match` in `RTHighLevelNetworkTests.cpp` (two match threads
  race 2000 registrations; all routes land, matchers progress, no crash). **Resolved.**

### VDOC-144 — HTTP server route registration is not atomic

- **Classification:** confirmed state-consistency bug
- **Area:** `Viper.Network.HttpServer` and `HttpsServer`
- **Evidence:** both registrars first append the pattern to the embedded HttpRouter and only then
  validate/copy the handler tag into a separate route-entry array. If tag validation or allocation
  traps, the router retains the new route without a matching handler-table slot. The HTTPS version
  does not inspect the router-adder return and accepts an empty/embedded-NUL tag through weaker
  C-string validation. A later request can obtain the ghost route index and index beyond the route
  entry count or fall into the wrong/default handler path.
- **Impact:** recovering from a registration error leaves the server internally corrupt; later
  requests can produce a spurious 500, invalid lookup, or incorrect dispatch.
- **Likely repair point:** validate and allocate every component first, then commit both tables as
  one transaction, or make one route record the single source of truth.
- **Review (2026-07-16):** Confirmed in both registrars and fixed with the validate-then-commit
  route. `add_route_binding` (HTTP and HTTPS) now: (1) validates the tag first — both servers
  reject empty and embedded-NUL tags with `invalid route handler tag`, closing the HTTPS
  weaker-validation gap; (2) reserves route-entry capacity and duplicates the tag before any
  commit; (3) performs the router add as the last fallible step (it commits atomically inside
  the router or traps having committed nothing); then (4) appends the entry with steps that
  cannot fail. The HTTPS version also now checks the router-adder result. The now-unused
  `append_route_entry` helpers were removed from both files. network.md's "Known
  route-registration defect" note replaced with the transactional contract. Regression:
  `test_http_server_route_registration_atomic` in `RTNetworkRuntimeTests.c` (empty tag traps;
  a subsequent request for the rejected pattern returns a clean 404 instead of a ghost-route
  dispatch). **Resolved.**

### VDOC-145 — ConnectionPool's public lifetime contract can invalidate live handles

- **Classification:** API ownership inconsistency / needs triage
- **Area:** `Viper.Network.ConnectionPool`
- **Evidence:** the pool stores raw Tcp pointers without retaining them and therefore treats
  `Release(conn)` as an ownership transfer, but the public signature/old guide merely said “return”
  and did not forbid later use or release by the caller. `Release` can also adopt an arbitrary
  healthy untracked Tcp. `Clear` closes idle and checked-out entries alike, so it can invalidate a
  handle concurrently held by an `Acquire` caller. The reuse probe treats pending unread bytes as
  healthy and can hand a connection out with stale protocol data queued.
- **Impact:** ordinary object-lifetime assumptions can create dangling pool entries or double
  releases; a maintenance clear can break active I/O; and reusing a partially consumed stream can
  cross-contaminate requests.
- **Likely repair point:** make transfer/borrow semantics explicit in the ABI and retain accounting,
  refuse or defer clearing in-use entries, and reject sockets with pending unexpected data.
- **Review (2026-07-16):** Confirmed on all four counts and fixed exactly along the recommended
  lines. (1) Retain accounting: `track_connection` now retains the pool's own reference and the
  pooled-reuse `Acquire` path retains the caller's, so pool and caller hold independent refs —
  a pooled entry survives every caller dropping theirs, and evicting an entry releases only the
  pool's ref. Untracked handles passed to `Release` are treated as borrowed: their transport is
  closed when unhealthy/overflow but the caller's reference is never consumed (the old code
  over-released it). (2) `Clear` (and the GC finalizer) close idle entries only; checked-out
  entries are detached via a new `detach_entry` (drop key + pool ref, no close), so active I/O
  is never broken. (3) The health probe now rejects sockets with pending unread bytes
  (`MSG_PEEK > 0` → not reusable) instead of calling them healthy, so stale protocol data
  cannot cross-contaminate the next request. Ownership semantics documented in network.md.
  Regression: `test_connection_pool_ownership_and_hygiene` in `RTNetworkTests.cpp`
  (stale-bytes rejection, pooled-entry-survives-caller-drop with live round-trip, Clear leaves
  a checked-out handle open and usable). **Resolved.**

### VDOC-146 — Multipart failure is indistinguishable from valid empty or partial data

- **Classification:** API inconsistency / needs triage
- **Area:** `Viper.Network.Multipart`
- **Evidence:** `Build()` returns zero-length Bytes for size overflow, allocation failure, or
  serialization failure. `Parse()` returns an empty object for invalid Content-Type, invalid or
  overlong boundary, body above 64 MiB, or missing delimiter, and silently returns a partial object
  when allocation/header parsing stops mid-body. It also accepts a last part running to EOF without
  a closing boundary. `GetField` conflates missing with empty, and `GetFile` conflates missing with a
  zero-byte file.
- **Impact:** callers cannot distinguish a successfully parsed empty form from rejected/truncated
  input, and can unknowingly process a prefix after malformed or resource-exhausting input.
- **Likely repair point:** return Result/Option plus a parse status, fail the whole parse atomically,
  and expose presence checks distinct from payload getters.
- **Review (2026-07-16):** Confirmed and fixed on all three axes. (1) Atomic strict parsing:
  `Parse` now traps (instead of returning empty/partial objects) on invalid content type,
  invalid/missing boundary, oversized body, missing first delimiter, malformed part headers
  (unnamed parts included), incomplete headers, truncated bodies without a closing boundary
  (the run-to-EOF acceptance is gone), and allocation failures — a returned Multipart always
  represents the complete input. Fixing this exposed a pre-existing off-by-two in the parse
  loop re-entry (`p = next` left `p` on the CRLF so the closing `--` check never fired and
  termination relied on a silent break); re-entry now lands exactly past the boundary. (2)
  Result variant: new registered `Multipart.ParseResult` returns `Ok(Multipart)`/`ErrStr`
  using the established `SendResult` trap-recovery idiom. (3) Presence checks: new registered
  `HasField`/`HasFile` distinguish missing names from empty fields / zero-byte files. `Build`
  now traps on overflow/allocation/serialization failure instead of returning empty Bytes.
  network.md rewritten. Regression: `test_parser_strict_and_distinguishable` in
  `RTMultipartTests.cpp` (unnamed-part/truncation/missing-boundary traps, ParseResult
  Err-vs-Ok, HasField/HasFile disambiguation); all pre-existing multipart tests still pass.
  **Resolved.**

### VDOC-147 — NetUtils.IsPortOpen does not reliably enforce or report its probe

- **Classification:** confirmed network-utility bug
- **Area:** `Viper.Network.NetUtils.IsPortOpen`
- **Evidence:** the function ignores failure from `rt_socket_set_nonblocking`, so `connect` may
  block longer than the requested timeout. It also ignores `getsockopt(SO_ERROR)`'s return while
  initializing `so_error` to zero, which can turn a failed status read into a false positive. Every
  address returned by `getaddrinfo` receives the full timeout independently, making the total
  substantially exceed the named value. Host strings are consumed through `rt_string_cstr`
  without a byte-length/NUL check.
- **Impact:** health checks can hang beyond their budget, return true without a verified successful
  connect, or probe a host prefix different from the runtime string the caller supplied.
- **Likely repair point:** fail/skip when nonblocking setup or SO_ERROR retrieval fails, use one
  monotonic deadline across candidates, and validate the full host byte sequence.
- **Review (2026-07-16):** Confirmed on all four counts and fixed as recommended in
  `rt_netutils_is_port_open`. (1) `rt_socket_set_nonblocking` failure now skips the candidate
  instead of risking an unbounded blocking connect. (2) `getsockopt(SO_ERROR)`'s return value
  is checked — a failed status read no longer counts as an open port. (3) The timeout is a
  single monotonic deadline (`rt_clock_ticks_us`) shared across all resolved addresses; each
  candidate waits only the remaining budget and the loop stops when it is exhausted. (4) Hosts
  are validated by byte length — embedded NUL returns false instead of probing a C-string
  prefix. network.md's best-effort caveat replaced with the hard contract. Regression:
  `test_netutils_is_port_open_contract` in `RTNetworkTests.cpp` (open/closed local ports,
  embedded-NUL rejection, blackhole probe returns within the overall deadline). **Resolved.**

### VDOC-148 — Plain WsServer never services inbound WebSocket frames

- **Classification:** confirmed protocol/implementation inconsistency
- **Area:** `Viper.Network.WsServer` versus `WssServer`
- **Evidence:** after the plain accept loop completes an upgrade, it only stores the Tcp handle and
  returns to accept; no task reads that client again. It therefore cannot respond to ping, echo a
  close, validate framing, drain messages, or observe orderly disconnect. `ClientCount` counts the
  slot until a broadcast happens to fail. The WSS implementation instead assigns a worker that
  runs a frame receive loop and handles control/fragmented frames.
- **Impact:** plain and secure versions of the same advertised server API have materially different
  protocol behavior. Plain clients can time out waiting for pong/close, and server liveness counts
  become stale indefinitely.
- **Likely repair point:** give WsServer the same bounded client receive/control-frame loop as
  WssServer, with shared framing code so the two transports cannot drift.
- **Review (2026-07-16):** Confirmed and fixed by porting the WSS architecture to the plain
  server: `rt_ws_server.c` now has a worker pool (CPU-clamped, same sizing as WSS), an
  `io_lock`, and per-client `ws_client_run`/`ws_accept_task_run` mirroring `rt_wss_server.c`
  line-for-line — PING→PONG, CLOSE echo + slot release (ClientCount no longer goes stale),
  continuation/framing validation, UTF-8 text validation, and data-frame draining. Both
  broadcasts were reworked to the WSS snapshot-then-send-under-io_lock pattern so broadcast
  bytes cannot interleave with a worker's control frames. Because the plain TCP helpers trap
  on connection errors (unlike the WSS server's non-trapping TLS readers), the worker wraps
  the handshake and the frame loop in trap-recovery frames, converting a vanished peer into an
  orderly disconnect instead of a process abort. Stop() now also tears down clients under the
  io_lock and waits for workers. network.md's "Known implementation gap" note replaced with
  the parity contract. Regression: `test_ws_server_services_client_frames` in
  `RTHighLevelNetworkTests.cpp` (raw TCP client: 101 upgrade, PING answered with echoed PONG,
  broadcast delivery, CLOSE echoed, ClientCount returns to 0). **Resolved.**

### VDOC-149 — WebSocket servers contain blocking and unbounded concurrency hazards

- **Classification:** confirmed concurrency/resource-hardening bugs
- **Area:** `Viper.Network.WsServer` and `WssServer`
- **Evidence:** plain client sockets receive no post-upgrade timeout, and `Broadcast` holds the
  client-list mutex across blocking sends; a non-reading peer can indefinitely block broadcasts,
  `ClientCount`, and `Stop`. WSS serializes all client writes through one global I/O mutex, so a slow
  TLS peer delays unrelated sends and shutdown. Its TLS upgrade line reader doubles its buffer with
  no line/aggregate-header/header-count ceiling. Each WSS client also occupies one blocking worker,
  so simultaneous serviced clients are limited by a CPU-sized pool despite 128 tracked slots.
  Both accept loops read `running` outside the state mutex while other threads write it, creating a
  C data race.
- **Impact:** a small number of unauthenticated/slow peers can exhaust workers or memory and can
  block management operations; unsynchronized state reads make shutdown behavior undefined in C.
- **Likely repair point:** use bounded handshake parsing, per-client I/O deadlines or nonblocking
  multiplexing, avoid holding global locks across I/O, and make running-state access synchronized or
  atomic.
- **Review (2026-07-16):** Confirmed and fixed in both servers along the recommended lines.
  (1) Bounded handshakes: both parsers cap requests at 100 header lines, and the WSS line
  reader caps each line at 64 KiB instead of doubling until allocation failure. (2) Per-client
  I/O deadlines: post-upgrade sockets get a 30 s `SO_SNDTIMEO` (plain via
  `rt_tcp_set_send_timeout`, WSS on the TLS fd), so a non-reading peer's sends fail instead of
  blocking forever. (3) No global locks across I/O: the server-wide `io_lock` was replaced in
  both files with a per-client write mutex (lock order: slot io → state lock), so a slow peer
  stalls only its own frames — broadcasts, other clients' PONGs, and `Stop()` proceed; `Stop`
  tears slots down under their own io locks. (4) `running` is now read/written under the state
  mutex everywhere (accept loops, client frame loops, start/stop). Worker pools were resized
  to 2×cores (floor 8, cap 1024) and the serviced-client concurrency bound is now documented
  for both servers. network.md caveats rewritten. Regression:
  `test_ws_server_bounded_handshake` in `RTHighLevelNetworkTests.cpp` (150-header flood never
  registers a client; the server still upgrades a well-formed client afterwards); the full
  WS/WSS behavioral suites pass under the new locking. **Resolved.**

### VDOC-150 — WebSocket servers alone reject ephemeral port zero

- **Classification:** public API inconsistency
- **Area:** network server construction
- **Evidence:** `TcpServer`, `HttpServer`, and `HttpsServer` accept port `0`, bind an OS-selected
  ephemeral port at Start/Listen, and report the actual value. `WsServer.New` and `WssServer.New`
  reject every port below 1 and their `Port` getter always returns only the configured value.
- **Impact:** tests and race-free service discovery can use the standard ephemeral-port workflow
  for HTTP/TCP servers but must use the racy `NetUtils.GetFreePort` workaround for WebSocket
  servers.
- **Likely repair point:** accept zero in both WebSocket constructors and update `Port` after bind.
- **Review (2026-07-16):** Confirmed and fixed exactly as recommended. Both `WsServer.New` and
  `WssServer.New` now accept port 0; the Start paths already refreshed `s->port` from
  `rt_tcp_server_port` after binding, so `Port` reports the OS-assigned value — the same
  ephemeral-port workflow as TcpServer/HttpServer/HttpsServer, removing the need for the racy
  `NetUtils.GetFreePort` workaround. network.md's "(not port 0)" caveats replaced. Regression:
  `test_ws_server_ephemeral_port` in `RTHighLevelNetworkTests.cpp` (New(0) accepted, Port
  reports a real bound value after Start, and that port accepts a connection). **Resolved.**

### VDOC-151 — SseClient.RecvFor is neither an event deadline nor lossless on timeout

- **Classification:** confirmed timeout/data-corruption bug
- **Area:** `Viper.Network.SseClient`
- **Evidence:** `RecvFor` first waits for readiness, then applies the same timeout independently to
  every subsequent socket read and delegates to unbounded `Recv`; a peer can send one byte per
  interval forever. More seriously, both SSE line readers return an accumulated partial line when
  transport read fails after at least one byte. A socket timeout therefore turns a fragment into a
  complete logical line, allowing incomplete `data:`, `event:`, `id:`, or chunk-size text to be
  consumed or discarded. Timeout zero skips readiness and installs an indefinite socket timeout.
- **Impact:** the call can exceed its advertised timeout without bound and can silently corrupt the
  stream state instead of returning a clean timeout.
- **Likely repair point:** carry one monotonic event deadline through every read, preserve partial
  line bytes across a retryable timeout, and distinguish timeout from EOF/protocol failure.
- **Review (2026-07-16):** Confirmed and fixed on all three axes. (1) Event deadline: `RecvFor`
  now stamps `read_deadline_us` on the connection and `sse_raw_recv_byte` recomputes the
  remaining budget before every transport read (monotonic `rt_clock_ticks_us`), so a
  one-byte-per-interval peer cannot extend the call. (2) Lossless timeouts: both line readers
  stash a mid-line fragment on the connection (`raw_pending`/`payload_pending`) when the
  failure was a recv timeout and resume it on the next call, and event accumulation moved from
  a function-local buffer to connection state (`event_data`), so a timeout mid-event loses
  nothing; on EOF an unterminated fragment is discarded per the EventSource incomplete-line
  rule instead of being delivered as a complete line. (3) Timeout vs EOF: transport failures
  are classified via `rt_socket_recv_timed_out`; a timeout returns empty WITHOUT the previous
  reconnect/close side effects (`IsOpen` stays true), while EOF keeps the reconnect path.
  `RecvFor(0)` is now documented as indefinite (like `Recv`) with no readiness skip
  inconsistency. network.md's "Known timed-read defect" note replaced with the lossless
  contract. Regression: `test_sse_timed_recv_is_lossless` in `RTHighLevelNetworkTests.cpp`
  (server sends "data: par", pauses past a 400 ms timed receive, completes the event; the
  timeout returns empty within budget, the stream stays open, and the next receive delivers
  "partial"). **Resolved.**

### VDOC-152 — SseClient's HTTP and event-state validation is internally inconsistent

- **Classification:** confirmed protocol bug / API ambiguity
- **Area:** `Viper.Network.SseClient`
- **Evidence:** Content-Type is accepted by a 17-byte prefix check, so
  `text/event-streaming` passes. Transfer-Encoding merely searches for a `chunked` token and neither
  validates its final position nor rejects other unsupported codings. The parser retains the last
  `event:` value across later dispatched events that omit the field, while its header previously
  promised empty/default-message behavior. Empty `id:` remains set and is sent as an empty
  Last-Event-ID header; ID validation blocks CR/LF/DEL but permits other C0 controls. `Recv` also
  returns the same empty string for a valid empty-data event, timeout, close, reconnect failure, and
  allocation failure.
- **Impact:** invalid HTTP responses may be treated as SSE, event types can leak across event
  boundaries, request headers can contain unintended controls, and callers cannot distinguish data
  from transport state.
- **Likely repair point:** parse media/transfer codings exactly, reset per-event dispatch type,
  validate the full header-value control range, and return an Option/Result-shaped receive outcome.
- **Review (2026-07-16):** Confirmed and fixed on every listed axis. (1) Media type is exact:
  `text/event-stream` must be followed by end/`;`/whitespace, so `text/event-streaming` is
  rejected. (2) Transfer-Encoding accepts only the exact `chunked` coding; anything else (or a
  coding list) fails the connect with `SSE: unsupported Transfer-Encoding`. (3) Per-event
  dispatch type: `event:` now buffers into `pending_event_type` and moves to `LastEventType`
  only at dispatch, so an untyped event reports the default (empty) type instead of leaking
  the previous one. (4) ID hygiene: `sse_header_value_is_valid` rejects the full C0 range plus
  DEL, and an empty stored ID is no longer emitted as an empty `Last-Event-ID` header. (5)
  Outcome disambiguation: new registered `SseClient.RecvForResult` returns `Ok(data)` for a
  dispatched event (empty data included), `ErrStr("SSE: timeout")`, or
  `ErrStr("SSE: stream closed")`, driven by a new `last_recv_delivered` flag. network.md
  rewritten. Regression: `test_sse_validation_and_dispatch_state` in
  `RTHighLevelNetworkTests.cpp` (both header rejections trap with the right messages;
  typed→untyped event resets the type; Ok/timeout/closed outcomes all distinguished).
  **Resolved.**

### VDOC-153 — HttpClient.SetCookie bypasses the jar's safety and scope rules

- **Classification:** confirmed API/security-hardening bug
- **Area:** `Viper.Network.HttpClient` cookie jar
- **Evidence:** response cookies are syntax/domain/public-suffix checked and begin host-only, but
  manual `SetCookie` directly stores `host_only = 0` without validating domain, name, or value.
  Evaluator probes show `SetCookie("com", "super", "cookie")` is returned for `example.com`, a
  cookie for `example.com` is returned for `sub.example.com`, and names/values containing `;` are
  preserved. Empty value always deletes the cookie. Replacement compares cookie names with
  `strcasecmp`, so setting `SID` then `sid` overwrites the first even though cookie names are
  case-sensitive.
- **Impact:** the API advertised as an exact-host helper can create public-suffix supercookies,
  inject additional Cookie header syntax, broaden scope to subdomains, and cannot represent valid
  empty-valued or case-distinct cookies.
- **Likely repair point:** reuse the response-cookie validation path, make manual cookies host-only
  by default, compare names case-sensitively, and provide an explicit delete operation.
- **Review (2026-07-16):** Confirmed via the listed probes and fixed along all four recommended
  lines. `SetCookie` now validates with the response path's `cookie_name_is_valid` /
  `cookie_value_is_valid` / `cookie_domain_is_valid` (trapping on violations) and stores
  cookies HOST-ONLY — which also neutralizes the supercookie probe structurally: a cookie for
  `com` can only ever match the literal host `com` (the public-suffix deny list is therefore
  not applied to manual cookies, keeping single-label hosts like `localhost` usable).
  `replace_cookie_locked` compares names with `strcmp` (RFC 6265 case-sensitive; domains stay
  case-insensitive), so `SID`/`sid` coexist — this also corrects the response-cookie path.
  Empty values are now stored as valid cookies (only an already-expired persistent cookie
  means delete), and a new registered `HttpClient.DeleteCookie(domain, name)` provides
  explicit removal. network.md's "not a safe general cookie-injection API" block replaced with
  the validated host-only contract. Regression: `test_http_client_cookie_jar_rules` in
  `RTHighLevelNetworkTests.cpp` (finding's `com` probe now invisible to example.com,
  `;`-injection traps, host-only scope, case-distinct coexistence, empty-value storage,
  explicit deletion). **Resolved.**

### VDOC-154 — HttpClient.SetTimeout(0) silently restores 30-second operation timeouts

- **Classification:** confirmed API inconsistency
- **Area:** `Viper.Network.HttpClient`
- **Evidence:** the setter accepts and stores zero, and its header documented zero as “no timeout.”
  Request setup calls `rt_http_req_set_timeout` only when the stored value is greater than zero.
  Each fresh HttpReq therefore retains its constructor default of 30,000ms when the client value is
  zero.
- **Impact:** callers cannot disable the timeout through the advertised value and may incorrectly
  treat a long-lived request/stream as unbounded.
- **Likely repair point:** always apply the accepted timeout, including zero, or reject zero and
  expose a separate explicit timeout-disable policy.
- **Review (2026-07-16):** Confirmed and fixed via the first route: the request builder now
  calls `rt_http_req_set_timeout(req, timeout_ms)` unconditionally instead of only when
  positive, so a stored zero propagates to the request where zero already means "no socket
  timeouts / blocking connect" — `SetTimeout(0)` genuinely disables the timeout instead of
  silently restoring the 30 s constructor default. Header and network.md docs updated to state
  the disable semantics (the stale case-sensitive-headers bullet adjacent to it was also
  corrected to the VDOC-139 contract). Covered by the existing HttpClient suites (timeout
  plumbing is exercised end-to-end); the change is a one-line contract alignment verified by
  inspection of `rt_http_req_set_timeout` and `http_open_connection`'s zero handling.
  **Resolved.**

### VDOC-155 — SmtpClient Result sends can trap and constructor validation is incomplete

- **Classification:** confirmed error-model/API inconsistency
- **Area:** `Viper.Network.SmtpClient`
- **Evidence:** `SendResult` simply calls the Boolean send and wraps its returned value; it installs
  no trap recovery. TCP connect/TLS setup functions can trap before the Boolean path returns, so an
  invalid/unreachable host escapes rather than producing `ErrStr`. The installed evaluator confirms
  that `SmtpClient.New("", 25)` succeeds with empty LastError and that its `SendResult` then traps
  `Network: invalid host`. The constructor checks only a non-null C-string pointer, not nonempty or
  embedded-NUL-free text; auth/subject/body paths likewise use `strlen` and truncate.
- **Impact:** Result does not reliably contain the network failures it was introduced to model,
  and validation can target a different host/message prefix than the supplied runtime string.
- **Likely repair point:** validate full runtime string lengths at mutation/construction boundaries
  and recover transport traps inside each Result-producing operation.
- **Review (2026-07-16):** Confirmed (including the evaluator probes) and fixed as recommended.
  (1) `SendResult`/`SendHtmlResult` install trap recovery around the Boolean send (the
  `SendResult` idiom from HttpReq), converting connect/TLS traps into `ErrStr(message)` — the
  finding's `New("", 25)`-then-trap escape is gone twice over because (2) the constructor now
  validates byte length: empty hosts and embedded-NUL hosts trap at `New`. (3) Full-length
  validation at the other boundaries: `SetAuth` traps on embedded-NUL credentials, and both
  send paths fail with `LastError`/`ErrStr` "message fields must not contain NUL bytes"
  instead of transmitting a truncated prefix. network.md's caveats (per-class and the
  top-of-file NUL-truncation summary) updated. Regression:
  `test_smtp_validation_and_result_model` in `RTHighLevelNetworkTests.cpp` (empty/NUL host
  traps, connection-refused SendResult returns Err without trapping, NUL-truncating subject
  fails with a named diagnostic). **Resolved.**

### VDOC-156 — SMTP STARTTLS handshake failure can double-close a socket descriptor

- **Classification:** confirmed resource-lifetime bug
- **Area:** `Viper.Network.SmtpClient` STARTTLS upgrade
- **Evidence:** the SMTP client constructs a TLS session around `rt_tcp_socket_fd(s->tcp)` but
  detaches the Tcp wrapper only after a successful handshake. On handshake failure it calls
  `rt_tls_close`, which closes the descriptor, sets only `s->tls = NULL`, and returns. The outer send
  failure cleanup then calls `rt_tcp_close` on the still-open-marked wrapper and closes the same
  numeric descriptor again.
- **Impact:** if the operating system reuses that descriptor between closes, cleanup can close an
  unrelated connection; even without reuse, ownership invariants are violated.
- **Likely repair point:** transfer/detach the descriptor immediately after TLS session creation, or
  give TLS a non-owning/explicit-transfer constructor and centralize failure cleanup.
- **Review (2026-07-16):** Confirmed and fixed via the first route: `smtp_connect_and_handshake`
  now detaches and releases the TCP wrapper immediately after `rt_tls_new` succeeds (the TLS
  session owns the descriptor from that point), BEFORE running the handshake — so a handshake
  failure's `rt_tls_close` is the only close, and the outer cleanup finds `s->tcp == NULL`.
  The `rt_tls_new`-failure path leaves the descriptor with the still-attached wrapper (verified
  `rt_tls_new` does not close on failure), preserving single ownership there too. network.md's
  "Known STARTTLS defect" note replaced with the single-owner contract. Regression:
  `test_smtp_starttls_handshake_failure_is_clean` in `RTHighLevelNetworkTests.cpp` (mock server
  advertises STARTTLS then answers the ClientHello with garbage; the send fails as
  `ErrStr(...TLS...)` and the client object survives a follow-up attempt). **Resolved.**

### VDOC-157 — AsyncSocket leaks one producer reference per object-valued success

- **Classification:** confirmed runtime lifetime bug
- **Area:** `Viper.Network.AsyncSocket` and `Threads.Promise`
- **Evidence:** `rt_promise_set` now retains runtime-managed values until Future consumption or
  finalization. AsyncSocket workers create a Tcp, Bytes, or String, call `rt_promise_set`, and never
  release/transfer the worker's original reference. Connect, Recv, HttpGet, and HttpPost all follow
  this pattern. Promise's explicit `rt_promise_set_transferred` API exists for producer-owned
  results but is not used here.
- **Impact:** every successful object-valued async operation leaves one reference that the Future
  cannot release, leaking sockets, buffers, or strings after normal consumption.
- **Likely repair point:** use `rt_promise_set_transferred` for newly produced values or release the
  producer reference immediately after a successful retaining Set.
- **Review (2026-07-16):** Confirmed and fixed via the first suggested route: the Connect, Recv,
  HttpGet, and HttpPost workers now call `rt_promise_set_transferred`, handing the worker's
  freshly produced reference to the Future so consumption releases the only reference.
  network.md's "Known result defects" note replaced with the transfer-ownership contract.
  Regression: `test_async_socket_reference_transfer_and_boxed_send` in `RTNetworkTests.cpp`
  asserts `rt_heap_hdr(result)->refcnt == 1` after consuming and dropping the Future for both
  the Connect and Recv results (pre-fix this read 2). **Resolved.**

### VDOC-158 — AsyncSocket.SendAsync exposes a raw pointer as an Integer result

- **Classification:** confirmed runtime type/ABI bug
- **Area:** `Viper.Network.AsyncSocket.SendAsync`
- **Evidence:** the worker casts the signed byte count directly through
  `(void *)(intptr_t)sent` into generic `Promise.Set`. It does not create an Integer box, while the
  public Future surface has only object `Get` and no typed Integer getter/unbox contract. The class
  registry also declares only unqualified `obj` for both Future and result.
- **Impact:** the advertised “resolves to bytes sent” value is not a valid runtime object and cannot
  be safely consumed through normal language APIs; object inspection/unboxing may interpret a small
  integer as an address.
- **Likely repair point:** box the Integer in a defined runtime scalar object or add typed
  Promise/Future value signatures and accessors end to end.
- **Review (2026-07-16):** Confirmed and fixed via the boxing route: the send worker now
  resolves the Future with `rt_box_i64(sent)` (transferred), matching the boxed-result
  convention `Async.Run` callbacks already use, so `SendAsync.Get()` returns a real runtime
  object consumable through `Viper.Core.Box` accessors. Docs updated (method table + result
  contract). Regression: same test as VDOC-157 — asserts the Future payload is an
  `RT_BOX_I64` box whose `rt_unbox_i64` equals the payload byte count (pre-fix the payload was
  a pointer-cast scalar with box type -1). **Resolved.**

### VDOC-159 — AsyncSocket has a fixed blocking pool and non-interrupting cancellation

- **Classification:** API scalability inconsistency / needs triage
- **Area:** `Viper.Network.AsyncSocket`
- **Evidence:** all operations share one lazily initialized pool of exactly four workers and run the
  ordinary blocking Tcp/Http functions there. Four long-lived Recv tasks occupy the entire pool;
  later connect/send/HTTP tasks remain queued. Cancelling the returned Future changes Future state
  but does not cancel the queued task or interrupt a worker/socket operation.
- **Impact:** the former “non-blocking socket” description implied independent asynchronous
  progress, but a handful of reads can globally starve the surface and cancelled work continues to
  consume resources or produce side effects.
- **Likely repair point:** document and expose an executor/cancellation model, or use scalable
  platform readiness plus cancellation that reaches the underlying operation.
- **Review (2026-07-16):** Confirmed and resolved via the document-and-expose route (a
  readiness-multiplexed rewrite is out of proportion for this surface). The shared pool is no
  longer fixed at four workers: it defaults to 2×logical cores (floor 8, cap 1024 — matching
  the WS-server sizing), so a handful of long reads no longer starves the whole surface, and a
  new registered `AsyncSocket.SetPoolSize(n)` exposes the executor knob (before first use;
  trapping afterwards, since the pool is a process singleton). The executor and cancellation
  models are now documented precisely in network.md, including the practical escape hatch
  (close the underlying Tcp to interrupt a blocked operation). Existing AsyncSocket suites
  (harden + RTNetworkTests transfer test) pass on the resized pool. **Resolved.**

### VDOC-160 — Multiple network functions continue after a trap hook returns

- **Classification:** confirmed systemic runtime control-flow bug
- **Area:** network runtime trap discipline
- **Evidence:** `rt_trap.h` explicitly allows a configured hook to return and requires local code to
  stop unsafe flow. Beyond VDOC-141, confirmed violations include: HttpsServer's invalid-port path
  continuing into allocation; ConnectionPool invalid receiver/host/key paths continuing into
  dereference or socket work; Multipart mutators dereferencing a null receiver after trapping;
  HttpClient request methods continuing after null programming errors; SSE Connect freeing its
  object, trapping, then returning the freed pointer (and issuing both TLS-specific and generic
  traps); and AsyncSocket Recv/HTTP validation continuing after a trap.
- **Impact:** embedders/tests using a returning hook can observe duplicate traps, null/dangling
  dereferences, network activity after rejection, or use-after-free. This violates the runtime's
  documented recovery contract even when the ordinary VM longjmps on the first trap.
- **Likely repair point:** audit every network trap site for an immediate typed return/goto cleanup,
  and add a returning-hook validation test independent of CTest execution in this review.
- **Review (2026-07-16):** Confirmed and fixed with a full sweep. Every named site now stops
  after its trap: HttpsServer's invalid-port path returns NULL before allocation; ConnectionPool
  NULL-receiver/invalid-host/oversized-key paths return NULL; Multipart's header-param helper
  returns its fallback instead of a NULL cstr; the four HttpClient verb wrappers return NULL on
  a NULL receiver; SSE Connect raises exactly ONE categorized trap (TLS-specific OR generic,
  no longer both) and returns NULL instead of the freed pointer, and its OOM path no longer
  falls through into `sse_open_url` on a freed object; AsyncSocket Recv/HttpGet/HttpPost
  validation returns a failed Future after trapping. A scripted sweep of all 32 remaining
  `rt_trap*` fallthrough candidates in `src/runtime/network/` verified each is safe by design
  (terminal `rt_abort`, worker-local setjmp recovery, or cleanup-then-return). The
  returning-hook validation harness requested by the finding exists as the dedicated
  `test_rt_trap_return_network` binary (installed returning `vm_trap`, built for VDOC-141) and
  was extended with four probes asserting exactly one trap and a NULL/stopped result for
  ConnectionPool, HttpsServer, Multipart, and SSE. **Resolved.**

### VDOC-161 — Convenience network surfaces inconsistently truncate strings at embedded NUL

- **Classification:** confirmed input-validation/API inconsistency
- **Area:** HttpRouter, SseClient, SmtpClient, NetUtils, and WebSocket servers
- **Evidence:** core connection APIs added runtime-length checks, but these surfaces still copy or
  parse `rt_string_cstr` with `strlen`/`strdup`: router method/pattern/path and parameter names; SSE
  URL/event/ID fields; SMTP host/auth/subject/body; NetUtils host/IP/CIDR; and WebSocket-server text
  broadcasts. `WssServer.New` calls `strdup(rt_string_cstr(cert_file/key_file))` without even
  checking for null, so null credential handles can crash instead of trapping. Embedded-NUL
  certificate paths and host text silently target only the prefix.
- **Impact:** the runtime value logged/validated by an application can differ from the OS/protocol
  string actually used; text payloads are truncated; and the WSS constructor has a direct null
  dereference path.
- **Likely repair point:** centralize full-length string-to-C-field validation and use byte lengths
  for payloads; validate certificate handles before `strdup`.
- **Review (2026-07-16):** Confirmed and fixed across every listed surface (SMTP and
  `NetUtils.IsPortOpen` were already handled under VDOC-155/147). HttpRouter rejects
  embedded-NUL methods/patterns at registration (trap, including the Get/Post/Put/Delete
  convenience wrappers) and treats NUL-bearing method/path inputs to `Match` as no-match.
  `SseClient.Connect` rejects NUL-bearing URLs. `NetUtils.MatchCidr`/`IsPrivateIp` return false
  for NUL-bearing ip/cidr text instead of classifying a prefix. Both WebSocket servers' text
  broadcasts (and the plain per-client send) now use the runtime byte length, so embedded NUL
  is payload data. `WssServer.New` validates certificate/key handles before `strdup` — NULL
  handles and embedded-NUL paths trap instead of dereferencing NULL. network.md updated in all
  five places plus the top-of-file NUL summary. Regressions: router NUL-pattern trap in
  `RTNetworkRuntimeTests.c`; NUL-bearing broadcast byte-length and WssServer credential-trap
  assertions in `RTHighLevelNetworkTests.cpp`. **Resolved.**

### VDOC-162 — String.FromSingle has a mismatched C ABI

- **Classification:** confirmed runtime ABI bug
- **Area:** `Viper.String.FromSingle` across registry dispatch and runtime backends
- **Evidence:** the runtime registry, generated signature tables, and emitted IL declare
  `Viper.String.FromSingle(f64) -> str`, while the exported C function `rt_str_f_alloc` accepts a C
  `float`. The signature-handler path explicitly casts its double argument to float, but direct
  symbol dispatch has no adapter. With the existing arm64 evaluator, both Zia and BASIC produce
  `"0"` for `FromSingle(1.5)`; Zia produces `"126443839488"` for `FromSingle(3.14)`.
- **Impact:** an advertised scalar formatter returns values derived from the wrong ABI bit layout,
  and behavior depends on which runtime dispatch path a backend uses.
- **Likely repair point:** make the exported ABI accept `double` and narrow inside it, or route every
  direct call through an f64-to-float adapter consistent with `invokeRtStrFAlloc`.
- **Review (2026-07-16):** Confirmed (both listed repros reproduced) and fixed via the first
  route: `rt_str_f_alloc` now takes a C `double` — matching the registered `str(f64)`
  signature on every dispatch path — and narrows to `float` internally so the output keeps
  single-precision semantics. The `invokeRtStrFAlloc` signature-handler adapter passes the
  double straight through (the function narrows), and no other C callers existed. Verified
  live: `FromSingle(1.5)` → `1.5` (was `0`) and `FromSingle(3.14)` → `3.14000010490417` (the
  true float32 value; was garbage), with identical output from the VM and a natively compiled
  binary — VM==native determinism restored for this entry point. Regression: `from_single_*`
  probes in `45_runtime_api_conformance.zia`. **Resolved.**

### VDOC-163 — String.FromI16 and FromI32 reach invalid IL from Zia

- **Classification:** confirmed compiler/API inconsistency
- **Area:** narrow runtime parameters and cross-language String formatting surface
- **Evidence:** both functions are registered with `i16`/`i32` parameters, but ordinary Zia integer
  expressions are i64. `Viper.String.FromI16(42)` and `FromI32(42)` pass semantic analysis and then
  fail IL verification because the call arguments remain i64. BASIC resolves `FromI32(42)` and
  returns `"42"`, but reports unknown procedure `viper.string.fromi16` for the parallel public name.
- **Impact:** the generated and handwritten public APIs cannot be called from Zia and are not even
  mutually available from BASIC; a source type mismatch escapes to the IL verifier.
- **Likely repair point:** expose language-level i64 parameters with checked/documented narrowing or
  teach runtime-call lowering to insert the required conversions, then make BASIC name resolution
  expose the same pair.
- **Review (2026-07-16):** Confirmed both halves and fixed via the lowering route. Zia:
  `emitRuntimeCallResult` now inserts `cast.si_narrow.chk` conversions for runtime parameters
  declared `i16`/`i32`, so `FromI16(42)`/`FromI32(42)` verify and run (and out-of-range values
  such as `FromI16(70000)` trap with the checked-narrow diagnostic instead of silently
  wrapping). BASIC: `mapIlToBasic` was silently dropping every `i16`-parameter runtime
  function from the procedure registry (which is why `FromI32` resolved but
  `viper.string.fromi16` was "unknown"); `i16` now maps to the BASIC integer type and the
  runtime-call arg coercion emits `narrow_to(64, 16)` alongside the existing 32-bit case, so
  both names resolve in both languages. Verified live in both frontends; full basic+zia suite
  (699 tests) green. Regression: `from_i16_narrowed_call`/`from_i32_narrowed_call` probes in
  `45_runtime_api_conformance.zia`. **Resolved.**

### VDOC-164 — String.SplitFields silently accepts malformed quoting

- **Classification:** parser/API inconsistency / needs triage
- **Area:** comma-separated field parsing
- **Evidence:** `rt_str_split_fields` toggles quote state for every quote byte, never reports an
  unclosed quote, and permits quotes after unquoted text. The evaluator parses `"\"a,b"` and
  `"a\"x,b"` as one field each instead of rejecting the malformed record; the latter silently
  suppresses the comma delimiter. Outer quotes are stripped only when both trimmed endpoint bytes
  are quotes, independently of where quote state changed inside the field.
- **Impact:** malformed user data can merge columns or preserve/strip quote bytes unpredictably,
  while callers receive a normal Seq with no failure signal.
- **Likely repair point:** define whether this is deliberately lenient INPUT syntax or CSV syntax;
  otherwise enforce quote placement/closure and provide a Result/Option failure surface.
- **Review (2026-07-16):** Confirmed and resolved by defining the contract both ways. The plain
  splitter is now documented as deliberately lenient INPUT-style parsing (it backs BASIC INPUT
  field splitting, where tolerance is the point), and a new registered
  `Viper.String.SplitFieldsResult` provides the strict surface: it validates quote structure
  (quoted fields must open at field start, close exactly once, be followed only by whitespace
  before the delimiter; no quotes inside unquoted text; no EOF inside a quote) and returns
  `Ok(Seq[str])` or `ErrStr` naming the violation. Both of the finding's repros now yield
  precise errors ("unclosed quote" / "quote inside unquoted field") through the strict
  surface. core.md rewritten. Regression: split-fields probes in
  `45_runtime_api_conformance.zia`. **Resolved.**

### VDOC-165 — String case-shape methods truncate at embedded NUL

- **Classification:** confirmed byte-string data-loss bug
- **Area:** `CamelCase`, `PascalCase`, `SnakeCase`, `KebabCase`, and `ScreamingSnake`
- **Evidence:** the shared splitter consumes the runtime's explicit byte length but stores each word
  as a C string. All five formatters then call `strlen` on those words. The evaluator shows that
  CamelCase and SnakeCase turn bytes `61 62 00 63 64` (`ab\0cd`) into only `61 62`; the suffix is
  silently discarded.
- **Impact:** these methods violate Viper.String's length-aware byte-string contract and can lose
  data that other String methods preserve.
- **Likely repair point:** carry a byte length for every split word rather than repacking words as
  NUL-terminated strings.
- **Review (2026-07-16):** Confirmed and fixed exactly as recommended: `split_words` now records
  each word's exact byte length into a parallel `word_lens` array (allocated by
  `split_words_dynamic`), and all five formatters iterate `word_lens[w]` instead of
  `strlen(word)`, so embedded NUL bytes flow through as ordinary word bytes. core.md's
  truncation caveat replaced with the byte-string contract. Regression:
  `test_case_shapes_preserve_embedded_nul` in `RTCaseConvertTests.cpp` (all five conversions
  on `ab\0cd` preserve the full five bytes with correct casing on both sides of the NUL).
  **Resolved.**

### VDOC-166 — Flip and Mid treat malformed UTF-8 as characters

- **Classification:** confirmed validation inconsistency
- **Area:** UTF-8-shaped String traversal
- **Evidence:** `Flip` derives a unit length from only the lead byte and checks remaining length, not
  continuation bytes. It accepts bytes `c2 41` and returns the same malformed pair. The Mid helper
  checks continuation prefixes but does not reject overlong encodings, surrogates, or values above
  U+10FFFF: `MidLen(c0 80, 1, 1)` returns `c0 80`. Existing strict Codec validation and Like's
  partial validator reject some of the same inputs.
- **Impact:** claims about Unicode code points are false, malformed strings cross APIs with
  incompatible behavior, and Flip can group unrelated bytes into one apparent character.
- **Likely repair point:** use the strict shared UTF-8 decoder for unit boundaries, or name and
  document these operations as deliberately structural byte grouping.
- **Review (2026-07-16):** Confirmed both repros and fixed via the strict-shared-decoder route.
  A new internal `rt_utf8_strict_step` (factored from Like's validator: rejects bad
  lead/continuation bytes, overlong encodings, surrogates, and scalars above U+10FFFF, without
  trapping so each caller raises its own context) now drives `Flip`'s two passes,
  `utf8_char_to_byte_offset` (the Mid helper), and `like_utf8_step`, so all codepoint-aware
  String operations share exactly one definition of a valid code point. `Flip("\xc2\x41")`
  and `MidLen("\xc0\x80", 1, 1)` now trap with invalid-UTF-8 diagnostics instead of returning
  the malformed bytes. core.md rewritten to state the strict contract (byte-oriented methods
  still accept arbitrary bytes). Regression: `test_flip_and_mid_reject_malformed_utf8` in
  `RTStringEdgeTests.cpp` (both repros trap; valid multibyte input still flips by codepoint);
  the 69-test string sweep stays green. **Resolved.**

### VDOC-167 — String padding can create malformed UTF-8

- **Classification:** confirmed Unicode/API bug
- **Area:** `String.PadLeft` and `PadRight`
- **Evidence:** both functions accept a String `padChar` but repeat only `pad_str->data[0]` until a
  byte width is reached. The evaluator reports hex `e7c3a9` for
  `PadLeft("é", 3, "界")`: the first byte of `界` is prepended without its continuation bytes.
  Passing that result to `Like("_")` traps on invalid UTF-8.
- **Impact:** ordinary valid UTF-8 arguments can produce invalid output, and the name `padChar`
  implies a character contract that the implementation does not meet.
- **Likely repair point:** either require/document one ASCII byte and reject multibyte padding, or
  repeat a complete UTF-8 unit with a clearly defined byte/code-point width policy.
- **Review (2026-07-16):** Confirmed the repro and fixed via the first route, which matches the
  byte-width contract: `PadLeft`/`PadRight` now require the padding string to be exactly one
  byte and trap (`String.PadLeft: padding must be a single byte`) on multibyte input instead
  of repeating a lead byte into malformed UTF-8. The empty-padding no-op is preserved. core.md
  rewritten to state the single-byte requirement. Regression:
  `test_pad_rejects_multibyte_padding` in `RTStringExtTests.cpp` (both directions trap on `界`;
  single-byte padding still pads). **Resolved.**

### VDOC-168 — String index methods disagree on an empty needle

- **Classification:** API inconsistency / needs triage
- **Area:** `IndexOf`, `IndexOfFrom`, and `LastIndexOf`
- **Evidence:** `IndexOf("x", "")` returns 1 and `IndexOfFrom` returns its clamped one-based start,
  following BASIC INSTR semantics. `LastIndexOf("x", "")` returns 0, the same sentinel used for a
  miss. All three otherwise return one-based byte positions and use 0 for absence.
- **Impact:** generic search code cannot apply one empty-needle rule across the first/from/last
  variants, and LastIndexOf cannot distinguish its chosen empty case from a real miss.
- **Likely repair point:** define a shared empty-needle policy or give LastIndexOf an explicitly
  documented compatibility rationale.
- **Review (2026-07-16):** Confirmed and fixed with a shared policy derived from the existing
  INSTR-style behavior: an empty needle matches at every position `1..Length+1`, so `IndexOf`
  returns the first match (1), `IndexOfFrom` its clamped start (already returned `Length+1` at
  the end boundary), and `LastIndexOf` now returns the final match `Length + 1` instead of the
  0 miss sentinel — generic search code can apply one rule across all three, and a real miss
  remains uniquely 0. core.md's "deliberately special but not uniform" caveat replaced with
  the unified rule. Regression: empty-needle assertions added to `test_last_index_of` in
  `RTStringExtTests.cpp`. **Resolved.**

### VDOC-169 — Tracked C/C++ files are missing the required Viper source header

- **Classification:** repository-policy inconsistency
- **Area:** source-file documentation and contribution policy
- **Evidence:** `scripts/audit_doc_comments.sh` currently reports 18 tracked, non-generated C/C++
  files without a recognized project/file header: one Xenoscape sound generator, eleven benchmark
  sources, and six runtime/unit-test sources. The repository operating guide requires the full Viper
  source header on new or modified source files.
- **Impact:** provenance and file-purpose documentation are inconsistent across tracked sources,
  and the repository's own audit has a nonzero baseline that can hide newly introduced omissions.
- **Next check:** determine which files predate the policy, add compliant headers when each area is
  next modified, and make the audit distinguish an intentional legacy baseline if one must remain.
- **Review (2026-07-16):** Already remediated during the VDOC-133 pass: all 18 files (the
  Xenoscape sound generator, the eleven benchmark sources, and the six runtime/unit-test
  sources) received full Viper source headers, taking `scripts/audit_doc_comments.sh`'s
  "Files missing file-level header" count to 0 — re-verified today. With a zero baseline, any
  newly introduced omission is immediately visible, so no legacy-baseline mechanism is needed.
  **Resolved.**

### VDOC-170 — The runtime-header documentation audit has a four-digit baseline

- **Classification:** implementation-documentation debt / needs triage
- **Area:** C runtime header contracts
- **Evidence:** `scripts/audit_doc_comments.sh` reports 1,127 runtime-header declarations without an
  adjacent recognized documentation comment at this review checkpoint. Examples begin in the audio
  diagnostics/effects headers and include the numeric-buffer surface. The script is deliberately
  heuristic, so this aggregate count is an audit backlog rather than proof that every reported
  declaration lacks a usable contract.
- **Impact:** the implementation-adjacent C surface is much less completely documented than the
  handwritten/generated public references imply, and meaningful new regressions are hard to spot
  against such a large warning baseline.
- **Next check:** validate and reduce the findings area by area during this documentation review,
  while refining false-positive handling in the audit rather than bulk-adding low-value comments.
- **Review (2026-07-16):** In progress as directed, treated as tracked debt with a shrinking
  baseline rather than a one-shot fix. This pass: (1) refined the audit's false-positive
  handling — multi-line function-pointer struct members and callback typedefs (documented at
  the struct/typedef level) are no longer miscounted as undocumented prototypes; (2) fully
  documented the audit's current head-of-queue areas with real contracts (not filler):
  `rt_quests.h` (19 declarations — registration/lifecycle/query/event/persistence semantics),
  `rt_gameui_internal.h` (23 — saturating math, UTF-8 caret helpers, validation/lifetime
  helpers), plus the four headers already covered under VDOC-133. Baseline: 1,129 at the
  finding → 1,004 today. The audit lists the next 50-item area (`rt_scene_editor.h`) each run,
  so reduction continues area by area as subsequent findings touch each subsystem.
  **Resolved as tracked, monotonically shrinking debt.**

### VDOC-171 — The example audit skipped real APIs whose method names end in `Result`

- **Classification:** documentation-validation tooling bug
- **Status:** corrected in `scripts/audit_bible_examples.py`
- **Area:** runnable documentation-fence classification
- **Evidence:** the placeholder heuristic treated any capitalized token ending in `Result` as a
  domain type. Because a word boundary also exists after `.`, qualified runtime members such as
  `Cipher.DecryptResult` and `Tls.ConnectResult` matched and were skipped without compilation. The
  crypto review exposed five real compiler diagnostics hidden by that rule. The heuristic now
  requires a standalone type-like name rather than a member following `.`, and low-level
  `Viper.Crypto.Tls` examples are classified as side-effectful so they are compile-checked without
  making network connections.
- **Impact:** a validation report could appear clean while exactly the Result-oriented examples
  recommended for robust error handling were broken.
- **Review (2026-07-16):** Verified the recorded correction is present in
  `scripts/audit_bible_examples.py`: the placeholder heuristic uses a negative lookbehind
  (`(?<!\.)`) so qualified members like `Cipher.DecryptResult` no longer match the
  domain-type skip rule, and only standalone type-like names are treated as placeholders.
  Nothing further to change; the five previously hidden crypto diagnostics were surfaced and
  handled by the crypto review that produced these findings. **Resolved (verified).**

### VDOC-172 — Cipher's raw-key nonce construction has only a 32-bit cross-process margin

- **Classification:** confirmed cryptographic nonce-design defect / needs security triage
- **Area:** `Viper.Crypto.Cipher` raw-key ChaCha20-Poly1305 and AES-256-GCM formats
- **Evidence:** `cipher_random_nonce()` fills only the first four nonce bytes from the CSPRNG and
  writes a process-global 64-bit atomic counter into the remaining eight bytes. Counter values are
  unique within one process until exhaustion, but the counter resets on every process start. Two
  processes or restarts that reuse the same raw key and draw the same 32-bit prefix will therefore
  reuse the entire sequence of AEAD nonces at matching counter positions. The counter is stored as
  signed `int64_t`; the `fetch_add(...) + 1` path also has no safe exhaustion handling before signed
  overflow. Password mode derives a fresh key from an independently random 128-bit salt per call,
  so the raw-key/restart case is the direct concern.
- **Impact:** a persistent application key reused across enough process starts gets a birthday
  bound based on 32 random bits, not the documented 96-bit random-nonce space. Nonce reuse under
  either exposed AEAD destroys the intended confidentiality/authentication security.
- **Likely repair point:** define a per-key nonce policy with persistent state, use a full-width
  random nonce with an explicit per-key message bound, or derive a fresh subkey from a larger random
  message salt; use an unsigned counter with a checked exhaustion path.
- **Review (2026-07-16):** Confirmed and fixed via the full-width-random-nonce route.
  `cipher_random_nonce` now fills all 12 nonce bytes from the CSPRNG instead of 4 random + an
  8-byte process-global counter, so every AEAD nonce is independently random per message AND
  per process/restart — the 32-bit cross-process collision window (and the signed-counter
  overflow path) are gone; the process counter global and its now-dead `write_be64` helper were
  removed. This restores the documented 96-bit random-nonce space; the per-key message bound
  (~2^32 messages per NIST SP 800-38D) is now documented in crypto.md alongside a
  rotate-before-the-bound note for high-volume use. Ciphertext format is byte-identical (nonce
  is still a 12-byte field), so decrypt is fully backward compatible. Encryption is inherently
  non-deterministic, so VM/native determinism is unaffected. Regression:
  `test_key_nonce_is_full_random` in `RTCipherTests.cpp` (64 encryptions of one plaintext under
  one key all produce distinct nonces, with non-monotonic low bytes); the full cipher suite
  stays green. **Resolved.**

### VDOC-173 — Random legacy ciphertext prefixes can collide with current format magic

- **Classification:** confirmed backward-compatibility/framing defect
- **Area:** `Cipher.Decrypt*` and `Aes.DecryptStr*`
- **Evidence:** legacy password Cipher frames begin with a random salt, legacy raw-key Cipher frames
  begin with a random nonce, and legacy AES string frames begin with a random IV. Decrypt dispatch
  decides that a frame is current solely by comparing those first four bytes with current magic
  (`VCP2`/`VCA1`, `VCK2`/`VKA1`, or `VAG1`). A legacy frame whose random prefix happens to match is
  parsed only as the current format and is not retried as legacy after iteration or authentication
  failure. For each Cipher legacy form two magic values are recognized (about one collision in
  2^31 frames); AES recognizes one (about one in 2^32).
- **Impact:** the absolute backward-compatibility claim is false: a valid archived legacy payload
  can become undecryptable based only on its random prefix.
- **Likely repair point:** retain external format metadata or introduce unambiguous framing for
  migrated data. Any fallback after current-format authentication failure needs explicit downgrade
  analysis rather than an unconditional retry.
- **Review (2026-07-16):** Confirmed and resolved with the downgrade analysis the finding
  demanded, treating the two format families differently by their security properties.
  **Authenticated Cipher formats (VCP2/VCK2):** compatibility mode now falls back to the legacy
  interpretation whenever the current-format parse fails to authenticate (password path via a
  `try_legacy` path that also catches the short/bad-iteration structural cases; raw-key path via
  a versioned→unversioned retry loop). The fallback is provably safe — the legacy Cipher format
  is itself AEAD-authenticated, so it only yields plaintext for a genuinely valid legacy frame,
  a real current-format frame authenticates on the first try and never reaches it, and it never
  runs in approved mode. Backward compatibility for authenticated formats is now unconditional.
  **Unauthenticated legacy AES-CBC (VAG1):** deliberately NOT given an auto-fallback — retrying
  CBC after a GCM auth failure would return unauthenticated garbage for tampered frames (a
  downgrade), so the ~2^-32 colliding legacy IV is documented as undecryptable with a
  re-encrypt recommendation (rt_aes.c comment + crypto.md). Regression:
  `test_key_magic_collision_legacy_fallback` in `RTCipherTests.cpp` constructs a valid legacy
  raw-key frame with a forced `VCK2`-prefixed nonce and confirms it decrypts through the
  fallback. **Resolved.**

### VDOC-174 — `Password.NeedsRehash` marks stronger custom scrypt hashes as stale

- **Classification:** confirmed password-policy bug
- **Area:** `Viper.Crypto.Password.NeedsRehash`
- **Evidence:** `HashScryptParams` accepts every supported tuple whose `N`, `r`, and `p` are each at
  least the defaults (`16384`, `8`, `1`). In compatibility mode, `NeedsRehash` nevertheless returns
  current only when all three stored values equal the defaults exactly. A valid hash created with
  `N=32768, r=8, p=1`, for example, is immediately reported as needing rehash.
- **Impact:** applications following the advertised login-upgrade pattern can rehash on every login
  and replace a deliberately stronger record with the weaker default tuple.
- **Likely repair point:** treat supported parameters at or above the active policy minimum as
  current, or define a monotonic policy comparison that accounts for the intended scrypt cost
  tradeoffs instead of exact tuple equality.
- **Review (2026-07-16):** Confirmed and fixed with the recommended monotonic comparison.
  `rt_password_needs_rehash`'s scrypt branch replaced exact-tuple equality
  (`log2n != DEFAULT || r != DEFAULT || p != DEFAULT`) with a below-minimum test
  (`log2n < MIN_N_LOG2 || r < MIN_R || p < MIN_P`), matching the PBKDF2 branch's
  `iterations < DEFAULT` logic. A hash at or above the policy minimum — including a deliberately
  stronger `N=32768` or `p=2` tuple — is now current and no longer rehashed-and-downgraded on
  every login. crypto.md rewritten (and the misleading `' 1 in compatibility mode` example
  comment corrected). Regression: the existing Test 3 in `RTPasswordTests.cpp` was inverted to
  assert the stronger `p=2` hash does NOT need rehash, plus a new larger-N (`N=32768`) case.
  **Resolved.**

### VDOC-175 — The Unix random fallback has an unsynchronized first-use descriptor read

- **Classification:** confirmed C data race / portability bug
- **Area:** `Viper.Crypto.Rand` on non-Apple POSIX `/dev/urandom` fallback paths
- **Evidence:** `rand_urandom_fd()` reads its function-static `fd` in an unlocked fast path before
  taking the mutex, while the first initializing call writes `fd` under that mutex. Concurrent
  first use therefore performs an unsynchronized read/write of the same C object. Linux reaches
  this path when `getrandom()` is unavailable; other supported Unix targets use it directly.
- **Impact:** the implementation has undefined behavior in the exact concurrent initialization
  case despite the former blanket thread-safety claim.
- **Likely repair point:** use `pthread_once`, an atomic descriptor state, or keep every read and
  write of the cache under the mutex.
- **Review (2026-07-16):** Confirmed and fixed via the atomic-descriptor-state route. The
  cached `/dev/urandom` descriptor in `rand_urandom_fd` is now accessed only through
  `__atomic_*`: the lock-free fast path uses `__ATOMIC_ACQUIRE` and the initializing write
  publishes with `__ATOMIC_RELEASE` under the existing mutex, so the concurrent first-use
  read/write is data-race-free and the fast path stays lock-free after initialization. (The
  parallel byte-level path in `rt_entropy_platform_posix.c` opens/closes per call and never
  cached, so it was already race-free.) crypto.md's thread-safety caveat replaced with the
  clean guarantee. Verified by inspection and the crypto/rand/concurrency suites (19 tests
  green); the affected fallback branch compiles only where `getrandom`/`getentropy` is absent,
  and the fix is a standard acquire/release singleton with no behavioral change to output.
  **Resolved.**

### VDOC-176 — SipHash seed initialization can publish predictable or racily accessed state

- **Classification:** confirmed security/control-flow and concurrency defects
- **Area:** `Hash.Fast*` and the runtime's shared keyed collection hash
- **Evidence:** when the CSPRNG fails, `hash_seed_init()` calls `rt_trap` but continues if an allowed
  embedder/test trap hook returns, marks the seed initialized, and leaves both 64-bit key halves at
  their zero-initialized values. On native MSVC, the inline fast path also reads
  `rt_siphash_seeded_` as a plain `int` while the initialization callback writes it, creating a
  formal data race even though `InitOnceExecuteOnce` protects the callback itself.
- **Impact:** a returning trap hook can permanently select a known SipHash key, defeating the
  HashDoS purpose of the keyed hash; concurrent Windows first use has undefined behavior.
- **Likely repair point:** stop local control flow and never publish seeded state after entropy
  failure, and make callers enter `InitOnceExecuteOnce` unconditionally or use an interlocked state
  for the fast path.
- **Review (2026-07-16):** Confirmed both defects and fixed as recommended. (1) Predictable-key
  path: `hash_seed_init` now returns early on CSPRNG failure with `rt_trap` **followed by
  `rt_abort`**, so a returning trap hook can never reach the `seeded = 1` publication — a keyed
  hash with a zero key is a security failure that terminates the process instead of silently
  downgrading HashDoS protection. The seeded flag and key are only ever published after a
  successful entropy read. (2) MSVC data race: the inline `rt_siphash24` fast path no longer
  reads the plain `int rt_siphash_seeded_` on real MSVC; it enters `InitOnceExecuteOnce`
  unconditionally (cheap after first completion, and it provides the acquire/release
  synchronization for the key), eliminating the racy plain read while the non-MSVC path keeps
  its `__atomic` acquire-load fast path. crypto.md's Hash.Fast caveat replaced with the clean
  guarantee. The abort-on-entropy-failure and MSVC-race branches are not unit-testable without
  breaking the CSPRNG or a Windows TSAN run; verified by inspection and the passing hash suite
  (normal seeding/hashing unchanged). **Resolved.**

### VDOC-177 — Bytes methods dereference failed receiver checks after recoverable traps

- **Classification:** confirmed systemic runtime trap-discipline bug
- **Area:** `Viper.Collections.Bytes` and crypto/network callers that accept Bytes
- **Evidence:** `rt_bytes_require()` returns `NULL` for a null or wrong-class receiver (trapping for
  the latter), but many callers immediately dereference it. Direct examples include `Len`,
  `IsEmpty`, `Get`, `Set`, `Slice`, `ToStr`, `ToHex`, `Fill`, `Find`, `Clone`, and the typed integer
  read/write helpers. Several construction paths likewise dereference a `NULL` returned by
  `rt_bytes_alloc()` if an allocation trap hook returns.
- **Impact:** the runtime trap contract explicitly permits test/embedder hooks to return, yet an
  invalid Bytes handle or allocation failure can then become a null dereference or unsafe memory
  access. Crypto and TLS wrappers that delegate to these helpers inherit that failure mode.
- **Likely repair point:** require every checked cast/allocation result to be tested before access,
  return a type-appropriate sentinel after a trap, and cover the surface with a returning-hook
  audit.
- **Review (2026-07-16):** Confirmed and fixed by guarding every checked-cast/allocation result
  before use. A sweep of all 28 `rt_bytes_require` sites and the `rt_bytes_alloc` construction
  paths found: the binary read/write helpers were already safe (their NULL flows into
  `bytes_check_bounds`, which traps and returns 0), and `Data`/`ToBase64`/`ExtractRaw` already
  guarded. The remaining 11 accessor sites (`Len`, `IsEmpty`, `Get`, `Set`, `Slice`, `Copy`
  ×2, `ToStr`, `ToHex`, `Fill`, `Find`, `Clone`) now check the require result and return a
  type-appropriate sentinel (0 / 1 / -1 / empty Bytes / empty String / void) after a returning
  trap; the four construction paths (`FromStr`, `FromHex`, base64 decode, `FromRaw`) return
  NULL after a returning allocation trap instead of dereferencing. Normal (trapping) behavior
  is unchanged. Regression: `test_invalid_handle_with_returning_hook` in `RTBytesTests.cpp`
  installs a RETURNING `vm_trap` and calls the accessors on a non-Bytes object, asserting each
  returns its sentinel without a NULL dereference. Crypto/TLS wrappers that delegate to these
  helpers inherit the fix. **Resolved.**

### VDOC-178 — Legacy AES string decryption truncates passwords at 256 bytes

- **Classification:** confirmed legacy security/API inconsistency
- **Area:** `Viper.Crypto.Aes.DecryptStr` legacy CBC compatibility path
- **Evidence:** `derive_key_legacy()` hashes a fixed domain separator, a one-byte length, and at
  most the first 256 password bytes, then performs 10,000 SHA-256 rounds. For every password of 256
  bytes or more the stored length byte wraps to zero, and suffix bytes after the first 256 never
  affect the key. The adjacent source comment instead called this a single-pass SHA-256 derivation.
  Current `VAG1` encryption uses the full stored password length with PBKDF2 and is unaffected.
- **Impact:** distinct long passwords with the same first 256 bytes are equivalent for old CBC
  payloads, a material limitation hidden by the public length-aware password claim.
- **Likely repair point:** preserve the derivation only for reading old data, disclose its cap, and
  migrate successfully decrypted content immediately to the authenticated current format.
- **Review (2026-07-16):** Confirmed and resolved as a disclosure fix (the derivation math must
  NOT change — it is bit-compatible with existing legacy CBC ciphertexts, so altering it would
  make old data undecryptable). `derive_key_legacy` is used only on the read-only legacy path;
  current `VAG1` encryption already uses the full password length with PBKDF2 and is unaffected.
  Added an explicit WEAKNESS note in the code (one-byte length prefix → zero for ≥256-byte
  passwords; only the first 256 bytes contribute; re-encrypt on successful decrypt) and
  sharpened the public crypto.md disclosure to state the length-prefix wrap and the
  first-256-bytes cap precisely. No behavior change. **Resolved (disclosure).**

### VDOC-179 — TLS `ConnectFor` applies its timeout repeatedly instead of as a deadline

- **Classification:** confirmed timeout/API inconsistency
- **Area:** `Viper.Crypto.Tls.ConnectFor*` and `ConnectOptions*`
- **Evidence:** each address returned by `getaddrinfo` receives the full timeout independently. Once
  connected, the same value becomes the socket timeout used again by individual handshake and later
  record reads/writes; header and payload reads can each consume it. Values at or below zero are
  normalized to the 30-second default rather than representing an immediate or unbounded operation.
- **Impact:** `ConnectFor(..., 5000)` can take far longer than five seconds, and the timeout remains
  a per-I/O budget after connection. Callers cannot use the API as an overall connection deadline.
- **Likely repair point:** compute one monotonic deadline for resolution/address attempts/handshake,
  expose post-connect I/O timeout separately, and either reject or explicitly name the nonpositive
  default sentinel.
- **Review (2026-07-16):** Confirmed and fixed as recommended. `rt_tls_connect` now computes one
  monotonic deadline (`rt_clock_ticks_us`) before the address loop; each candidate's connect wait
  uses the remaining budget and the loop stops when it is exhausted, so `ConnectFor(t)` honors
  `t` across all resolved addresses instead of `N×t`. The handshake is bounded by the remaining
  budget (min 1 ms) via the socket timeout, and only AFTER a successful handshake is the socket
  timeout reset to the configured value as the separate post-connect per-I/O budget — so the
  connection deadline and the post-connect I/O timeout are now distinct. The nonpositive→30s
  default sentinel is documented in crypto.md. Regression:
  `test_tls_connect_for_honors_deadline` in `RTTlsWrapperTests.cpp` (a 500 ms ConnectFor to a
  TEST-NET-1 blackhole returns well under 3 s). crypto.md timeout-scope wording rewritten in all
  three places. **Resolved.**

### VDOC-180 — BASIC `PRINT` double-releases string-valued runtime properties

- **Classification:** confirmed BASIC lowering / ownership bug
- **Area:** BASIC console `PRINT` with runtime-class string property getters
- **Evidence:** with an explicitly typed `Viper.Crypto.Tls` receiver, the minimal statement
  `PRINT conn.Host` fails IL verification with `call %t29: double release of %29`. `conn.Host`
  lowers through the runtime-property catalog, whose string result is already registered with
  `deferReleaseStr`. `IoStatementLowerer::lowerPrint` then classifies every `MemberAccessExpr` as a
  borrowed lvalue and emits an additional retain/release pair; the statement-boundary deferred
  release is consequently rejected as a second release. `PRINT conn.Port` passes, as does assigning
  `conn.Host` to a typed string local before printing it.
- **Impact:** ordinary direct printing of a string property on a runtime object produces invalid IL
  and a compile-time `V-IL-VERIFY` error. The TLS documentation example exposed the issue, but the
  ownership path is generic to string-valued runtime properties.
- **Likely repair point:** distinguish borrowed field loads from owned property-getter results in
  `IoStatementLowerer::lowerPrint`, or carry ownership in `RVal` so the consumer can suppress or
  consume the already-deferred release without AST-shape guesses.
- **Review (2026-07-16):** Confirmed (`PRINT conn.Host` reproduced the `double release of %29`
  V-IL-VERIFY error) and fixed by distinguishing owned from borrowed in `lowerPrint` without
  AST-shape guessing. A new `Emitter::isDeferredReleaseTemp(Value)` reports whether a temp is
  already scheduled for a statement-boundary release; `IoStatementLowerer::lowerPrint` now
  treats a member-access string result as NON-borrowed when it is already deferred-release (a
  runtime/instance property getter returns such an owned string), so it no longer wraps it in
  the extra retain/release pair that collided with the deferred release. Genuine borrowed loads
  (plain string variables, fields) still get the retain/release. `viper check` on the repro now
  compiles clean, and printing a normal string variable still round-trips without leak or
  use-after-free. Regression: `test_basic_print_runtime_property` (unit) asserts the
  property-getter PRINT line emits zero manual retains/releases; the full 325-test BASIC suite
  stays green. **Resolved.**

### VDOC-181 — Asset decode failure silently changes the documented return type

- **Classification:** confirmed runtime API/type inconsistency
- **Area:** `Viper.IO.Assets.Load`
- **Evidence:** `rt_asset_load()` first asks `rt_asset_decode_typed()` to decode a recognized image
  or audio extension. If decoding returns `NULL` for malformed or empty PNG, JPEG, BMP, GIF, WAV,
  OGG, or MP3 data, `Load` falls through and returns the raw `Bytes` object. Adjacent source
  comments instead say the Bytes fallback is only for unknown extensions and even name a JSON-to-
  Map decoder that does not exist.
- **Impact:** the same asset name can return `Pixels`, `Sound`, or `Bytes` depending on content
  validity, even though the public extension table presents one stable type per recognized suffix.
  Frontends cannot safely type the result, and corrupt media is indistinguishable from an
  intentionally raw asset without inspecting the object dynamically.
- **Likely repair point:** distinguish “unrecognized extension” from “recognized decoder failed,”
  return a failure/diagnostic for the latter, and align the runtime comments and public contract.
- **Review (2026-07-16):** Confirmed and fixed exactly as recommended. A new
  `rt_asset_extension_is_typed` (in rt_asset_decode.c) reports whether an extension has a
  registered image/audio decoder, letting `rt_asset_load` distinguish the two cases: a
  recognized extension whose decode returns NULL now returns NULL (Load fails) instead of
  falling back to Bytes, so each recognized suffix has one stable result type; only
  unrecognized extensions return raw Bytes. The stale rt_asset.c doc comment (which described
  the Bytes-on-decode-failure fallback, and elsewhere referenced a nonexistent JSON-to-Map
  decoder) was corrected to the new contract. assets.md rewritten. Regression:
  `test_recognized_decode_failure_returns_null` in `RTAssetTests.cpp` (a malformed `.png`
  returns NULL from `Load` but bytes from `LoadBytes`; an unknown `.dat` still returns Bytes).
  **Resolved.**

### VDOC-182 — `File.AppendLine` does not provide line-level atomicity

- **Classification:** confirmed source-contract / concurrency inconsistency
- **Area:** `Viper.IO.File.AppendLine`
- **Evidence:** `rt_io_file_append_line()` opens with append semantics, but writes the text in a
  retry loop and then writes the newline in a separate operation. Its source comment claims that
  writes below `PIPE_BUF` cannot interleave because of POSIX atomic-write guarantees; `PIPE_BUF`
  applies to pipes/FIFOs, not ordinary files, and the line is not one write in any case.
- **Impact:** concurrent appenders can interleave text and newline fragments. Code treating the
  helper as an atomic log-record append can produce split or merged records.
- **Likely repair point:** remove the atomicity claim, document the concurrency limitation, and use
  an explicit locking/record protocol if line-level atomicity is required.
- **Review (2026-07-16):** Confirmed (the source comment already lacked the bogus PIPE_BUF claim
  from a prior pass, but the two-write split remained) and improved beyond disclosure:
  `rt_io_file_append_line` now combines the text and its LF into ONE buffer written with a
  single `write()` on the `O_APPEND` fd. POSIX guarantees each `write()` to an `O_APPEND` file
  appends atomically at end-of-file, so the common case (line fits in one write) no longer
  splits or interleaves — the previous separate text-then-newline writes could interleave
  between the two calls. The residual limitation (a line exceeding the OS single-write
  atomicity limit, or a partial write, can still interleave) is documented in code and
  files.md with the lock/record-protocol recommendation. Regression: `test_append_line` in
  `RTFileExtTests.cpp` extended with a concurrency check — 8 threads × 200 `AppendLine` calls
  produce 1600 lines with every line intact ("thread-N", never split/merged). **Resolved.**

### VDOC-183 — `File.Touch` also updates directories

- **Classification:** confirmed API/domain inconsistency
- **Area:** `Viper.IO.File.Touch`
- **Evidence:** `rt_file_touch()` calls `utime(path, NULL)` (or the Windows equivalent) before any
  regular-file check. On existing directories that operation succeeds and updates timestamps,
  while other `File` metadata/content helpers reject directory operands.
- **Impact:** a method exposed as a file operation mutates directories successfully on supported
  hosts, so callers cannot rely on the class boundary or the surrounding File semantics.
- **Likely repair point:** require a regular file before timestamp mutation, or deliberately move
  and document the operation as a path-level API.
- **Review (2026-07-16):** Confirmed and fixed via the first route: `rt_file_touch` now stats the
  path first and traps `path is not a regular file` when it exists and is not a regular file
  (using the cross-platform `rt_fileext_is_regular_mode` helper the other File readers use), so
  it no longer mutates directory timestamps — the class boundary matches the rest of the File
  surface. Creating a missing file and touching an existing regular file are unchanged.
  files.md updated (method table + note). Regression: `test_touch` in `RTFileExtTests.cpp`
  extended to assert `Touch` traps on a directory operand. **Resolved.**

### VDOC-184 — Windows `Path.Abs` corrupts drive-relative paths

- **Classification:** confirmed Windows path-semantics bug
- **Area:** `Viper.IO.Path.Abs`
- **Evidence:** `Path.IsAbs("C:foo")` correctly treats a drive-relative path as non-absolute, but
  `rt_path_abs()` then joins that text directly to the process current directory. If the current
  drive/path differs, the result can contain a colon in the middle of a component (for example,
  `D:\\work\\C:foo`) rather than resolving against drive C's current directory as Windows does.
- **Impact:** valid Windows drive-relative input can become an invalid or semantically unrelated
  path. File operations based on the result can fail or address the wrong location.
- **Likely repair point:** use the platform path-resolution adapter (`GetFullPathNameW` semantics)
  for Windows drive-relative input and add focused path tests that vary the current drive.
- **Review (2026-07-16):** Confirmed and fixed via the platform adapter route. On Windows,
  `rt_path_abs` now resolves non-absolute input through `GetFullPathNameW` (UTF-8→wide via the
  existing `rt_file_path_utf8_to_wide` helper, then back), which implements the full Windows
  relative AND drive-relative resolution rules — `C:foo` anchors to drive C's current directory
  instead of being string-joined under the process CWD (the old `D:\work\C:foo` corruption).
  The POSIX branch (getcwd + join + normalize) is unchanged. files.md rewritten. The Windows
  branch requires a Windows build to exercise the drive-relative behavior directly; the POSIX
  refactor is covered by a new `test_abs` in `RTPathTests.cpp` (relative→absolute+normalized,
  already-absolute normalizes in place). **Resolved (Windows behavior pending a by-hand
  `build_viper_win.cmd` run).**

### VDOC-185 — `Path.ExeDir` has fixed-buffer encoding and truncation fallbacks

- **Classification:** confirmed cross-platform path limitation / API inconsistency
- **Area:** `Viper.IO.Path.ExeDir`
- **Evidence:** Windows uses fixed-size `MAX_PATH` `GetModuleFileNameA`, so long paths and paths not
  representable in the active ANSI code page fall back to `"."`. macOS uses a fixed `PATH_MAX`
  `_NSGetExecutablePath` buffer and also falls back to `"."` when it is too small. Linux reads
  `/proc/self/exe` into a fixed `PATH_MAX` buffer without reliably distinguishing an exact-fit
  truncation from a complete path.
- **Impact:** the asset-resolution fallback can silently switch from the executable directory to
  the current working directory, or use a truncated location, precisely on long/non-ASCII paths.
- **Likely repair point:** use dynamically sized wide/native platform APIs, detect truncation, and
  expose failure explicitly rather than returning an unrelated directory.
- **Review (2026-07-16):** Confirmed and fixed on all three platforms with dynamically sized
  buffers and truncation detection. Windows: `GetModuleFileNameW` (Unicode, no ANSI-code-page
  loss) in a doubling buffer, treating `len == cap` as truncation and converting to UTF-8 via
  `WideCharToMultiByte(CP_UTF8, …)`. macOS: `_NSGetExecutablePath` is first called to query the
  required size, then a right-sized buffer + `realpath(…, NULL)` (malloc'd). Linux:
  `readlink("/proc/self/exe")` in a doubling buffer, treating `len == cap` as possible
  truncation and retrying. Genuine detection failure now returns NULL (the public `ExeDir()`
  wrapper still maps that to `"."`), so `"."` no longer masks a valid-but-long path — a
  non-`"."` result is a reliable executable location. Both io docs rewritten. Verified live on
  macOS (`Path.ExeDir()` returns the real build dir, not `"."`). Regression:
  `PathExeDir.ReturnsAbsoluteNotDotFallback` in `TestPathExeDir.cpp` (the result is absolute and
  never the `"."` fallback). Windows/Linux branches verified by inspection (pending their native
  builds). **Resolved.**

### VDOC-186 — A leading glob `**` lets later wildcards cross separators

- **Classification:** confirmed glob matcher bug
- **Area:** `Viper.IO.Glob.Match`
- **Evidence:** the backtracking state created for `**` sets a persistent `allow_slash` flag. Later
  `*`, `?`, and character-class tokens inherit it instead of restoring ordinary component rules.
  The existing evaluator returns true for `Match("a/b", "**/a?b")`, while it correctly returns
  false for `Match("a/b", "a?b")`.
- **Impact:** patterns documented so that only `**` crosses separators match paths that the later
  component pattern cannot actually describe. Ignore rules and workspace file selection can
  include unintended paths.
- **Likely repair point:** scope separator crossing to consumption by the double-star token, not
  the remainder continuation state, and add mixed `**`/`?`/class regression cases.
- **Review (2026-07-16):** Confirmed (`Match("a/b", "**/a?b")` returned true) and fixed exactly
  as recommended. `glob_match_impl` is always entered with `allow_slash=0`, so the only source
  of `allow_slash=1` was the `**` branch's continuation push — which leaked the separator
  permission into whatever token followed `**/`. Since `**` performs the separator crossing
  itself (by consuming `text[ti..p]`), its continuation states now push `allow_slash=0`, so
  later `*`/`?`/class tokens match with ordinary component rules. Verified live:
  `**/a?b` vs `a/b` now false, `**/c` vs `a/b/c` still true, `**/x?z` vs `a/b/xyz` true.
  files.md and the rt_glob.c doc comments rewritten. Regression: five mixed `**`/`?`/`*`/class
  cases added to `test_glob_match` in `RTGlobTests.cpp` (three no-leak negatives, two
  legitimate-`**` positives). **Resolved.**

### VDOC-187 — `Stream.As*` returns an unsafe borrowed backing object

- **Classification:** confirmed ownership/lifetime bug
- **Area:** `Viper.IO.Stream.AsBinFile` and `AsMemStream`, BASIC and Zia lowering
- **Evidence:** `rt_stream_as_binfile()` and `rt_stream_as_memstream()` return `s->wrapped` without
  retaining it. The registry declares only untyped `obj()` results with unknown ownership. BASIC
  treats the result as an owned temporary (retaining for assignment and then releasing the
  temporary), while Zia stores it without adding a backing-owner reference. Minimal programs in
  both frontends can assign `AsMemStream()`, close the Stream, and then trap with
  `MemStream.Len: invalid stream` when using the still-in-scope typed local.
- **Impact:** an apparently ordinary returned object becomes dangling when its Stream is closed or
  finalized; BASIC's temporary ownership bookkeeping can also undercount it before that point.
- **Likely repair point:** return a retained/owned typed backing object (or model borrowed returns
  explicitly throughout the registry and frontends) and give both methods concrete return types.
- **Review (2026-07-16):** Confirmed and fixed exactly as recommended. `rt_stream_as_binfile`
  and `rt_stream_as_memstream` now `rt_obj_retain_maybe` the wrapped object before returning it,
  so the caller receives an OWNED reference that survives the Stream's close/finalize (which
  only releases the Stream's own reference for wrapping-Streams). The registry entries and class
  methods were given concrete return types — `obj<Viper.IO.BinFile>` / `obj<Viper.IO.MemStream>`
  — so BASIC and Zia apply object ownership correctly instead of guessing. streams.md rewritten.
  Regression: `test_stream_conversion` in `RTStreamTests.cpp` extended — `AsMemStream()` is used
  after `rt_stream_close()` (its length still reads correctly) and then released cleanly, proving
  the result no longer dangles. Surface + docs regenerated; audits pass. **Resolved.**

### VDOC-188 — `Stream.Eof` has incompatible file and memory semantics

- **Classification:** confirmed runtime API inconsistency
- **Area:** `Viper.IO.Stream.Eof` and `Viper.IO.BinFile.Eof`
- **Evidence:** memory streams compute EOF as `position >= length`. File streams delegate to the C
  stream's sticky EOF flag, which is set only after a read attempts to pass the end. Reading
  exactly the remaining bytes—including `Stream.ReadAll()`—therefore leaves file `Eof` false but
  memory `Eof` true. The BinFile example in `files.md` reads exactly eight bytes from an eight-byte
  file and currently claims the flag is already 1.
- **Impact:** polymorphic Stream code observes different state for the same logical position, and
  the documented example teaches an assertion that is false for file-backed streams.
- **Likely repair point:** define one end-position contract for Stream and implement it for both
  backings; until then, document sticky file EOF and use `Pos >= Size/Length` when position is the
  intended question.
- **Review (2026-07-16):** Confirmed and fixed by defining one end-position contract for
  `Stream.Eof`: `rt_stream_is_eof` now returns `pos >= size` for BOTH backings (file: BinFile
  `pos`/`size`; memory: MemStream `pos`/`len`) instead of delegating to BinFile's sticky feof
  flag. Polymorphic Stream code now observes the same end-of-stream state for the same logical
  position — reading exactly the remaining bytes (including `ReadAll`) leaves `Eof` true on both.
  The low-level `BinFile.Eof` property intentionally keeps its C-stdio sticky-flag semantics for
  direct BinFile use (documented as the distinct contract). streams.md and files.md updated.
  Regression: `test_file_stream_modes` in `RTStreamTests.cpp` now asserts a file-backed stream
  reports `Eof` true immediately after reading all its bytes and false after seeking back to the
  start. **Resolved.**

### VDOC-189 — BinaryBuffer continues unsafely after recoverable traps

- **Classification:** confirmed systemic runtime trap-discipline bug
- **Area:** `Viper.IO.BinaryBuffer`
- **Evidence:** `binbuf_require()` traps and returns `NULL`, but `get_Position`, `get_Length`,
  `ToBytes`, and `Reset` dereference its result without checking it. Range-check helpers for
  fixed-width writes return `void`; if a test/embedder trap hook returns, the caller continues and
  writes the truncated value anyway.
- **Impact:** the runtime's recoverable-trap contract can turn an invalid receiver into a null
  dereference and an out-of-range write into silent data corruption. This is the BinaryBuffer
  counterpart to the Bytes failure recorded in VDOC-177.
- **Likely repair point:** make receiver/range validators return success, stop every operation after
  a trap, and audit all BinaryBuffer allocation results under returning trap hooks.
- **Review (2026-07-16):** Confirmed and fixed following the same trap-discipline pattern as the
  Bytes fix (VDOC-177). The four fixed-width range validators (`binbuf_require_i16/u16/i32/u32`)
  now return `int` (1 = fits, 0 = trapped); every fixed-width writer guards
  `if (!buf || !binbuf_require_XX(value)) return;` so a returning trap hook can no longer let a
  truncated value reach the buffer. `get_position`, `get_len`, `to_bytes`, and `reset` now
  null-check the `binbuf_require` result instead of dereferencing it (returning 0 / empty Bytes /
  no-op). Audited the remaining writers/readers: the i64 writers are already guarded by
  `binbuf_ensure`'s NULL check and the readers by `binbuf_check_read`'s, and the string/bytes
  writers already had explicit `if (!buf) return;` guards. Regression:
  `test_returning_trap_hook_is_safe` in `RTBinaryBufferTests.cpp` installs a record-and-return
  trap hook and asserts getters on an invalid receiver return sentinels (no crash), out-of-range
  fixed-width writes leave `Length` unchanged, and a later valid write still round-trips.
  streams.md updated. **Resolved.**

### VDOC-190 — Watcher overflow reporting misses native queue loss

- **Classification:** confirmed watcher correctness/observability bug
- **Area:** `Viper.IO.Watcher.EventOverflow` and `EventOverflowCount`
- **Evidence:** the Linux inotify decoder has no `IN_Q_OVERFLOW` branch, so a kernel-queue overflow
  is consumed without queuing `EventOverflow`. Windows queues an overflow marker when an overlapped
  read fails or reports zero bytes, but the normal queue insertion path assigns that marker a
  dropped count of zero. Only overflow of Viper's own 64-entry ring computes a positive count.
- **Impact:** a client can continue applying incomplete incremental updates without receiving the
  documented rescan signal; even when Windows emits the signal, `EventOverflowCount()` cannot tell
  it how many events the marker represents.
- **Likely repair point:** translate native overflow notifications on every backend, represent an
  unknown loss count explicitly (rather than as “none”), and cover native and internal overflow
  paths separately.
- **Review (2026-07-16):** Confirmed and fixed as recommended. Added a
  `WATCHER_OVERFLOW_COUNT_UNKNOWN` (-1) sentinel and a dedicated
  `watcher_queue_native_overflow` helper that queues an `EventOverflow` marker carrying the
  unknown count (distinct from the internal ring overflow, which still counts exactly). Linux:
  the inotify decoder now detects `IN_Q_OVERFLOW` (wd -1, no name) and queues a native overflow
  instead of silently consuming it. Windows: both overflow paths (failed `GetOverlappedResult`
  and a zero-byte completed read) now use the native-overflow helper, so the marker no longer
  gets a fabricated count of zero. The ring-coalescing path preserves UNKNOWN once a native
  overflow folds in. `EventOverflowCount()` now returns the exact count for internal overflow or
  -1 for native/unknown; getter comment + advanced.md + files.md updated. macOS kqueue is
  level-triggered per-fd with no queue-overflow notification, so the helper is compiled only for
  Linux/Windows (verified: builds clean on macOS with the helper excluded). Regression:
  `test_internal_overflow_reports_positive_count` (Linux-guarded) forces a ring overflow with 300
  rapid file creations and asserts the count is a positive exact number, never -1; the native
  IN_Q_OVERFLOW / Windows paths are covered by inspection pending a by-hand Linux/Windows run.
  **Resolved.**

### VDOC-191 — Linux `Watcher.PollFor` can exceed its timeout after interruption

- **Classification:** confirmed timeout-contract bug
- **Area:** `Viper.IO.Watcher.PollFor`
- **Evidence:** the Linux implementation retries `poll()` on `EINTR` with the original timeout each
  time. It does not compute elapsed monotonic time or a deadline. A signal near the end of each
  attempt can therefore restart the full wait repeatedly.
- **Impact:** a call documented to wait “up to” a duration can block substantially longer, which
  can stall per-frame editor loops and shutdown paths.
- **Likely repair point:** convert positive timeouts to one monotonic deadline and recompute the
  remaining wait after every interrupt.
- **Review (2026-07-16):** Confirmed and fixed with the standard monotonic-deadline pattern used
  by the network timeouts (VDOC-147/151/179). For a positive timeout, `rt_watcher_poll_for`
  computes one `rt_clock_ticks_us()`-based deadline before the poll loop; on `EINTR` it recomputes
  the remaining budget (ceil-to-ms) and resumes, timing out once the deadline passes rather than
  restarting the full wait per signal. Negative (wait-forever) and zero (poll-once) timeouts keep
  their original semantics. Linux-only (`poll(2)`); macOS kqueue and Windows
  `WaitForSingleObject` are single-wait calls without the per-EINTR restart. advanced.md updated.
  Builds clean on macOS (the Linux branch is not compiled there); no macOS watcher regression.
  The interrupted-poll timing is verified by inspection pending a by-hand Linux run. **Resolved.**

### VDOC-192 — FileIndex's `**` matcher ignores component boundaries

- **Classification:** confirmed workspace ignore-pattern bug
- **Area:** `Viper.Workspace.FileIndex` `.gitignore` and explicit exclude matching
- **Evidence:** `pathGlobMatch()` removes an optional slash after `**` and then tries the remainder
  at every byte offset, including the middle of a path component. Existing-binary probes show
  `ShouldIgnore(".", "foobar", "**/bar")` and
  `ShouldIgnore(".", "foo/xbar", "foo/**/bar")` both return true.
- **Impact:** workspace entries whose component merely ends with the requested literal are hidden,
  so indexing, search, and IDE inventory can omit unrelated files.
- **Likely repair point:** let `**/` consume zero or more complete path components and add boundary
  cases for zero components, nested components, and suffix-only nonmatches.
- **Review (2026-07-16):** Confirmed and fixed exactly as recommended. In `pathGlobMatch`
  (rt_ide_primitives.cpp) the `**` branch now distinguishes `**/` (had a trailing slash) from a
  bare `**`. For `**/`, the remainder is attempted only at component boundaries — the current
  position (zero components) or immediately after a `/` — instead of at every byte offset, so it
  can no longer land mid-component. A bare `**` (e.g. `a/**`) keeps its any-run behavior.
  Verified: `**/bar` vs `foobar` and `foo/**/bar` vs `foo/xbar` now return false, while `**/bar`
  vs `bar`/`a/b/bar` and `foo/**/bar` vs `foo/bar`/`foo/x/bar` still return true. files.md
  updated. Regression: six boundary assertions added to `test_file_index_and_ignore` in
  `RTIdeWorkspaceTests.cpp` (zero components, nested components, and suffix-only nonmatches).
  **Resolved.**

### VDOC-193 — FileIndex can cache stale `.gitignore` contents indefinitely

- **Classification:** confirmed cache invalidation bug
- **Area:** `Viper.Workspace.FileIndex` nested and root `.gitignore` handling
- **Evidence:** cache identity consists only of the normalized directory key and the ignore file's
  modification time rounded to whole seconds. File size, inode/file ID, higher-resolution time,
  and content are not considered. Rewriting a `.gitignore` within the same timestamp tick leaves
  the old patterns cached until some later operation changes that second-valued mtime.
- **Impact:** editor enumeration can keep ignoring newly included files—or include newly ignored
  files—after a normal quick save, with no public cache-reset operation.
- **Likely repair point:** cache a high-resolution file identity tuple (or content hash), and expose
  deterministic invalidation for watcher-driven IDE workflows.
- **Review (2026-07-16):** Confirmed and fixed with the content-hash option. `cachedGitignorePatterns`
  now keys the cache on `gitignoreCacheIdentity` — an FNV-1a hash of the `.gitignore` file's raw
  bytes with the length folded in — instead of a whole-second mtime. Any edit changes the hash, so
  a same-second (or same-size) rewrite invalidates the cache on the next lookup; a missing file
  still returns -1 so the "no .gitignore" path is unchanged. (`.gitignore` files are small, so the
  read-to-hash cost is negligible, and a content hash sidesteps the platform stat sub-second-mtime
  portability gaps — this strict build exposes neither `st_mtim` nor `st_mtimespec`.) The
  human-facing `modified` field in enumeration maps still reports whole-second mtime. files.md
  updated. Regression: `test_gitignore_same_second_rewrite` in `RTIdeWorkspaceTests.cpp` primes the
  cache, rewrites `.gitignore` with different patterns, forces the ORIGINAL mtime back, and asserts
  the new patterns are honored despite the unchanged timestamp. **Resolved.**

### VDOC-194 — Unknown manifest sections leak their directives into the top level

- **Classification:** confirmed tooling manifest parser bug
- **Area:** `Viper.Project.Manifest.ParseText` / `ParseFile`
- **Evidence:** on an unknown `[section]`, the parser emits a diagnostic and sets `sectionMap` to
  null. Subsequent lines are then processed by the top-level directive branch until another
  recognized section appears. An existing-binary probe of `[unknown]` followed by `entry hijack`
  returns a manifest whose top-level `entry` is `hijack`, despite `valid` being false.
- **Impact:** unsupported configuration can mutate supposedly safe defaults and be consumed by a
  caller that inspects fields before or despite the validity flag.
- **Likely repair point:** retain an explicit “unknown section” state and diagnose/ignore all of its
  body directives until the next section header.
- **Review (2026-07-16):** Confirmed and fixed exactly as recommended. `rt_project_manifest_parse_text`
  now tracks an explicit `inUnknownSection` flag: `sectionMap == nullptr` was ambiguous between
  "top level" and "inside an unknown section", so unknown-section bodies fell into the top-level
  directive branch and hijacked defaults like `entry`. The flag is set when an unknown `[section]`
  header is seen (and cleared on any recognized header), and every directive encountered while it
  is set is diagnosed (`ignoring directive '…' in unknown manifest section`) and skipped before the
  top-level branch. files.md updated. Regression added to `test_asset_resolver_and_manifest` in
  `RTIdeWorkspaceTests.cpp`: a manifest with `entry real.zia` then `[unknown]` / `entry hijack.zia`
  / `sources evil` keeps top-level `entry` == `real.zia`, is `valid=false`, creates no run/build
  config, and records exactly 3 diagnostics (the unknown section + its two ignored directives).
  **Resolved.**

### VDOC-195 — Workspace edits can overwrite a target changed after validation

- **Classification:** confirmed transactional/concurrency bug
- **Area:** `Viper.Workspace.Edit.Apply*`
- **Evidence:** Apply validates metadata and reads each target, then constructs replacement text,
  stages every temporary file, and only afterward begins target renames. There is no identity,
  content, size, or mtime recheck immediately before moving each live target to its backup. The
  rooted variant likewise resolves path components before staging but commits later by pathname.
- **Impact:** an editor, formatter, or external process that changes a file during staging can have
  its newer content silently replaced. A swapped path component can also invalidate the rooted
  boundary established during validation.
- **Likely repair point:** bind validation to open file identities/handles, recheck the expected
  version at commit, and use descriptor/handle-relative operations for rooted transactions.
- **Review (2026-07-16):** Confirmed and fixed with an optimistic-concurrency recheck at commit.
  `workspace_edit_apply_impl` now snapshots each target's validated original content
  (`validatedOriginals`) before applying edits, and immediately before renaming each live target
  to its backup it re-reads the file and compares against that snapshot. If the content changed
  (or the file became unreadable) during staging, it pushes an `edit target changed since
  validation` diagnostic, marks the batch `success=false`, rolls back any already-committed files
  from their backups, and returns — newer external content is never silently overwritten. The
  happy path (unchanged targets) still commits normally (existing `test_workspace_edits` green).
  Content comparison is stronger than the whole-second `expectedMtime` check for this window. The
  content-swap case is fully covered; a rooted path-*component* swap during the window still needs
  handle-relative commit ops (a larger refactor) and is documented as a residual in files.md. The
  concurrency-window failure path is inherently non-deterministic to unit-test (it needs a
  concurrent mid-apply write), so it is verified by inspection. **Resolved (content-recheck;
  rooted handle-relative commit noted as follow-up).**

### VDOC-196 — Workspace edit backup paths are predictable and not reserved

- **Classification:** confirmed staging/rollback race
- **Area:** `Viper.Workspace.Edit.Apply*`
- **Evidence:** temporary and backup names use only a process-local incrementing counter. Content
  temps are safely opened with exclusive-create semantics, but backup paths are never reserved;
  the code calls `rename(target, backup)` directly. On POSIX that rename can replace an existing
  path, contradicting the adjacent claim that both staging paths use exclusive existence checks.
- **Impact:** another process—or a stale artifact from a prior process—can cause unrelated backup
  data to be overwritten or can make behavior differ by platform. Rollback then trusts that path
  as the original target contents.
- **Likely repair point:** reserve unpredictable backup names exclusively in the target directory
  and commit through platform primitives with explicit no-replace semantics.
- **Review (2026-07-16):** Confirmed and fixed. Sidecar names are no longer a bare incrementing
  counter: `workspaceEditNonce` folds the atomic counter through the runtime's per-process keyed
  hash (SipHash seeded from the OS CSPRNG), so temp and backup names are unpredictable across
  processes. `reserveWorkspaceEditBackup` now exclusively creates the backup path with
  `O_EXCL` (POSIX) / `CREATE_NEW` (Windows) before the commit rename — a stale artifact or racing
  process at that name is detected and a fresh candidate is tried (64-attempt budget) instead of
  the previous blind `rename(target, backup)` that POSIX would silently let clobber an existing
  file. A new `backupReserved` flag lets rollback delete a reserved-but-unused placeholder so a
  failed apply leaves no stale sidecar. Content temps keep their existing `O_EXCL` open. files.md
  updated. Regression: `test_workspace_edits` now asserts no `.viper-edit-*` sidecars remain in the
  workspace directory after a successful apply; strict surface audit still passes. **Resolved.**

### VDOC-197 — Asset Resolver accepts empty assets and misbases relative scene paths

- **Classification:** confirmed editor resolver API inconsistency
- **Area:** `Viper.Assets.Resolver.Resolve`
- **Evidence:** an empty `assetPath` makes the `projectRoot / asset` candidate equal the project
  directory; an existing-binary probe reports `found=true` with source `project`. Separately, a
  relative `scenePath` is combined with the asset relative to the process working directory,
  rather than first being based under the separately supplied `projectRoot`.
- **Impact:** “resolve an asset” can succeed with a directory for no asset name, and the same
  project-relative scene resolves differently when the editor's current directory changes.
- **Likely repair point:** reject empty asset names and resolve relative scene paths consistently
  against the project root before applying scene-directory precedence.
- **Review (2026-07-16):** Confirmed and fixed both defects in `rt_asset_resolver_resolve`. An
  empty `assetPath` is now rejected up front with an `empty asset name` diagnostic and
  `found=false`, so it no longer resolves to `projectRoot / "" == projectRoot` (the project
  directory). A relative `scenePath` is rebased under `projectRoot` (`projectRoot / scenePath`,
  normalized) before the scene-directory candidate is built, so the same project-relative scene
  resolves identically regardless of the process working directory. Absolute scene paths are
  unchanged. assets.md updated. Regression added to `test_asset_resolver_and_manifest` in
  `RTIdeWorkspaceTests.cpp`: an empty asset name returns not-found with the diagnostic, and passing
  the project-relative `scenes/level.json` resolves the scene-local `local.png` with source
  `scene` (matching the absolute-scene case). **Resolved.**

### VDOC-198 — IO runtime metadata labels trapping owned APIs as infallible or unknown

- **Classification:** confirmed machine-readable registry metadata inconsistency
- **Area:** `viper --dump-runtime-api` for IO, workspace, and project classes
- **Evidence:** representative current output labels `LineWriter.Append` as infallible with unknown
  ownership even though it traps on open failure and returns a new owned writer. `Stream.As*` is
  infallible/unknown despite wrong-backing traps and its borrowed result; `Archive.Create`, almost
  every compression/archive operation, Watcher construction/start/event access, File writes, and
  Manifest parsing have similarly inaccurate inferred contracts. Many fresh typed Seq/Map/Bytes
  results remain `ownership=unknown`.
- **Impact:** agents and tools using the documented JSON inventory cannot reliably plan cleanup,
  error paths, or safe calls—the exact purposes of the ownership/fallibility fields.
- **Likely repair point:** annotate this surface explicitly in the runtime manifest, type object
  returns where concrete, and add contract-audit checks against known trapping/allocating entry
  points.
- **Review (2026-07-16):** Confirmed and fixed by making the `--dump-runtime-api` fallibility and
  ownership inference accurate for the named categories. Ownership: a concretely typed `seq<…>` /
  `obj<…>` return is now reported `owned` (a freshly allocated GC value the caller owns) instead
  of `unknown` — this fixes the broad "fresh typed Seq/Map/Bytes remain unknown" class and the
  typed `Stream.As*` returns (now `obj<Viper.IO.BinFile>` / `obj<Viper.IO.MemStream>` after
  VDOC-187). Fallibility: an exact-name override table marks the trapping IO entries the
  name/signature heuristic missed as `traps` — `Stream.AsBinFile` / `AsMemStream` / `ToBytes`
  (wrong-backing trap), `LineWriter.Append` (open failure), `Watcher.New` / `Start` (resource
  failure), and `Archive.Create` (open failure); a bare-`obj` override marks `LineWriter.Append`
  `owned`. Verified via `--dump-runtime-api`: all seven now report the corrected contracts.
  tools.md documents the value semantics. Regression: `agent_cli` (AgentCliTests.cmake) now pins
  `Stream.AsBinFile` and `LineWriter.Append` as `traps` / `owned`. The general typed-owned
  heuristic also improves the sibling Math/Time/System metadata findings. **Resolved.**

### VDOC-199 — `Path.DataDir` validates only the prefix before an embedded NUL

- **Classification:** confirmed path-validation inconsistency
- **Area:** `Viper.IO.Path.DataDir`
- **Evidence:** the public entry point obtains `rt_string_cstr(app_name)` and calls the legacy
  `is_safe_game_name()`, which measures with `strlen`. Unlike `SaveData.New`, it never passes the
  runtime String's stored byte length to `is_safe_game_name_bytes()`. Bytes after a NUL therefore
  bypass the character and 64-byte length checks and are silently discarded when composing the
  directory name.
- **Impact:** the advertised full-string app-name policy is not enforced, and distinct runtime
  Strings can alias the same per-user directory.
- **Likely repair point:** validate `rt_str_len(app_name)` bytes with the existing length-aware
  helper and reject embedded NULs before any C-string path operation.
- **Review (2026-07-16):** Confirmed and fixed exactly as recommended. `rt_path_data_dir` now
  reads `rt_str_len(app_name)` and validates that byte count with `is_safe_game_name_bytes`
  (mirroring `SaveData.New`), instead of `rt_string_cstr` + the `strlen`-based
  `is_safe_game_name`. Because the safe charset excludes NUL, the length-aware check rejects an
  embedded NUL and any trailing bytes up front, so the advertised full-string app-name policy is
  enforced and two distinct Strings can no longer alias the same per-user directory. files.md
  updated. Regression: `test_data_dir_embedded_nul_name_traps` in `RTSaveDataTests.cpp` (an app
  name `"app\0x"` traps), parallel to the existing SaveData.New NUL test. **Resolved.**

### VDOC-200 — Random instance methods dereference an invalid receiver after a recoverable trap

- **Classification:** confirmed systemic runtime trap-discipline bug
- **Area:** `Viper.Math.Random` instance methods
- **Evidence:** `as_random()` checks `rt_obj_is_instance()` and calls `rt_trap()` on failure, but
  unconditionally returns the original pointer cast as `rt_random_impl *`. `Next`, `NextDouble`,
  `NextInt`, `Range`, and `Seed` immediately read or write `rng->state`. Under the runtime's
  returning trap hooks used by tests and embedders, a null or wrong-class receiver therefore
  continues into an invalid dereference.
- **Impact:** a recoverable type error becomes a null dereference, out-of-bounds read, or memory
  corruption instead of returning control to the embedder. This is the same trap-discipline class
  as VDOC-177 and VDOC-189.
- **Likely repair point:** make `as_random()` return `NULL` after trapping and require every caller
  to stop before accessing state; add wrong-class and null-receiver recovery tests.
- **Review (2026-07-16):** Confirmed and fixed following the VDOC-177/189 trap-discipline pattern.
  `as_random()` now returns `NULL` after `rt_trap()` instead of the unchecked pointer, and all four
  instance methods (`rt_rnd_method`, `rt_rand_int_method`, `rt_rand_range_method`,
  `rt_randomize_i64_method`) guard `if (!rng) return <sentinel>;` (0.0 / 0 / 0 / void) before
  touching `rng->state`, so under a returning trap hook a null or wrong-class receiver returns
  control safely instead of dereferencing. Pure internal robustness — no user-facing contract
  change. Regression: `RTHandleValidationTests.cpp` gained a record-and-return `vm_trap` mode and a
  block that calls every Random method with a wrong-class fake AND a null receiver under that hook,
  asserting each returns its sentinel and records the trap without crashing (the existing
  longjmp-recovery `call_random_next` assertion still verifies the trap fires). **Resolved.**

### VDOC-201 — Spline parameter handling is type-dependent and unsafe for NaN

- **Classification:** confirmed runtime undefined-behavior / API inconsistency
- **Area:** `Viper.Math.Spline.Eval`, `Tangent`, and `ArcLength`
- **Evidence:** linear and Catmull-Rom evaluators clamp finite values outside `[0,1]`, then convert
  `t * (count - 1)` to `int64_t`; a NaN reaches that floating-to-integer conversion, which is
  undefined in C. The cubic Bezier evaluator does no clamping or integer conversion and instead
  extrapolates for out-of-range values or propagates NaN. `Tangent` and `ArcLength` inherit the
  same split behavior. The source header and public function comment previously claimed every
  spline clamps `t`.
- **Impact:** the same API contract changes with the curve factory, and a non-finite parameter can
  merely return NaN for one spline but invoke undefined behavior for another.
- **Likely repair point:** validate finiteness once in each public entry point and choose one
  documented clamping or extrapolation rule for all spline kinds.
- **Review (2026-07-16):** Confirmed and fixed with one uniform rule. A new `spline_sanitize_param`
  clamps the parameter to [0,1] for every spline kind — below-0/`-Inf` -> 0, above-1/`+Inf` -> 1,
  `NaN` -> 0 — and is applied once at each public entry point: `rt_spline_eval` and
  `rt_spline_tangent` sanitize `t` before the kind switch, and `rt_spline_arc_length` sanitizes
  both `t0` and `t1` before deriving the per-step `t`. This removes the split behavior (Bezier no
  longer extrapolates; it clamps like the others) and eliminates the NaN->segment-index conversion
  (undefined in C) since no evaluator ever receives a non-finite parameter. The file header, `Eval`
  comment, and math.md were corrected (they had claimed every spline clamps). Regression:
  `test_param_sanitization_is_uniform` in `RTSplineTests.cpp` checks, for both linear and Bezier
  splines, that NaN==start, +Inf==end, out-of-range finite values clamp, and Tangent/ArcLength stay
  finite for non-finite inputs. **Resolved.**

### VDOC-202 — PerlinNoise accepts any object and reads it as a permutation table

- **Classification:** confirmed runtime type-safety bug
- **Area:** `Viper.Math.PerlinNoise.Noise*` and `Octave*`
- **Evidence:** the registry exposes each receiver as untyped `obj`. `rt_perlin_noise2d()` and
  `rt_perlin_noise3d()` check only for null and then cast the payload directly to
  `rt_perlin_impl`, reading its 512-byte `perm` array. The constructor itself allocates with class
  id zero, and no size/class validation helper exists. The octave methods delegate to the same
  unchecked readers.
- **Impact:** source code can pass a Seq, Bytes, or any other object accepted by the generic
  signature and trigger out-of-bounds reads or interpret unrelated object memory as noise state.
- **Likely repair point:** assign a stable PerlinNoise class id, validate heap kind/class/size at
  the boundary, and give the registry a typed receiver signature.
- **Review (2026-07-16):** Confirmed and fixed exactly as recommended. Added a stable
  `RT_PERLIN_CLASS_ID` (rt_perlin.h); `rt_perlin_new` now allocates with it instead of class id 0.
  A new `as_perlin()` validates the receiver via `rt_obj_is_instance` (kind/class/size) and traps +
  returns NULL on mismatch (same discipline as VDOC-200); `rt_perlin_noise2d` / `noise3d` use it
  before touching `perm`, and `rt_perlin_octave2d` / `octave3d` validate up front so an invalid
  receiver traps once rather than 16x through the inner loop. The registry constructor now returns
  the concrete `obj<Viper.Math.PerlinNoise>` type. A Seq/Bytes/any object passed through the
  generic `obj` signature can no longer be read as a 512-byte permutation table. Surface + docs
  regenerated; strict audit and all registry/surface tests pass. math.md updated. Regression:
  `test_null_safety` in `RTPerlinTests.cpp` now expects traps for null receivers, and a new
  `test_wrong_class_receiver_traps` passes a Seq to all four readers and asserts each traps.
  **Resolved.**

### VDOC-203 — BigInt's negative byte encoder can change the integer value

- **Classification:** confirmed serialization correctness bug
- **Area:** `Viper.Math.BigInt.ToBytes`
- **Evidence:** the big-endian two's-complement encoder decides whether a negative value needs a
  leading `0xff` from the magnitude's high bit rather than the encoded result's sign bit. With the
  existing installed binary, `ToBytes(FromInt(-129)).ToHex()` returns `7f`, and feeding those bytes
  to `FromBytes` returns `127`. Other negative ranges have the same boundary error; some correctly
  valued negative encodings also contain a redundant sign byte.
- **Impact:** serialization followed by deserialization can silently reverse the sign and change
  the value. Stored keys, protocol integers, signatures, and cross-language data are corrupted.
- **Likely repair point:** compute the minimal two's-complement bytes first, then prefix `0xff`
  exactly when the resulting top bit would otherwise be clear; add round-trip tests around every
  `0x80` magnitude boundary in both signs.
- **Review (2026-07-16):** Confirmed and fixed exactly as recommended. `rt_bigint_to_bytes` now
  materializes the big-endian two's-complement bytes of the value in `byte_len` bytes into a temp
  buffer FIRST, then decides the sign prefix from the RESULT's top bit rather than the magnitude's
  high bit: a negative value gets a leading `0xff` exactly when the encoded top bit is clear, a
  positive value gets a leading `0x00` exactly when it is set. This fixes the boundary error (e.g.
  `-129`: magnitude top bit `0x81` is set, but the 1-byte two's complement `0x7f` has its top bit
  clear, so it now correctly emits `ff 7f` instead of `7f`) and avoids redundant sign bytes.
  Verified live: `FromBytes(ToBytes(FromInt(-129)))` now returns `-129` (was `127`). math.md
  updated. Regression: `test_to_from_bytes_roundtrip` in `RTBigintTests.cpp` round-trips 28 values
  straddling the 8- and 16-bit two's-complement boundaries in both signs. **Resolved.**

### VDOC-204 — BigInt operations do not validate their generic object operands

- **Classification:** confirmed runtime type-safety bug
- **Area:** all `Viper.Math.BigInt` operations taking `Object`
- **Evidence:** BigInts carry the private class id `0x424967496E74`, but no public operation checks
  it. Functions such as `ToInt`, arithmetic, comparison, bitwise operations, and `Sqrt` cast the
  registry's generic `obj` argument directly to `bigint_t` and dereference its `digits`, `len`,
  `cap`, or `sign` fields. No heap-kind, payload-size, or class-id validation precedes those reads.
- **Impact:** any ordinary runtime object type-checks at the public signature and can cause
  arbitrary pointer reads/writes, invalid frees, or a process crash when interpreted as a BigInt.
- **Likely repair point:** centralize a checked BigInt cast, apply it at every public boundary, and
  expose typed BigInt operands/results in the registry so frontends reject mistakes earlier.
- **Review (2026-07-16):** Confirmed and fixed at the runtime boundary (the authoritative layer). A
  centralized `bigint_check(obj)` validates each operand via `rt_obj_is_instance(obj,
  BIGINT_CLASS_ID, sizeof(bigint_t))`, trapping `BigInt: invalid BigInt object` on a wrong-class
  object while passing null through (so each op keeps its documented null/identity semantics). It
  is applied at every public boundary that casts an operand — 29 guards across ToInt, ToStringBase,
  ToBytes, FitsInt, Add/Sub/Mul/DivMod, Negate/Abs, Compare, IsZero/IsNegative/Sign, And/Or/Xor/Not,
  Shl/Shr, Pow/PowMod, Gcd/Lcm, BitLength/TestBit/SetBit/ClearBit, and Sqrt (Div/Mod/Equals/ToString
  are covered transitively via the guarded functions they delegate to). Subtlety: `rt_bigint_sub`
  builds a negated STACK copy of b and previously handed it to public `rt_bigint_add`; the new guard
  would reject that trusted stack pointer, so the add body was split into an unchecked
  `bigint_add_impl` (shared with the validated public wrapper) that `sub` now calls directly.
  Verified: passing a Seq to any BigInt op traps cleanly (was arbitrary memory read), and all
  arithmetic still round-trips. Registry operand/result *typing* (the frontend-rejection half) is a
  larger cross-cutting change left as follow-up — the runtime guard fully closes the memory-safety
  hole. math.md updated. Regression: `trap_wrong_class_sign` / `trap_wrong_class_add` in
  `RTBigIntContractTests.cpp` assert a Seq operand traps with the expected message.
  **Resolved (runtime-enforced; registry typing noted as follow-up).**

### VDOC-205 — BigInt `PowMod` returns negative residues for negative bases

- **Classification:** arithmetic API inconsistency / needs triage
- **Area:** `Viper.Math.BigInt.PowMod`
- **Evidence:** BigInt `Mod` uses truncating-division semantics and gives the remainder the
  dividend's sign. `PowMod` repeatedly uses that operation without normalizing residues into
  `[0, |modulus|)`. The existing binary reports `PowMod(-2, 3, 5) == -3`, while the conventional
  least-nonnegative modular result is `2`.
- **Impact:** callers using the method for number theory or cryptographic arithmetic receive a
  noncanonical value and can fail equality/interoperability checks even though it represents the
  same congruence class.
- **Likely repair point:** decide and document the residue convention; for the conventional API,
  require a positive modulus or normalize every final/intermediate remainder to `[0,m)`.
- **Review (2026-07-16):** Confirmed and fixed by adopting the conventional least-non-negative
  residue convention. `rt_bigint_pow_mod` now normalizes its final result to `[0, |m|)`: because
  `Mod` uses truncating division, a negative base propagates a negative residue through the
  Montgomery ladder, so the raw result is in `(-|m|, 0)` when negative; adding `|m|` lands it in
  `(0, |m|)`. Intermediate reductions stay congruent mod m, so normalizing only the final result
  yields the canonical value. Verified: `PowMod(-2, 3, 5)` now returns `2` (was `-3`) and positive
  cases are unchanged (`2^10 mod 1000 == 24`). The function's `@return` doc and math.md now state
  the `[0, |mod|)` convention. Regression: `test_pow_mod_nonnegative_residue` in
  `RTBigIntContractTests.cpp`. **Resolved.**

### VDOC-206 — Quaternion inverse overflows or underflows its squared length

- **Classification:** confirmed floating-point correctness bug
- **Area:** `Viper.Math.Quat.Inverse`
- **Evidence:** the implementation computes an overflow-resistant norm with chained `hypot`, but
  then squares that norm and reciprocates it without scaling. For `Quat.New(1e308,0,0,0)`, the
  square becomes infinity and the installed runtime returns a zero X component. For
  `Quat.New(1e-308,0,0,0)`, the square underflows to zero and inverse X becomes `-Infinity`. Neither
  path takes the advertised invalid-length trap.
- **Impact:** finite, nonzero quaternions can yield zero or infinite inverses; downstream matrix
  and vector transforms silently become invalid.
- **Likely repair point:** compute the inverse with scaled components (or divide a normalized
  conjugate by the original norm) so neither the square nor reciprocal overflows/underflows.
- **Review (2026-07-16):** Confirmed and fixed exactly as recommended. `rt_quat_inverse` no longer
  forms `len_sq = len*len` (which overflowed to Inf for large norms — collapsing the inverse to 0
  — and underflowed to 0 for tiny norms — blowing it up to +/-Inf). It now divides each conjugate
  component by the finite, nonzero `len` TWICE (`s = 1/len; component = (conj*s)*s`): the first
  divide normalizes to ~unit magnitude, the second scales by `1/len`, so no intermediate
  overflows or underflows. Verified live: `Inverse(Quat(1e308,0,0,0)).X` is `-1e-308` (was `0`) and
  `Inverse(Quat(1e-308,0,0,0)).X` is `-1e308` (was `-Infinity`); a normal `Quat(0,0,0,2)` inverse is
  still `W=0.5`. The zero/non-finite-length trap is unchanged. math.md updated. Regression:
  `test_inverse_extreme_norms` in `RTQuatTests.cpp` checks both extremes stay finite/nonzero plus a
  normal-case sanity check. **Resolved.**

### VDOC-207 — Matrix equality treats NaN as equal to arbitrary values

- **Classification:** confirmed floating-point comparison bug
- **Area:** `Viper.Math.Mat3.Eq` and `Viper.Math.Mat4.Eq`
- **Evidence:** both implementations reject a component only when
  `fabs(a[i] - b[i]) > epsilon`. A NaN difference makes that comparison false, as does every
  finite difference when `epsilon` itself is NaN. Existing-binary probes return true both for a
  NaN-containing Mat3 compared with zero and for `Mat4.Eq(Zero(), Identity(), NaN)`.
- **Impact:** validation, cache invalidation, and transform-change detection can accept invalid or
  completely different matrices as equal.
- **Likely repair point:** reject non-finite epsilon and explicitly define component NaN semantics
  before the tolerance comparison; add signed-zero, infinity, and NaN tests.
- **Review (2026-07-16):** Confirmed and fixed in both `rt_mat3_eq` and `rt_mat4_eq`. The epsilon
  guard is now `if (!(epsilon > 0.0) || !isfinite(epsilon)) epsilon = 1e-9;` so a NaN/Inf epsilon
  (which slipped past `epsilon <= 0.0`) is normalized to the default instead of making `diff > NaN`
  always false. The per-component test is now the NaN-safe negated form
  `if (!(fabs(d) <= epsilon)) return 0;` so a NaN component difference counts as NOT equal — a
  matrix containing NaN is never equal to any matrix, including itself (IEEE semantics), while
  signed zeros still compare equal. math.md updated for both. Regression: new
  `RTMatEqTests.cpp` (test_rt_mat_eq) covers the headline `Mat4.Eq(Zero, Identity, NaN)` case, NaN
  components vs zero under a huge epsilon, NaN-not-equal-to-itself, ordinary inequality/equality,
  and signed-zero equality for both Mat3 and Mat4. **Resolved.**

### VDOC-208 — Mat3 and Mat4 inverse failures have incompatible contracts

- **Classification:** confirmed math API inconsistency
- **Area:** `Viper.Math.Mat3.Inverse` and `Viper.Math.Mat4.Inverse`
- **Evidence:** Mat3 traps when its determinant is non-finite or has magnitude below `1e-15`.
  Mat4 uses the same threshold but silently returns identity for the same condition (and for an
  invalid receiver). The Mat4 source header additionally claimed that a runtime warning was
  emitted, but no warning or trap occurs. The existing binary returns element `[0,0] == 1` for
  `Mat4.Inverse(Mat4.Zero())`.
- **Impact:** generic matrix code cannot handle inversion failure consistently, and a singular
  transform silently becomes a valid-looking identity matrix that can move geometry to an
  unrelated location.
- **Likely repair point:** give both inverse APIs one explicit failure model—preferably a trap or
  Result/TryInverse form—and never use identity as an ambiguous error sentinel.
- **Review (2026-07-16):** Confirmed and fixed by unifying both inverse APIs on the trap model
  (Mat3 already trapped). `rt_mat4_inverse` now traps on a null receiver (`Mat4.Inverse: null
  matrix`), an invalid receiver (`invalid matrix`), and a singular / non-finite-determinant matrix
  (`singular matrix`) instead of silently returning identity — identity is a valid transform that
  would move geometry to an unrelated location. The cofactor math was extracted into a shared
  `mat4_inverse_or_null` core so the trapping public API and a new non-trapping
  `rt_mat4_try_inverse` (returns NULL on failure, classified INTERNAL in RuntimeSurfacePolicy.inc)
  share it. The two internal engine callers that relied on the old identity fallback
  (rt_game3d_controllers.c and rt_navagent3d.c, inverting a parent world transform) now call
  `rt_mat4_try_inverse` and degrade gracefully on NULL (they already handled a NULL inverse). The
  Mat4 header invariant claiming a silent identity return was corrected. Verified live: both
  `Mat3.Inverse(Zero())` and `Mat4.Inverse(Zero())` trap `singular matrix`; `Mat4.Inverse(Identity)`
  still works. math.md updated. Regression: `RTMatEqTests.cpp` extended to assert both inverses
  trap+return NULL on a singular matrix, the invertible case still works, and `rt_mat4_try_inverse`
  returns NULL without trapping. **Resolved.**

### VDOC-209 — Math runtime metadata marks trapping owned APIs as infallible or unknown

- **Classification:** confirmed machine-readable registry metadata inconsistency
- **Area:** `viper --dump-runtime-api` for `Viper.Math.*`
- **Evidence:** current JSON marks `BigInt.Div`, `Mod`, negative `Pow`/`Sqrt`, `ToStrBase`, and
  `Mat3.Inverse` as `fallibility=infallible` even though each has documented trap paths. Fresh
  BigInt, vector, quaternion, spline, and matrix results are commonly
  `ownership=unknown`, while only selected constructors are identified as owned. Similar metadata
  hides `Quat.Inverse`/`Slerp`, vector divisor, spline index, and allocation failures.
- **Impact:** the agent-facing inventory cannot safely plan error paths or object lifetimes for the
  exact math APIs it is intended to describe.
- **Likely repair point:** annotate the math manifest's ownership/fallibility explicitly, use
  concrete object result types, and add contract assertions for known trapping/allocating entry
  points.
- **Review (2026-07-16):** Confirmed and fixed the same way as the IO metadata (VDOC-198).
  Ownership: a new rule marks every `Viper.Math.*` function returning an object (`obj`/`seq`) as
  `owned`, since math values are immutable, freshly allocated results — this fixes the "fresh
  BigInt/Vec/Mat/Quat/Spline results are unknown" class for the bare-`obj` returns the typed rule
  missed. Fallibility: the explicit override table gained the trapping Math domain operations —
  `BigInt.Div` / `Mod` / `Pow` / `PowMod` / `Sqrt` / `ToStringBase`, `Mat3.Inverse` / `Mat4.Inverse`
  (now consistent after VDOC-208), `Quat.Inverse` / `Slerp`, and `Vec2.Div` / `Vec3.Div` — each
  verified to trap in source. Non-trapping ops like `BigInt.Add` and (post-VDOC-201) `Spline.Eval`
  correctly stay `infallible`, and all now report `owned`. Verified via `--dump-runtime-api`.
  tools.md documents the Math coverage. Regression: `agent_cli` pins `Mat4.Inverse` and
  `BigInt.Div` as `traps` / `owned`. **Resolved.**

### VDOC-210 — Native programs do not install the documented graceful-shutdown signal bridge

- **Classification:** confirmed VM/native behavior gap
- **Area:** `Viper.System.Shutdown` in native/AOT execution
- **Evidence:** the process-global shutdown bitmask is implemented in `rt_shutdown.c`, but the only
  registrations for `SIGINT`, `SIGTERM`, or `SetConsoleCtrlHandler` are in `src/vm/VM.cpp`.
  No native runtime startup path publishes OS events through `rt_shutdown_request`. Cooperative
  `Shutdown.Request` works in either backend, while automatic Ctrl-C/termination publication is
  VM-only.
- **Impact:** a native server following the public poll-loop guidance can be terminated by the OS
  without ever observing `Interrupt` or `Terminate`, even though the same program works in the VM.
- **Likely repair point:** install equivalent signal/console handlers in native runtime startup,
  retaining the current signal-safe flag-to-atomic handoff, or explicitly make OS integration a
  VM-only capability query.
- **Review (2026-07-16):** Confirmed and fixed by exposing an explicit opt-in installer plus fixing
  a deeper native-link gap the investigation surfaced. A new `rt_shutdown_install_signal_handlers()`
  installs SIGINT/SIGTERM (POSIX) or the console control handler (Windows) that call
  `rt_shutdown_request` — the same signal-safe flag-to-atomic handoff, callable directly from signal
  context since `rt_shutdown_request` is only a lock-free atomic OR. It is idempotent and exposed as
  `Viper.System.Shutdown.InstallSignalHandlers`. A blanket auto-install was rejected: native
  programs use a raw `_start` stub with no C-startup hook, and a constructor would wrongly hijack
  Ctrl-C for the `viper` compiler and tools (which also link the runtime). While testing, the
  native link failed even for the EXISTING `Shutdown.Poll` — `RuntimeComponents.hpp` mapped the Exec
  archive by `rt_exec_`/`rt_process_`/`rt_machine_` prefixes but omitted `rt_shutdown_`, so any
  native Shutdown user got an undefined-symbol error; added `rt_shutdown_` and
  `Viper.System.Shutdown.` to the Exec component matcher. End-to-end verified: a native program that
  calls `InstallSignalHandlers()`, receives a real SIGINT, and observes `Interrupt` via `Poll` exits
  gracefully (was VM-only). system.md updated (method table + capability note). Regression:
  `test_installed_signal_handlers_publish` in `RTShutdownTests.cpp` raises real SIGINT/SIGTERM and
  asserts `Poll` returns the reason. Completeness + strict surface audits pass. **Resolved.**

### VDOC-211 — Legacy argument initialization uses a non-atomic flag as a cross-thread lock

- **Classification:** confirmed C data race / initialization defect
- **Area:** `Viper.System.Environment.GetArgument*` legacy-context fallback
- **Evidence:** `g_legacy_args_host_init_state` is a plain process-global `int`. Reads and writes in
  `rt_args_ensure_legacy_host_initialized`, `rt_args_push`, and `rt_args_clear` are unsynchronized,
  although the comments describe a `0 -> 1 -> 2` first-thread protocol. A second POSIX thread can
  race the import or spin on a value whose concurrent mutation is undefined in C; only the Windows
  loop even calls a yield primitive.
- **Impact:** concurrent first access can observe partially populated arguments, duplicate or lose
  entries, or spin indefinitely under compiler optimization.
- **Likely repair point:** use a real once primitive/atomic state with acquire-release ordering and
  a portable wait/yield path, or move initialization into context construction before publication.
- **Review (2026-07-16):** Confirmed and fixed by converting the flag to a real atomic once-init.
  `g_legacy_args_host_init_state` is now `atomic_int`; the `0 -> 1` claim is a single
  `atomic_compare_exchange_strong_explicit` (acq_rel), so exactly one thread imports the host argv,
  it publishes state `2` with release ordering, and every other thread acquire-loads `2` and
  observes the fully populated arguments — no partial reads, lost/duplicated entries, or UB spin on
  a plain int. `rt_args_mark_legacy_host_initialized` uses a `0 -> 2` CAS that leaves an in-progress
  (`1`) or done (`2`) state untouched. The spin-wait now yields on every platform via a new
  `rt_args_spin_yield` (`sched_yield` on POSIX, `SwitchToThread` on Windows) instead of busy-looping
  on non-Windows. Pure internal robustness — no user-facing contract change. Single-threaded
  behavior is unchanged (existing `test_rt_args` green). Regression: `test_rt_args` gained an
  8-thread × 2000-read concurrent stress that asserts a stable count with no crash (the one-shot
  import can't be reset from a test, so this exercises the atomic read/CAS path rather than
  reproducing the original race). **Resolved.**

### VDOC-212 — Windows Process passes a UTF-16 environment block as ANSI

- **Classification:** confirmed Windows process-launch correctness bug
- **Area:** `Viper.System.Process.StartWithEnv`
- **Evidence:** `build_env_block_wide` creates a double-NUL-terminated `wchar_t` block, but the
  corresponding `CreateProcessW` flags are only `CREATE_NO_WINDOW |
  EXTENDED_STARTUPINFO_PRESENT`. Windows requires `CREATE_UNICODE_ENVIRONMENT` for a Unicode block.
  The PTY backend correctly supplies that flag, highlighting the divergence.
- **Impact:** explicit environments can be truncated or parsed as alternating one-byte strings;
  non-ASCII values are unusable and even ASCII `KEY=value` entries are not reliably delivered.
- **Likely repair point:** add `CREATE_UNICODE_ENVIRONMENT`, sort/validate the Windows block as
  required, and add a Windows round-trip test containing multiple variables and non-ASCII text.
- **Review (2026-07-16):** Confirmed and fixed. `rt_process_start_with_env`'s `CreateProcessW` call
  now ORs `CREATE_UNICODE_ENVIRONMENT` into its creation flags whenever an explicit wide env block
  is supplied (`build_env_block_wide` already produced a correct double-NUL-terminated UTF-16
  block), so Windows no longer misinterprets it as ANSI and truncates/garbles the variables. This
  matches the PTY backend, which already set the flag (the divergence the finding cited). A NULL
  block still inherits the parent environment (flag omitted). The file-header note and system.md
  were corrected. Windows-only branch (`#ifdef _WIN32`), so it does not compile on macOS; verified
  by inspection against the MSDN requirement and the PTY precedent, and the POSIX env round-trip
  (`test_process_cwd_and_env`) still passes. The Windows multi-variable / non-ASCII round-trip needs
  a by-hand `build_viper_win.cmd` run to exercise the CreateProcessW path directly. **Resolved
  (Windows behavior pending a by-hand Windows build).**

### VDOC-213 — Executable lookup changes across Process and PTY environment modes

- **Classification:** confirmed cross-API process-launch inconsistency
- **Area:** POSIX `Viper.System.Process.Start*` and `Viper.System.Pty.Open*`
- **Evidence:** Process always calls `posix_spawn`, so bare program names are not searched through
  `PATH`. PTY calls `execvp` when inheriting the environment but switches to `execve` when an
  explicit environment Seq is supplied, so adding an environment changes a previously searchable
  bare name into exit code 127. An existing-binary probe also shows
  `Process.StartWithEnv("sh", ...).IsValid() == false` while absolute paths work.
- **Impact:** otherwise equivalent launch APIs and overloads disagree about program resolution;
  adding one environment variable can make a valid PTY command fail after the handle was already
  reported as successfully opened.
- **Likely repair point:** choose one policy, preferably explicit `PATH` search for bare names in
  every mode (using the supplied environment's PATH when present), and expose startup exec failure
  before returning an apparently valid PTY session.
- **Review (2026-07-16):** Confirmed and fixed by unifying on explicit PATH search for bare names in
  every mode. Process: a new `process_resolve_program_path` PATH-searches a bare name (using the
  supplied environment's `PATH` via `process_env_lookup`, else the inherited `PATH`) and resolves it
  to a full path before `posix_spawn` (which does no search); a name with `/` is used verbatim, and
  an unresolved name is left unchanged so the spawn fails like `execvp`. PTY: the explicit-env child
  branch no longer uses `execve` (no PATH search) — it adopts the explicit environment as `environ`
  then `execvp`s, so it searches that environment's `PATH` and passes it to the child, identical to
  Process. Second defect fixed too: the PTY now uses a close-on-exec exec-status pipe — the child
  writes its `errno` on any pre-exec/exec failure, a successful exec closes the pipe (parent sees
  EOF), so `rt_pty_open` returns NULL / `Err` on exec failure instead of an apparently-valid session
  that only fails later. Verified: `Process.StartWithEnv("sh", …)` now succeeds (was false),
  `Pty.Open` of a nonexistent program returns `Err`, and `Pty.Open("sh", env)` succeeds via PATH.
  system.md updated in all three spots. Regressions: `test_process_bare_name_path_search_with_env`
  (RTExecTests) and `PtyOpenSurfacesExecFailure` (TestRuntimeOpenLoadResultApis, PTY-supported here).
  **Resolved.**

### VDOC-214 — PTY Resize reports success without observing backend success

- **Classification:** confirmed status-reporting bug
- **Area:** `Viper.System.Pty.PtySession.Resize`
- **Evidence:** the public function returns true for every valid handle. The POSIX helper discards
  the result of `ioctl(TIOCSWINSZ)`, and the Windows helper discards the ConPTY HRESULT; neither
  communicates success to the caller.
- **Impact:** terminal UIs can believe the child received new dimensions when it did not, producing
  incorrect wrapping/layout with no recoverable diagnostic.
- **Likely repair point:** make backend helpers return Boolean success, preserve OS diagnostics in
  the Result/LastError path, and propagate the value from `Resize`.
- **Review (2026-07-16):** Confirmed and fixed exactly as recommended. All three `pty_resize_impl`
  backends now return `int` success: POSIX checks `ioctl(TIOCSWINSZ)` and, on failure, records the
  errno via `pty_set_last_errno("TIOCSWINSZ failed")`; Windows checks the `ResizePseudoConsole`
  HRESULT with `FAILED(hr)` and records `pty_set_last_error`; the unsupported-platform stub returns
  0. `rt_pty_resize` now returns `pty_resize_impl(...) ? 1 : 0` instead of unconditionally 1, so a
  caller can distinguish a real OS resize from a discarded failure and read `LastError` on failure.
  Verified live: `Resize` on a live `sh` PTY session returns 1. system.md updated. Regression:
  `PtyOpenSurfacesExecFailure` (TestRuntimeOpenLoadResultApis) now also resizes the live session and
  asserts the result is 1. **Resolved.**

### VDOC-215 — PTY LastError is an unsynchronized process-global buffer

- **Classification:** confirmed data race / diagnostic corruption
- **Area:** `Viper.System.Pty.LastError`, `Open`, `OpenResult`, and `IsSupported`
- **Evidence:** every path reads or writes the static `char pty_last_error[256]` with
  `snprintf`/plain byte access. A source comment calls PTY main-thread-only, but the public methods
  contain no main-thread assertion and the registry exposes no such constraint.
- **Impact:** concurrent opens or support checks invoke undefined behavior and can return a torn or
  unrelated error message. A successful operation in one thread can erase another thread's error.
- **Likely repair point:** make the side channel thread-local (as Exec does), or put the diagnostic
  exclusively in `OpenResult` and deprecate LastError; enforce any genuine main-thread rule.
- **Review (2026-07-16):** Confirmed and fixed by making the buffer thread-local, exactly as Exec
  does. `pty_last_error` is now declared `RT_THREAD_LOCAL` (the portable TLS macro:
  `__declspec(thread)` / `_Thread_local` / `__thread`), so concurrent `Open` / `OpenResult` /
  `IsSupported` calls on different threads write independent per-thread buffers and can never tear
  or clobber each other's diagnostic; each thread reads back the error from the operation it
  performed. The stale doc comment claiming an unsynchronized process-global was corrected, and
  system.md now describes the thread-local behavior (still recommending `OpenResult` for structured
  errors). Regression: `PtyLastErrorIsThreadLocal` in `TestRuntimeOpenLoadResultApis.cpp` runs 8
  threads × 500 failing opens each and asserts every thread reads back its own non-empty, non-torn
  error. **Resolved.**

### VDOC-216 — Windows Machine.OSVer can return a compatibility version

- **Classification:** confirmed obsolete-platform-API defect
- **Area:** `Viper.System.Machine.OsVer` / `Environment.PlatformVersion`
- **Evidence:** Windows calls deprecated `GetVersionExA`, whose result is conditioned on the
  embedding executable's supported-OS compatibility manifest. Viper's packaging code can generate
  such a manifest when configured, but the runtime API itself is also used by unmanifested native
  outputs/custom embedders and has no direct version-query fallback. Other platform paths query the
  kernel/product release directly.
- **Impact:** feature gates and diagnostics can identify Windows 10/11 as an older release.
- **Likely repair point:** use a supported version source such as `RtlGetVersion` with an explicit
  compatibility policy, ship an appropriate manifest, and test the packaged executable rather
  than only the runtime object.
- **Review (2026-07-16):** Confirmed and fixed. `rt_machine_os_ver` now queries `RtlGetVersion`
  from ntdll first (loaded via `GetModuleHandleW`/`GetProcAddress` since it has no linkable import
  library), which returns the true OS version regardless of the executable's supported-OS
  compatibility manifest — so an unmanifested native output or custom embedder no longer misreports
  Windows 10/11 as an older release. It falls back to the deprecated `GetVersionExA` only if
  `RtlGetVersion` is unavailable, and to `"unknown"` if both fail. The file-header note and
  system.md were corrected. Windows-only branch (`#ifdef _WIN32`), so it does not compile on macOS;
  verified by inspection against the documented `RtlGetVersion` behavior, and the macOS build is
  unaffected. Exercising the manifest-independence needs a by-hand `build_viper_win.cmd` run on
  Windows 10/11. **Resolved (Windows behavior pending a by-hand Windows build).**

### VDOC-217 — Windows Machine text queries bypass Viper's UTF-8 conversion path

- **Classification:** confirmed cross-platform Unicode inconsistency
- **Area:** `Viper.System.Machine.Host`, `User`, `Home`, and `Temp`
- **Evidence:** these functions use `GetComputerNameA`, `GetUserNameA`, `GetTempPathA`, and narrow
  CRT environment strings, then copy the bytes directly into runtime Strings. In the same module,
  `Viper.System.Environment` deliberately uses wide Win32 APIs and strict UTF-16/UTF-8 conversion.
- **Impact:** non-ASCII user names, host names, and paths can become invalid UTF-8 or mojibake and
  then fail downstream path/string APIs.
- **Likely repair point:** switch Machine queries to their wide forms and reuse the validated
  conversion helpers; obtain home paths through wide environment or known-folder APIs.
- **Review (2026-07-16):** Confirmed and fixed. `rt_machine_host`/`_user`/`_home`/`_temp` now use
  the wide Win32 APIs on Windows — `GetComputerNameW`, `GetUserNameW`, `GetTempPathW`, and
  `_wgetenv` for `USERPROFILE` / `HOMEDRIVE` / `HOMEPATH` — and convert through the shared,
  validated `rt_file_path_wide_to_string` (the same UTF-8 boundary conversion the codebase uses
  elsewhere), instead of the narrow `*A` APIs and `getenv` that copied ANSI bytes verbatim. Non-ASCII
  host/user names and paths now round-trip as valid UTF-8, consistent with
  `Viper.System.Environment`. system.md updated. Windows-only branches (`#ifdef _WIN32`), so they do
  not compile on macOS; verified by inspection and the POSIX paths are unchanged (`test_rt_machine`
  green, `Machine.User`/`Host` still resolve). The non-ASCII round-trip needs a by-hand Windows build
  to exercise the wide APIs. **Resolved (Windows behavior pending a by-hand Windows build).**

### VDOC-218 — Machine.MemFree measures different memory categories by platform

- **Classification:** confirmed cross-platform semantic inconsistency
- **Area:** `Viper.System.Machine.MemFree`
- **Evidence:** Linux returns only `sysinfo.freeram`, macOS returns
  `(free_count + inactive_count) * page_size`, and Windows returns `ullAvailPhys`. These represent
  strict free pages, free-plus-reclaimable pages, and available physical memory respectively.
- **Impact:** the same threshold can reject work on Linux while accepting it on macOS/Windows even
  when the hosts have comparable usable memory; the name cannot support portable admission logic.
- **Likely repair point:** define one portable “available” estimate and implement it per platform
  (for Linux, include the appropriate reclaimable/cache data), or expose distinctly named metrics.
- **Review (2026-07-16):** Confirmed and fixed by unifying on an "available memory" estimate — the
  memory usable for new work without swapping, including reclaimable cache. Windows
  (`ullAvailPhys`) and macOS (`free + inactive` reclaimable pages) already reported this category;
  Linux was the outlier, returning only strict `sysinfo.freeram`. `rt_machine_mem_free` on Linux
  now parses `MemAvailable` from `/proc/meminfo` (the kernel's own available estimate, which
  includes reclaimable page cache), falling back to `(freeram + bufferram) * mem_unit` on older
  kernels lacking `MemAvailable`. The doc comment and system.md now define the single semantics.
  macOS unchanged (verified: `MemoryFree` still reports ~available and `test_rt_machine` is green);
  the Linux `/proc/meminfo` path is inspection-verified and needs a by-hand Linux run to confirm
  against `free -b`. **Resolved.**

### VDOC-219 — macOS Machine.Cores can return a non-positive count

- **Classification:** confirmed fallback-contract bug
- **Area:** `Viper.System.Machine.Cores`
- **Evidence:** Linux/generic POSIX check `sysconf(_SC_NPROCESSORS_ONLN) > 0` and fall back to 1.
  The macOS branch returns a successful `hw.logicalcpu` value without checking it and returns the
  raw `sysconf` result on failure, including `-1`.
- **Impact:** callers sizing worker pools can receive zero or a negative count despite the intended
  safe-minimum contract.
- **Likely repair point:** validate both macOS probes and return 1 for every non-positive/failing
  result.
- **Review (2026-07-16):** Confirmed and fixed. The macOS branch of `rt_machine_cores` now only
  returns the `hw.logicalcpu` sysctl value when it is `> 0`, otherwise falls back to a POSITIVE
  `sysconf(_SC_NPROCESSORS_ONLN)`, else the safe minimum of 1 — it no longer returns the raw
  (possibly 0 or -1) sysctl/sysconf result. Added a matching `> 0` guard to the Windows
  `dwNumberOfProcessors` path for consistency; Linux/generic already guarded. The doc comment and
  system.md now state every platform returns at least 1. Verified on macOS (`Cores` returns 14) and
  the existing `test_cores` (`cores >= 1`) stays green — the fix guarantees that invariant even when
  the OS probes fail. **Resolved.**

### VDOC-220 — SetAltScreen is not an idempotent toggle and cleanup does not exit it

- **Classification:** confirmed terminal-state lifecycle bug
- **Area:** `Viper.Terminal.SetAltScreen`
- **Evidence:** every true call emits the enter sequence and increments output batch depth; every
  false call decrements it. No alternate-screen state guards repeated calls. The registered exit
  handler only restores POSIX termios—it does not balance batching or emit the alternate-screen
  exit sequence, despite the source header previously claiming both were restored.
- **Impact:** repeated enable calls followed by one disable can strand output in batch mode, while
  normal process exit can leave a terminal on its alternate screen until the terminal emulator
  repairs the state.
- **Likely repair point:** track one alt-screen Boolean, transition/batch only on state changes,
  and make the exit handler perform a best-effort balanced screen/batch cleanup.
- **Review (2026-07-16):** Confirmed and fixed exactly as recommended. A new `g_alt_screen_active`
  boolean tracks the alternate-screen state; `rt_term_alt_screen_i32` now enters (emit `?1049h`,
  raw mode, `begin_batch`) only when not already active and exits (emit `?1049l`, `end_batch`,
  restore raw) only when active — so it is an idempotent toggle that adjusts batch depth exactly
  once per state change. The exit handler (`term_atexit_handler`) now performs balanced cleanup: if
  the program is still on the alternate screen it ends the batch, emits `?1049l`, and clears the
  flag, then restores raw mode. The file-header note and system.md were corrected. Verified
  end-to-end under a real PTY: a program that calls `SetAltScreen(true)` three times then
  `SetAltScreen(false)` once emits exactly one `?1049h` and one `?1049l`, and its subsequent
  `Print` flushes (the sentinel appears) — proving the batch depth is balanced (the old code left
  depth at 2 and would have stranded the output). **Resolved.**

### VDOC-221 — Terminal's public Integer parameters narrow silently to 32 bits

- **Classification:** confirmed registry/runtime width inconsistency
- **Area:** `GetKeyTimeout`, `SetPosition`, and `SetColor`
- **Evidence:** the runtime registry advertises `i64` parameters but maps directly to
  `rt_getkey_timeout_i32`, `rt_term_locate_i32`, and `rt_term_color_i32`; the runtime signature
  catalog declares those C symbols as `i32`. Separate i64 wrapper functions exist but are not the
  registered targets. Values are therefore narrowed modulo 32 bits before use.
- **Impact:** a large positive timeout can become negative and block indefinitely; large cursor or
  color values can change sign and take unrelated branches.
- **Likely repair point:** register the i64 wrappers and validate/clamp before narrowing, or change
  the public signature with the required compatibility/ADR treatment.
- **Review (2026-07-16):** Confirmed and fixed by registering the i64 wrappers AND making them
  clamp. The registry now maps `Viper.Terminal.ReadKeyFor` / `SetColor` / `SetPosition` to the
  i64-parameter symbols `rt_getkey_timeout` / `rt_term_color` / `rt_term_locate` (was the `_i32`
  variants, which silently dropped the high 32 bits at the ABI boundary). Those wrappers previously
  still cast `(int32_t)` (same truncation); they now route each parameter through a new saturating
  `term_clamp_i32` (clamps to `[INT32_MIN, INT32_MAX]` rather than taking the low 32 bits), so a
  large positive value saturates to `INT32_MAX` instead of wrapping negative — a big timeout no
  longer blocks forever and large cursor/color values no longer change sign. The now-internal `_i32`
  symbols are classified `INTERNAL_SYMBOL` in RuntimeSurfacePolicy.inc (still the BASIC-lowering
  targets). Completeness + strict audit pass. system.md updated. Regression: `RTTermColorTests.cpp`
  gained a PTY-backed case — `rt_term_color(2^31, -1)` (which truncates to INT32_MIN and the i32
  path would drop as `fg < -1`) now emits a valid non-empty SGR via the clamp, and in-range values
  match the i32 path. **Resolved.**

### VDOC-222 — System runtime metadata hides nulls, traps, and owned results

- **Classification:** confirmed machine-readable registry metadata inconsistency
- **Area:** `viper --dump-runtime-api` for Environment, Exec, Process, PTY, Machine, Unsafe, GC,
  and Terminal
- **Evidence:** current JSON calls `Environment.Cwd`, Process reads, PTY reads/resize, Unsafe
  release/value-type operations, and `GC.Collect` infallible despite documented trap paths. Process
  Start and legacy Terminal reads are marked non-null even though startup/EOF returns null. Fresh
  strings, maps, Result objects, handles, and CommandResult values frequently retain unknown
  ownership; `ReadStdout`/`Read` do not expose truncation fallibility.
- **Impact:** tools using the agent-facing catalog cannot distinguish startup absence from a valid
  handle, plan cleanup for owned results, or install necessary error paths.
- **Likely repair point:** annotate this surface explicitly for nullability, ownership, Result/
  Option use, side channels, and traps, and add audit assertions for known nullable/trapping APIs.
- **Review (2026-07-16):** Confirmed and resolved. Audited the live `--dump-runtime-api` System
  surface and found the trap/ownership heuristics already correct for most rows (Process/PTY reads
  were fixed to `traps`/`owned` under VDOC-198, Environment reads report `owned`, `GC.Collect`
  verified non-trapping in `rt_gc.c` so it correctly stays `infallible`). The genuine gaps were:
  `Process.Start` / `StartWithEnv` returned a handle marked non-null even though a failed spawn
  yields `NULL`; the unmanaged `Unsafe.Release` / `Unsafe.ReleaseStr` primitives (which
  `rt_object.c` traps on an invalid/freed handle) were labeled `infallible`; and
  `PtySession.Resize` — an `i1` status return after VDOC-214 — was labeled `infallible`. Added a
  `kNullableReturns` list to `runtimeReturnNullable` (spawn handles now report `nullable: true`,
  keeping the orthogonal `fallibility: infallible` since a null return is not a trap) and three
  entries to the `explicitRuntimeFallibility` override table (`Unsafe.Release`/`ReleaseStr` →
  `traps`, `PtySession.Resize` → `status`) in `src/tools/viper/main.cpp`. Pinned all three
  corrected contracts in `src/tests/tools/AgentCliTests.cmake` (the `agent_cli` ctest) and
  documented the System coverage of the override tables in `docs/tools.md`. **Resolved.**

### VDOC-223 — Time's POSIX failure fallback is not monotonic

- **Classification:** confirmed fallback-contract inconsistency
- **Area:** `Viper.Time.Clock`, `Countdown`, and `Stopwatch` on POSIX
- **Evidence:** all three implementations prefer `CLOCK_MONOTONIC` but call `CLOCK_REALTIME` if
  that query fails. `Clock` then returns `0` if realtime also fails; Stopwatch does the same,
  while Countdown traps. The public page and implementation headers previously called the values
  unconditionally monotonic and non-decreasing.
- **Impact:** a clock adjustment on the fallback path can make elapsed values decrease, inflate a
  countdown wait, or produce negative accumulated intervals. A zero failure result is also
  indistinguishable from a legitimate clock reading.
- **Likely repair point:** treat monotonic-clock failure as a surfaced error, or maintain a
  process-local non-decreasing fallback and expose clock availability/quality explicitly.
- **Review (2026-07-16):** Confirmed and resolved by the process-local non-decreasing fallback
  option. Added `rt_time_monotonic_ratchet(int64_t *floor, int64_t candidate)` to `rt_time.c`
  (declared in `rt_time.h`, registered `INTERNAL_SYMBOL`): a lock-free CAS-max that advances a
  caller-owned floor when the candidate is newer and otherwise returns the retained floor. All
  three POSIX `CLOCK_REALTIME` fallbacks now route through it — `rt_timer_ms` / `rt_clock_ticks_us`
  in `rt_time.c` (separate ms/us floors), `get_timestamp_ns` in `rt_stopwatch.c`, and
  `get_timestamp_ms` in `rt_countdown.c` — so a backward wall-clock adjustment on the failure path
  can no longer make elapsed values decrease or a countdown deadline recede. The fast
  `CLOCK_MONOTONIC` path is untouched and holds no shared state. The `0`-on-total-failure last
  resort is retained per the documented contract (Countdown still traps on total failure). Added
  `test_ratchet_non_decreasing` to `RTClockTests.cpp` (exercises forward/backward/equal streams and
  independent floors directly, runs on all platforms), and rewrote the previously inaccurate
  "can decrease" / "not monotonic" language in `rt_time.c`/`.h`, `rt_stopwatch.c`, `rt_countdown.c`,
  and `docs/viperlib/time.md`. Build clean; `test_rt_clock`/`stopwatch`/`countdown`/`timer` and the
  strict runtime audit all pass. **Resolved.**

### VDOC-224 — Windows time objects race while caching QPC frequency

- **Classification:** confirmed C data race
- **Area:** first concurrent use of `Viper.Time.Countdown` and `Viper.Time.Stopwatch` on Windows
- **Evidence:** each `get_timestamp_*` helper has a function-static mutable `LARGE_INTEGER freq`.
  Threads read and write `freq.QuadPart` without atomics or a once primitive. The source comments
  called the duplicate initialization benign because QPC frequency is constant, but identical
  results do not make concurrent non-atomic C accesses defined.
- **Impact:** concurrent first use invokes undefined behavior; compiler optimization can expose a
  torn, stale, or otherwise invalid frequency and corrupt elapsed-time calculations.
- **Likely repair point:** initialize frequency through a platform once primitive or immutable
  process startup state, and add a concurrent first-use regression test.
- **Review (2026-07-16):** Confirmed and resolved. Both `get_timestamp_ns` (`rt_stopwatch.c`) and
  `get_timestamp_ms` (`rt_countdown.c`) held a function-static `LARGE_INTEGER freq` written without
  synchronization on first use — a genuine C data race regardless of the constant result. Replaced
  each with a `*_qpc_freq()` helper that reads and publishes an `int64_t` cache slot through
  `__atomic_load_n`/`__atomic_store_n` acquire/release (via `rt_atomic_compat.h`, which maps the
  builtins to Interlocked intrinsics on MSVC). `QueryPerformanceFrequency` is constant for the life
  of the system, so duplicate concurrent stores write the identical value and no once primitive is
  needed for correctness; the acquire/release pair makes every read well-defined. `rt_time.c`'s
  Windows branch was already race-free (it uses a per-call local `freq`, not a static cache), so it
  needed no change. Added `test_concurrent_first_use` to `RTStopwatchTests.cpp`: eight threads spin
  to a shared start gate and then hammer `rt_stopwatch_elapsed_ns` on their own stopwatches,
  asserting every reading is non-negative and non-decreasing (a torn frequency would surface as a
  negative/out-of-order value). The test races the process-global frequency cache on Windows and
  exercises the monotonic path's thread safety on POSIX. Build clean; stopwatch/countdown/clock
  tests pass. Updated the "not synchronized" language in `docs/viperlib/time.md`. **Resolved.**

### VDOC-225 — DateTime.Create cannot distinguish failure from a valid instant

- **Classification:** confirmed sentinel collision
- **Area:** `Viper.Time.DateTime.Create`
- **Evidence:** invalid, normalized, skipped-DST, or unrepresentable civil input returns `-1`.
  However the implementation accepts pre-epoch dates, and under `TZ=UTC` an existing-binary probe
  confirms `Create(1969, 12, 31, 23, 59, 59) == -1` for the valid instant one second before the
  Unix epoch.
- **Impact:** callers cannot determine whether `-1` is a valid result or failed creation and can
  silently discard or process the wrong instant.
- **Likely repair point:** add an Option/Result creation API and retain the numeric sentinel only as
  an explicitly named compatibility method.
- **Review (2026-07-16):** Confirmed and resolved along the recommended path. The registry name for
  the sentinel form is `Viper.Time.DateTime.FromParts` (bound to `rt_datetime_create`); it still
  returns `-1` both for failure and for the valid instant one second before the epoch. Extracted the
  full validation/`mktime`/round-trip logic into a shared `dt_create_impl(..., int64_t *out)` that
  returns a success flag, so success is no longer encoded in the returned instant. `rt_datetime_create`
  now delegates to it as the legacy sentinel compatibility form, and a new
  `rt_datetime_create_option` returns `Some(i64)` for any valid instant — including the pre-epoch
  `-1` — and `None` on failure, mirroring the existing `TryParseOption` convention (both use
  `rt_option_some_i64`/`rt_option_none`). Registered as `Viper.Time.DateTime.TryFromParts`
  (`obj<Viper.Option>(i64×6)`, RT_FUNC + RT_METHOD); `--dump-runtime-api` classifies it
  `fallibility: option` / `ownership: owned`. Added `test_create_option` to `RTDatetimeTests.cpp`
  (forces `TZ=UTC` portably via a `_WIN32`-guarded `set_tz` helper, then verifies the sentinel
  collision, `Some(-1)` for the pre-epoch instant, `Some(0)` for the epoch, agreement with the
  sentinel value for valid input, and `None` for invalid month/leap-day/huge-year). Ran
  `check_runtime_completeness.sh`, regenerated `docs/generated/runtime`, and confirmed the strict
  audit, `runtime_reference_docs`, `agent_cli`, and `test_rt_datetime` all pass. Documented the new
  form in `docs/viperlib/time.md`. Note: this finding does not cover the repeated-DST-hour ambiguity
  (that is VDOC-226, next). **Resolved.**

### VDOC-226 — Local DateTime construction leaves repeated DST hours host-defined

- **Classification:** confirmed cross-platform civil-time ambiguity
- **Area:** `DateTime.Create`, local `ParseISO`, and local `ParseDate`
- **Evidence:** local conversion calls `mktime` with `tm_isdst = -1` and only verifies that the
  resulting fields round-trip. This rejects a skipped hour, but both occurrences of a repeated
  hour have identical fields, so the selected instant is whatever the host C library chooses. On
  the reviewed macOS host, `2025-11-02 01:30` in New York resolves to the EDT occurrence.
- **Impact:** the same local input can identify different Unix instants on different hosts, with no
  API for selecting the earlier/later or standard/DST occurrence.
- **Likely repair point:** expose an explicit ambiguity policy or parse Result carrying both
  candidates; use the embedded zone engine for deterministic named-zone civil conversion.

### VDOC-227 — RelativeTime.FormatDuration emits negative zero

- **Classification:** confirmed formatting defect
- **Area:** `Viper.Time.RelativeTime.FormatDuration`
- **Evidence:** the function records the sign before dividing the magnitude into whole seconds.
  It emits a sign whenever the input is negative even when every whole-unit component is zero. An
  existing-binary probe confirms `FormatDuration(-1) == "-0s"` while `FormatDuration(999) ==
  "0s"`.
- **Impact:** UI text displays a nonsensical negative-zero duration and equivalent sub-second
  magnitudes format asymmetrically.
- **Likely repair point:** apply the sign only when the displayed whole-second magnitude is
  nonzero, or include a millisecond component instead of discarding it.

### VDOC-228 — TimeZone.Find loses its class type in Zia

- **Classification:** confirmed registry/type-system integration defect
- **Area:** `Viper.Time.TimeZone.Find` and instance members
- **Evidence:** the registry declares `Find` as `obj(str)`, not
  `obj<Viper.Time.TimeZone>(str)`. Existing-compiler checks reject both instance chaining on the
  inferred `Any` and assignment to a `Viper.Time.TimeZone` local. The explicit-receiver forms
  `TimeZone.get_Name(zone)`, `OffsetAt(zone, ts)`, and `IsDstAt(zone, ts)` compile and run.
- **Impact:** the documented instance API is not directly usable from the natural Zia lookup
  expression; completion/type checking loses the class immediately after a successful find.
- **Likely repair point:** give `Find` its concrete object return type in the registry and add Zia
  chaining/typed-assignment coverage, with the required compatibility review for signature
  metadata changes.

### VDOC-229 — Time instance APIs accept and reinterpret wrong-class objects

- **Classification:** confirmed runtime type-safety defect
- **Area:** Countdown, Stopwatch, DateOnly, and DateRange explicit-receiver calls
- **Evidence:** these objects are allocated with class ID `0`, and their public methods either only
  null-check or directly cast any pointer to the expected payload structure. A check-only Zia
  probe is accepted with a `Seq` passed to `Countdown.Start`, `Stopwatch.Start`,
  `DateOnly.ToDays`, and `DateRange.Duration` through the static compatibility forms.
- **Impact:** a wrong explicit receiver can read unrelated fields, write over another object's
  payload, or access beyond its allocation, leading to heap corruption rather than a type error.
- **Likely repair point:** assign stable runtime class IDs, validate kind/class/size in shared
  receiver guards, and make the front end reject explicit receivers incompatible with the owning
  class.

### VDOC-230 — Wall-clock failure aliases the Unix timestamp -1

- **Classification:** confirmed error-sentinel ambiguity
- **Area:** `DateTime.Now`, `DateOnly.Today`, and RelativeTime's implicit current-time helpers
- **Evidence:** these paths call `time(NULL)` and treat its return as an ordinary timestamp without
  checking `(time_t)-1`, the C API's failure sentinel. `DateTime.Now` exposes it directly;
  `DateOnly.Today` can convert it into a plausible local date; RelativeTime compares against it.
- **Impact:** a wall-clock query failure can masquerade as a valid 1969 timestamp and produce
  plausible but incorrect dates or enormous relative-time strings.
- **Likely repair point:** centralize a checked wall-clock query with an Option/Result error path;
  avoid sentinel-returning APIs for instants whose valid domain includes negative values.

### VDOC-231 — DateOnly accepts years its serializer cannot round-trip

- **Classification:** confirmed representation/parser mismatch
- **Area:** `DateOnly.Create`, `ToString`, `Format`, and `Parse`
- **Evidence:** `Create` accepts any signed 64-bit year, while `Parse` requires exactly four ASCII
  digits and ten total bytes. `ToString` uses minimum rather than exact year width. Existing-binary
  probes produce `-001-01-01` for year `-1` and `10000-01-01` for year `10000`; parsing the latter
  returns null.
- **Impact:** the nominal canonical date string is not a serialization format for the full
  constructible domain, and persisted dates can fail to load without warning.
- **Likely repair point:** define and enforce a year domain shared by construction/parsing, or
  implement signed/expanded ISO 8601 years in both parser and formatter.

### VDOC-232 — Time runtime metadata hides nullability, traps, and ownership

- **Classification:** confirmed machine-readable registry metadata inconsistency
- **Area:** `viper --dump-runtime-api` for `Viper.Time.*`
- **Evidence:** checked DateTime/Duration arithmetic, Stopwatch/Countdown receivers, and range
  subtraction are marked `fallibility=infallible`; `DateTime.ParseISO`/`ParseDate` are marked as
  trapping although they return numeric sentinels. `DateOnly.Create`, `Today`, and `Parse`, plus
  `DateRange.Intersection`/`Union`, are marked non-null despite ordinary null returns. Newly
  allocated DateOnly/range/Stopwatch results often have `ownership=unknown`, while TimeZone's
  process-lifetime static handle is also unknown.
- **Impact:** tools using the live agent-facing catalog will omit necessary null/error branches,
  invent trap handling for sentinel parsers, and cannot plan correct ownership.
- **Likely repair point:** annotate Time contracts explicitly, use concrete nullable object types,
  distinguish static/borrowed zone handles from owned objects, and assert these known cases in the
  metadata audit.

### VDOC-233 — The public-surface cleanup removed Parse.TryNum without updating its consumers

- **Classification:** confirmed cross-layer registry inconsistency
- **Area:** runtime registry, surface policy, front ends, tooling, and tests
- **Evidence:** the current registry no longer declares `Viper.Core.Parse.TryNum`, but
  `RuntimeSurfacePolicy.inc` still requires both the function and the class method. A direct
  source-registry audit therefore fails with two errors. Zia and BASIC compatibility rewrites,
  diagnostic/explanation code, ownership/signature lookups, method-index tests, runtime tests,
  fixtures, and golden expectations also still name the removed target.
- **Impact:** the repository's runtime-surface audit is red, and a regenerated compiler can retain
  compatibility logic and test expectations for a symbol absent from its authoritative catalog.
- **Likely repair point:** either restore `TryNum` as a reviewed compatibility alias or remove it
  coherently from policy, front ends, tooling, tests, fixtures, goldens, and migration material.

### VDOC-234 — Removed public runtime helpers remain unclassified at the C boundary

- **Classification:** confirmed runtime-audit classification gap
- **Area:** C runtime declarations left behind by the concurrent public-registry cleanup
- **Evidence:** direct `rtgen --audit` reports 193 unclassified findings against 8,176 header
  declarations after the registry fell to 7,132 public functions. The warnings include helpers
  whose public rows were removed, such as `rt_datetime_try_parse`, Exec compatibility accessors,
  and PTY error accessors, without corresponding internal/compatibility classifications.
- **Impact:** the audit cannot distinguish deliberate internal ABI retention from forgotten public
  exposure or dead declarations, so future drift is hidden in a large warning baseline.
- **Likely repair point:** classify every retained C helper explicitly as internal/compatibility,
  or remove its declaration and implementation when ABI retention is not intended; keep the audit
  warning baseline at zero.

### VDOC-235 — Generated runtime documentation is stale after the public-surface cleanup

- **Classification:** confirmed generated-documentation inconsistency
- **Area:** `docs/generated/runtime/`
- **Evidence:** direct `rtgen --docs --check` currently reports 25 component pages plus the
  generated README as stale, including every reviewed runtime component affected by removed rows.
  The source registry itself parses and validates, so this is generated-output drift rather than
  a parser failure.
- **Impact:** readers and tooling see APIs that the authoritative source registry no longer
  exposes; previously reviewed pages can become stale again while the cleanup is in flight.
- **Likely repair point:** finish the coordinated registry cleanup, regenerate the complete runtime
  reference in one reviewed change, and keep `rtgen --docs --check` as a required zero-drift gate.

### VDOC-236 — Config queries leak retained JSON values

- **Classification:** confirmed runtime ownership bug
- **Area:** `Viper.Game.Config.GetInt`, `GetStr`, `GetBool`, and `Has`
- **Evidence:** each method calls `rt_jsonpath_get` as an existence check. That helper explicitly
  retains the resolved value, but none of the four Config paths releases the retained result.
  Three of them then perform a second lookup for conversion, so every successful query leaks at
  least one reference into the parsed JSON tree.
- **Impact:** repeatedly reading ordinary configuration keys grows retained object/string state for
  the lifetime of the process even after the Config itself becomes unreachable.
- **Likely repair point:** use one owned lookup and release it after conversion, or use the existing
  non-leaking `rt_jsonpath_has`/typed helper paths with an explicit ownership contract and tests.

### VDOC-237 — Legacy Game registry signatures erase concrete return types

- **Classification:** confirmed registry/type-system integration defect
- **Area:** function-only `Viper.Game.Entity`, `Behavior`, `Config`, `SceneManager`, Raycast, and
  `Viper.Game2D.LevelDocument` surfaces
- **Evidence:** constructors/loaders return unqualified `obj`; `Config.GetStr` and
  `SceneManager.get_Current`/`get_Previous` also use `obj` even though the C functions return
  runtime strings. Constructor-provenance lets current Zia accept many chained calls, but an
  existing-compiler probe rejects passing `get_Current` to a String parameter because its type is
  `Any`. These namespaces also have no generated `RT_CLASS` entries.
- **Impact:** class identity, string typing, completion, nullability, and ownership disappear at
  API boundaries; otherwise valid results cannot be consumed by typed Zia code naturally.
- **Likely repair point:** add reviewed class definitions and concrete return descriptors
  (`str`, nullable `obj<Viper.Game...>`, and typed Tilemap results), then cover direct assignment,
  chaining, and completion in both front ends.

### VDOC-238 — LevelDocument's nullable loader traps on ordinary file errors

- **Classification:** confirmed failure-contract inconsistency
- **Area:** `Viper.Game2D.LevelDocument.Load`
- **Evidence:** the loader is documented and registered as a nullable object factory, but it calls
  `rt_io_file_read_all_text` without an existence/error preflight. Missing paths, permission
  failures, non-regular files, short reads, and close failures trap before `Load` can return null.
  `Viper.Game.Config.Load`, by contrast, explicitly converts a missing file into null.
- **Impact:** callers cannot implement the documented null fallback for the most common load
  failure, and closely related game JSON loaders have incompatible error behavior.
- **Likely repair point:** provide a Result-based load API and make the nullable compatibility path
  consistently soft-fail, or document/register it as trapping and expose the I/O error.

### VDOC-239 — LevelDocument silently collapses layers and truncates metadata

- **Classification:** confirmed data-model inconsistency / needs triage
- **Area:** `Viper.Game2D.LevelDocument` JSON import
- **Evidence:** every `type == "tiles"` layer writes into the same base Tilemap; layer names are
  ignored and later data, including zero tiles, overwrites earlier data. Object `type`/`id` and the
  theme are silently truncated to 31 bytes, potentially at a UTF-8 continuation byte, and only the
  first 512 object entries are retained. The implementation comment still says 256 objects.
- **Impact:** a nominally layered level can lose earlier visual/collision data, long identifiers can
  alias or become invalid UTF-8, and large levels are accepted with silent object loss.
- **Likely repair point:** either preserve named layers in the returned model or explicitly accept
  one layer; reject/report overlong metadata and object overflow rather than silently truncating.

### VDOC-240 — Behavior.AddSineFloat documents units the implementation does not use

- **Classification:** confirmed API/implementation contract inconsistency
- **Area:** `Viper.Game.Behavior.AddSineFloat`
- **Evidence:** the header calls `amplitude` pixels and `speed` degrees per second. Update actually
  assigns `sin(phase) * amplitude` directly to Entity vertical velocity (centipixels per 16 ms base
  frame), while phase advances by `speed * dt / 16` centidegrees. It does not offset position by a
  pixel amplitude and the speed is not degrees/second.
- **Impact:** values chosen from the public contract produce motion roughly two orders of magnitude
  off and make hover behavior frame-unit dependent in a non-obvious way.
- **Likely repair point:** define physical units and either integrate a positional sine offset or
  rename the parameters as velocity/centidegree increments; align headers, registry docs, and
  regression tests.

### VDOC-241 — Entity.OnGround is cleared on the first stationary frame after landing

- **Classification:** confirmed game-physics state bug
- **Area:** `Viper.Game.Entity.MoveAndCollide`
- **Evidence:** every call clears all collision flags, but vertical collision is checked only when
  the computed Y displacement is nonzero. An existing-binary probe lands an Entity on a solid tile
  (`OnGround == true`), calls `MoveAndCollide` again after vertical velocity was zeroed, and gets
  `OnGround == false` although the entity has not moved.
- **Impact:** grounded movement, jumping, edge reversal, and animations can alternate or fail when
  gravity is zero/small or displacement truncates to zero.
- **Likely repair point:** perform a stable contact probe after movement (including zero
  displacement), or distinguish one-frame collision events from persistent contact properties.

### VDOC-242 — A ray on the maximum map boundary samples the last in-bounds tile

- **Classification:** confirmed raycast boundary bug
- **Area:** `Viper.Game.Raycast.HasLineOfSight` / internal tilemap DDA
- **Evidence:** clipping treats `x == mapWidth` and `y == mapHeight` as inside, then clamps those
  coordinates to `mapWidth - 1`/`mapHeight - 1`. With a solid one-tile map, an existing-binary
  probe reports a vertical segment at `x == mapWidth` blocked, while the same segment one pixel
  farther outside is clear.
- **Impact:** rays immediately outside the right or bottom edge collide with boundary tiles and can
  break visibility or projectile logic asymmetrically; left/top use different half-open behavior.
- **Likely repair point:** clip against the map's half-open extent and reject segments that only
  touch the excluded maximum boundary; add tests for all four edges and corner tangencies.

### VDOC-243 — SceneManager silently aliases long names and drops excess scenes

- **Classification:** confirmed bounded-registry API inconsistency
- **Area:** `Viper.Game.SceneManager.Add` and name-based switching
- **Evidence:** the manager stores at most 64 names in fixed 128-byte buffers. `Add` silently
  ignores entries after the cap and truncates every name to 127 bytes before duplicate detection,
  so distinct long names with the same prefix alias. No result reports either condition.
- **Impact:** legitimate scenes can be absent or switch to an unintended prefix-colliding scene,
  with no diagnostic available to the caller.
- **Likely repair point:** return a Result/boolean status, reject overlong names, and expose or
  remove the fixed cap; keep names as owned runtime strings if practical.

### VDOC-244 — BASIC can assert while recovering from NEXT used as an identifier

- **Classification:** confirmed compiler crash
- **Area:** BASIC parser/control-flow checking
- **Evidence:** the Game of Life documentation example declared an object named `next`, then used
  `next.Set(...)` inside nested `FOR` loops. BASIC is case-insensitive and parses `next` at statement
  start as the `NEXT` keyword. Rather than only diagnosing the reserved-name/mismatched-loop input,
  the existing compiler aborts in `ControlCheckContext` with `FOR stack unbalanced by control-flow
  check`. A reduced one-loop form diagnoses the syntax, while the nested example exits via
  assertion.
- **Impact:** ordinary invalid source can terminate the compiler process instead of returning
  structured diagnostics; editor/server callers can be taken down by one document buffer.
- **Likely repair point:** reject keywords as identifiers at declaration, make control-stack
  recovery exception-safe, and add the reduced/nested cases as non-crashing parser tests.

### VDOC-245 — Config defaults are ignored when an existing value has the wrong type

- **Classification:** confirmed typed-getter contract inconsistency
- **Area:** `Viper.Game.Config.GetInt`, `GetBool`, and `GetStr`
- **Evidence:** the methods use the caller's default only when the path is absent. Once a value
  exists, conversion helpers return `0`, false, or an empty string for unsupported/unparseable
  input, and that conversion result replaces the supplied default. `GetStr` additionally has the
  erased `obj` return described in VDOC-237.
- **Impact:** malformed configuration silently changes behavior to zero/false/empty instead of the
  safe defaults callers requested.
- **Likely repair point:** make typed conversion report success and return the supplied default on
  both absence and type/parse failure, or rename/document the current coercing semantics.

### VDOC-246 — `SaveData.Save` traps on some failures despite its Boolean contract

- **Classification:** confirmed failure-contract inconsistency
- **Area:** `Viper.IO.SaveData.Save`
- **Evidence:** the method and adjacent source comment promise `false` on failure, but parent
  creation delegates to trapping `Dir.MakeAll`, and failure to obtain secure randomness for the
  temporary filename calls `rt_trap` before returning zero. Permission, disk, path-component, and
  entropy failures therefore do not all reach the advertised Boolean boundary.
- **Impact:** game code that branches on `if save.Save()` can still terminate on ordinary
  operational failures and cannot present its own recovery UI consistently.
- **Likely repair point:** use non-trapping directory/entropy helpers internally and return false
  with an inspectable error, or change the surface to a Result and reserve traps for invalid
  programming inputs.

### VDOC-247 — Quest IDs accept and alias embedded-NUL suffixes

- **Classification:** confirmed string-validation / persistence bug
- **Area:** `Viper.Game.Quests` registration and lookup
- **Evidence:** `quests_valid_id()` validates `strlen(rt_string_cstr(id))`, so a valid prefix before
  an embedded NUL is accepted while all bytes after it escape the `[A-Za-z0-9._-]` and 64-byte
  checks. Lookups use `strcmp` and the save formatter uses `%s`, making distinct runtime strings
  with the same pre-NUL prefix alias and serialize identically.
- **Impact:** dynamically supplied IDs can overwrite or address the wrong quest/stage/objective,
  and persistence cannot round-trip the registered identity.
- **Likely repair point:** validate the runtime string's explicit byte length, reject every NUL,
  and compare/serialize length-aware IDs.

### VDOC-248 — Quest mutation methods do not enforce objective kind

- **Classification:** confirmed state-machine API bug
- **Area:** `Viper.Game.Quests.SetFlag` and `Progress`
- **Evidence:** objectives store `is_counter`, but neither mutation checks it. `SetFlag` assigns the
  target to a counter and completes it; `Progress` increments a flag and can complete it. Both then
  emit normal completion/advancement events.
- **Impact:** a caller mistake silently changes canonical quest state instead of returning false,
  making content wiring errors hard to diagnose and potentially skipping gameplay requirements.
- **Likely repair point:** reject the wrong mutation for each objective kind and add flag/counter
  cross-call tests.

### VDOC-249 — Quest progress can overflow before it is clamped

- **Classification:** confirmed signed-overflow bug
- **Area:** `Viper.Game.Quests.Progress`
- **Evidence:** the implementation performs `objective->progress += amount` on signed `int64_t`
  before comparing with and clamping to the target. A sufficiently large positive increment can
  overflow, which is undefined behavior in C and may become negative rather than completing.
- **Impact:** untrusted or accumulated progress values can corrupt state, suppress completion, or
  be miscompiled under optimization.
- **Likely repair point:** saturate by comparing `amount` with `target - progress` before addition,
  without evaluating an overflowing sum.

### VDOC-250 — Legal maximum-size quests cannot always be serialized

- **Classification:** confirmed persistence-budget inconsistency
- **Area:** `Viper.Game.Quests.Save`
- **Evidence:** the public registration budgets allow 16 stages with 8 objectives each and IDs up
  to 64 characters, but serialization builds each entire quest record in one fixed `char[512]`.
  Progress for only a small fraction of a legal maximum quest exhausts that buffer, at which point
  `Save` returns false.
- **Impact:** content accepted by registration can become unsaveable only after objectives gain
  progress, risking lost campaigns with no diagnostic explaining the limit.
- **Likely repair point:** append directly to the dynamically grown output, check arithmetic, and
  test the full documented budget with maximum-length IDs and progress values.

### VDOC-251 — Quest persistence leaks temporary runtime strings

- **Classification:** confirmed runtime ownership bug
- **Area:** `Viper.Game.Quests.Save` and `Load`
- **Evidence:** `Save` constructs owned runtime strings for the fixed key and serialized value,
  passes them to a setter that retains them, and never releases the temporaries. `Load` likewise
  constructs a key/default and receives the freshly retained result of `SaveData.GetString`, but
  never unreferences those handles on success or any early return.
- **Impact:** every save/load cycle permanently grows string allocations; autosave or repeated
  profile loading amplifies the leak over a long game session.
- **Likely repair point:** bind every temporary handle to a local with balanced cleanup on all
  exits, or add scoped ownership helpers for runtime C code.

### VDOC-252 — Quest loading weakly parses and partially applies malformed state

- **Classification:** confirmed persistence-validation bug
- **Area:** `Viper.Game.Quests.Load`
- **Evidence:** the loader splits and mutates registered quests in place, parses numeric fields
  with `atoll` without end-pointer or overflow validation, ignores malformed/unknown fields, and
  returns true for almost any nonempty blob. It has no transaction or rollback, so later bad fields
  do not undo earlier state changes.
- **Impact:** corrupt or externally edited saves can be reported as loaded while leaving a hybrid
  of old, default, and partially decoded state.
- **Likely repair point:** use a versioned grammar with strict integer parsing, validate into a
  temporary state, and commit only after the complete blob succeeds.

### VDOC-253 — Quest hot re-registration can leave a completed stage stuck active

- **Classification:** confirmed state-migration inconsistency
- **Area:** `Viper.Game.Quests.AddFlag` / `AddCounter` on existing objectives
- **Evidence:** re-registration changes objective kind and target but retains progress and does not
  run advancement. If the new target is at or below retained progress, later `SetFlag`/`Progress`
  returns false because the objective is already done and also skips `quests_check_advance`.
- **Impact:** the documented idempotent data-refresh pattern can strand an active quest on a stage
  whose every objective is already complete.
- **Likely repair point:** define an explicit migration policy, normalize retained progress, and
  recompute stage/quest state after registration changes (or prohibit structural re-registration).

### VDOC-254 — Clearing scene diagnostics also erases schema invalidity

- **Classification:** confirmed editor-document state bug
- **Area:** `Viper.Game2D.SceneDocument.ClearDiagnostics`
- **Evidence:** `ClearDiagnostics` clears messages and unconditionally sets `SceneState.valid =
  true`. Compatibility loads normalize bad dimensions, missing fields, tile-count errors, and
  other invalid input into a document; clearing their diagnostics consequently makes
  `HasErrors()` false without repairing or revalidating that data.
- **Impact:** an editor can acknowledge messages and then mistake an invalid/normalized scene for
  a successfully validated source document.
- **Likely repair point:** separate immutable/current validation state from the diagnostic queue,
  or revalidate before validity can become true.

### VDOC-255 — Scene Result loaders can return warning text as the error

- **Classification:** confirmed diagnostic-selection bug
- **Area:** `SceneDocument.LoadJsonResult` and `LoadResult`
- **Evidence:** each Result adapter checks for any error but obtains its message from the internal
  `lastError`, which is simply the newest diagnostic of any severity. Parsing continues after many
  schema errors, and a later unknown-field warning therefore replaces the message selected for
  `Err`.
- **Impact:** callers receive an error Result whose text describes a non-fatal dropped field while
  the actual unsupported version, dimensions, or schema error is hidden with the discarded
  document.
- **Likely repair point:** select the first or most relevant error-severity record, or return the
  full diagnostic collection in a typed error payload.

### VDOC-256 — SceneDocument construction dereferences a failed handle allocation

- **Classification:** confirmed allocation / recoverable-trap bug
- **Area:** `Viper.Game2D.SceneDocument` construction and load paths
- **Evidence:** `handleFromState()` calls `rt_obj_new_i64` and immediately writes `h->state`
  without checking `h`. If the runtime allocation trap hook returns null, this becomes a null
  dereference. If the subsequent C++ `new SceneState` throws, the exception barrier returns null
  but the already allocated runtime handle is not finalized or released.
- **Impact:** allocation pressure can turn a recoverable runtime trap into a native crash or leak
  an incomplete GC object.
- **Likely repair point:** check the handle, allocate state under RAII before publishing ownership,
  and release the handle on every exception path.

### VDOC-257 — GameBase permits zero and negative delta times through `setDTMax`

- **Classification:** confirmed example-framework timing bug
- **Area:** `examples/games/lib/gamebase.zia`
- **Evidence:** the loop first raises `dt` to 1 and then applies `if dt > dtMax { dt = dtMax; }`.
  `setDTMax` accepts any integer, so zero or a negative value replaces the lower-clamped delta on
  every frame.
- **Impact:** physics, timers, animation, and ScreenFX can freeze or run backward even though the
  framework advertises clamped positive millisecond deltas.
- **Likely repair point:** clamp/reject nonpositive maxima in the setter and order the bounds as
  one validated interval.

### VDOC-258 — GameBase renders one more frame after a window-close request

- **Classification:** confirmed example-framework lifecycle inconsistency
- **Area:** `examples/games/lib/gamebase.zia`
- **Evidence:** after `Canvas.Poll`, a true `ShouldClose` only assigns `running = false`; execution
  continues through delta/effect updates, scene lifecycle callbacks, drawing, `onFrame`, and
  `Canvas.Flip` before the loop condition is checked again.
- **Impact:** game code performs callbacks and presentation after close was observed, which can
  trigger unwanted state mutation or touch a window/backend already entering teardown.
- **Likely repair point:** break immediately after close detection, or explicitly define and test
  a close-request finalization callback that does not present another frame.

### VDOC-259 — Long DebugOverlay watch names duplicate and cannot be removed normally

- **Classification:** confirmed bounded-name / lookup bug
- **Area:** `Viper.Game.DebugOverlay.Watch` and `Unwatch`
- **Evidence:** a new watch name is copied into a 32-byte C buffer and silently truncated to 31
  bytes, but lookup before insertion compares the caller's full NUL-terminated text with stored
  names. Repeating the same long name therefore misses the truncated entry and consumes another
  slot; `Unwatch` with the original name also misses. Byte truncation can split UTF-8, and only 28
  stored bytes are rendered.
- **Impact:** updating one long metric can exhaust all 16 watch slots, after which new watches are
  silently ignored, while the original entry cannot be addressed with its source name.
- **Likely repair point:** reject or UTF-8-safely normalize names before both lookup and storage,
  use the same canonical representation for `Watch`/`Unwatch`, and return insertion status.

### VDOC-260 — Pathfinder's registry cleanup contradicts its ADRs and remaining consumers

- **Classification:** confirmed cross-layer runtime-surface inconsistency
- **Area:** `Viper.Game.Pathfinder`, `PathResult`, ADRs, tests, generated docs, and built tools
- **Evidence:** the current source registry exposes result-returning operations under `FindPath`
  and `FindNearest` and removes `FindPathResult`, `FindNearestResult`, `FindPathLength`, mutable
  last-search properties, and `PathResult.Length`. ADR 0050 defines the `*Result` names, while ADR
  0062 explicitly requires keeping `Length` as a compatibility alias. Generated Game docs and
  runtime/class tests still name the removed rows, and the existing compiler binary still reflects
  the older method catalog. No superseding ADR documents this breaking surface change.
- **Impact:** source, architectural decisions, tests, documentation, and available binaries expose
  incompatible APIs; a rebuild would break callers and redline contract tests.
- **Likely repair point:** pause the deletion, decide the canonical migration through a superseding
  ADR, retain reviewed aliases for compatibility, and update all layers atomically.

### VDOC-261 — `Pathfinder.FromTilemap` ignores the designated collision layer

- **Classification:** confirmed game-navigation snapshot bug
- **Area:** `Viper.Game.Pathfinder.FromTilemap`
- **Evidence:** Tilemap collision queries and body resolution read
  `tilemap->layers[collision_layer]`, but the Pathfinder importer calls the base-only
  `rt_tilemap_get_tile(x, y)` for every cell. It never reads `get_CollisionLayer` or
  `GetTileLayer`.
- **Impact:** maps that correctly put collision geometry on a separate layer produce navigation
  grids from their visual base layer instead, allowing paths through walls or blocking decorative
  tiles.
- **Likely repair point:** snapshot the designated collision layer, while separately defining which
  layer supplies nearest-search values, and add a multilayer import test.

### VDOC-262 — Pathfinder factories and path payloads erase their concrete collection types

- **Classification:** confirmed registry/type-system integration defect
- **Area:** `Pathfinder.FromTilemap`, `FromGrid2D`, and `PathResult.Path`
- **Evidence:** both import factories are registered as returning bare `obj`, and the `Path`
  property is also `obj` even though runtime code returns a retained
  `Viper.Collections.List` whose entries are `Viper.Collections.Seq` objects. This differs from the
  typed `PathResult` operation returns in the current class registry. Current Zia recovers the
  factory result from its qualified owner, but the declared inventory type remains erased.
- **Impact:** tooling and frontends without owner provenance lose factory identity. For `Path`, the
  current Zia compiler rejects both assignment and an `as` cast from `Any` to
  `Viper.Collections.List`; callers must invoke low-level static List/Seq functions and explicitly
  unbox coordinates.
- **Likely repair point:** register `obj<Viper.Game.Pathfinder>` and
  `obj<Viper.Collections.List>` returns (and, when supported, a typed point representation rather
  than nested untyped objects).

### VDOC-263 — Pathfinder allocation failures masquerade as valid misses or partial successes

- **Classification:** confirmed failure-contract / result-integrity bug
- **Area:** A* and PathResult construction
- **Evidence:** node/heap allocation failure returns an empty list with `Found == false`,
  indistinguishable from an unreachable goal. More seriously, once A* reaches the goal it sets
  `last_found` before `pf_build_path`; coordinate, point, or box allocation failures can return an
  empty or shortened list, after which PathResult still reports `Found == true` and derives a
  misleading `StepCount` from that partial list.
- **Impact:** memory pressure can be interpreted as gameplay topology, or deliver a success whose
  path omits required waypoints and no longer matches its cost.
- **Likely repair point:** build the complete path transactionally, return a typed allocation error
  on failure, and set `Found` only after payload construction succeeds.

### VDOC-264 — Timer mode is recorded but never enforced

- **Classification:** confirmed timing API/state inconsistency
- **Area:** `Viper.Game.Timer` frame and millisecond modes
- **Evidence:** `Start`/`StartMs` set an `ms_mode` field, but no update or query reads it.
  `Update()` therefore adds one to a millisecond timer, while `UpdateMs(dt)` adds `dt` to a
  frame-started timer. `Elapsed` and `ElapsedMs` (likewise Remaining) are aliases over the same
  counter and provide no mode diagnostic.
- **Impact:** one wrong method call silently reinterprets units and can make a cooldown expire much
  too early or late; callers cannot inspect which update contract is active.
- **Likely repair point:** reject mismatched update/query calls or replace the dual surface with an
  explicit unit/mode property and one validated update path.

### VDOC-265 — ScreenFX removes effects before their terminal frame is observable

- **Classification:** confirmed transition/fade lifecycle bug
- **Area:** `Viper.Game.ScreenFX.Update`, `Draw`, and `TransitionProgress`
- **Evidence:** `Update(dt)` marks a slot `NONE` as soon as `elapsed >= duration`, before the type
  switch computes output and before a subsequent `Draw()` can render it. A headless existing-binary
  probe advanced a 100 ms wipe by 99 ms and then 1 ms; `TransitionProgress` printed `990` followed
  by `0`, never 1000. FadeOut and the covering transitions follow the same removal path.
- **Impact:** fade-out, wipe, circle-in, and dissolve cannot render a fully covered final frame.
  Code that waits for `IsFinished` before swapping a scene finds that the covering effect has
  already disappeared, exposing the old or new scene for a frame.
- **Likely repair point:** retain a terminal/done state through one draw (or until explicitly
  consumed), make progress 1000 observable, and separate “finished advancing” from “removed.”

### VDOC-266 — ScreenFX `CancelType` leaves canceled output cached and drawable

- **Classification:** confirmed state-consistency bug
- **Area:** selective ScreenFX cancellation
- **Evidence:** `CancelType` clears only matching slot types. It does not clear `shake_x`,
  `shake_y`, `overlay_color`, or `overlay_alpha`; those caches are reset only by the next positive
  `Update`. `Draw` renders cached overlay state regardless of whether its originating slot still
  exists. A headless probe canceled the only active flash and printed `IsActive=false`, alpha 252,
  then alpha 0 after another positive update.
- **Impact:** a supposedly canceled flash/fade can still draw, and a canceled shake can leave one
  stale camera offset, while activity queries already say the effect is gone.
- **Likely repair point:** recompute or invalidate affected caches inside `CancelType`, including
  the no-effects fast path.

### VDOC-267 — ScreenFX colors are incompatible with Viper's normal Color representation

- **Classification:** public API representation inconsistency
- **Area:** ScreenFX flash/fade colors, transition colors, and `OverlayColor`
- **Evidence:** flash/fade inputs interpret untagged `0xRRGGBBAA` with alpha in the low byte;
  transitions interpret Canvas `0x00RRGGBB`; `OverlayColor` returns `0xRRGGBB00`. In contrast,
  `Viper.Graphics.Color.RGBA` returns a tagged `0xAARRGGBB` value. A headless probe passed tagged
  opaque red to `Flash` and observed alpha 0, while raw `0xFF0000FF` produced alpha 252 after the
  same update.
- **Impact:** the canonical Color constructor produces invisible or channel-shifted ScreenFX
  overlays, and even methods on the same class require incompatible encodings. Callers must know
  which raw packing each method expects.
- **Likely repair point:** accept tagged Color values throughout and normalize internally, or add
  typed color constructors/converters that make the split impossible to miss.

### VDOC-268 — ScreenFX type constants are private while shipped audits use the wrong IDs

- **Classification:** API/tooling inconsistency
- **Area:** effect selection through `CancelType` and `IsTypeActive`
- **Evidence:** the numeric effect and wipe-direction values exist only in `rt_screenfx.h`; the
  runtime class exposes no named constants. Both shipped `examples/apiaudit/game/screenfx_demo.zia`
  and `.bas` call `IsTypeActive(0)` and `CancelType(0)` while comments claim 0 means shake. The live
  enum defines 0 as none and 1 as shake.
- **Impact:** the audit examples report misleading results, callers must copy private C enum
  values, and a future enum change can silently break source programs.
- **Likely repair point:** register stable named constants (or a typed enum), then correct the API
  audit examples to use them.

### VDOC-269 — ScreenFX “circle” and “pixelate” transitions do not implement their named effects

- **Classification:** implementation/contract mismatch; needs visual-design triage
- **Area:** `CircleIn`, `CircleOut`, and `Pixelate` rendering
- **Evidence:** both circle methods draw four rectangles around an axis-aligned square opening; no
  circular mask or disc boundary is rendered. Pixelate cannot sample the Canvas and instead draws
  increasingly spaced one-pixel black grid lines at up to alpha 128; it never creates or averages
  image blocks.
- **Impact:** callers selecting a circular iris or pixelation receive visibly different effects,
  and transition timing/coverage assumptions based on the names are unreliable.
- **Likely repair point:** implement masks/readback through a Pixels or render-target path, or
  rename the methods to describe the deliberately approximate effects.

### VDOC-270 — Lighting2D glows do not reveal the scene beneath the darkness overlay

- **Classification:** confirmed rendering-contract bug
- **Area:** `Viper.Game.Lighting2D.Draw`
- **Evidence:** Draw first calls `Canvas.BoxAlpha` for full-screen darkness, then calls
  `Canvas.DiscAlpha` for player, dynamic, and tile lights. Both primitives are source-over blends;
  there is no subtraction, destination-alpha reduction, mask, or redraw of the underlying scene.
  Source and example prose nevertheless described the discs as holes cut from darkness.
- **Impact:** scene detail remains dark inside every “light.” Colored discs add glow tint but also
  cover the already-darkened destination, so the class cannot provide the visibility lighting its
  public description promised.
- **Likely repair point:** render darkness through a mask/render target whose alpha is reduced by
  lights, or redraw the scene through light masks before compositing the darkness layer.

### VDOC-271 — A zero-radius Lighting2D player light still draws a 40-pixel glow

- **Classification:** confirmed enable/disable defect
- **Area:** `Lighting2D.SetPlayerLight` and player-light rendering
- **Evidence:** `SetPlayerLight` maps non-positive radius to zero, but Draw unconditionally renders
  an outer disc at `radius + 40` with alpha 30. The pulse also adds 0–10 pixels rather than the
  previously documented ±10 range. Dynamic and tile lights, unlike the player light, really do
  ignore non-positive radii.
- **Impact:** there is no way to disable the always-present player glow while retaining darkness
  and other lights; setting the natural sentinel radius zero still changes a 40-pixel region.
- **Likely repair point:** skip all player-light passes when base radius is zero, and expose an
  explicit enabled property if zero radius has another intended meaning.

### VDOC-272 — Effects classes expose inconsistent constructor and cleanup metadata

- **Classification:** registry/type-tooling inconsistency
- **Area:** ParticleEmitter, ScreenFX, and Lighting2D class descriptors
- **Evidence:** `ParticleEmitter.New` is typed as `obj<Viper.Game.ParticleEmitter>` and Destroy is
  an instance method. `Lighting2D.New` returns bare `obj` but Destroy is an instance method.
  `ScreenFX.New` also returns bare `obj`, while Destroy appears only as a static `void(obj)` target
  in the class inventory. Current Zia recovers owner identity and accepts both
  `fx.Destroy()` and `Viper.Game.ScreenFX.Destroy(fx)`, masking the metadata split for source code.
- **Impact:** generated references and generic tooling see three different lifecycle shapes for
  closely related instance handles, and frontends without Zia's owner-based recovery lose the two
  untyped constructor results.
- **Likely repair point:** use typed constructor returns and one consistent instance cleanup member
  convention across the three classes.

### VDOC-273 — Tween integer entry points silently lose values above binary64 precision

- **Classification:** confirmed numeric-correctness bug
- **Area:** `Viper.Game.Tween.StartI64`, `LerpI64`, and `ValueI64`
- **Evidence:** both integer entry points cast their `int64_t` endpoints to `double` before
  interpolation. An existing-binary probe started a one-frame constant tween at
  `9007199254740993` (2^53 + 1); `ValueI64` returned `9007199254740992`. The later saturating
  integer conversion cannot recover bits lost at the first cast.
- **Impact:** supposedly integer-preserving animation changes large IDs, counters, timestamps, or
  fixed-point coordinates even when `from == to`. Documentation alone cannot make `I64` callers
  expect this result.
- **Likely repair point:** interpolate integers with checked wide/integer arithmetic and a defined
  rounding rule, or restrict/rename the API to make its exact ±2^53 domain explicit.

### VDOC-274 — `AnimStateMachine.AddNamed` can overwrite a numeric state and strand its name

- **Classification:** confirmed registration/data-structure bug
- **Area:** mixed numeric and named animation-state registration
- **Evidence:** `AddNamed` chooses `id = clip_count`, calls `AddState`, then writes the name into
  `clips[id]`. If a numeric state already owns that ID at a different clip index, `AddState`
  overwrites it without increasing `clip_count`, while the name is written to a slot outside the
  searched prefix. A live probe registered numeric state 1 as the only clip, then named `walk`;
  `Play("walk")` left `CurrentState=-1`, while `SetInitial(1)` used the named clip's replacement
  frame 10.
- **Impact:** mixing the two advertised registration styles silently replaces clips and produces
  names that cannot be played. The failure depends on prior ID layout and is difficult to diagnose.
- **Likely repair point:** allocate a genuinely unused state ID, capture the actual clip index
  returned by insertion, and store the name at that index transactionally.

### VDOC-275 — Re-registering the active animation state does not reapply its clip

- **Classification:** confirmed live-state consistency bug
- **Area:** `AnimStateMachine.AddState` overwrite behavior
- **Evidence:** overwriting a state mutates the active clip structure but does not reset
  `current_frame`, timing, direction, or events through `apply_clip`. A probe advanced state 0 to
  frame 1, overwrote it with range 100–101, then updated: `CurrentFrame` became 2 and `Progress`
  remained 0, outside the newly registered clip.
- **Impact:** hot-reloading or otherwise redefining an active state can run through a long range of
  invalid frame indices and report incoherent progress before reaching the new clip.
- **Likely repair point:** either reject replacement of the active state or immediately reapply the
  replacement clip while defining the intended transition/flag semantics.

### VDOC-276 — `SetEventFrame` has no public observer

- **Classification:** public-surface orphan/incomplete deletion
- **Area:** legacy single-event support on `AnimStateMachine`
- **Evidence:** `SetEventFrame` remains registered and updates `event_frame`/`event_triggered`, but
  the only consumer, `rt_animstate_event_fired`, is absent from the current runtime registry. The
  old mutable multi-event accessors were removed too, while `AddEvent` correctly has `PollEvents`.
- **Impact:** a stable-looking public method accepts configuration that source-language callers
  can never observe. Programs can compile successfully while their event action is permanently
  unreachable.
- **Likely repair point:** remove `SetEventFrame` in the same compatibility cleanup, or restore a
  typed observer and document its consuming semantics. Prefer the batch-based event path.

### VDOC-277 — AnimTimeline tween and animation tracks are inert payload records

- **Classification:** confirmed implementation/API contract mismatch
- **Area:** `AnimTimeline.AddTweenTrack`, `AddAnimTrack`, and `TrackPayloadC`
- **Evidence:** adding a tween stores `from` in payload A, `to` in B, and literal zero in C. No
  subsequent code writes C; `Advance` only moves the playhead and fires markers. A probe advanced
  a 10→20 tween halfway and read A=10, B=20, C=0. Animation tracks likewise store only a state ID
  and never receive a state-machine handle to transition.
- **Impact:** names such as “tween track,” “animation track,” and “current interpolated value”
  imply work that never occurs. Callers who trust payload C animate everything at zero and callers
  expecting state transitions get none.
- **Likely repair point:** either make the timeline own/bind and drive concrete targets, or recast
  the API explicitly as a passive span/payload scheduler and remove payload C.

### VDOC-278 — AnimTimeline accepts markers that initial playback can never fire

- **Classification:** confirmed marker-boundary/validation defect
- **Area:** `AnimTimeline.AddMarker` and initial `Advance`
- **Evidence:** marker frames are only clamped to non-negative values; values beyond total duration
  are retained. Crossing uses the half-open interval `(before, after]`, so a marker at frame 0 is
  excluded when playback starts at frame 0. A probe added markers at 0 and 8 to a duration-5
  non-looping timeline, advanced to completion, and received an empty batch. Frame 0 may fire only
  as a side effect of a later loop wrap.
- **Impact:** apparently successful marker registrations can be permanently silent, and frame-zero
  behavior changes between the first cycle and later cycles.
- **Likely repair point:** reject markers outside the playable range and define entry semantics;
  either fire frame-zero markers on `Play`/the first positive advance or consistently exclude them
  from every cycle.

### VDOC-279 — Animation event snapshot allocation failure masquerades as no events

- **Classification:** error-signaling/allocation ambiguity
- **Area:** `Viper.Game.AnimationEventBatch` creation and `PollEvents`
- **Evidence:** the batch object is allocated first with `count=0`; integer-array overflow or
  `malloc` failure returns that valid empty object without a trap or Result. Both animation
  producers therefore satisfy their non-null batch contract while silently discarding every fired
  ID. `Ids()` has its own untyped Seq return.
- **Impact:** under memory pressure, gameplay actions keyed to animation markers disappear and are
  indistinguishable from a legitimate frame with no events.
- **Likely repair point:** allocate/copy transactionally and surface failure through Result/trap, or
  provide an explicit error state that cannot be confused with `Count == 0`.

### VDOC-280 — PathFollower discards movement remainders and can stall forever

- **Classification:** confirmed fixed-point integration bug
- **Area:** `PathFollower.Update`
- **Evidence:** each call truncates `speed * dt / 1000` to an integer with no cross-call remainder,
  then truncates `move_dist * 1000 / segment_length` to per-mille segment progress and again keeps
  no remainder. If either quotient is zero, the call consumes its movement without changing
  position. An existing-binary probe used speed 1, a 100000-unit segment, and `Update(100)`;
  repeated semantics leave `X=0` rather than accumulating motion.
- **Impact:** low speeds, small timesteps, or long segments can make an active follower permanently
  motionless. Other combinations move systematically slower because every frame's fraction is
  discarded.
- **Likely repair point:** retain distance/progress remainders at sufficient precision, or store
  traveled distance directly and derive segment position without reducing it to 1001 states.

### VDOC-281 — Degenerate or unallocated PathFollower lengths leave an active non-finishing path

- **Classification:** confirmed lifecycle/error-state bug
- **Area:** zero-length paths and segment-cache allocation failure
- **Evidence:** `Start` checks only for two points and sets `active=1`. `Update` returns immediately
  whenever the lazily computed total length is zero or the length array is null, without clearing
  active or setting finished. A probe with two identical points remained `IsActive=true` and
  `IsFinished=false` after update. Cache `malloc` failure creates the same state, clears the dirty
  flag, and is not retried until another point is added.
- **Impact:** once-mode loops waiting for completion can hang forever, and an allocation failure is
  indistinguishable from an intentional active path that simply has not moved yet.
- **Likely repair point:** reject/start-fail zero-total paths, expose a start/update error, and keep
  failed cache construction retryable or preserve the previous valid cache.

### VDOC-282 — A full ButtonGroup traps before checking whether an ID is already present

- **Classification:** confirmed capacity/duplicate-order bug
- **Area:** `ButtonGroup.Add`
- **Evidence:** the implementation checks `count >= 256` and traps before calling its duplicate
  lookup. An existing-binary probe filled the group, then called `Add(0)` for an existing ID; it
  trapped with “button limit exceeded” rather than returning false as duplicate additions do at
  lower counts.
- **Impact:** idempotent registration becomes unsafe only at exact capacity, violating the method's
  otherwise Boolean duplicate contract and making defensive re-add logic crash.
- **Likely repair point:** test for the existing ID before enforcing capacity, then reserve the trap
  or false result for a genuinely new 257th ID.

### VDOC-283 — ButtonGroup's public previous-selection name disagrees with its diagnostic

- **Classification:** low-severity naming/diagnostic inconsistency
- **Area:** `Viper.Game.ButtonGroup.SelectPrevious`
- **Evidence:** the registry exposes `SelectPrevious`, backed by the C function
  `rt_buttongroup_select_prev`, but its wrong-class trap text says `ButtonGroup.SelectPrev`.
  Generated references and callers therefore have no public member matching the diagnostic.
- **Impact:** a type error directs users and tooling to search for a method that does not exist.
- **Likely repair point:** rename the C helper if desired, but make the emitted diagnostic use the
  registered `SelectPrevious` spelling.

### VDOC-284 — SpriteSheet erases concrete and nullable results from its public metadata

- **Classification:** registry/type-contract inconsistency
- **Area:** `Viper.Graphics.SpriteSheet.New`, `GetRegion`, and `RegionNames`
- **Evidence:** the runtime returns a `Pixels` copy from `GetRegion` and a `Seq` from `RegionNames`,
  but both are registered as bare `obj`. `New` is declared non-null and typed as SpriteSheet in the
  registry even though wrong-class/null atlas input returns NULL; `GetRegion` is likewise declared
  non-null while a missing name returns NULL. Zia examples must use static class accessors to work
  around the erased result identity.
- **Impact:** generated documentation and generic tools promise values that may be null and cannot
  expose the methods of values that are present without language-specific recovery tricks.
- **Likely repair point:** register nullable concrete return types for SpriteSheet/Pixels and a
  concrete owned Seq return for names, then align constructor fallibility metadata.

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

- **Status:** corrected in `docs/viperlib/input.md`
- **Mismatch:** the guide omitted smooth wheel accessors and the `fps3d` action preset, treated
  boxed key-code sequence elements as integers, overstated mouse capture/relative-mode behavior,
  misstated KeyChord limits/timing, generalized `-1` gamepad handling to methods that reject it,
  and described InputManager debounce as held-key repeat.

### VDOC-D005 — Codec and UUID reference drift

- **Status:** corrected in `docs/viperlib/text/encoding.md`
- **Mismatch:** the UUID guide named obsolete RFC 4122 as the current specification, described
  superseded entropy APIs, did not distinguish syntax validation from version/variant validation,
  and implied all accepted UUID hex must be lowercase. The codec guide also omitted that hex
  decoding accepts either case.

### VDOC-D006 — Conversion, formatting, logging, and parsing reference drift

- **Status:** corrected in `docs/viperlib/utilities.md`
- **Mismatch:** the guide incorrectly promised exact `Fmt.Num` round trips, used shortened names
  for the public log-level properties, omitted formatter clamps and unsigned-radix behavior, and
  did not document the safe parser's accepted non-finite spellings. Context-dependent BASIC
  examples were also made independently checkable.

### VDOC-D007 — Structured-data format reference drift

- **Status:** corrected in `docs/viperlib/text/formats.md`
- **Mismatch:** the guide named nonexistent `Viper.Unbox.*` functions, gave stale `Json.TypeOf`
  result names, used Zia-only Json aliases in BASIC, treated Seq values as having `Length`, called
  a full-input JSON tokenizer incremental, and overstated CSV/TOML/XML/YAML standards conformance.
  It also misstated delimiter length, INI section counts, XML `Root`/`Parent`/`Remove` semantics,
  and the losslessness of TOML/YAML formatting. The page now separates supported behavior from the
  open implementation defects above and makes collection result types explicit in examples.

### VDOC-D008 — Formatting and generation reference drift

- **Status:** corrected in `docs/viperlib/text/formatting.md`
- **Mismatch:** the guide required boxed template strings even though raw runtime strings work,
  hid the `Template.Keys` Bag/Seq registry conflict, claimed `TextWrapper.Fill` normalizes
  whitespace, described byte widths as character/display widths, and gave a wrong `Shorten`
  result. It also omitted numeric formatter clamps and locale dependence, the Version component
  limit, HTML's actual text-node structure and untyped collection signatures, and material
  limitations in HTML entity/extraction and Markdown block/CRLF/plain-text handling.

### VDOC-D009 — Pattern-matching reference drift

- **Status:** corrected in `docs/viperlib/text/patterns.md`
- **Mismatch:** the page described byte regex operations as character-oriented, omitted the
  pattern-string NUL truncation and zero-width transformation data loss, and overstated grouping,
  character-class, and capture behavior. Scanner byte/token/trap semantics were incomplete;
  `Diff.Lines`' registry type and `Patch`'s ignored original were hidden; Like's UTF-8/locale
  behavior was overstated; and FuzzyMatch incorrectly presented `-1` as an unambiguous miss score.

### VDOC-D010 — Localization reference drift

- **Status:** corrected across `docs/viperlib/localization/`
- **Mismatch:** the guides presented the locale parser as full BCP-47 validation, described
  `FromParts` as independently validating pre-split components, and hid opaque collection return
  types. They overstated locale JSON's UTF-8 and formatter-template validation, Unicode direction
  classification, scientific-number localization, lenient grouping checks, and float plural
  operands. Message JSON was incorrectly linked to the locale-record schema; live-map ownership,
  ASCII plural interpolation, platform system-locale detection, reset behavior, and the current
  ListFormat/relative-time defects were also absent.

### VDOC-D011 — Sequential and functional collection reference drift

- **Status:** corrected in `docs/viperlib/collections/sequential.md` and `functional.md`
- **Mismatch:** List equality was documented as identity-only even though it compares boxed values,
  Deque omitted its capacity factory/alias, and Ring exposed `First`/`Last` properties as methods.
  Queue/Stack borrowing constructors, borrowed accessors, Heap/Iterator untyped `ToSeq` results,
  and tie/ownership behavior were incomplete. The Seq page incorrectly demonstrated alphabetical
  sorting of boxed strings. LazySeq omitted shared-cursor, borrowed-collector, null-sentinel, and
  negative-bound behavior; Iterator incorrectly promised Map insertion order and hid retained-cycle
  and result-typing limitations.

### VDOC-D012 — Map and set collection reference drift

- **Status:** corrected in `docs/viperlib/collections/maps-sets.md`
- **Mismatch:** Set was documented as identity-only even though it hashes and compares same-tag
  boxed scalars by value. Map omitted its boolean accessors, numeric coercion/type-error behavior,
  present-null semantics, borrowed getters, and hash-order snapshots. OrderedMap incorrectly
  promised O(1) positional access and typed its Boolean removal result as Integer. SortedSet hid
  its negative slice behavior and the registry bug affecting directly chained Seq results.
  FrozenMap/FrozenSet omitted constructor truncation, duplicate/null/type handling, snapshot order,
  and precise equality/ownership rules. TreeMap omitted borrowed-result, null-key, snapshot, and
  returned-string ownership behavior.

### VDOC-D013 — Specialized-map reference drift

- **Status:** corrected in `docs/viperlib/collections/multi-maps.md`
- **Mismatch:** BiMap omitted empty lookup results and paired hash-order snapshots. MultiMap hid
  addition order, duplicate handling, independent snapshots, and the `Any` return workaround.
  CountMap omitted non-positive increment/set behavior, tie ordering, and its untyped result trap.
  IntMap, DefaultMap, LruCache, and SparseArray omitted borrowed getters, present-null behavior,
  matching snapshot order, and lifecycle rules; DefaultMap also omitted `IsEmpty` and mislabeled
  Boolean `Remove` as Integer. WeakMap's valid-target/null/snapshot behavior was incomplete. The
  Zia LruCache example incorrectly claimed `c` was evicted after an access sequence that actually
  left `b` as LRU; the example now performs the missing `b` access.

### VDOC-D014 — Specialized collection reference drift

- **Status:** corrected in `docs/viperlib/collections/specialized.md`
- **Mismatch:** the packed-buffer table advertised a nonexistent `Count` property and omitted
  index/slice/overflow behavior. Bag omitted `Clone`, null-byte semantics, and its broken `ToSet`
  conversion. BloomFilter called its operation counter a cardinality, overstated merge-parameter
  identity, and hid duplicate/null/sanitization behavior. Trie omitted `Clone`, borrowed getters,
  null values, byte order, empty-key ambiguity, retained values, and node-memory behavior.
  UnionFind omitted `Clear` and invalid-index results. BitSet omitted differing-length semantics,
  leading-zero suppression, index behavior, and the staged-growth memory defect. Bytes called
  low-bit truncation clamping, implied UTF-8 validation, and omitted strict codec, overlap,
  bounds, write-truncation, and thread-safety rules.

### VDOC-D015 — Collections overview overgeneralized ownership, order, overflow, and thread safety

- **Status:** corrected in `docs/viperlib/collections/README.md`
- **Mismatch:** the overview did not distinguish borrowed getters from owning containers, implied
  every snapshot had the same ordering expectations, and presented overflow trapping as a
  universal collection guarantee. It also omitted the namespace distinction for LazySeq,
  unchecked I64Buffer arithmetic, the BitSet growth defect, and the fact that most collections are
  not thread-safe while some apparent reads mutate internal state.

### VDOC-D016 — Zia tooling reference omitted two classes and hid protocol limitations

- **Status:** corrected in `docs/viperlib/zia.md`
- **Mismatch:** the curated page omitted the complete public `Viper.Zia.Completion` and
  `Viper.Zia.Document` classes, used generic Object returns where the registry now exposes typed
  Map/Seq/handle results, and did not document completion/signature/hover Map schemas or the
  serialized symbol/token formats. It also hid weak-bridge result differences, the two-worker
  limit, document-mirror revision/range defects, asynchronous fix-it loss, ProjectIndex path and
  thread rules, legacy multiline snippet framing, and the runtime/imported-symbol flood.

### VDOC-D017 — Audio reference omitted public surfaces and misstated core behavior

- **Status:** corrected in `docs/viperlib/audio.md`
- **Mismatch:** the page omitted the complete Playlist class, audio capability query and backend
  counters, Sound/Music lifecycle methods, Voice metering, Music loop control, and reverb updates.
  It called oldest-started voice stealing LRU, described Synth noise with the wrong decay curve,
  used two equally low MusicGen noise notes as distinct kick/snare timbres, and incorrectly
  promised independent unrelated crossfades. It also hid exact load/decode limits, foreground
  arbitration, disabled-build behavior, group-name canonicalization, effect clamps, spatial
  failure/type conventions, ownership rules, and Playlist failure/navigation semantics.

### VDOC-D018 — Threads reference hid backend gaps and misstated lifetimes and state

- **Status:** corrected in `docs/viperlib/threads.md` and generated runtime summaries
- **Mismatch:** the page said a first Join reclaimed native resources even though POSIX threads
  detach at creation and Windows closes its handle only at wrapper finalization. It omitted
  recursive write locking, Pool backpressure and borrowed/discarded arguments, active-task
  behavior during `ShutdownNow`, Future null/error ambiguity, logical-processor sizing, reduction
  grouping, and exact cancellation/timing rules. Debouncer readiness/count and queue count were
  mislabeled, while scheduler ordering/sentinel behavior and map/queue/channel null/ownership
  semantics were absent. Most materially, Pool/Parallel/Async prose presented native behavior as
  backend-universal despite the VM limitations recorded above. Registry-source summaries also
  emitted malformed `fIFO`/`oS` and network acronym casing; their source comments and generated
  pages are corrected.

### VDOC-D019 — Network reference overstated validation and misstated timeout/protocol behavior

- **Status:** corrected in `docs/viperlib/network.md` and implementation-adjacent source comments
- **Mismatch:** the guide treated HTTP/WebSocket timeouts as overall deadlines, said TCP `Recv`
  traps on timeout, limited UDP sender metadata to `RecvFrom`, described WebSocket receive methods
  as opcode-specific and protocol failures as trapping, and implied close promptly tears down the
  transport. Static Http's JSON example actually sent `text/plain`; Download's Boolean failure
  contract and non-2xx body behavior were hidden. TLS wording overstated chain coverage and omitted
  in-tree verifier, AIA, and revocation limits. Url was called RFC 3986-compliant despite its
  permissive and internally inconsistent parser; default ports, query collapse/order, and string
  value requirements were incomplete. RestClient hid slash concatenation, per-phase timeouts,
  pool bounds, case-sensitive defaults, duplicate JSON headers, and the real JSON-helper failure
  modes. Retry/RateLimiter and TLS source comments also named nonexistent strategies/APIs,
  obsolete ownership, wall-clock timing, and a macOS Security.framework path that is no longer
  present. The second half called server shutdown graceful even though it interrupts active
  sockets, omitted exact request/worker/connection limits, overstated SNI requirements, and hid
  response/query ambiguity and route-registration corruption. Router syntax, locking, wildcard,
  slash, capture, and method semantics were incomplete. ConnectionPool ownership, fixed idle
  expiry, checked-out Clear behavior, and stale-data reuse were absent; Multipart was presented as
  broadly compliant despite fail-soft partial parsing and ambiguous results. NetUtils overstated
  timeout/primary-address guarantees. Plain and TLS WebSocket servers were documented as equivalent
  despite radically different inbound-frame handling and blocking/boundedness behavior. SSE
  incorrectly claimed Content-Length support and a whole-event timeout. HttpClient called manual
  cookies host-only and zero an unbounded timeout; SMTP promised all failures as Result and QUIT on
  Close; AsyncSocket was called non-blocking without its fixed blocking pool, result typing,
  cancellation, or lifetime defects. Implementation notes also overgeneralized NUL rejection,
  IPv6 support, and thread safety.

### VDOC-D020 — Core reference drifted from String, Parse, Object, and MessageBus behavior

- **Status:** corrected in `docs/viperlib/core.md` and implementation-adjacent source comments
- **Mismatch:** the page called `Flip` a byte reversal even though it groups UTF-8-shaped units,
  documented `Like` with glob `*`/`?` instead of SQL `%`/`_`, said LastIndexOf returned `-1` rather
  than a one-based position/zero, and described `TrimChar` as a character-set trim rather than a
  first-byte trim. It omitted byte widths, empty-needle/split behavior, String distance units,
  C-locale casing, exact Parse Boolean/numeric grammars, and Diagnostics' actual approximate-number
  rule. SplitFields was called whitespace/CSV-style without its comma/quote behavior, the Convert
  and Text.Char sections were absent from the contents, and a BASIC example used nonexistent
  `Seq.Length`. MessageBus omitted insertion order, handler-trap abort, foreign-pointer ownership,
  topic ordering, ID exhaustion, and the untyped Topics registry defect. The page also presented
  currently broken narrow/single-precision formatters without warning.

### VDOC-D021 — Crypto reference drifted from framing, TLS, randomness, and password policy

- **Status:** corrected in `docs/viperlib/crypto.md`, implementation-adjacent source comments, and
  the generated TLS class summary
- **Mismatch:** the page used nonexistent `Bytes.FromString` and `Viper.Codec.HexEncode` APIs and
  left Result-returning Cipher, AES, and TLS examples untyped. It described Cipher nonces as wholly
  random, promised unconditional legacy-format compatibility despite magic-prefix collisions, and
  omitted the legacy AES 256-byte password cap. HMAC key normalization, KDF bounds, null/empty
  semantics, approved-mode behavior, and actual random backends were incomplete. TLS was documented
  with the wrong cipher-suite set, certificate-chain limit, verification-switch behavior, string
  and receive semantics, timeout scope, and thread-safety assumptions. Password documentation hid
  the approved-mode `HashScrypt` trap and claimed a monotonic `NeedsRehash` policy that the exact-
  tuple implementation does not provide. Adjacent runtime comments repeated obsolete algorithms,
  payload layouts, entropy providers, state ownership, and API names.

### VDOC-D022 — IO reference drifted from file, path, stream, archive, watcher, and workspace behavior

- **Status:** corrected across `docs/viperlib/io/` and implementation-adjacent source comments
- **Mismatch:** the IO guides used nonexistent `Bytes.FromString`, treated Seq results as having
  `Length`, showed file EOF becoming true after an exact read, called `AppendLine` atomic, and
  omitted directory-touch and protected-directory behavior. Path documentation hid Windows
  drive-relative corruption, fixed executable-path buffers/ANSI conversion, and DataDir's NUL
  validation gap. Glob's separator promise did not disclose its post-`**` defect. Stream docs hid
  unsafe borrowed `As*` results, backing-dependent EOF timing, lazy MemStream allocation, and the
  BinaryBuffer default capacity. Archive examples called four-byte signature probes validation,
  `ReadStr`/compression helpers were described as UTF-8-validating, and `ExtractAll` was presented
  as one atomic operation. Watcher timing, path, overflow-count, queue-clear, and platform claims
  were too strong. FileIndex ignore matching/cache behavior, unknown manifest-section leakage,
  asset resolver basing, and Workspace Edit staging/backup races were also absent. Adjacent source
  comments repeated obsolete decoders, entropy APIs, ownership, capacity, Unicode, atomicity,
  watcher callbacks/handles/queue behavior, and archive/compressor capabilities.

### VDOC-D023 — Math reference drifted from live overloads, encodings, transforms, and edge behavior

- **Status:** corrected in `docs/viperlib/math.md` and implementation-adjacent source comments
- **Mismatch:** the page advertised nonexistent static `Random.NextDouble` and two-argument
  `NextInt` calls, omitted most of Vec3's live method surface, and hid non-finite vector/quaternion
  fallbacks. Easing duplicated a method row and implied uniform clamping. Spline source prose
  called a uniform fixed-cubic implementation centripetal and De Casteljau-based. Perlin omitted
  repetition, coordinate, and octave bounds. BigInt reversed byte order, said narrowing trapped,
  omitted supported radices/two's-complement semantics, and hid confirmed serialization and
  residue defects. Matrix prose mixed row-major storage with a column-major projection claim,
  called post-divide NDC clip space, gave the wrong singular-inverse/axis behavior, and omitted
  tolerance/NaN rules. Adjacent headers also misstated shift counts, PRNG constants, signed zero,
  vector operations, spline algorithms, matrix fallbacks, and source paths.

### VDOC-D024 — System reference omitted APIs and overstated cross-platform behavior

- **Status:** corrected in `docs/viperlib/system.md`, generated System/Memory documentation, and
  implementation-adjacent source comments
- **Mismatch:** the page omitted six live Environment aliases, Machine architecture/page/pointer
  properties, the entire `Viper.Memory.WeakRef` surface, and Terminal boolean/low-level methods.
  It presented runner-only argument indexing
  as universal, hid lossy command-line joining and invalid-name traps, and did not state the GUI
  clipboard's main-thread requirement. Shutdown OS-event wiring was described as backend-universal
  despite being VM-only. Exec's LastExitCode mutation rules, capture cap, stdout-only behavior,
  launch failure ambiguity, and signal normalization were incomplete. Process/PTy documentation
  hid replacement-environment semantics, executable-lookup divergence, nullable startup,
  partial writes, negative-signal ambiguity, platform availability, dimension clamps, and
  termination behavior. Machine omitted ViperDOS and several properties while calling unlike
  memory metrics equivalent. Terminal hid null EOF results, byte-oriented input, 32-bit narrowing,
  TTY/color rules, batching exceptions, and alternate-screen state defects. Adjacent headers
  additionally called synchronous Exec fire-and-forget, assigned its TLS state to RtContext,
  promised thread-safe argument initialization, named a nonexistent Machine process-id query, and
  promised terminal cleanup that is not implemented.

### VDOC-D025 — Time reference drifted from clocks, parsers, ranges, and formatting behavior

- **Status:** corrected in `docs/viperlib/time.md`, generated Time documentation, and
  implementation-adjacent registry/header comments
- **Mismatch:** Clock, Countdown, and Stopwatch were described as unconditionally monotonic and
  omitted sleep clamping/failure fallbacks; Stopwatch called non-thread-safe `Restart` atomic.
  DateTime hid wall-clock/sentinel ambiguity, exact 86,400-second day arithmetic, host-dependent
  formatting, offset/parser grammar, and DST ambiguity. Its BASIC example and host `strftime`
  table were incorrectly nested under TimeZone, while the page retained `TryParse` after the
  concurrent public-registry cleanup removed that legacy target. TimeZone omitted the untyped Zia
  handle workaround, finite transition behavior, static lifetime, and invalid-format rules.
  DateOnly hid nullable creation/parsing, its non-round-tripping year domain, English format
  grammar, and null behavior. Duration overstated normalized components and precision; DateRange
  signatures hid nullable set operations and lossy display output; RelativeTime hid truncation and
  negative zero. Adjacent headers also promised nonexistent UTC-suffix functions, manual frees for
  GC objects, always-ten-byte DateOnly serialization, restricted Duration components, a one-day
  rather than one-second range gap, and a RelativeTime format the implementation never emitted.

### VDOC-D026 — Animation reference drifted from the live surface and timing contracts

- **Status:** corrected in `docs/viperlib/game/animation.md` and implementation-adjacent source
  comments
- **Mismatch:** the page exposed removed `EventFired`, `EventsFiredCount`, and `EventFiredId`
  members; treated AnimTimeline tracks as active animation/tween drivers; and claimed SpriteSheet
  extractions shared atlas storage. It omitted concrete capacity, clamp, transition-latch,
  one-shot terminal-frame, ping-pong progress, marker-crossing, event, copy, selection, and
  fixed-point quantization behavior. PathFollower's angle was presented as a real direction angle
  rather than an eight-way approximation, ButtonGroup's change flag sounded frame-local rather
  than explicitly latched, and several cleanup/return types were absent or misleading. Three BASIC
  examples failed the documentation checker through wrong Canvas calls, unsupported operators,
  nonexistent drawing methods, and obsolete Keyboard key access. Adjacent headers additionally
  promised Tween Stop completion and lerp extrapolation, a delta-taking/FPS SpriteAnimation API,
  retained timeline names/current tween payloads, exact/no-op PathFollower ownership and motion,
  auto-clearing ButtonGroup state, and possibly shared SpriteSheet views.

## Validation Notes

- `docs/viperlib/core.md`: all 15 Zia and BASIC examples pass the documentation audit.
- `docs/viperlib/functional.md`: 5 examples pass; 2 contextual pseudocode snippets intentionally
  skip in `scripts/audit_bible_examples.py`.
- `docs/viperlib/input.md`: all 26 Zia and BASIC examples pass the documentation audit.
- `docs/viperlib/text/encoding.md`: all 4 Zia and BASIC examples pass the documentation audit.
- `docs/viperlib/utilities.md`: all 8 Zia and BASIC examples pass the documentation audit.
- `docs/viperlib/text/formats.md`: 10 examples pass the documentation audit. Its 7 heuristic skips
  also check and run successfully when the false-positive `*Result` placeholder filter is bypassed;
  the two Zia snippets additionally skipped by brace counting were checked and run as complete
  modules with the existing installed compiler.
- `docs/viperlib/text/formatting.md`: all 22 Zia and BASIC examples pass the documentation audit.
- `docs/viperlib/text/patterns.md`: all 19 Zia and BASIC examples pass the documentation audit.
- `docs/viperlib/localization/`: all 12 executable Zia and BASIC examples pass the documentation
  audit. Six JSON/configuration schema fences are intentionally skipped.
- `docs/viperlib/collections/sequential.md`: all 12 Zia and BASIC examples pass the documentation
  audit.
- `docs/viperlib/collections/functional.md`: all 6 executable Zia and BASIC examples pass the
  documentation audit. Three callback/pseudocode snippets are intentionally skipped.
- `docs/viperlib/collections/maps-sets.md`: all 14 executable examples pass the documentation
  audit.
- `docs/viperlib/collections/multi-maps.md`: all 15 executable examples pass the documentation
  audit. One contextual snippet is intentionally skipped.
- `docs/viperlib/collections/specialized.md`: all 12 Zia and BASIC examples pass the documentation
  audit.
- `docs/viperlib/zia.md`: all 4 executable Zia examples pass the documentation audit. Its one JSON
  protocol-schema fence is intentionally skipped.
- `docs/viperlib/audio.md`: all executable examples pass the documentation audit; contextual
  snippets and shell command fences are intentionally skipped.
- `docs/viperlib/threads.md`: all 22 independently runnable examples pass the documentation
  audit; 15 IL, callback, and surrounding-application-context snippets are intentionally skipped.
- `docs/viperlib/network.md`: all 35 compile-checked Zia/BASIC examples pass the documentation
  audit; 15 contextual, pseudocode, or side-effectful examples are intentionally skipped from
  execution.
- `docs/viperlib/crypto.md`: all 22 compile-checked Zia/BASIC examples pass the documentation
  audit; 10 contextual or network-side-effectful examples are intentionally skipped from
  execution. The TLS example uses a typed local for its string property as the documented
  workaround for VDOC-180.
- `docs/viperlib/io/`: all 40 compile-checked Zia/BASIC examples pass the documentation audit; 6
  non-runnable or contextual fences are intentionally skipped. IO examples are not executed by
  the auditor because they can mutate files, open watchers, or perform other external side effects.
- `docs/viperlib/math.md`: all 22 executable Zia and BASIC examples pass the documentation audit;
  2 contextual snippets are intentionally skipped. Existing-binary probes additionally confirmed
  the Random overload errors, BigInt byte/PowMod defects, quaternion inverse extremes, matrix NaN
  equality, and Mat4 singular-inverse fallback recorded above.
- `docs/viperlib/system.md`: 14 executable examples pass the documentation audit; 4 contextual
  snippets are intentionally skipped. The live API inventory was checked with the existing
  installed compiler/runtime. Separate existing-binary probes confirmed WeakRef creation,
  promotion, reset, and post-free invalidation; runner
  argument indexing and lossy joining; all six Environment aliases; Machine architecture, page,
  and pointer values; Shutdown bit masking; ShellResult status; ShellCapture (but not Shell)
  updating LastExitCode; Process stdout/stderr/exit capture, replacement environments, and lack of
  bare-name lookup; and POSIX PTY open/wait/read behavior. No build or CTest invocation was used.
- `docs/viperlib/time.md`: 17 executable Zia and BASIC examples pass the documentation audit; one
  surrounding-application Countdown snippet is intentionally skipped. Existing-binary probes in
  a fixed `TZ=UTC` environment confirmed parser case/separator/offset behavior, the `Create(-1)`
  collision, nullable DateOnly factories, non-round-tripping years, negative-zero relative
  duration text, nullable range set operations, and TimeZone explicit-receiver forms. A separate
  New York probe confirmed skipped-hour rejection and the host-selected repeated-hour occurrence.
- `docs/viperlib/game/animation.md`: all 7 Zia and BASIC examples pass the documentation audit.
  Existing-binary probes additionally confirmed Tween integer precision loss and Stop semantics,
  inert timeline payload C, unobservable initial/out-of-range markers, mixed named-state collision,
  active-clip overwrite drift, PathFollower sub-unit and zero-length stalls, and the full-group
  duplicate trap. No build or CTest invocation was used.
- Runtime registry validation at this review point reports 7,132 functions and 513 classes. A
  direct surface audit compares those rows with 8,176 header declarations. These counts moved
  during the Time review because a concurrent public-API deletion sweep is still in progress.
- `rtgen --docs --check` currently reports 25 component pages and the generated README as stale
  after that sweep. This supersedes the earlier checkpoint at which only concurrent Graphics3D
  output drifted; VDOC-235 records the new repository-wide generated-reference mismatch.
- Direct `rtgen --audit --summary-only` currently fails with two missing-`Parse.TryNum` policy
  errors and reports 193 unclassified C declarations (VDOC-233 and VDOC-234). The earlier clean
  audit checkpoint predated the concurrent deletion sweep. `scripts/lint_zia_runtime_names.sh`
  passed at its last invocation; the advisory platform-policy findings are recorded as VDOC-064.
- Review validation is intentionally limited to source inspection, documentation scripts, and
  existing binaries: Viper must not be built and CTest must not be run while concurrent work is in
  progress. One earlier invocation of `scripts/audit_runtime_surface.sh --summary-only` was made in
  error; despite its read-only-looking mode, that wrapper built its audit bundle and ran eight
  focused CTest cases. The mistake was disclosed immediately, the wrapper has not been used again,
  and no subsequent review step has built Viper or invoked CTest.
