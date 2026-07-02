# ADR 0028: Terminal Option and Result Input APIs

## Status

Accepted

## Context

`Viper.Terminal.ReadLine` and `Viper.Terminal.Ask` historically returned
nullable strings. That shape preserves compatibility with earlier Zia, BASIC,
and IL examples, but it is not the clearest public API for robust applications:
EOF is an expected outcome, while allocation failures and overlong input are
runtime failures. New user-facing APIs need to communicate that distinction
without deleting the existing nullable functions.

The runtime already exposes `Viper.Option` and `Viper.Result`, so terminal input
can use the same explicit success/failure vocabulary as parsing and other
modern runtime surfaces.

## Decision

Add four terminal input functions while preserving all existing functions:

- `Viper.Terminal.TryReadLine() -> Option<String>`
- `Viper.Terminal.TryAsk(prompt: String) -> Option<String>`
- `Viper.Terminal.ReadLineResult() -> Result<String, String>`
- `Viper.Terminal.AskResult(prompt: String) -> Result<String, String>`

EOF before any bytes are read is represented as `None` for the `Try*` APIs and
as `Err(String)` for the `*Result` APIs. Input allocation failures, overlong
lines, and other fatal runtime errors continue to trap. `ReadLine`, `Ask`, and
`InputLine` remain registered for source and IL compatibility.

## Consequences

New examples and documentation should prefer `TryReadLine` or
`ReadLineResult` depending on whether the caller needs a simple optional value
or a diagnostic string. Existing nullable calls continue to work, but they are
documented as compatibility APIs rather than the recommended public shape.

The runtime API dump reports these APIs as object-returning calls with
`option`/`result` fallibility metadata, which gives tools a structured way to
prefer the modern APIs during migrations.
