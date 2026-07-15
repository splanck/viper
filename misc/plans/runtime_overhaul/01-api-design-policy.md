# Runtime API Design Policy

This document records the target API rules for the runtime overhaul. These are
planning decisions. Implementation slices that change public runtime surface or
runtime ABI still need the normal ADR/process gate.

## D01: `runtime.def` Is The Public API Source

Decision: `src/il/runtime/runtime.def` remains the authoritative declaration
for public runtime names, class metadata, and public signature text.

Consequences:

- Generated catalogs must not invent different public spelling.
- `--dump-runtime-api` must reflect public `runtime.def` text, not lowered C ABI
  details.
- Any public API docs should be generated or audited against this registry.

## D02: Public Machine Signatures Use One Dialect

Decision: machine-readable runtime signatures use the `runtime.def` dialect:
`void`, `i1`, `i64`, `f64`, `str`, `obj`, `obj<Viper.Type>`, and `seq<T>`.

Rules:

- `ptr` is never public.
- `string` is not emitted in machine-readable signatures.
- Human docs may render `str` as `String` and `i1` as `Boolean`, but the JSON
  catalog must stay exact and parseable.
- Generic spelling must be normalized. Prefer `seq<T>` where the API promises a
  sequence of values; use `obj<Viper.Collections.Seq>` only when the exact object
  class is semantically required.

## D03: Namespace Names Explain Ownership

Decision: namespace ownership is explicit:

- `Viper.Core`: universal object, parse, convert, option/result bridges.
- `Viper.Collections`: data structures and typed buffers.
- `Viper.Math`: scalar/vector/matrix/math utilities.
- `Viper.Text` and `Viper.Data`: text processing and data formats.
- `Viper.IO`: filesystem, streams, assets, archives.
- `Viper.Time` and `Viper.Localization`: time/calendar and locale-aware text.
- `Viper.Threads`: concurrency primitives.
- `Viper.Network` and `Viper.Crypto`: transport/protocol/security.
- `Viper.Input`: all input devices and input constants.
- `Viper.Graphics`: shared graphics primitives and immediate 2D drawing.
- `Viper.Graphics2D`: retained 2D scene/tile/rendering systems.
- `Viper.Graphics3D`: retained 3D scene/rendering/assets/physics systems.
- `Viper.Game`: game-loop and 2D gameplay helpers.
- `Viper.Game2D`: 2D game document/persistence conveniences.
- `Viper.Game3D`: high-level 3D gameplay world/entity/controller conveniences.
- `Viper.GUI`: desktop/UI widgets and IDE-oriented controls.
- `Viper.System`: process, environment, terminal, shutdown, platform services.
- `Viper.Memory` and `Viper.Runtime.Unsafe`: sharp runtime/lifetime tools only.

## D04: One Canonical Surface Per Concept

Decision: public duplicates are removed unless they are clearly different
abstraction levels.

Examples:

- Input key constants belong in one canonical input namespace, not both
  `Viper.Input.Keyboard` and `Viper.Input.Key`.
- 3D file loading should have one low-level asset model and optional high-level
  convenience wrappers, not parallel vocabularies that imply different concepts.
- Map-like mutation should not alternate between `Set` and `Put` except where
  `Put` is an HTTP verb.

## D05: Public Names Prefer Words Over Abbreviations

Decision: public names optimize for comprehension.

Rules:

- Use PascalCase for public classes, methods, properties, and function leaves.
- Expand ambiguous abbreviations: `Fpr`, `DT`, `Num`, `BoolYN`, `LeadZ`, and
  `TrailZ` are not acceptable long-term public names.
- Keep standard domain acronyms when they are the term users search for:
  `HTTP`, `TLS`, `JSON`, `SHA256`, `RGB`, `RGBA`, `AABB`, `GLTF`, `FBX`, `KTX2`.
- Do not use underscores in public leaves except generated accessor prefixes
  `get_` and `set_`.

## D06: Verbs Have Stable Meanings

Decision:

- `New`: canonical constructor only.
- `From*`: conversion from an existing representation.
- `Parse*`: parse text/data into a value.
- `Load*`: load from filesystem/asset storage.
- `Open*`: acquire a stream/session/process-like resource.
- `Connect*`: establish network/IPC connection.
- `Create*`: create external filesystem/system artifacts or calendar values
  where `New` would be misleading.
- `With*`: return a configured copy or construct with named options.
- `Get`: trap or return a required value.
- `TryGet`/`Find`: return optional value.
- `Set`: store/replace a value.
- `Add`/`Remove`: collection membership.
- `Push`/`Pop`: stack/deque sequence operations.
- `Put`: HTTP verb only, except legacy names being migrated.

## D07: `Try*` Has A Small Number Of Meanings

Decision:

- `TryEnter`, `TrySend`, `TryAcquire`, and similar operation-attempt methods may
  return `i1`.
- `TryParse`, `TryGet`, `TryRecv`, `TryPop`, and other value-producing methods
  return `Option<T>` unless failure detail matters.
- Fallible operations with useful diagnostics return `Result<T>`.
- Public failure must not be encoded as ambiguous `0`, `""`, or `NULL`.

## D08: Null Is Not A Public Success/Failure Protocol

Decision: nullable object returns are allowed only where null is a real domain
value and the docs say so. Missing value, timeout, failed parse, unavailable
capability, and lookup failure should use `Option` or `Result`.

## D09: Properties Represent State

Decision:

- A readable property should look like state, not like a command result.
- If state is assignable, expose a writable property when the language supports
  it.
- Avoid a read-only property plus `Set<Property>` method for the same concept.
- If a mutation has behavior beyond assignment, name it as a command:
  `MoveTo`, `ResizeTo`, `Configure`, `Bind`, `Apply`, etc.

## D10: Factories Should Read Like Constructors, Not C Symbols

Decision:

- Prefer `Mesh3D.Box(...)` over `Mesh3D.Box(...)`.
- Prefer `Light3D.Point(...)` over `Light3D.Point(...)`.
- Prefer `World3D.WithCamera(...)` over `World3D.WithCamera(...)`.
- Keep `FromOBJ`, `FromSTL`, `LoadKTX2`, and similar import names when the file
  format is the point.

## D11: Public Internals Move Under Unsafe/Internal

Decision:

- `Retain`, `Release`, `RetainStr`, `ReleaseStr`, boxed value-type construction,
  and similar runtime hooks are not normal user APIs.
- Compiler/runtime bridges should use `RT_INTERNAL_FUNC` or a clearly unsafe
  namespace.
- Public docs should not present unsafe hooks alongside ordinary runtime APIs.

## D12: Capability-Disabled Builds Fail Consistently

Decision:

- Public constructors/factories must not silently return null in disabled builds.
- Use capability probes for predictable branching.
- Use traps for APIs whose signature cannot represent failure yet.
- Prefer `Result`/`Option` for new fallible factories.
- Stub behavior must be tested in both strict and capability-disabled modes.

## D13: Large Classes Need Subsurfaces

Decision: classes with more than roughly 50 public members should be reviewed for
subsurface extraction.

Candidates:

- `Viper.Input.Keyboard`
- `Viper.Graphics3D.Canvas3D`
- `Viper.Input.Key`
- `Viper.GUI.CodeEditor`
- `Viper.Graphics.Canvas`
- `Viper.Game2D.SceneDocument`
- `Viper.Game3D.World3D`
- `Viper.Graphics.Pixels`

Subsurface examples: `Canvas3D.Stats`, `Canvas3D.PostFx`, `CodeEditor.Selection`,
`CodeEditor.Diagnostics`, `SceneDocument.Assets`, `Keyboard.TextInput`.

## D14: No New API Without An Audit Rule

Decision: every policy above should become either a generator invariant, a
runtime surface audit, or a documented exception list. The overhaul is not done
until regressions fail locally.

## D15: Nullable Syntax Must Be Formal Or Removed

Decision: public signatures must not use undocumented ad hoc nullable spellings.

Rules:

- `str?`, `obj?`, and similar suffixes are invalid until the runtime signature
  grammar, JSON schema, docs generator, and language bindings all define them.
- Missing text, EOF, cancellation, and failed reads should use `Option<str>` or
  `Result<str>`.
- If formal nullable types are added later, they must be represented as parsed
  type metadata, not just punctuation in an opaque string.

## D16: Primitive Values Need Domain And Unit Metadata

Decision: raw `i64` and `f64` values are acceptable only when the catalog
declares their semantic domain or unit.

Examples:

- timeout and delay parameters must declare `milliseconds`, `seconds`, or
  `Duration`.
- key codes must declare the `Input.Key` domain.
- color, blend mode, layer mask, log level, watcher event, HTTP status, and
  modal-result integers must declare typed domains.
- high-arity calls should prefer value objects or options objects when
  positional primitives are not self-evident.

## D17: Last-Error State Is Not A Primary Contract

Decision: public fallible operations should return explicit results.

Rules:

- `LastError`, `Error()`, `LastStatus`, `LastResponse`, and similar side-channel
  state may exist only as diagnostics or migration compatibility.
- New production APIs return `Result<T>`, `Option<T>`, or a domain-specific
  result object carrying value, status, diagnostics, and warnings.
- Side-channel diagnostics must document scope: process, thread, context, or
  receiver object.

## D18: Security APIs Are Safe By Default

Decision: dangerous or legacy security operations must be named and grouped so a
user cannot choose them by accident.

Rules:

- Legacy hashes such as MD5 and SHA1 do not sit beside SHA256 as equally
  recommended choices.
- Legacy encryption modes are under `Legacy` or `Unsafe`.
- Certificate-verification bypass APIs use explicit names such as
  `AllowInsecureCertificates`, not a casual boolean setter.
- Decryption/authentication failure returns data, not `NULL` plus a side
  channel.

## D19: The Runtime Catalog Declares API Contracts

Decision: `--dump-runtime-api` should become a real public contract catalog, not
only a list of names and compact signatures.

Every public row should eventually expose:

- kind: function, property, method, constructor, or enum value.
- class kind: static module, instance handle, value object, enum-like constants,
  or namespace facade.
- stability: stable, preview, experimental, legacy, unsafe, or internal.
- capability tags: graphics, audio, gui, network, tooling, filesystem, etc.
- fallibility: traps, result, option, sentinel, side-channel, or infallible.
- numeric units and enum domains.
- ownership/lifetime of returned objects.
- thread-safety and callback invocation rules.
- documentation anchor.

## D20: Async And Query APIs Return Events Or Results

Decision: asynchronous operations, event collection, searches, and queries
should return explicit event/result objects instead of mutating hidden "last"
state.

Examples:

- pathfinding returns `PathResult`, not `LastFound` plus `LastSteps`.
- quadtree queries return `QueryResult`, not `ResultCount` plus `GetResult`.
- animation polling returns `AnimationEventBatch`, not `EventsFiredCount`.
- HTTP clients return `HttpResult`, not `LastStatus` and `LastResponse`.
