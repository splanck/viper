---
status: active
audience: contributors
last-verified: 2026-07-21
---

# ADR 0147: Make Managed Reference Ownership Explicit Across Lowering and Native Codegen

## Status

Accepted (2026-07-21).

## Context

Ashfall's frame time degraded from smooth to unplayable within seconds even
though its enemy count and draw workload did not grow. A 7,200-frame sustained
combat probe found thousands of additional tracked objects and roughly 929 MiB
of resident memory. Allocation-stack and IL inspection showed that this was not
one game-level leak: ownership facts were being lost independently at Zia
lexical scopes, control-flow merges, runtime object-returning calls, and native
string lowering.

The runtime ownership catalog already describes consumed arguments and owned
results, but the frontend used it only for a subset of string calls. Pointer
results from math, physics, navigation, animation, and Game3D APIs could
therefore remain alive until process exit. Zia also had no complete convention
for owning object slots, managed parameters and returns, or temporaries that
exist on only one CFG edge.

The AArch64 and x86-64 backends defensively retained every string call result
and many string loads. That defense was necessary for unannotated IL, but it
manufactured a second reference after the frontend had already established and
balanced ownership. Because native register teardown does not release managed
values the way the VM does, those extra references accumulated permanently.

This is a cross-layer dependency between Zia lowering, runtime ownership
metadata, the IL ownership verifier, the VM/native ownership convention, and
both native backends. It does not change IL v0.3.0 syntax, opcodes, serialized
format, or the public runtime C ABI, but it refines release-state verification
and establishes a shared convention that those layers must not let drift.

## Decision

### Zia managed-value convention

Zia lowering makes source-level ownership explicit in IL:

- A managed local slot owns exactly one reference to its non-null contents.
  This applies to mutable and immutable bindings. Initialization and assignment
  move a deferred owned temporary when one exists; otherwise they retain a
  borrowed value. Assignment releases the displaced value.
- Managed parameters are borrowed at function entry. The callee retains each
  parameter into its owning local slot and releases that slot on every exit.
- Class, interface, collection, `Any`, and instantiated type-parameter slots
  are managed owners. Automatic object cleanup uses
  `rt_obj_release_check0`: it decrements heap objects and string handles but is
  a no-op for opaque runtime handles such as GUI widgets. Strict unsafe-memory
  release APIs keep their existing invalid-handle traps and return-count
  semantics.
- Managed function and method returns transfer exactly one reference to the
  caller. The caller either moves that reference into another owner, passes it
  to a consuming operation, or schedules one statement-boundary release.
- A struct returned across a call boundary is an owned heap box. Materializing
  it copies the value to caller stack storage, retaining managed fields, and
  schedules the transferred box for release. `Optional<Struct>` keeps the owned
  box until the optional value itself is consumed or released.
- Managed temporaries created on a conditional or short-circuit edge are
  released before leaving that edge. Ternary, value-`if`, coalesce, and optional
  expressions merge managed results through an owning slot.
- Taking a managed merge result is spelled as an immediate `load` followed by a
  raw null `store` to the same slot. This is an ownership move: it transfers the
  slot's reference without decrementing it.
- Lexical cleanup releases every managed slot introduced by that exact scope,
  including shadowed bindings. `break`, `continue`, and `return` release the
  iteration and lexical owners they exit. Internal `for` iterables and managed
  element bindings follow the same rules.
- A runtime argument marked consumed removes the matching temporary from
  deferred cleanup. `const_str` produces an owned string temporary.
- A weak-field store transfers the newly allocated weak-reference handle into
  the field instead of leaving it in statement cleanup. Zia-created Queue and
  Stack instances opt into owning their boxed elements so statement cleanup
  cannot leave dangling container entries; their low-level C constructors keep
  their borrowing defaults.

For direct IL-defined calls, parameters borrow caller references unless the
source lowering explicitly creates or transfers another reference. Registered
runtime calls override that convention only through
`RuntimeOwnershipEffects`: `consumedArgMask`, `retainedArgMask`,
`ownedOutArgMask`, and `returnsOwned`.

### Runtime owned-result classification

`classifyRuntimeOwnership` is the authority for pointer-returning runtime calls.
An object result is automatically cleaned up only when the catalog explicitly
marks it owned. This change classifies the allocating operations used by the 3D
game stack, including:

- immutable Vec2, Vec3, Mat3, Mat4, Quat, and Spline operations;
- explicitly enumerated Graphics3D and Game3D value snapshots;
- PhysicsWorld3D ray, sweep, overlap, ledge, and vault query results; and
- Game3D spatial-audio source creation.

Classification is deliberately operation-specific. Nearby accessors that
return borrowed children or stored values, such as hit-list elements and
controller offsets, remain unowned. Both public qualified names and C symbols
carry the same ownership facts.

### Native string retain elision

AArch64 and x86-64 apply the same conservative policy:

- A defensive retain on a direct string call result is elided when the callee
  transfers a reference. Every IL-defined direct function follows the transfer
  return convention; a registered runtime helper must have `returnsOwned`.
  Indirect calls and unclassified registered runtime results keep the retain.
- A defensive string-load retain may be elided only for one of four structural
  proofs: the sole use is an explicit release; all uses are borrow-only direct
  calls in the same block and no intervening string store can invalidate the
  source slot; the frontend emits an explicit retain immediately after the
  load; or the load is immediately followed by the same-slot null store that
  marks a managed-slot take.
- An explicit retain in a different block is not a proof because it need not
  dominate every use. A consuming runtime argument is never treated as a
  borrow. Any unproven case keeps the defensive retain.

`ZANNA_NO_RETAIN_ELIDE=1` remains a diagnostic compatibility hatch that restores
the conservative native retains. It is not a language or runtime feature flag.

### Verifier release-state rule

Release-state dataflow tracks dynamic values represented by SSA result IDs.
When control flow returns to a loop block, executing a result-producing
instruction creates a fresh dynamic value and therefore kills any incoming
"released" fact for that result ID after its operands are checked. This applies
to every instruction classification, including calls that simultaneously
retain an argument and return a fresh owned result such as `Box.Str`.

The rule does not permit an ordinary use after release or double release: the
fresh definition must execute before the later use. Destructor dispatch and
`rt_obj_free` remain the allowed finalization sequence following a successful
`rt_obj_release_check0`.

## Consequences

- Long-running Zia programs have deterministic managed cleanup across lexical,
  call, and control-flow boundaries instead of depending on process teardown.
- The frontend, AArch64, and x86-64 now share one transfer/borrow convention.
  New backends must implement the same convention or retain conservatively.
- New allocating runtime operations must be added to the central ownership
  catalog. Object-shaped return types alone are insufficient evidence because
  borrowed object accessors remain valid and common.
- Existing IL source and binaries remain compatible. There is no grammar,
  opcode, serialized-format, or public runtime ABI change. The verifier now
  accepts valid loop-local redefinitions that were previously rejected when a
  retained-argument call also produced the fresh result.
- The sustained Ashfall workload holds its tracked-object baseline constant
  through 36,000 frames, with stable frame cost. Heap checkpoints separated by
  roughly 26,000 frames differ by less than 100 KiB; the remaining delta is
  platform graphics/dispatch bookkeeping rather than Zanna frame allocations.

## Alternatives Considered

- **Patch Ashfall to reuse or manually release each temporary.** Rejected
  because the same leaks appeared in ordinary language expressions and runtime
  calls; game-level workarounds would hide compiler/runtime contract defects.
- **Run cycle collection every frame.** Rejected because these were live
  reference-count leaks, not unreachable cycles, and forced collection would
  add frame-time cost without removing the extra native references.
- **Remove every backend defensive retain.** Rejected because indirect,
  consuming, and unclassified calls still need conservative protection.
- **Infer ownership from pointer return types or getter naming.** Rejected
  because the runtime intentionally mixes fresh value snapshots with borrowed
  children and stored values that share the same IL representation.
