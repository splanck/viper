---
status: active
audience: contributors
last-verified: 2026-07-15
---

# ADR 0053: Search Option APIs

Date: 2026-07-02

## Status

Accepted

## Context

Several public runtime search APIs returned raw sentinel values:

- `-1` for missing byte, list, sequence, scene object, and audio group indexes.
- `NULL` for missing XML, scene graph, and lazy sequence values.
- `""` for missing regex matches, which also made valid empty-string matches ambiguous.

These contracts were compact but hard to compose in robust application code.
They also made the generated runtime catalog report stable sentinel-returning
APIs even after the broader overhaul had standardized on `Option<T>` for normal
absence.

## Decision

Keep every existing search API and add sentinel-free Option companions:

- `Zanna.Collections.Bytes.FindOption`
- `Zanna.Collections.List.FindOption`
- `Zanna.Collections.Seq.FindOption`
- `Zanna.Collections.Seq.FindWhereOption`
- `Zanna.Functional.LazySeq.FindOption`
- `Zanna.Data.Xml.FindOption`
- `Zanna.Text.Pattern.FindOption`
- `Zanna.Text.Pattern.FindFromOption`
- `Zanna.Text.Pattern.FindPosOption`
- `Zanna.Text.CompiledPattern.FindOption`
- `Zanna.Text.CompiledPattern.FindFromOption`
- `Zanna.Text.CompiledPattern.FindPosOption`
- `Zanna.Game2D.SceneDocument.FindObjectOption`
- `Zanna.Graphics2D.SceneNode.FindOption`
- `Zanna.Graphics2D.SceneGraph.FindOption`
- `Zanna.Audio.Mixer.FindGroupOption`

Index-returning APIs return `SomeI64(index)` or `None`. Object/string-returning
APIs return `Some(value)` / `SomeStr(value)` or `None`.

Regex Option APIs preserve valid empty-string matches as `SomeStr("")`, unlike
the legacy `Find` and `FindFrom` APIs where an empty string can mean no match.

## Consequences

- Existing source remains compatible.
- New code can use `Option` consistently for ordinary absence.
- The runtime API dump marks old sentinel rows as legacy and points them at the
  corresponding Option APIs.
- `FindAll` APIs are not classified as sentinel APIs because they already return
  an empty sequence for no matches.
