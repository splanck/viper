---
status: active
audience: contributors
last-verified: 2026-07-13
---

# ADR 0102: Graphics3D Runtime Boundary and Contract Manifest

Date: 2026-07-13

## Status

Accepted

## Context

The Graphics3D and Game3D implementation is written in C and exposes thousands
of `rt_*` entry points to the VM bridge. The language-facing API, however, is
the canonical registry rooted at `src/il/runtime/runtime.def`. The two surfaces
previously lacked an explicit boundary decision: the live API dump listed
canonical names and compact signatures, but omitted the backing C symbols and
inferred several ownership, nullability, and failure contracts as `unknown`.

Treating every C declaration as a stable SDK ABI would freeze private object
layouts and implementation-only helpers. Treating the C layer as entirely
undocumented would make registry-to-runtime binding drift hard to audit. The
3D runtime review also found one private Body3D kinematics layout in a public C
header and identified screenshot readback as an allocation-sensitive API that
needed a reusable-buffer form.

This is a runtime C ABI surface and cross-layer registry change, so an ADR is
required by the repository policy.

## Decision

- The canonical public programming boundary is the runtime registry. Its
  fully-qualified names, compact signatures, classes, properties, methods,
  ownership contracts, and failure contracts define the public Graphics3D and
  Game3D API.
- The `rt_*` C functions form an internal embedding ABI used by generated code,
  the VM bridge, tests, and Viper-owned adapters. They are not a separately
  versioned C SDK. Runtime object pointers remain opaque handles; private object
  layouts are not part of that ABI.
- `viper --dump-runtime-api` schema version 4 declares
  `public_boundary: "registry"` and
  `c_abi_status: "internal-embedding"`. Every registry function reports its
  backing `c_symbol`. Non-empty constructors, property accessors, and methods
  report their resolved C symbols as well.
- Graphics3D and Game3D rows use an explicit
  `contract_source: "three-d-boundary-policy"`:
  - primitive results have value ownership and are non-null;
  - `void` has no ownership;
  - raw pointers are borrowed and conservatively nullable;
  - runtime objects, strings, and sequences are managed results; object and raw
    pointer results are conservatively nullable where the compact signature
    cannot express the distinction, while strings and sequences follow their
    non-null empty-value contracts;
  - boolean `Try*` operations report status fallibility;
  - nullable object/pointer results report nullable fallibility.
- The live dump is the readable complete manifest. A deterministic contract
  test fingerprints every 3D function name, signature, and C symbol plus every
  class constructor, property accessor, and method binding. Function, class,
  property, and method counts are guarded alongside the fingerprint. Any drift
  requires deliberate ABI review and a test update.
- Public C headers keep runtime object payloads opaque (`void *` handles).
  Explicit POD option/descriptor types may remain where they are part of an
  internal embedding call contract, but object layouts are never exposed. The
  Body3D kinematics view moves to an internal header shared solely by physics
  implementation modules.
- Canvas3D adds `TryCopyScreenshotTo(Pixels)` and
  `TryCopyScreenshotFinalTo(Pixels)`. These status-returning methods reuse the
  caller's same-size `Pixels` object and a canvas-owned GPU staging buffer.
  Existing `Screenshot` methods remain compatible managed-allocation forms.
- The internal Graphics3D commit queue uses a status-returning concurrent-queue
  enqueue. Allocation failure or a close race leaves payload ownership with the
  producer, allowing async streaming to use an allocation-free emergency
  handoff instead of trapping or leaking a retained stream.
- Collider mutation advances an internal process-wide geometry epoch. Physics
  worlds and scenes use it only to invalidate runtime-owned acceleration
  structures after in-place shape changes; it is not part of the frontend API.

## Compatibility and ABI Impact

The schema bump is additive. Existing canonical names and compact signatures
remain unchanged except for the two new Canvas3D methods. Consumers that only
read schema-v3 fields continue to find those fields. Strict consumers that
reject unknown schema versions must opt into schema v4.

The C-symbol fields document live bindings but do not promise independent C SDK
stability. Moving the kinematics structure is source-incompatible only for code
that depended on a private layout through a public runtime header; such code was
already outside the supported opaque-handle contract. Runtime object layouts
and calling conventions do not change.

## Consequences

- Tools can audit a canonical entry all the way to its compiled C symbol
  without scraping headers.
- Ownership, nullability, and failure behavior are explicit for the entire 3D
  surface instead of inferred as unknown.
- Legitimate 3D API changes must update the registry, generated docs, ADR when
  required, and the reviewed manifest fingerprint together.
- The manifest hash is intentionally sensitive to order and spelling. This
  creates a small maintenance cost in exchange for complete drift detection.
- C embedders may continue using Viper-owned `rt_*` declarations, but cannot
  rely on private struct layouts or independent semantic-version guarantees.

## Alternatives

- Declare the complete C header set a stable public SDK. Rejected because it
  would expose internal layouts, prevent implementation evolution, and create a
  second public surface beside the canonical registry.
- Omit C symbols from the API dump. Rejected because signature and binding
  drift would remain invisible to registry consumers and review tooling.
- Maintain a second handwritten JSON ABI file. Rejected because it would
  duplicate the canonical registry and could drift from the binary.
- Make all object returns non-null. Rejected because lookup, capture, invalid
  handle, resource, and backend failure paths legitimately produce no object.

## Spec Impact

This changes the runtime API dump schema, the runtime descriptor metadata, the
internal C embedding surface, and the Graphics3D public method set. It does not
change IL grammar, opcodes, verifier rules, or runtime object layout.
