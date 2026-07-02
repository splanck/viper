# ADR 0052: Connect, Open, and Load Result APIs

## Status

Accepted

## Context

Some runtime APIs still reported routine setup failures through NULL returns plus
side-channel diagnostics:

- `Viper.Crypto.Tls.Connect*` and `Tls.Error`
- `Viper.System.Pty.Open` and `Pty.LastError`
- `Viper.Game2D.SceneDocument.Load*` and `SceneDocument.LastError`

The existing behavior remains useful for compatibility and low-level probes, but
production code benefits from carrying success or failure as one value. Scene
documents have an additional wrinkle: the legacy load APIs intentionally return
diagnostic documents for malformed user input, so editor tooling can inspect
structured diagnostic records.

## Decision

Add Result-returning variants while preserving every existing API:

- `Tls.ConnectResult(host, port) -> Viper.Result`
- `Tls.ConnectForResult(host, port, timeoutMs) -> Viper.Result`
- `Tls.ConnectOptionsResult(...) -> Viper.Result`
- `Pty.OpenResult(program, args, cwd, env, cols, rows) -> Viper.Result`
- `SceneDocument.LoadJsonResult(text) -> Viper.Result`
- `SceneDocument.LoadResult(path) -> Viper.Result`

TLS and PTY return `Ok(handle)` for successful setup and `Err(message)` for
routine validation, platform, connection, timeout, certificate, handshake, or
startup failures.

SceneDocument Result loaders return `Ok(document)` only when the loaded document
has no retained error diagnostics. Load failures that legacy APIs represent as a
diagnostic document become `Err(lastError)`. Callers that need full diagnostic
records can continue using `Load` / `LoadJson` followed by
`DiagnosticRecords()`.

API dump migration metadata points `Tls.Error`, `Pty.LastError`, and
`SceneDocument.LastError` to the corresponding Result APIs.

## Consequences

- Application code can use composable Result handling for common setup flows.
- Existing UI/editor workflows that inspect diagnostic documents remain intact.
- Docs and examples should teach Result APIs first, with side-channel readers
  documented as compatibility diagnostics.
