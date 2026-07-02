# Failure, Errors, And Nullability

This document defines the public failure model.

## Canonical Model

| Situation | Canonical public shape |
|---|---|
| Operation can fail | `Result<T>` |
| Value may be absent normally | `Option<T>` |
| Predicate | `i1` |
| Programmer error | trap |
| Capability unavailable | `Result` unavailable error or explicit capability trap |
| Unsafe runtime mutation | `Viper.Runtime.Unsafe` |
| Debug telemetry | scoped diagnostics object |

## Traps

Traps are appropriate for:

- invalid arguments that indicate a programmer bug;
- index out of range;
- use-after-free or invalid handle;
- disabled operation in strict capability mode;
- strict convenience APIs whose name/docs promise trapping behavior.

Traps are not the canonical shape for:

- parse failure;
- file not found;
- network connection failure;
- TLS validation failure;
- asset not found;
- user cancellation;
- no search match;
- queue empty.

Decision: for every trapping `Parse`, `Load`, `Open`, `Connect`, and
`Decrypt`, provide a canonical `Result` operation and mark the trap form as
strict convenience or legacy.

## Result APIs

Canonical names should be direct and predictable:

- `ParseResult` or `Parse` returning `Result<T>`.
- `LoadResult` or `Load` returning `Result<T>`.
- `OpenResult` or `Open` returning `Result<T>`.
- `ConnectResult` or `Connect` returning `Result<T>`.
- `DecryptResult` or `Decrypt` returning `Result<T>`.

Long-term decision: once the language and docs can teach `Result` cleanly,
prefer the simple verb returning `Result<T>` as canonical. During migration,
use `*Result` names to avoid changing existing trapping behavior in place.

## Option APIs

Use `Option<T>` for normal absence:

- map lookup without default;
- find by name/id;
- queue/stack/deque pop when empty;
- pattern find when no match;
- non-blocking channel receive;
- terminal read cancellation or EOF if it is not an error.

Avoid bare `obj` or `i64` sentinel values. A `Try*` name should not return a
sentinel. It should return `Option<T>`, `Result<T>`, or `i1` for a pure probe.

## Side Channels

Current side-channel examples include:

- `Crypto.Tls.Error`;
- `System.Pty.LastError`;
- `Text.JsonStream.Error`;
- `Data.Xml.Error`;
- `Data.Yaml.Error`;
- `Data.Serialize.Error`;
- `Game2D.SceneDocument.LastError`;
- `Network.RestClient.LastStatus`, `LastResponse`, `LastOk`;
- `Graphics3D.AssetDiagnostics3D.LastLoadError`.

Decision: side channels are legacy unless they are explicitly scoped
diagnostic telemetry. They must not be the primary way to obtain failure
details.

Replacement shape:

- operation returns `Result<T, ErrorInfo>`;
- result carries status, response, diagnostic code, and message;
- async jobs expose `Result<T>` when joined or polled;
- diagnostic snapshots are immutable objects.

## Runtime Trap State

Current public APIs expose trap-state mutation through both
`Viper.Runtime.Unsafe` and `Viper.Error`.

Decision:

- `SetThrowMsg`, `ClearThrowMsg`, `SetTrapFields`, and `RaiseKind` belong only
  under `Viper.Runtime.Unsafe` or internal runtime registration.
- `Viper.Error` should expose formatting/description helpers only if they are
  useful to application code.
- `Viper.Diagnostics.CurrentTrap` should be the canonical read-only user API.

## Error Object Shape

Define a standard error object with at least:

- `Code`;
- `Message`;
- `Domain`;
- `Operation`;
- `Path` or `Endpoint` when applicable;
- `StatusCode` for network/HTTP;
- `Cause` optional nested error;
- `DiagnosticCode` when compiler/tooling related;
- `Recoverable` boolean where meaningful.

Do not encode structured errors only as strings.

## Migration Plan

1. Add catalog metadata for `fallibility`, `error_domain`, and
   `replacement`.
2. Add result-returning canonical APIs where missing.
3. Mark sentinel and side-channel APIs legacy.
4. Update examples to use `Result`/`Option`.
5. Add audits to prevent new stable sentinel or side-channel APIs.
6. Decide whether strict trap convenience names stay public or move behind
   docs/compatibility sections.

