---
status: active
audience: contributors
last-verified: 2026-07-02
---

# ADR 0049: Terminal Signature Nullability Cleanup

## Status

Accepted

## Context

`Zanna.Terminal.ReadLine` and `Zanna.Terminal.Ask` are compatibility APIs that
historically returned nullable strings on EOF. ADR 0028 added explicit
`Option`-returning and `Result`-returning APIs for robust application code, but
the public runtime signature text still exposed `str?` for the older calls.

The runtime API dump signature dialect documents primitive, object, pointer,
and void shapes. It does not otherwise use nullable suffixes, so `str?` made
terminal input look like it had a separate type-system feature instead of a
compatibility behavior.

## Decision

Publish the terminal compatibility APIs with ordinary string signatures:

- `Zanna.Terminal.ReadLine() -> str`
- `Zanna.Terminal.Ask(prompt: str) -> str`
- `Zanna.Terminal.InputLine() -> str`

The compatibility APIs remain available and keep their existing EOF behavior.
Runtime API metadata keeps fallibility information and migration targets:

- `ReadLine` and `InputLine` point to `Zanna.Terminal.TryReadLine`.
- `Ask` points to `Zanna.Terminal.TryAsk`.
- Documentation recommends `TryReadLine`/`TryAsk` for optional input and
  `ReadLineResult`/`AskResult` when callers need a diagnostic value.

## Consequences

- Generated API dumps use one consistent signature dialect.
- Existing source, IL, and examples that call the compatibility APIs continue
  to work.
- Tools can still discover the EOF behavior through fallibility metadata and
  migration targets.
- Docs no longer teach `str?` as a runtime signature form.
