# Production Contracts And Metadata Plan

## Goal

Make the public runtime catalog strong enough for users, docs generators,
language servers, compatibility checks, and agents to understand the API without
reverse-engineering implementation details.

The compact runtime signature remains useful, but it is not enough for a
production-facing API.

## Required Catalog Fields

Every public API row should eventually include:

| Field | Purpose |
|---|---|
| `kind` | function, method, property, constructor, enum-value, or alias. |
| `owner` | namespace/class that owns the row. |
| `class_kind` | static-module, instance-handle, value-object, enum-like, or namespace-facade. |
| `is_static` | whether a method is invoked without an instance receiver. |
| `signature` | compact `runtime.def` dialect string. |
| `return_type` and `params` | parsed machine-readable type metadata. |
| `fallibility` | infallible, traps, option, result, sentinel, side-channel, or callback. |
| `stability` | stable, preview, experimental, legacy, unsafe, or internal. |
| `capabilities` | graphics, audio, gui, network, filesystem, tooling, etc. |
| `units` | milliseconds, seconds, bytes, pixels, radians, degrees, samples, etc. |
| `domains` | key, mouse-button, color, layer-mask, HTTP-status, log-level, etc. |
| `ownership` | owned, borrowed, retained, singleton, process-global, or receiver-owned. |
| `thread_safety` | reentrant, main-thread-only, thread-safe, synchronized, or unknown. |
| `callbacks` | invocation thread, lifetime, reentrancy, and cancellation rules. |
| `docs_anchor` | canonical docs location for generated references and stale-doc checks. |

## Proposed JSON Shape

```json
{
  "schema_version": 2,
  "signature_dialect": "runtime-def-v1",
  "classes": [
    {
      "name": "Viper.Network.Socket",
      "class_kind": "instance-handle",
      "stability": "preview",
      "capabilities": ["network"],
      "methods": [
        {
          "name": "Connect",
          "is_static": true,
          "signature": "obj<Viper.Result>(str,i64)",
          "return_type": {
            "kind": "object",
            "class": "Viper.Result",
            "result_of": "Viper.Network.Socket"
          },
          "params": [
            {"name": "host", "type": {"kind": "string"}},
            {
              "name": "timeout",
              "type": {"kind": "integer"},
              "unit": "milliseconds"
            }
          ],
          "fallibility": "result",
          "ownership": "owned",
          "thread_safety": "thread-safe",
          "docs_anchor": "docs/viperlib/network.md#socket-connect"
        }
      ]
    }
  ]
}
```

## Class Kind Decisions

### Static module

Use for pure namespaces with no receiver state:

- formatting helpers.
- hash helpers.
- parse helpers.
- capability probes.

Rules:

- no hidden receiver.
- methods may be static only.
- no public `Free`, `Destroy`, or receiver lifecycle methods.

### Instance handle

Use for resource-like objects:

- sockets, files, streams, PTYs, futures, jobs, worlds, canvases.

Rules:

- constructor/factory returns owned handle or result.
- lifecycle is explicit when the resource is external.
- instance methods do not take an extra first receiver parameter in public
  metadata.

### Value object

Use for immutable or small semantic values:

- `Duration`, `Color`, `Rect`, `Vec2`, `Vec3`, `DateTime`, `HttpStatus`.

Rules:

- methods should be side-effect free unless clearly documented.
- constructors should validate invariants.
- high-arity creation should have named alternatives where readability matters.

### Enum-like constants

Use for finite domains:

- keys, mouse buttons, alpha modes, body shapes, collision phases, log levels,
  watcher event types, modal results, shutdown reasons.

Rules:

- each value has a name, integer value, docs string, and deprecation state.
- duplicate enum domains are not allowed.
- raw integer parameters reference the enum domain until the language has a
  stronger enum type.

### Namespace facade

Use only when a class exists to group a large domain and delegate to subsurfaces.

Rules:

- facade docs point users to owned subsurfaces.
- facades should not accumulate unbounded methods.

## Fallibility Metadata

Every fallible operation needs one of these values:

- `infallible`: no expected failure path.
- `traps`: traps on invalid input or unavailable capability.
- `option`: absence is expected and carries no detail.
- `result`: failure carries diagnostics.
- `sentinel`: legacy sentinel return. Must include `migration_target`.
- `side-channel`: legacy side-channel error. Must declare scope.
- `callback`: completion happens through callback/event. Must declare callback
  contract.

Audit rule: new public APIs may not use `sentinel` or `side-channel` unless
explicitly marked legacy.

## Units And Domains

Numeric primitives need metadata whenever the name alone is insufficient.

Required units:

- timeouts, sleeps, delays, durations, frame deltas.
- byte counts and buffer capacities.
- pixels versus world units.
- degrees versus radians.
- sample rates, frames, channels, and audio sample counts.

Required domains:

- input keys and buttons.
- colors and color formats.
- collision layers and masks.
- blend modes and render modes.
- HTTP status codes and method constants.
- log levels and diagnostic severities.
- file watcher event types.
- modal results and shutdown reasons.

## Ownership And Lifetime

Returned `obj` values should declare lifetime:

- `owned`: caller owns a new runtime object.
- `borrowed`: valid only while receiver lives.
- `retained`: reference count retained for caller.
- `receiver-owned`: released with the parent object.
- `singleton`: process/context singleton.
- `process-global`: global runtime state.

This is especially important for:

- weak references.
- collections returning contained objects.
- GUI widgets and child controls.
- scene graph nodes.
- asset handles.
- async jobs and callbacks.

## Thread-Safety And Main-Thread Rules

The catalog should distinguish:

- thread-safe operations.
- synchronized operations.
- main-thread-only GUI/graphics operations.
- reentrant callbacks.
- APIs that may block.
- APIs that are safe in signal/trap contexts.

Docs should not be the only source of this information.

## Stability And Capability

Stability tiers:

- `stable`: recommended for ordinary application use.
- `preview`: usable but may change before 1.0.
- `experimental`: available for early feedback.
- `legacy`: retained for migration.
- `unsafe`: advanced or dangerous.
- `internal`: should not appear in ordinary public discovery.

Capability tags should be consistent with disabled-build behavior. A missing
graphics capability should be visible in catalog data and runtime capability
probes, not discovered by null constructors.

## Implementation Steps

1. Add schema-versioned metadata fields to the generated API dump.
2. Backfill static `stability`, `capabilities`, and docs anchors first.
3. Add class kind and method static/instance classification.
4. Add fallibility metadata, starting with `Try*`, `Load`, `Open`, `Connect`,
   `Parse`, and `Read`.
5. Add unit/domain metadata for timeouts, durations, keys, colors, statuses, and
   masks.
6. Add ownership and thread-safety metadata for handles, callbacks, GUI, and
   graphics.
7. Create audits that fail when new public rows omit required metadata.

## Acceptance Criteria

- The public catalog can generate accurate docs tables without handwritten type
  guesses.
- Language servers can offer enum/domain completions for raw integer domains.
- Compatibility checks can distinguish rename, signature, stability, and
  fallibility changes.
- Docs can link every public row back to a canonical anchor.
- New APIs cannot enter the public surface without stability, capability, and
  fallibility classification.
