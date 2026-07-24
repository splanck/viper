---
status: active
audience: contributors
last-verified: 2026-07-23
---

# ADR 0156: Expose Selected ListBox Row Data

## Status

Accepted (2026-07-23)

## Context

`Zanna.GUI.ListBox` supports Ctrl/Command and Shift multi-selection, and retained
rows can carry byte-exact string data through `ItemSetData`. The only public
multi-selection read API is `GetSelectedText`, which joins display labels with
newlines. Applications cannot recover stable row identities from that value:
labels need not be unique, labels can change independently of the model, and a
label may itself contain a newline.

Zanna Studio's scene hierarchies need stable identities to apply one
transaction to several selected objects or nodes. Parsing display text would
couple editor correctness to presentation. Reading only `SelectedIndex` would
silently discard all but the primary row. The missing operation changes the
public runtime C ABI and registry surface, so ADR 0006 requires an explicit
decision.

## Decision

`Zanna.GUI.ListBox` gains one additive instance method:

```text
GetSelectedData() -> Seq[String]
```

Its C ABI entry point is:

```c
void *rt_listbox_get_selected_data(void *listbox);
```

In retained-item mode, the returned sequence contains one string for every
selected row, in current row order. Each value is a byte-exact copy of the
row's `ItemSetData` value. A selected row without attached data contributes an
empty string, preserving the selected-row count and positional correspondence.
The caller owns the returned sequence.

The method does not change selection, consume a selection-change edge, or
expose internal item pointers. Invalid handles and an empty selection return a
valid empty sequence.

Virtual ListBox rows do not own retained item handles or `ItemSetData` values.
`GetSelectedData` therefore returns an empty sequence in virtual mode.
Virtualized clients must use their model's stable row IDs and selection APIs
instead of assuming retained-row data exists.

## Consequences

- Multi-selection consumers can use stable, non-display row identities without
  parsing labels or newline-delimited text.
- Embedded NUL bytes and newlines survive the round trip.
- Empty data remains distinguishable from an absent selected row because the
  sequence preserves cardinality.
- Existing programs remain compatible; `GetSelectedText` and
  `SelectedIndex` retain their current behavior.
- Runtime registry, generated API documentation, authored ListBox
  documentation, and native tests must cover the new operation.

## Alternatives Considered

- **Parse `GetSelectedText`.** Rejected because display labels are not stable
  identities and newline joining is intentionally presentation-oriented.
- **Return selected item handles.** Rejected because it exposes lifetime-bound
  GUI implementation objects and still requires one call per row to retrieve
  data.
- **Return only selected indices.** Rejected because indices change when rows
  are inserted or reordered; callers already have an explicit stable-data slot.
- **Synthesize virtual row data.** Rejected because virtual rows are supplied
  by an external model and have no retained `ItemSetData` contract.
