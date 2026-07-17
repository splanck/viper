---
status: active
audience: contributors
last-verified: 2026-07-02
---

# ADR 0057: SMTP Send Result API

Date: 2026-07-02
Status: Accepted

## Context

`Zanna.Network.SmtpClient.Send(...)` and `SendHtml(...)` return only a Boolean.
When a send fails, callers must read `LastError` on the same client to learn the
SMTP, validation, or transport diagnostic. That side-channel pattern is easy to
lose during refactors, and it makes retry/reporting code depend on mutable client
state after the operation has already completed.

## Decision

Add Result-returning send APIs:

- `SmtpClient.SendResult(from, to, subject, body) -> Zanna.Result`
- `SmtpClient.SendHtmlResult(from, to, subject, html) -> Zanna.Result`

Successful sends return `OkI64(1)`. Failed sends return `ErrStr(message)` using
the same diagnostic that the compatibility `LastError` property exposes.

`Send`, `SendHtml`, and `LastError` remain available for compatibility. Runtime
API metadata marks them as legacy and points callers to the Result-returning
forms.

## Consequences

- New SMTP code can branch on `Result.IsOk` / `Result.IsErr` and keep the error
  message attached to the operation.
- Existing Boolean-send callers keep their current behavior.
- `LastError` remains useful for compatibility and debugging but is no longer the
  primary production error path.
