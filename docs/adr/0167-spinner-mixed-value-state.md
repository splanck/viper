---
status: active
audience: contributors
last-verified: 2026-07-23
---

# ADR 0167: Expose Spinner Mixed-Value State

## Status

Accepted (2026-07-23)

## Context

Property inspectors routinely represent several selected objects at once. A
numeric field is truthful only when it can distinguish one common value from
different values across that selection. `Zanna.GUI.Checkbox` already exposes
an indeterminate state, and `Dropdown` can present a placeholder with no
selection, but `Zanna.GUI.Spinner` can only display one concrete number.

Without a mixed state, an inspector must either disable group editing, add a
parallel override control beside every scalar, or display the primary object's
number as if it applied to the entire selection. Zanna Studio's 3D material
inspector disabled all material controls for multiple nodes, preventing safe
batch PBR authoring. Presenting the primary value would be worse because an
artist could unknowingly replace distinct alpha, metallic, roughness, or
ambient-occlusion values.

Adding public runtime entry points changes the C ABI and registered GUI surface,
so ADR 0006 requires an explicit decision.

## Decision

`Zanna.GUI.Spinner` gains two additive instance methods:

```text
SetIndeterminate(indeterminate: Boolean)
IsIndeterminate() -> Boolean
```

Their C ABI entry points are:

```c
void rt_spinner_set_indeterminate(void *spinner, int64_t indeterminate);
int64_t rt_spinner_is_indeterminate(void *spinner);
```

The native toolkit stores the mixed flag independently from the committed
numeric value. While mixed:

- the spinner displays `Mixed` instead of presenting the retained number as a
  value common to the represented objects;
- `Value` still returns the retained, range-clamped number so an inspector can
  seed editing from its primary object;
- setting a concrete value, typing, stepping, or scrolling resolves the mixed
  state before presenting the result;
- changing range, step, decimal formatting, layout, or enabled state does not
  resolve it; and
- entering or leaving mixed state advances the common value-change edge and
  monotonic widget revision, while a no-op assignment does neither.

The existing numeric callback remains a numeric-transition callback: resolving
mixed to the same retained number records the generic change edge but does not
invoke the numeric callback. Accessibility snapshots report `mixed`; Linux
AT-SPI value descriptions do the same while retaining the range-value seed.

The runtime rejects null, stale, and wrong-type handles as inert no-ops.
Graphics-disabled builds provide matching inert C symbols so registry and link
surfaces remain configuration-independent.

Zanna Studio uses the state for multi-node material scalar controls. Unresolved
fields are omitted from a sparse material patch, while a resolved field applies
to the complete selection in one rollback-safe scene transaction.

## Consequences

- Numeric property inspectors can present group state honestly without one
  auxiliary checkbox per scalar.
- Keyboard, pointer, wheel, and programmatic editing all have the same
  mixed-to-concrete transition.
- Existing Spinner programs remain source- and binary-compatible because the
  methods and C symbols are additive.
- Callers that explicitly enter mixed state must check `IsIndeterminate()` when
  deciding whether `Value` is a group value or only an editing seed.
- Toolkit tests, runtime handle/event tests, the reviewed GUI ABI manifest,
  generated runtime documentation, authored widget documentation, accessibility
  snapshots, and Studio scene probes must cover the contract.

## Alternatives Considered

- **Disable numeric controls for multi-selection.** Rejected because it blocks
  routine batch authoring and was the limitation this change removes.
- **Display the primary value without a mixed marker.** Rejected because it
  falsely represents distinct object state and invites destructive edits.
- **Add an override checkbox beside every scalar.** Rejected as unnecessary
  layout and interaction overhead for a state intrinsic to numeric editors.
- **Use a sentinel floating-point value.** Rejected because NaN and out-of-range
  sentinels weaken the finite, clamped Spinner value contract and leak
  presentation state into application data.
- **Implement mixed state only inside Zanna Studio.** Rejected because every
  inspector and data editor would have to recreate widget behavior, input
  resolution, event semantics, and accessibility.
