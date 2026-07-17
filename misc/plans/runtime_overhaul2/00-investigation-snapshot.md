# Investigation Snapshot

Date: 2026-07-02

This snapshot records the evidence behind the overhaul plan. It should be
refreshed whenever implementation work starts, because the runtime surface is
large and still moving.

## Source Inputs

- Live API dump: `/tmp/zanna_runtime_api_overhaul2.json`.
- Registry source: `src/il/runtime/runtime.def`.
- Generator: `src/tools/rtgen/rtgen.cpp`.
- Runtime class lookup: `src/il/runtime/classes/RuntimeClasses.cpp`.
- CLI catalog emission: `src/tools/zanna/main.cpp`.
- Stub implementations: `src/runtime/core/*_stub.c` and
  `src/runtime/graphics/common/*_stubs.c`.

## Surface Counts

| Category | Count |
|---|---:|
| Public functions | 6,624 |
| Runtime classes | 474 |
| Class properties | 1,598 |
| Class methods | 4,836 |
| Public signatures with raw `ptr` | 0 |

Function stability:

| Stability | Count |
|---|---:|
| stable | 4,304 |
| preview | 2,028 |
| legacy | 266 |
| unsafe | 26 |

Function fallibility:

| Fallibility | Count |
|---|---:|
| infallible | 6,321 |
| traps | 130 |
| option | 71 |
| sentinel | 45 |
| result | 41 |
| side-channel | 16 |

Function ownership:

| Ownership | Count |
|---|---:|
| value | 2,569 |
| none | 1,949 |
| owned | 1,123 |
| unknown | 983 |

Method ownership remains similarly incomplete, and every property currently
reports `ownership: unknown`.

## Highest-Risk Findings

### Duplicate Public Names Backed By The Same Symbol

`RT_ALIAS` is explicitly rejected by `rtgen` in
`src/tools/rtgen/rtgen.cpp`, but duplicate public `RT_FUNC` rows still provide
alias-like behavior. The live surface has 175 duplicate C-symbol/signature
groups involving 352 public names.

Representative groups:

- `Zanna.Math.Bits.LeadZ` and `CountLeadingZeros`.
- `Zanna.Math.Bits.Rotl` and `RotateLeft`.
- `Zanna.Collections.BloomFilter.Fpr` and `FalsePositiveRate`.
- `Zanna.Collections.LruCache.Put` and `Set`.
- `Zanna.Collections.MultiMap.Put` and `Add`.
- `Zanna.IO.BinaryBuffer.NewCap` and `NewCapacity`.
- `Zanna.Math.Vec2.Len` and `Length`.
- `Zanna.Math.Vec3.Norm` and `Normalize`.
- `Zanna.Runtime.GC.*` and `Zanna.Runtime.GC.*`.
- `Zanna.Memory.Retain/Release` and `Zanna.Runtime.Unsafe.*`.

Decision: each concept gets one canonical public name. Compatibility names are
either removed before release, hidden from normal discovery, or marked legacy
with a migration target.

### Bare Object Handles Still Dominate

The previous `ptr` problem is fixed, but bare `obj` is still too common:

| Pattern | Count |
|---|---:|
| Function returns bare `obj` | 1,049 |
| Function has any bare `obj` parameter | 4,889 |
| Method returns bare `obj` | 1,014 |
| Method has any bare `obj` parameter | 1,026 |
| Property type is bare `obj` | 50 |

Decision: public handles use typed `obj<T>` unless the value is intentionally
dynamic, such as JSON values, boxes, heterogeneous collections, or callbacks.

### Failure Model Is Mixed

The current surface exposes traps, sentinel values, `Option`, `Result`, and
side-channel "last error" state at the same time. This makes simple code terse
in places, but it makes robust code hard to write consistently.

Decision: the canonical public model is:

- `Result<T>` for fallible work.
- `Option<T>` for normal absence.
- `i1` for pure predicates.
- traps only for programmer errors, invalid operations, and explicit strict
  convenience APIs.
- side-channel diagnostics only for scoped debugging, not primary flow control.

### Stub Behavior Can Look Like Success

Language-service and graphics-disabled builds expose public APIs that can
return empty payloads, `NULL`, `0`, no-op success, or fake values. Some stubs
are well documented internally, but the public catalog does not declare which
APIs are unavailable, silently degraded, or strict traps.

Examples:

- Zia/BASIC completion stubs are always compiled into runtime core source
  lists.
- Graphics stubs use a mix of traps and optional traps.
- `Transform3D.New` silently returns `NULL` in disabled graphics builds.
- `rt_accessibility_contrast_ratio` returns `1.0` in the disabled runtime
  supplemental stubs.

Decision: every capability-dependent API must declare availability behavior in
the runtime catalog. Silent fallback is not acceptable for stable public APIs
unless the method name explicitly communicates a probe or default.

### Docs Metadata Is Mostly Synthetic

The dump emits `docs_anchor` for every row, but the anchors are generated
mechanically. A heading check found 13,532 checked anchors and no matching
headings under the simple GitHub-style heading model. Even if future generated
reference pages use those anchors, the current handwritten docs do not support
them.

Decision: docs anchors become generated-reference-owned IDs or verified
handwritten anchors. A release audit fails when a public row has an unresolved
docs target.

## Focused Bugs Found

- Five `Zanna.GUI.CodeEditor` setters accept `i1` but matching getters return
  `i64`: `ShowLineNumbers`, `InsertSpaces`, `WordWrap`, `ShowIndentGuides`,
  and `ReadOnly`.
- Two public names still have non-PascalCase leaves:
  `Zanna.Core.Convert.ToString_Int` and `ToString_Double`.
- `RuntimeRegistry` method lookup keys only by class, method, and arity. No
  current conflict exists, but type-different overloads with the same arity
  would overwrite in the index.
- Constructor methods can be auto-injected by `rtgen`, so the source class
  block can look thinner than the emitted public class.

