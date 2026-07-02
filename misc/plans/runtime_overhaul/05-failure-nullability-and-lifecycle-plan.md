# Failure, Nullability, And Lifecycle Plan

## Problem

Public runtime APIs currently encode failure several different ways:

- `Option<T>` for `Core.Parse.TryInt/TryNum/TryBool`.
- `i1` for operation attempts such as `TryEnter`, `TrySend`, `TryAcquire`.
- `NULL` object for `TryPop`, `TryRecv`, `TryGet`, and some parse operations.
- `0` for `DateTime.TryParse` failure.
- `""` for `MessageBundle.TryGet` failure.
- Traps in some disabled graphics stubs.
- Silent null/no-op in other disabled graphics stubs.
- `str?` signatures on terminal input APIs without a documented nullable type
  grammar.
- `Error()`, `LastError`, `LastStatus`, `LastResponse`, and similar side-channel
  state as the only way to discover what happened.

This makes the API hard to learn and unsafe for tools.

## Decision: Three Failure Shapes

### 1. Boolean attempt

Use `i1` only when the success value is already the side effect:

```text
TryEnter() -> i1
TryAcquire() -> i1
TrySend(value) -> i1
TryJoin() -> i1
```

These names mean "attempt the operation now."

### 2. Optional value

Use `Option<T>` when the operation produces a value or no value:

```text
TryParse(text) -> Option<T>
TryGet(key) -> Option<T>
TryRecv() -> Option<T>
TryPop() -> Option<T>
TryPeek() -> Option<T>
Find(...) -> Option<T>
```

These names mean "return a value if one exists."

### 3. Result value

Use `Result<T>` when callers need an error message/code:

```text
Load(path) -> Result<T>
Open(path) -> Result<T>
Connect(host, port) -> Result<T>
Compile(source) -> Result<T>
```

Existing non-Result APIs may continue to trap until a breaking cleanup slice
changes their signature, but new fallible APIs should not use sentinel values.

## Specific Fixes

### DateTime

Current:

```text
DateTime.TryParse(str) -> i64
```

Problem: failure returns `0`, and Unix epoch is also `0`.

Target:

```text
DateTime.TryParse(str) -> Option<DateTime>
DateTime.Parse(str) -> DateTime or trap
DateTime.ParseResult(str) -> Result<DateTime>
```

If the runtime representation stays `i64`, the public type can be
`Option<i64>` initially, but docs should expose the semantic value as DateTime.

### Collections

Current nullable APIs:

```text
Queue.TryPop() -> obj
Stack.TryPop() -> obj
Heap.TryPop() -> obj
Deque.TryPopFront() -> obj
Deque.TryPopBack() -> obj
Heap.TryPeek() -> obj
```

Target:

```text
TryPop() -> Option<obj>
TryPeek() -> Option<obj>
TryPopFront() -> Option<obj>
TryPopBack() -> Option<obj>
```

Keep trapping `Pop`/`Peek` APIs for callers who expect the value to exist.

### Threads

Current nullable APIs:

```text
Channel.TryRecv() -> obj
Future.TryGet() -> obj
ConcurrentQueue.TryDequeue() -> obj
```

Target:

```text
Channel.TryRecv() -> Option<obj>
Future.TryGet() -> Option<obj>
ConcurrentQueue.TryDequeue() -> Option<obj>
```

Keep `TrySend`, `TryEnter`, and `TryAcquire` as boolean.

### Localization

Current:

```text
MessageBundle.TryGet(key) -> str
Locale.TryParse(tag) -> obj
NumberFormat.TryParseDecimal(text) -> obj
```

Target:

```text
MessageBundle.TryGet(key) -> Option<str>
MessageBundle.GetOr(key, default) -> str
Locale.TryParse(tag) -> Option<Locale>
NumberFormat.TryParseDecimal(text) -> Option<f64>
```

Avoid `""` and `NULL` as failure protocols.

### Zia/Language Services

Current stubs can return empty strings, empty sequences, or unavailable marker
objects. That is acceptable only if the result explicitly carries status.

Target:

```text
Zia.Capabilities() -> feature object
Zia.Check(source) -> Result<Diagnostics>
Zia.Compile(source) -> Result<CompileOutput>
Zia.Completions(...) -> Result<Seq<Completion>>
```

Where capability is missing, return a meaningful unavailable result rather than
an empty success.

### Terminal

Current:

```text
Terminal.Ask(prompt) -> str?
Terminal.ReadLine() -> str?
```

Problem: `str?` is not part of the public signature dialect, and terminal input
has more than one possible non-value outcome: EOF, cancellation, unavailable
terminal, interrupted input, or an actual empty line.

Target:

```text
Terminal.ReadLine() -> Option<str>
Terminal.ReadLineResult() -> Result<str>
Terminal.Ask(prompt) -> Option<str>
Terminal.AskResult(prompt) -> Result<str>
```

The exact final names can collapse to only result-returning forms if preserving
the reason for no input is important.

### Side-channel diagnostics

Current examples:

```text
Crypto.Tls.Error(session) -> str
System.Pty.LastError() -> str
Text.JsonStream.Error(stream) -> str
Data.Xml.Error(parser) -> str
Data.Yaml.Error(parser) -> str
Data.Serialize.Error(serializer) -> str
Network.SmtpClient.LastError -> str
Network.RestClient.LastStatus/LastResponse/LastOk
Graphics3D.AssetDiagnostics3D.LastLoadError
Game3D.AssetHandle3D.Error
```

Target:

```text
JsonStream.Read() -> Result<JsonEvent>
Xml.Parse(text) -> Result<XmlDocument>
Yaml.Parse(text) -> Result<YamlDocument>
Pty.Open(options) -> Result<Pty>
SceneAsset.Load(path) -> Result<SceneAsset>
RestClient.Get(request) -> Result<HttpResponse>
```

Side-channel methods can remain only when scoped and documented as diagnostics.
They must not be the normal way to obtain an error from a production API.

### Trap-state internals

`Viper.Error.SetThrowMsg`, `ClearThrowMsg`, `SetTrapFields`, `RaiseKind`,
`GetTrapKind`, `GetTrapCode`, `GetTrapIp`, and `GetTrapLine` expose runtime trap
state as ordinary public API. These belong in one of two places:

- hidden `RT_INTERNAL_FUNC` entries for compiler/runtime use, or
- a read-only diagnostics surface such as `Viper.Diagnostics.CurrentTrap()` that
  cannot mutate global trap state.

Ordinary application code should not discover setters for the runtime's current
trap fields.

## Lifecycle Policy

### Ordinary users

Ordinary public APIs should use:

- `Destroy` or `Close` for external resources and handles.
- automatic object lifetime where possible.
- `IsClosed`/`IsValid`/`IsDestroyed` for handle state where useful.

### Unsafe/internal users

`Retain`, `Release`, `RetainStr`, `ReleaseStr`, and direct boxed value-type
construction are runtime/compiler hooks.

Target:

```text
Viper.Runtime.Unsafe.Retain(...)
Viper.Runtime.Unsafe.Release(...)
Viper.Runtime.Unsafe.RetainStr(...)
Viper.Runtime.Unsafe.ReleaseStr(...)
```

or hidden `RT_INTERNAL_FUNC` entries if not intended for user code.

GC controls such as `Collect`, `SetThreshold`, and `TrackedCount` should be
reviewed separately from ordinary memory APIs. If kept public, they belong under
`Viper.Runtime.GC` or `Viper.Runtime.Diagnostics` with clear tuning semantics,
not as beginner-facing system functionality.

## Capability-Disabled Runtime Policy

Decision:

- No silent null constructors.
- No no-op mutators that look successful.
- Every capability-disabled class has either:
  - a public capability probe,
  - a trap with a clear diagnostic,
  - or a `Result`/`Option` factory.

Examples to normalize:

- 3D world/transform/path/navmesh stubs that return null.
- Canvas3D stubs that trap on construction but return default values for some
  methods.
- Zia service stubs that return empty values without making status obvious.

## Migration Strategy

1. Add optional/result-returning replacements.
2. Update docs/examples/tests.
3. Remove sentinel APIs or move them under legacy/internal names.
4. Add audits that classify every public `Try*` function by return shape.
5. Add tests for failure cases, not just successful calls.

## Audit Rules

Fail public API lint when:

- `TryParse`, `TryGet`, `TryRecv`, `TryPop`, `TryPeek`, or `Find` returns raw
  `obj`, `str`, or `i64`.
- `Try*` value-returning APIs return ambiguous sentinel values.
- public signatures contain undocumented nullable syntax such as `str?`.
- primary error detail is available only through `Error()` or `LastError`.
- docs mention "returns null on failure", "returns 0 on failure", or "returns
  empty string when missing" for public APIs not explicitly grandfathered.
- disabled stubs silently return null from public constructors.
