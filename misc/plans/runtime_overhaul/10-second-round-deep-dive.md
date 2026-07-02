# Second-Round Runtime API Deep Dive

Date: 2026-07-02

This document records the additional issues found after the first overhaul
planning pass. It is intentionally biased toward production-readiness: clear
names, explicit contracts, tool-friendly metadata, and examples that show the
right style.

No builds or tests were run for this pass. Evidence came from source-only review
of `src/il/runtime/runtime.def`, representative `docs/viperlib/**` pages,
example manifests, and existing sample/demo directories.

Source-only registry parsing is not a replacement for the generated runtime
dump. This pass observed approximately 6,270 public `RT_FUNC` rows, 5 internal
rows, 462 classes, 1,472 properties, and 4,298 parsed class method rows from
`runtime.def`. Those numbers differ from the earlier generated dump in
`README.md`, so the first implementation step should refresh and freeze a
generated API snapshot.

## Executive Summary

The first pass correctly identified naming drift, duplicate domains, sentinel
failure returns, large classes, and namespace ownership problems. The second
pass found several deeper API-contract issues:

- public signatures include undocumented nullable syntax (`str?`).
- many fallible APIs still rely on side-channel `Error()` or `LastError` state.
- `Viper.Error.*` exposes trap-state mutation as ordinary application API.
- raw integer domains and units are not described by the catalog.
- high-arity positional APIs make robust applications harder to read and safer
  wrappers harder to generate.
- some APIs expose mutable "last result" state instead of returning result
  objects.
- static classes, handle classes, and namespace modules are mixed without
  catalog metadata.
- security-sensitive APIs need safe defaults and stronger legacy/unsafe naming.
- examples and demos are large enough to be treated as migration clients, not
  incidental cleanup.

## Priority 0: Public Signature Grammar Hole

Observed source signatures:

- `Viper.Terminal.Ask` uses `str?(str)`.
- `Viper.Terminal.ReadLine` uses `str?()`.
- matching terminal class methods also use `str?`.

Problem: `str?` is not defined in the public signature dialect. Tooling cannot
reliably parse it or know whether it means nullable string, optional string, or
frontend-only notation.

Decision needed:

- remove suffix-nullable syntax from public signatures for the overhaul.
- use `Option<str>` when no value is a normal outcome.
- use `Result<str>` when EOF, interruption, unavailable terminal, or
  cancellation should be distinguishable.
- add a signature audit that rejects `?` suffixes until a formal nullable schema
  version exists.

## Priority 0: Error And Diagnostic Side Channels

Representative public shapes:

- `Crypto.Tls.Error(session) -> str`
- `System.Pty.LastError() -> str`
- `Text.JsonStream.Error(stream) -> str`
- `Data.Xml.Error(parser) -> str`
- `Data.Yaml.Error(parser) -> str`
- `Data.Serialize.Error(serializer) -> str`
- `Network.SmtpClient.LastError -> str`
- `Network.RestClient.LastStatus`, `LastResponse`, `LastOk`
- `Graphics3D.AssetDiagnostics3D.LastLoadError`
- `Game3D.AssetHandle3D.Error`

Problem: these APIs require callers to know that failure details live somewhere
else. In concurrent code, nested calls, async callbacks, or repeated operations,
"last" state is easy to read too late or from the wrong scope.

Production decision:

- new fallible APIs return `Result<T>` or domain result objects.
- side-channel diagnostics are allowed only as scoped debug telemetry.
- every side-channel row must document whether it is process-scoped,
  thread-scoped, context-scoped, or receiver-scoped.
- docs should teach result objects first and side-channel diagnostics only as
  compatibility or debugging.

## Priority 0: Runtime Trap Internals Are Public

Observed `Viper.Error` functions include:

- `SetThrowMsg`
- `ClearThrowMsg`
- `GetThrowMsg`
- `SetTrapFields`
- `RaiseKind`
- `GetTrapKind`
- `GetTrapCode`
- `GetTrapIp`
- `GetTrapLine`

Problem: these look like compiler/runtime implementation hooks. A normal app API
should not expose setters for global trap state.

Production decision:

- move mutation hooks to `RT_INTERNAL_FUNC` or `Viper.Runtime.Unsafe`.
- expose user-facing inspection through read-only diagnostics, for example
  `Viper.Diagnostics.CurrentTrap() -> Option<TrapInfo>`.
- do not document trap-state mutation in beginner or application docs.

## Priority 1: Raw Integer Domains Are Under-Specified

The public API has many constant-like or domain-specific integer values:

- input keys and mouse buttons.
- body shapes, alpha modes, shading models, quality levels, collision phases.
- layer masks and collision masks.
- colors, blend modes, tile IDs, row/column indexes, frame indexes.
- log levels, watcher event types, modal results, shutdown reasons.
- HTTP status codes and protocol option flags.

Problem: a signature such as `i64(i64,i64)` is not understandable without docs,
and tools cannot provide completions or validate domain mismatches.

Production decision:

- add enum/domain metadata to the runtime catalog.
- introduce typed public value objects or enum-like classes where practical.
- migrate duplicated constants such as `Game3D.Keys` into canonical input
  domains.
- keep raw `i64` only at ABI boundaries or in compatibility aliases.

## Priority 1: Units And Durations Are Inconsistent

Observed patterns:

- `Clock.Sleep(i64)` uses milliseconds.
- 2D canvas exposes both millisecond and second delta time forms.
- 3D update APIs commonly use seconds as `f64`.
- `ConnectFor`, `RecvFor`, `WaitFor`, `TryEnterFor`, and `SetTimeout` use bare
  `i64` timeouts.
- timer and duration APIs mix suffixed and unsuffixed names.

Problem: bare numeric time APIs are easy to misuse and hard to document
compactly.

Production decision:

- prefer a `Duration` value object for new public APIs.
- otherwise suffix names with units: `SleepMilliseconds`,
  `SetTimeoutMilliseconds`, `UpdateSeconds`.
- catalog metadata must declare time units for every numeric timeout/duration.
- docs should consistently use the same unit vocabulary.

## Priority 1: High-Arity Positional APIs

Representative high-arity APIs:

- `Mat4.New` with 16 positional values.
- `Mat3.New` with 9 positional values.
- tilemap auto-tile setters with 10 values.
- bone weight setters with 10 values.
- light, world/camera, TLS, PTY, mesh, drawing, and transform APIs with 6 or
  more positional parameters.

Problem: high arity makes call sites brittle. It also hides which values are
optional, which are flags, and which share a domain such as color, rectangle, or
vector.

Production decision:

- keep compact math constructors only where domain convention is strong.
- provide value-object alternatives: `Rect`, `Point`, `Size`, `Color`,
  `Transform`, `MatrixRows`, `BoneWeights`.
- use options objects for configuration-heavy APIs such as TLS, PTY, world
  creation, and asset loading.
- add an audit that warns on public APIs above an arity threshold unless
  allowlisted with a reason.

## Priority 1: Stateful Query Results

Observed shapes:

- pathfinding stores `LastFound` and `LastSteps`.
- quadtree exposes `ResultCount` plus indexed `GetResult`.
- UI tables expose `LastHeaderClick`.
- animation APIs expose event counts plus indexed event getters.
- REST clients expose last response state.
- asset diagnostics expose last load state.
- terrain and navmesh APIs expose last telemetry or path cost.

Problem: this design prevents safe composition and makes async/event-driven
programming harder.

Production decision:

- pathfinding returns `PathResult`.
- quadtree searches return `QueryResult`.
- animations return `AnimationEventBatch`.
- HTTP clients return `HttpResult`.
- asset loads return `LoadResult`.
- last-state accessors are diagnostics or compatibility only.

## Priority 1: Class Kind Is Not Explicit

Observed shapes:

- static utility classes with normal static methods.
- layout-less classes whose methods take a receiver object.
- handle-like APIs represented as `obj`.
- enum-like constant classes.
- modules that are really namespaces.

Examples requiring decision:

- `LocaleInfo` and `LocaleManager` methods that operate on locale objects.
- `AsyncSocket`, `SemanticJob`, and `ProjectIndex` handle-like classes.
- `SpatialAudio3D`, `Lighting`, `PostFX`, `Quality`, and `Debug3D` operating on
  world or canvas objects.
- `NavMesh3D` as a static class whose methods operate on a navmesh object.
- overloaded `WeakRef.Get`, `Object.IsNull`, and `Gate.Leave` variants with
  static and instance-like shapes.

Production decision:

- catalog each class as static module, instance handle, value object,
  enum-like constants, or namespace facade.
- catalog each method as static or instance.
- if a method needs a receiver handle, prefer instance class shape or explicitly
  mark the first parameter as a handle operand.
- avoid mixing static and instance semantics under the same class unless the
  schema can represent it clearly.

## Priority 1: Security And Networking Defaults

Issues:

- MD5, SHA1, HMAC-MD5, HMAC-SHA1, and CRC32 are easy to discover beside modern
  hashes.
- legacy AES-CBC helpers live under the main AES surface.
- decryption/authentication failure can return `NULL`.
- TLS verification can be disabled through a simple boolean setter.
- module-level approved-mode toggles are public.
- HTTP convenience APIs return bare strings/objects without status, headers, or
  diagnostics.

Production decision:

- group legacy algorithms under `Viper.Crypto.Legacy` or clearly unsafe names.
- make authenticated encryption the obvious path.
- return `Result<Bytes>` or `TryDecrypt -> Option<Bytes>` for decryption.
- rename verification bypass APIs to names that expose risk.
- keep simple HTTP helpers as scripting conveniences and route production docs
  to response/result objects.

## Priority 2: Random API Shape

Observed issue:

- `Viper.Math.Random.Chance` returns `i64(f64)` but reads like a boolean
  predicate and is used as a probability check in examples/docs.

Decision needed:

- change `Chance(probability)` to return `i1`, or
- rename the integer-returning form to a name that makes the numeric result
  explicit.

Docs must separate deterministic/math randomness from `Crypto.Rand`.

## Priority 2: Orphan And Tooling Namespaces

Small top-level surfaces need ownership decisions:

- `Viper.Assets`
- `Viper.Project`
- `Viper.Basic`
- `Viper.Workspace`
- portions of `Viper.Zia`

Decision needed:

- decide whether these are stable application runtime APIs or tooling APIs.
- add capability and stability metadata.
- move one-off rows into owned namespaces or mark them preview/internal.

## Priority 2: Examples And Demos Are Migration Clients

The repo contains a large sample surface, including:

- `examples/apiaudit/**`
- `examples/apps/**`
- `examples/games/**`
- `examples/3d/**`
- `examples/vbasic/**`
- `examples/sqldb-basic/**`
- `examples/localization/**`
- `examples/embedding/**`
- `examples/il/**`
- `tests/runtime/demo_*.zia`
- `misc/video/**`
- `baseball/demos/**`
- `src/lib/graphics/examples/**`

`examples/smoke_manifest.tsv` already classifies fast, full, graphical,
project, benchmark, and non-standalone examples. The overhaul should use this
manifest as the migration tracking surface.

Decision:

- every runtime API slice must include an examples/demos impact review.
- update `examples/apiaudit/**` first because it should teach canonical API
  usage.
- update large apps and games after foundational examples demonstrate the new
  names and semantics.
- add stale-name scans so demos cannot keep showing removed or legacy APIs.

## Cross-File Plan Updates

The second pass resulted in these new plan tracks:

- [11-production-contracts-and-metadata-plan.md](11-production-contracts-and-metadata-plan.md)
- [12-errors-results-and-diagnostics-plan.md](12-errors-results-and-diagnostics-plan.md)
- [13-security-and-networking-api-plan.md](13-security-and-networking-api-plan.md)
- [14-events-async-and-stateful-results-plan.md](14-events-async-and-stateful-results-plan.md)
- [15-examples-demos-migration-plan.md](15-examples-demos-migration-plan.md)
