---
status: active
audience: contributors
last-verified: 2026-06-27
---

# ADR 0017: CRLF-Aware Line Splitting (Viper.String.Lines)

## Status

Accepted (runtime implemented; ViperIDE source-analysis modules adopt it as the
first consumer).

## Context

Splitting text into logical lines is one of the most common operations in any
editor, formatter, or text tool. The runtime already provides
`Viper.String.Split(str, delim)`, but `Split(source, "\n")` leaves a trailing
carriage return on every line of CRLF-terminated input. As a result, callers
must pair every split with a per-line carriage-return strip.

In ViperIDE alone this `Split("\n")` + strip-`\r` idiom appears in 30+ places
across the formatter, refactors, bind utilities, and source-analysis modules,
each re-implementing the same two-step dance (and a hand-rolled `StripTrailingCr`
helper backed it). Any Viper program that processes multi-line text — log
viewers, config parsers, code tools — hits the same friction. This is a missing
standard-library primitive, not application logic.

Adding a runtime function is a runtime C-ABI surface change, which requires an
ADR.

## Decision

Add `Viper.String.Lines(str) -> Seq(String)` to the runtime.

Semantics (deliberately a drop-in for the existing idiom):

- Split `str` on `"\n"`.
- Drop **one** trailing `"\r"` from each resulting segment (CRLF → LF), including
  the final segment.
- The number of segments is **identical** to `Split(str, "\n")` — a trailing
  newline still yields a trailing empty segment, an empty string yields one empty
  line. Only embedded/terminating carriage returns differ.
- An embedded `\r` that is not immediately before a `\n` (or end of a segment) is
  preserved; only the line-terminating `\r` is removed.

Surface:

- `RT_FUNC(StrLines, rt_str_lines, "Viper.String.Lines", "seq<str>(str)", always)`
  (free-function form, e.g. `Viper.String.Lines(source)`).
- `RT_METHOD("Lines", "seq<str>()", StrLines)` on the `Viper.String` class
  (method form, e.g. `text.Lines()`).

The C implementation (`rt_str_lines` in `rt_string_advanced.c`) mirrors
`rt_str_split`: a counting pass sizes the result sequence, a build pass emits each
segment. No new runtime class, no new ID, no new `.c`/`.h` file (so no
`source_health` surface change).

## Consequences

- **Adoption:** the `Split("\n")` + `StripTrailingCr` pairs collapse to a single
  `Str.Lines(source)` call. ViperIDE adopts it as the first consumer; the
  hand-rolled `StripTrailingCr` helper is retained only for the few non-split
  callers.
- **Determinism / cross-platform:** the function is pure and platform-independent
  (no OS line-ending detection); the same bytes produce the same lines on every
  target, satisfying the VM/native determinism requirement.
- **No behavior risk for existing callers:** because the segment count matches
  `Split("\n")` exactly, migrating a `Split("\n")`+strip pair is byte-for-byte
  equivalent.

## Alternatives Considered

- **Keep a Zia/library helper (`StripTrailingCr`) per project.** Rejected: every
  Viper text program re-derives the same primitive, and the helper still requires
  a `Split` + per-line call. A runtime primitive removes the idiom platform-wide.
- **A `splitlines`-style function that also collapses a trailing empty line.**
  Rejected: that would make it a non-drop-in for `Split("\n")` and silently
  change line counts, breaking round-trip text transforms. Matching `Split`'s
  segmentation keeps it safe to adopt mechanically.
