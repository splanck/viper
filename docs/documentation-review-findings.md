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

### VDOC-016 — UUID source comments describe obsolete guarantees and entropy providers

- **Classification:** documentation/API inconsistency
- **Area:** UUID runtime implementation comments
- **Evidence:** `rt_guid.c` says Unix uses `/dev/urandom` and Windows uses `CryptGenRandom`, but
  `Uuid.New()` delegates to the shared crypto RNG (approved-mode DRBG, Windows `BCryptGenRandom`,
  Linux `getrandom()` with fallback, and macOS `arc4random_buf()`). The same comments say two
  generated UUIDs are “guaranteed” different, although UUIDv4 uniqueness is probabilistic. They
  also present RFC 4122 as current even though RFC 9562 superseded it in 2024.
- **Impact:** maintainers can infer the wrong security backend and an impossible collision-free
  guarantee from comments adjacent to the implementation.
- **Source:** `src/runtime/text/rt_guid.c`, `src/runtime/network/rt_crypto.c`, and the platform
  entropy adapters.

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

### VDOC-035 — `Toml.Format` can emit a float as integer syntax

- **Classification:** confirmed data-type loss bug
- **Area:** `Viper.Text.Toml.Format` and TOML projection through `Viper.Data.Serialize`
- **Evidence:** boxed doubles are emitted with C `%.17g` and no check that the result contains a
  decimal point or exponent. The installed evaluator formats `Box.F64(1.0)` as `value = 1` through
  the Serialize TOML facade. Under TOML, `1` is an integer token, not a float token.
- **Impact:** formatting and reparsing a supported value can silently change its numeric type;
  negative zero and other whole-valued doubles have the same issue.
- **Source:** `append_toml_value()` in `src/runtime/text/rt_toml.c` and the TOML value grammar.

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

### VDOC-077 — `PluralRules.Categories` leaks each result string's initial reference

- **Classification:** confirmed memory-management bug
- **Area:** `Viper.Localization.PluralRules.Categories`
- **Evidence:** the method passes each freshly allocated `rt_string_from_bytes()` result directly
  to `rt_list_push()`. The list retains the value, but the method never releases the temporary
  caller-owned reference. Other localization list builders explicitly balance this retain.
- **Impact:** each call leaks one string reference for every distinct category returned.
- **Source:** `rt_plural_rules_categories()` in
  `src/runtime/localization/rt_plural_rules.c`.

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

### VDOC-080 — Number-format C-locale initialization has a data race

- **Classification:** thread-safety bug / needs triage
- **Area:** `Viper.Localization.NumberFormat`
- **Evidence:** `loc_cached_c_locale()` lazily reads and writes a process-static locale pointer
  without an atomic or once primitive. Its own comment calls concurrent first use a benign race,
  but unsynchronized conflicting accesses are a C data race and therefore undefined behavior.
- **Impact:** concurrent first-time number formatting/parsing is not covered by the surrounding
  localization thread-safety claims and can also allocate/leak more than one locale object.
- **Source:** `src/runtime/localization/rt_numformat.c`.

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

### VDOC-093 — LazySeq allocation failure dereferences null

- **Classification:** confirmed allocation-path bug
- **Area:** `Viper.Functional.LazySeq`
- **Evidence:** `alloc_lazyseq()` calls `rt_obj_new_i64()` and immediately passes the result to
  `memset` before checking it. Every factory checks the returned node only after this helper has
  already dereferenced it.
- **Impact:** out-of-memory handling crashes in the allocator path instead of returning null or
  producing the runtime's normal allocation trap.
- **Source:** `alloc_lazyseq()` in `src/runtime/oop/rt_lazyseq.c`.

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

### VDOC-117 — Audio-disabled mix-group initialization and registration are unsynchronized

- **Classification:** confirmed data-race bug
- **Area:** `Viper.Sound.Audio` mix-group settings in audio-disabled builds
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

### VDOC-118 — Distinct public mix-group names silently alias after 31 bytes

- **Classification:** API inconsistency / needs triage
- **Area:** named `Viper.Sound.Audio` mix groups
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

### VDOC-119 — Playlist playback stalls permanently on an unloadable selected entry

- **Classification:** needs triage
- **Area:** `Viper.Sound.Playlist` auto-advance and failure reporting
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

### VDOC-121 — SoundBank accepts detached Sound wrappers as successful registrations

- **Classification:** confirmed API-state validation bug
- **Area:** `Viper.Sound.SoundBank.RegisterSound`
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

### VDOC-122 — Peaking EQ accepts values that poison its filter coefficients

- **Classification:** confirmed numeric-safety bug / needs triage
- **Area:** `Viper.Sound.Audio.GroupAddPeaking`
- **Evidence:** the shared biquad setup sanitizes frequency and Q but passes `gainDb` directly to
  `pow(10, gainDb / 40)`. NaN, infinity, or sufficiently extreme finite values produce non-finite
  coefficients and state. The per-sample sanitizer then converts each non-finite output to zero,
  while the poisoned delay state remains non-finite.
- **Impact:** one accepted Float argument can make every sample through that group's active
  peaking insert silent until the effect is removed or cleared. Sibling effects clamp or replace
  their non-finite public inputs; peaking gain currently requires caller-side finite/range checks.
- **Source:** `biquad_set()`, `biquad_sample()`, and `rt_audio_fx_add_peaking()` in
  `src/runtime/audio/rt_audio_fx.c`.

### VDOC-123 — Sound loaders disagree on whether a missing asset is recoverable

- **Classification:** API failure-mode inconsistency
- **Area:** `Viper.Sound.Sound.Load` and `LoadAsset`
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

### VDOC-124 — SpatialAudio3D signatures erase Sound and Vec3 argument types

- **Classification:** runtime-registry typing inconsistency
- **Area:** `Viper.Sound.SpatialAudio3D`
- **Evidence:** the registry declares `SetListener` as `void(obj,obj)`, `PlayAt` as
  `i64(obj,obj,f64,i64)`, and `UpdateVoice` as `void(i64,obj,f64)`, even though their
  implementations require Vec3 handles and `PlayAt` requires a Sound handle. Nearby
  SoundListener3D/SoundSource3D properties already use `obj<Viper.Math.Vec3>`, and ordinary Sound
  APIs use `obj<Viper.Sound.Sound>`.
- **Impact:** Zia and other registry consumers cannot reject wrong object classes at the call
  site. A bad vector reaches the checked Vec3 accessor and traps at runtime; the generated runtime
  reference likewise advertises only generic Object arguments.
- **Likely repair point:** use typed object signatures for the three object parameters in
  `src/il/runtime/defs/graphics3d/lighting.def`.

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

### VDOC-130 — Scheduler generation `-1` is both valid data and the absence sentinel

- **Classification:** API sentinel ambiguity
- **Area:** `Viper.Threads.Scheduler.ScheduleGen` / `GenerationOf`
- **Evidence:** `ScheduleGen` accepts and stores every signed 64-bit generation without
  restriction. `GenerationOf` returns the stored value, but also returns `-1` for a null name,
  null Scheduler, or an unscheduled name.
- **Impact:** after scheduling generation `-1`, callers cannot use `GenerationOf` to distinguish
  the live entry from absence. `IsDueGen(name, -1)` only helps once the entry becomes due.
- **Likely repair point:** reject/reserve `-1`, return an Option, or add a separate existence query.

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
- Runtime registry validation at this review point reports 7,384 functions, 514 classes, and
  8,176 matching header declarations.
- `rtgen --docs --check` reports only concurrent drift in generated Graphics3D documentation and
  its generated README index; it does not report stale output for the core/runtime pages reviewed
  in this batch. The prior network-review checkpoint passed with all generated output current.
- Direct `rtgen --audit --summary-only` and `scripts/lint_zia_runtime_names.sh` checks pass. The
  advisory platform-policy findings are recorded as VDOC-064.
- No build or CTest invocation is used for this review.
