---
status: active
audience: contributors
last-verified: 2026-07-02
---

# ADR 0051: Data Format Result Parse APIs

## Status

Accepted

## Context

Several text and data parsing APIs reported failures through side-channel error
methods:

- `Zanna.Text.JsonStream.Error`
- `Zanna.Data.Xml.Error`
- `Zanna.Data.Yaml.Error`
- `Zanna.Data.Serialize.Error`

That pattern is easy to use in small examples, but it is fragile in production
code because the successful value and failure diagnostic are split across
separate calls. YAML also has a specific ambiguity: valid YAML null and parse
failure both return NULL from `Yaml.Parse`, with only `Yaml.Error` available to
distinguish them.

## Decision

Add Result-returning parse and token APIs while preserving every existing API:

- `JsonStream.NextResult() -> Zanna.Result`
- `Xml.ParseResult(xml) -> Zanna.Result`
- `Yaml.ParseResult(yaml) -> Zanna.Result`
- `Serialize.ParseResult(text, format) -> Zanna.Result`
- `Serialize.AutoParseResult(text) -> Zanna.Result`

Successful document parses return `Ok(value)`. Valid null documents return
`Ok(NULL)`. Parse, tokenization, format-detection, nil-input, and unknown-format
failures return `Err(message)`.

The existing `Parse`, `AutoParse`, `Next`, and `Error` methods remain available
for compatibility and low-level diagnostics. Runtime API dump metadata points the
old side-channel `Error` rows to the new Result APIs as migration targets.

## Consequences

- Production parsers can carry success/failure as a single composable value.
- YAML null is no longer ambiguous when callers use `Yaml.ParseResult`.
- Existing programs keep compiling because legacy parse and error methods remain.
- Docs and API audit examples should teach Result APIs first and keep
  side-channel examples only as compatibility coverage.
