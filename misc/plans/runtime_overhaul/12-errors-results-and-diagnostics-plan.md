# Errors, Results, And Diagnostics Plan

## Goal

Make failure behavior easy to understand and safe in real applications. A user
should know from the name, return type, and catalog metadata whether an API
returns a value, returns no value, returns diagnostics, traps, or updates
diagnostic state.

## Problems

The current public surface mixes:

- `Option` for some parse helpers.
- booleans for some operation attempts.
- raw `NULL`, `0`, and `""` sentinels.
- traps for some disabled capabilities or invalid states.
- object-specific `Error()` methods.
- process or receiver "last" state.
- runtime trap-state mutation under `Viper.Error`.

This makes robust code harder because failure handling differs by namespace.

## Error Model

### Option

Use `Option<T>` when absence is normal and no extra details are needed.

Examples:

- map lookup.
- non-blocking receive with no value available.
- optional terminal input if EOF/cancel details are intentionally ignored.
- locale/message lookup with a separate `GetOr`.

### Result

Use `Result<T>` when the caller can act on failure details.

Examples:

- filesystem open/load.
- network connect/request.
- data parse.
- crypto decrypt/verify.
- language compile/check.
- asset load.
- PTY/process launch.

Required fields for a useful result object:

- `Ok` or equivalent boolean state.
- `Value` when successful.
- `Error` object when failed.
- stable error code where practical.
- human message.
- optional diagnostics or warnings.
- optional source/range/path/status metadata.

### Trap

Use traps for programmer errors and impossible states:

- invalid argument shape.
- out-of-range access in trapping APIs.
- using a destroyed handle when no recovery makes sense.
- capability-required API called after a clear capability probe says it is
  unavailable and no result-returning alternative is used.

Trapping APIs should have result/option alternatives where users naturally need
to branch.

### Side-channel diagnostics

Allowed only when:

- the operation also returns a usable result, or
- the API is explicitly legacy, or
- the diagnostic state is scoped and documented as debug telemetry.

Side-channel rows must declare scope:

- receiver-scoped.
- thread-scoped.
- context-scoped.
- process-global.

Process-global diagnostic state should be avoided for normal app code.

## Public Trap-State Cleanup

Current public trap-state functions should be reclassified:

| Current shape | Target |
|---|---|
| `Viper.Error.SetThrowMsg` | internal runtime/compiler hook. |
| `Viper.Error.ClearThrowMsg` | internal runtime/compiler hook. |
| `Viper.Error.SetTrapFields` | internal runtime/compiler hook. |
| `Viper.Error.RaiseKind` | public only if redesigned as `Error.Raise` with structured input; otherwise internal. |
| `Viper.Error.GetTrapKind/Code/Ip/Line` | read-only diagnostics object if needed. |
| `Viper.Error.KindName/Message/Location` | stable diagnostic helpers if tied to structured error values. |

Target public shape:

```text
Viper.Diagnostics.CurrentTrap() -> Option<TrapInfo>
TrapInfo.Kind -> ErrorKind
TrapInfo.Code -> str
TrapInfo.Message -> str
TrapInfo.Location -> Option<Location>
```

No ordinary public API should mutate current trap fields directly.

## Domain Result Objects

Introduce domain-specific result objects only where they add useful structure.

Recommended result types:

- `ParseResult<T>` or plain `Result<T>` with parse diagnostics.
- `HttpResult` with status, headers, body, redirects, and TLS diagnostics.
- `LoadResult<T>` with value, warnings, source path, and asset diagnostics.
- `QueryResult<T>` for spatial queries.
- `PathResult` for pathfinding.
- `ProcessResult` for command execution.
- `DecryptResult` or `Result<Bytes>` for crypto decrypt.

Avoid inventing a new result class when generic `Result<T>` is sufficient.

## Specific Migration Targets

### Terminal input

Replace `str?` with `Option<str>` or `Result<str>`.

### Data formats

Replace parser `Error()` polling with parse/read result objects.

### Assets

Replace global `AssetDiagnostics3D.LastLoadError` as the primary contract with
`SceneAsset.LoadResult` or `SceneAsset.Load -> Result<SceneAsset>`.

### Networking

Replace `RestClient.LastStatus`, `LastResponse`, and `LastOk` as primary
outcome state with returned `HttpResult`.

### PTY/process

Replace `Pty.LastError` as the primary way to diagnose open failures with
`Pty.Open(options) -> Result<Pty>`.

### Collections and concurrency

Replace nullable value-producing `Try*` APIs with `Option`.

## Documentation Rules

Docs for every fallible API must state:

- whether it returns `Option`, returns `Result`, traps, or is legacy.
- what conditions count as failure.
- whether failure consumes input or changes state.
- how to inspect diagnostics.
- whether returned objects are owned or borrowed.

Forbidden ordinary-doc wording after migration:

- "returns null on failure".
- "returns 0 on failure".
- "returns empty string when missing".
- "call Error() for details" as the only error path.
- "check LastError" as the primary error path.

## Audit Plan

Add generated checks for:

- public `str?`, `obj?`, or other undocumented nullable syntax.
- value-producing `Try*` APIs returning raw `obj`, `str`, or `i64`.
- public `Error()`/`LastError`/`LastStatus`/`LastResponse` rows not classified
  as diagnostics or legacy.
- public `Viper.Error` mutation functions outside internal/unsafe.
- docs using sentinel/side-channel wording outside migration sections.

## Migration Order

1. Add catalog fallibility metadata.
2. Add result/option replacements.
3. Update examples and docs to use replacements.
4. Move side-channel APIs to diagnostics or legacy sections.
5. Hide or remove obsolete sentinel APIs.
6. Turn warnings into failing audits.

## Acceptance Criteria

- Users can predict failure behavior from naming and return type.
- Public docs do not teach sentinels or global last-error state as the modern
  style.
- `Viper.Error` no longer exposes mutable runtime trap state as a normal API.
- Async, network, data, asset, and process APIs can be used safely in nested and
  concurrent code.
