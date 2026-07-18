---
status: active
audience: contributors
last-verified: 2026-07-04
---

# ADR 0066: Process And PTY Read Result APIs

Date: 2026-07-04
Status: Accepted

## Context

`Zanna.System.Process.ProcessHandle.ReadStdout()`,
`ReadStderr()`, and `Zanna.System.Pty.PtySession.Read()` return strings
directly. The runtime stream buffers are finite, so these legacy methods trap
when unread child output was truncated before the caller drained it.

That behavior is acceptable for small scripts because it makes data loss loud,
but it is too severe for IDE frame loops. Zanna Studio build, debug, terminal, and
Git jobs must keep the UI alive, retain bounded output, and show the user that
some stream data was dropped.

## Decision

Add non-trapping read-result methods alongside the existing string methods:

- `ProcessHandle.ReadStdoutResult() -> Zanna.Collections.Map`
- `ProcessHandle.ReadStderrResult() -> Zanna.Collections.Map`
- `PtySession.ReadResult() -> Zanna.Collections.Map`

Each map contains:

- `text: String` with the bytes available at read time.
- `truncated: Boolean` indicating whether the runtime stream buffer had already
  dropped unread bytes.

The legacy string methods keep their current trapping semantics. That preserves
compatibility and keeps accidental data loss visible for simple callers, while
long-running tools can opt into explicit truncation handling.

## Consequences

- IDE and server-style code can surface truncation markers without crashing.
- Existing source code and runtime signatures keep working unchanged.
- Runtime API inventory gains three additive methods and corresponding C ABI
  entry points.
- Callers that need lossless output still need to drain promptly or redirect to
  a file; the result API reports truncation, it does not remove finite-buffer
  limits.
