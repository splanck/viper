---
status: active
audience: contributors
last-verified: 2026-07-02
---

# ADR 0052: Connect, Open, and Load Result APIs

## Status

Accepted

## Context

Some runtime APIs still reported routine setup failures through NULL returns plus
side-channel diagnostics:

- `Zanna.Crypto.Tls.Connect*` and `Tls.Error`
- `Zanna.System.Pty.Open` and `Pty.LastError`
- `Zanna.Game2D.SceneDocument.Load*` and `SceneDocument.LastError`

The existing behavior remains useful for compatibility and low-level probes, but
production code benefits from carrying success or failure as one value. Scene
documents have an additional wrinkle: the legacy load APIs intentionally return
diagnostic documents for malformed user input, so editor tooling can inspect
structured diagnostic records.

## Decision

Add Result-returning variants while preserving every existing API:

- `Tls.ConnectResult(host, port) -> Zanna.Result`
- `Tls.ConnectForResult(host, port, timeoutMs) -> Zanna.Result`
- `Tls.ConnectOptionsResult(...) -> Zanna.Result`
- `Pty.OpenResult(program, args, cwd, env, cols, rows) -> Zanna.Result`
- `SceneDocument.LoadJsonResult(text) -> Zanna.Result`
- `SceneDocument.LoadResult(path) -> Zanna.Result`

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
