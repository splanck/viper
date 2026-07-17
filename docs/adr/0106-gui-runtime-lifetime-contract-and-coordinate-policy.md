---
status: active
audience: contributors
last-verified: 2026-07-17
---

# ADR 0106: Make GUI Lifetimes, Contracts, and Coordinates Explicit

Date: 2026-07-16

## Status

Accepted. The user authorized the complete 2026-07-16 GUI modernization
program. This ADR governs review recommendations 1-4, 9, 14, 34, 35, and the
contract portions of 40.

## Context

The `Viper.GUI.*` registry exposes opaque objects whose compact signatures do
not encode exact nullability or ownership. Tooling consequently infers
constructors and object getters as non-null/owned even though application
creation can fail, lookups can return no object, and roots, split-pane children,
tree nodes, tabs, menu items, and similar values are owner-managed handles.

Removed tree nodes and tabs are retained as tombstones so stale runtime wrappers
can be rejected safely. Public prune methods currently free those tombstones
without invalidating wrappers; later validation can dereference the freed
address. Other subobjects remain retired until owner teardown, and wrapper
identity uses a process-global linear list. Separately, the widget liveness hash
tracks live entries but not tombstones and probes without a capacity bound,
allowing churn to eliminate every terminating empty slot.

GUI capability failure is silent in graphics-disabled builds. Application and
widget geometry also mixes logical setters with physical toolkit storage and
legacy physical getters. Multi-window static services use an implicit active
app that `App.Activate` does not itself install.

These are runtime C embedding surface, public API contract, and cross-layer
metadata changes, so an ADR is required.

## Decision

### Public boundary and contract manifest

- The runtime registry remains the canonical public boundary; `rt_*` GUI
  functions remain Viper's internal embedding ABI and opaque handles remain the
  only public object representation.
- GUI rows receive an explicit contract source named `gui-boundary-policy`.
  Constructors are managed and fallible; lookup/selection/optional child results
  are managed and nullable; owner-created roots, child containers, tabs, nodes,
  and item wrappers are managed borrowed handles; strings and sequences retain
  their documented managed empty-value contracts.
- A deterministic GUI manifest test fingerprints function name, compact
  signature, C symbol, fallibility, ownership, nullability, class binding,
  property binding, and method binding. Counts and the fingerprint are updated
  only after manual review of the live API dump.
- Existing constructors/getters remain compatible. `Viper.GUI.App.TryNew` and
  `Viper.GUI.System.IsAvailable`/`GetUnavailableReason` add explicit capability
  and failure reporting. `TryNew` returns a `Viper.Result`; exact failures are
  defined in the program validation matrix.

### Subobject and widget lifetime

- Existing `TreeView.PruneRetiredNodes` and `TabBar.PruneRetiredTabs` remain
  public. They become safe reclamation requests: tombstone memory is reclaimed
  only after every runtime wrapper for it is invalidated or finalized.
- Every wrapper has a stable `(kind, pointer, generation)` identity plus the
  owning widget's immutable ID. Wrapper lookup uses an open-addressed hash table
  and owner-linked index rather than a global linear scan. Owner destruction
  matches both pointer and owner ID, so allocator address reuse cannot make a
  stale wrapper appear to belong to a new widget at the same address.
- Removed list, menu, context-menu, toolbar, status-bar, tree, and tab subobjects
  use the same retained-wrapper policy. Retirements are grouped by payload;
  storage is reclaimed automatically as soon as the last wrapper in that group
  finalizes, without waiting for an unrelated stale wrapper or owner teardown.
- The live-widget table tracks live and occupied counts, rehashes when
  tombstones degrade occupancy, and caps every probe at capacity. Its linked
  live list remains the allocation-failure fallback and authoritative rebuild
  source.
- Fonts use explicit references from widgets and app-owned services. Replaced
  fonts are retired by frame generation and destroyed only after the last
  reference and safe presentation boundary; they no longer accumulate until
  `App.Destroy`.
- Heap-registry tombstones are never accepted as object payloads, including
  when a hostile or stale pointer numerically equals the internal tombstone
  sentinel. All payload/header probes reject the sentinel before hash lookup.

### Coordinates and app context

- Public layout setters and new logical bounds getters use logical units. The
  toolkit continues to arrange in physical framebuffer units, with conversion
  exactly once at the app boundary using effective scale
  `windowScale * userScale`.
- Existing physical getters remain unchanged for compatibility. New
  `Widget.GetLogical*` and `GetScreen*` methods make unit choice explicit.
- `App.MakeCurrent` explicitly installs widget, theme, focus, capture, cursor,
  dialog, and service context. `App.Activate` first performs `MakeCurrent`, then
  requests OS foreground activation.
- Wheel speed, widget cursor intent, theme, dialogs, and timers are per-app.
  Static convenience classes target the current app and return their documented
  empty/failure result if no app is current.
- Graphics-disabled builds provide every symbol and return deliberate capability
  failures rather than pretending successful construction.

## Consequences

- Stale-handle calls and prune requests cannot read freed memory.
- Widget churn always has bounded liveness lookup, including allocation-failure
  fallback behavior.
- Tooling and generated documentation accurately describe absence and ownership.
- Existing callers remain source-compatible; new callers can avoid sentinel and
  nullable ambiguity.
- Logical/physical behavior becomes explicit without changing legacy getters.
- Wrapper hashes, owner indexes, generations, and font references add modest
  bookkeeping to creation/destruction in exchange for predictable steady-state
  behavior.

## Alternatives Considered

- Remove the prune APIs. Rejected because the modernization request explicitly
  preserves every existing feature and compatibility can be made safe.
- Keep all retired payloads until owner destruction. Rejected because long-lived
  editors and dynamic menus can accumulate unbounded memory.
- Validate stale payloads by reading an in-object magic value. Rejected because
  no magic read is safe after storage is freed or reused.
- Change all legacy geometry getters to logical units. Rejected because it would
  silently break framebuffer and canvas callers covered by ADR 0021.
- Treat all GUI object results as nullable owned values. Rejected because it
  hides real borrowed-owner lifetimes and encourages callers to destroy
  container-owned children.
