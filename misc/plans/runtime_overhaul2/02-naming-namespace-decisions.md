# Naming And Namespace Decisions

This document defines the target naming style for the public runtime API.

## Naming Rules

### General Shape

- Public names use PascalCase.
- Prefer complete words over compressed abbreviations.
- A method name should explain the operation without knowing the C symbol.
- A property name should name state, not an action.
- Avoid duplicate verbs for the same concept inside one class.
- Accessors use catalog properties; `get_` and `set_` are implementation
  details in canonical function rows.

Examples:

| Legacy or weak name | Canonical style |
|---|---|
| `LeadZ` | `CountLeadingZeros` |
| `TrailZ` | `CountTrailingZeros` |
| `Rotl` | `RotateLeft` |
| `Rotr` | `RotateRight` |
| `Ushr` | `ShiftRightLogical` |
| `Fpr` | `FalsePositiveRate` |
| `Len` | `Length` |
| `Cap` | `Capacity` |
| `Norm` | `Normalize` |
| `Dist` | `Distance` |
| `SetDTMax` | `SetMaxDeltaTime` |
| `BoolYN` | `YesNo` or `BooleanYesNo` |

### Acronyms

Decision: canonical user-facing names word-case acronyms unless the acronym is
an externally standardized algorithm token that users normally type in all
caps.

Preferred examples:

- `Json`, `Xml`, `Yaml`, `Toml`, `Html`, `Csv`, `Url`.
- `Http`, `Tls`, `Tcp`, `Dns`, `Smtp`, `Sse`.
- `Aes`, `HmacSha256`, `Sha256`, `Pbkdf2Sha256`.
- `Gltf`, `Fbx`, `Ktx2`, `Png`, `Jpeg`, `Bdf`, `Psf`.

Migration rule: existing all-caps names can remain as legacy aliases during
the migration, but new canonical APIs should not introduce additional mixed
acronym styles such as `Pbkdf2SHA256`, `SetAOMap`, `AddSSAO`, or `AddDOF`.

### Verb Rules

| Concept | Preferred verb |
|---|---|
| Insert or replace value by key | `Set` |
| Add another value to a multi-value collection | `Add` |
| Mutate existing object state | writable property or `SetX` compatibility method |
| Allocate a new instance | `New` |
| Open external resource | `OpenResult` canonical, strict `Open` only with trap docs |
| Load external data | `LoadResult` canonical, strict `Load` only with trap docs |
| Parse user data | `ParseResult` canonical, `Parse` only as strict convenience |
| Absence lookup | `Find` returning `Option<T>` |
| Boolean predicate | `Is`, `Has`, `Can`, or direct property |

## Namespace Decisions

### Core And Runtime

Decision: `Zanna.Runtime.Unsafe` owns low-level memory, trap mutation, and
runtime-internal-adjacent operations. `Zanna.Memory` should expose safe memory
utilities only. Duplicate `Memory` and `Runtime.Unsafe` rows must be resolved.

Decision: public trap-state mutation does not belong under `Zanna.Error`.
Read-only diagnostics belong under `Zanna.Diagnostics`.

### Collections

Decision: collection APIs should use common vocabulary:

- `Count` for number of stored elements.
- `Capacity` for reserved capacity.
- `Length` only for fixed-size sequences, strings, bytes, bit lengths, or
  geometric length.
- `Set` for key replacement.
- `Add` for append or multi-map addition.
- `TryPop` should return `Option<T>`, not bare `obj` with sentinel behavior.

### Text And Data

Decision: `Zanna.Text` owns text algorithms, formatting, regex/patterns,
wrapping, codecs, and string utilities. `Zanna.Data` owns structured data
formats and serializers: `Json`, `Xml`, `Yaml`, `Toml`, `Csv`, and `Ini`.

Migration consequence: current `Zanna.Data.Json`, `Zanna.Data.Csv`,
`Zanna.Data.Toml`, and `Zanna.Data.Ini` should either move to `Zanna.Data.*`
or become legacy facades.

### Graphics, Game, And GUI

Decision: use dimension-specific roots for dimension-specific rendering:

- `Zanna.Graphics2D` for 2D rendering concepts.
- `Zanna.Graphics3D` for 3D rendering concepts.
- `Zanna.Graphics` only for shared primitives such as color, pixels, fonts,
  image codecs, and compatibility facades.

Decision: `Zanna.Game` owns dimension-independent engine systems.
`Zanna.Game2D` and `Zanna.Game3D` own dimension-specific gameplay/world
systems. Rendering-only APIs should not live under `Game`.

Decision: GUI remains `Zanna.GUI`, but large widgets such as `CodeEditor`
should expose structured option/result/event objects instead of accumulating
many positional methods.

### Input

Decision: `Zanna.Input.Key` is the canonical key constant domain. Deprecate
`Zanna.Input.Keyboard.Key*` constants and `Zanna.Input.Key`.

Add canonical companion domains:

- `Zanna.Input.MouseButton`.
- `Zanna.Input.GamepadButton`.
- `Zanna.Input.ModifierKey`.
- `Zanna.Input.KeyChord`.

### Audio

Decision: canonical root should be `Zanna.Audio`. Existing `Zanna.Sound` APIs
can remain as legacy facades if needed, but new public APIs should use `Audio`
for the domain and reserve `Sound` for concrete sound asset types.

### Network And Crypto

Decision: network APIs use safe defaults and `Result`-returning operations.
Security-affecting toggles must be named as process-scoped, test-only,
legacy, or unsafe.

Decision: weak crypto algorithms live only under `Zanna.Crypto.Legacy`.
Canonical hash/KDF/cipher names use the acronym policy above.

