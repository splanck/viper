---
status: active
audience: contributors
last-verified: 2026-07-16
---

# ADR 0105: CharacterController3D Gravity Magnitude

## Status

Accepted

## Context

`Zanna.Game3D.CharacterController3D` exposes a `gravity` property alongside a
positive `jumpSpeed`. The public documentation describes gravity as a downward
acceleration, but the default runtime value was negative while sample code often
treated the property as a positive downward magnitude. That mismatch made a
positive gravity value accelerate the character upward, so a freshly spawned
walking character could rise away from the ground and drag follow cameras with it.

`Input3D.moveAxis()` also encodes Space/Shift/Ctrl in the Y component for
free-fly controls. Reusing that 3D axis for walking normalized the Y component
with X/Z movement, so holding Shift for sprint reduced horizontal speed and Space
could affect movement independently of the jump edge.

## Decision

`CharacterController3D.gravity` is defined as a non-negative downward magnitude.
`CharacterController3D.update(input, camera, dt)` subtracts that magnitude from
vertical velocity while airborne. Positive Y velocity remains upward movement,
`jumpSpeed` remains a positive upward launch velocity, and ground stickiness
remains a small negative velocity.

Walking movement now derives its X/Z input from W/A/S/D and arrow keys only.
Space remains an edge-triggered jump input, while Shift/Ctrl are ignored by the
walking controller so applications can use them for sprint or other actions.
`Input3D.moveAxis()` keeps its existing free-fly 3D semantics.

## Consequences

Existing code that set a negative gravity value continues to work because the
setter already stores the absolute magnitude. Existing code that set a positive
gravity value now falls downward as documented. Code that depended on positive
gravity launching a walking controller upward was relying on the bug.

The public ABI shape is unchanged. The semantic contract is now aligned with the
documentation and with Game3D samples.
