# Signatures, Types, Ownership, And Units

This document defines the target public signature model.

## Signature Dialect

The current dump advertises `runtime-def-v1`. The dialect should become a
documented, audited contract.

Required grammar concepts:

- primitive scalars: `void`, `i1`, `i8`, `i16`, `i32`, `i64`, `f32`, `f64`,
  `str`;
- typed object handles: `obj<Viper.Domain.Type>`;
- intentionally dynamic object values: `obj`;
- generic containers: `seq<T>`, `obj<Viper.Option<T>>`,
  `obj<Viper.Result<T,E>>` if/when nested generics are supported;
- no raw `ptr` in public rows;
- no suffix-nullable syntax such as `str?` unless a future schema version
  formally defines it.

The current live dump has no `str?` rows, but the audit should stay because
older plans and code paths referenced nullable suffixes.

## Typed Handles

Decision: public object handles use `obj<T>` whenever the concrete class is
known.

Bare `obj` is allowed for:

- intentionally dynamic JSON/data values;
- heterogeneous collection values;
- boxed runtime values;
- callback closures or callable payloads;
- compatibility shims awaiting migration.

Bare `obj` is not acceptable for:

- concrete graphics handles;
- concrete GUI widgets;
- collection handles;
- stream/file/network handles;
- math objects;
- parser objects;
- async job handles;
- result objects.

## Ownership Metadata

Every public row returning or storing a string/object needs ownership metadata:

| Ownership | Meaning |
|---|---|
| `owned` | Caller owns a new reference and must release according to runtime rules. |
| `borrowed` | Valid only while the receiver or scope stays alive. |
| `retained` | Returned object has an incremented reference. |
| `view` | Non-owning view into another object. |
| `none` | No resource returned. |
| `value` | Scalar value. |

Current dump values `unknown` must be eliminated for stable APIs. Properties
also need ownership because getters can return object handles.

## Nullability

Decision: public nullability is not implicit.

- `Option<T>` means absence is expected.
- `Result<T>` means an operation can fail.
- `obj<T>` means a valid handle unless the docs and metadata say nullable.
- `obj` returning `NULL` is not a public stable contract.

Stub constructors returning `NULL` must move to `Option`, `Result`, or trap
behavior before stable release.

## Units

Every numeric unit needs metadata. The catalog should declare:

- duration units: seconds, milliseconds, microseconds, frames;
- coordinate units: pixels, logical pixels, world units, tile units;
- angle units: radians or degrees;
- color encoding: RGB, RGBA, premultiplied alpha, linear, sRGB;
- byte counts vs element counts;
- timeout semantics: blocking, non-blocking, deadline, duration.

Decision: new public APIs prefer value objects for units when practical:

- `Duration`;
- `Point2D`, `Size2D`, `Rect`;
- `Color`;
- `Transform`;
- `LayerMask`;
- `Key`, `MouseButton`, `GamepadButton`.

Where value objects are too heavy, names must include units:

- `SleepMilliseconds`;
- `SetTimeoutMilliseconds`;
- `UpdateSeconds`;
- `FrameTimeMicroseconds`.

## High-Arity APIs

The live surface has 210 public functions/methods with arity 6 or higher.

Decision: arity above 5 requires one of:

- a value-object alternative;
- an options/config object;
- a domain-specific waiver for conventional math constructors;
- a generated docs note naming every argument and unit.

Immediate candidates:

- `Mat4.New` with 16 values: keep only with matrix-row docs and add row/array
  constructors.
- `Mat3.New` with 9 values: same.
- tilemap auto-tile setters with 10 values: replace with auto-tile config.
- mesh bone weights with 10 values: replace with `BoneWeights`.
- drawing APIs with many coordinates: add `Rect`, `Point`, and `Color`
  alternatives.
- TLS, PTY, world, and asset loading APIs: use options objects.

## Typed Enum Domains

Raw `i64` constants need domain metadata. Examples:

- keys and mouse buttons;
- blend modes;
- alpha modes;
- shading models;
- texture formats;
- body shapes;
- collision phases;
- log levels;
- watcher event types;
- HTTP statuses;
- modal results.

Decision: add enum/domain metadata to catalog rows and parameters. Do not rely
on docs prose to distinguish unrelated integer domains.

