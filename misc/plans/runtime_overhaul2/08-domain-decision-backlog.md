# Domain Decision Backlog

This backlog groups the public API work by domain.

## Core, Runtime, Memory, Diagnostics

Decisions:

- `Runtime.Unsafe` owns low-level memory and trap mutation.
- `Diagnostics` owns read-only runtime diagnostic snapshots.
- `Error` should not expose mutable trap state as ordinary app API.
- `Memory.GC` and `Runtime.GC` must be canonicalized.

Work:

- migrate `Zanna.Error.Set*`, `Clear*`, and `Raise*`;
- move or mark `Memory.Retain/Release` unsafe;
- type weak refs and boxed values;
- document ownership for all returned objects.

## Collections

Decisions:

- `Count`, `Capacity`, and `Length` have distinct meanings.
- `Set` replaces by key; `Add` appends or adds another multi-value entry.
- empty pop/peek/dequeue returns `Option<T>`.

Work:

- replace `TryPop`/`TryPeek` bare object sentinels;
- canonicalize `Put`/`Set`, `Cap`/`Capacity`;
- type collection handles and iterator handles;
- document element ownership.

## Math

Decisions:

- full-word vector names are canonical for public API clarity;
- compact operator names are allowed only where the whole class is consistently
  mathematical and docs teach that style;
- matrix high-arity constructors need safer alternatives.

Work:

- canonicalize `Len`, `LenSq`, `Norm`, `Dist`, scalar `Mul`;
- add `LengthSquared`, `Normalize`, `Distance`, `Scale`;
- add row/array/config constructors for matrices;
- type all math object returns as `obj<Zanna.Math.*>`.

## Text And Data

Decisions:

- `Text` owns text algorithms;
- `Data` owns structured data formats;
- parse failures return `Result`;
- side-channel parser `Error` functions are legacy.

Work:

- move or facade `Json`, `Csv`, `Toml`, `Ini` into `Data`;
- add result-returning parsers for every format;
- replace side-channel `Error`;
- define typed data value and parse error objects;
- remove bare string-only structured errors.

## IO, System, Project, Workspace

Decisions:

- file, stream, process, terminal, and PTY APIs return `Result` for external
  failure;
- timeouts use `Duration` or unit-suffixed names;
- environment facade methods should not duplicate unrelated owners without
  metadata.

Work:

- add `OpenResult`, `ReadResult`, `WriteResult`, `ExecResult`;
- replace PTY `LastError`;
- type stream/file/archive handles;
- add unit metadata for waits/timeouts;
- decide whether `System.Environment` is a facade or canonical owner.

## Crypto And Security

Decisions:

- safe algorithms and safe defaults are canonical;
- weak algorithms live under `Crypto.Legacy`;
- process-scoped security switches must say "ForProcess";
- testing-only insecure certificate APIs remain unsafe/test-only.

Work:

- canonicalize acronym casing;
- move weak hash names out of stable `Crypto.Hash`;
- ensure decrypt/connect operations return `Result`;
- type key, nonce, ciphertext, plaintext, and certificate objects where useful;
- document approved mode semantics and process scope.

## Network

Decisions:

- network operations return `Result`;
- HTTP/REST response data is returned, not read from "last" state;
- TLS verification defaults to safe.

Work:

- replace `RestClient.LastStatus/LastResponse/LastOk`;
- add typed request/response/error objects;
- type sockets, clients, servers, and sessions;
- add timeout unit metadata;
- mark unsafe TLS toggles clearly.

## Input

Decisions:

- `Input.Key` is canonical;
- `Input.Keyboard.Key*` and `Game3D.Keys` are legacy;
- integer constants need enum/domain metadata.

Work:

- migrate examples to `Input.Key`;
- add `MouseButton`, `GamepadButton`, `ModifierKey`;
- type key chord APIs;
- hide duplicated key constants from default docs.

## Graphics2D, Graphics3D, Game, GUI

Decisions:

- dimension-specific rendering belongs in `Graphics2D` or `Graphics3D`;
- shared primitives stay in `Graphics`;
- game-world APIs belong in `Game2D` or `Game3D`;
- GUI widgets should prefer properties, options, events, and result objects over
  large positional methods.

Work:

- canonicalize Canvas/Canvas3D namespace story;
- resolve `ParticleEmitter`, `ParticleSystem2D`, and `Emitter2D` duplication;
- add capability metadata and unavailable behavior to all graphics APIs;
- split large classes into sub-objects or grouped methods;
- fix `CodeEditor` boolean getter types;
- replace fake/no-op disabled stubs;
- type all graphics and GUI handles.

## Audio

Decisions:

- canonical root should be `Audio`;
- `Sound` can remain as asset terminology or legacy namespace.

Work:

- decide `Zanna.Audio` migration path;
- type sound/music/voice handles;
- define result-returning load/open APIs;
- add capability metadata for audio availability.

## Tooling, Zia, BASIC

Decisions:

- language-service APIs are tooling capability APIs, not guaranteed core
  application APIs;
- unavailable analyzers return explicit unavailable results;
- async job handles should never be null-sentinel public contracts.

Work:

- type semantic job and project index handles;
- replace null async jobs with `Result<JobHandle>` or unavailable job objects;
- define common result shapes for Zia and BASIC;
- expose analyzer capability in runtime catalog.

