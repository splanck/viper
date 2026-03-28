# Plan: Animation State Machine + IK

## Overview
Upgrade the animation system from simple weight-based blending to a proper state machine with transitions, and add basic inverse kinematics.

## 1. Animation State Machine

### Current State
`AnimBlend3D` does per-state weight blending. No transition rules, no layer support, no entry/exit events.

### API
```
AnimStateMachine3D.New(skeleton)
AnimStateMachine3D.AddState(name, animation)
AnimStateMachine3D.AddTransition(fromState, toState, blendDurationMs)
AnimStateMachine3D.SetState(name)              // Trigger transition
AnimStateMachine3D.ForceState(name)            // Instant switch (no blend)
AnimStateMachine3D.Update(dt)
AnimStateMachine3D.CurrentState -> String
AnimStateMachine3D.IsTransitioning -> Boolean
AnimStateMachine3D.GetBoneMatrix(boneIdx) -> Mat4

// Per-state config
AnimStateMachine3D.SetStateSpeed(name, speed)
AnimStateMachine3D.SetStateLooping(name, loop)
```

### Implementation
**File:** New file `src/runtime/graphics/rt_animfsm3d.c`
- State struct: `{ name, animation, speed, loop, current_time }`
- Transition struct: `{ from_state, to_state, blend_duration_ms }`
- On `SetState`: find transition rule (or default 200ms), start crossfade
- During crossfade: decompose TRS, SLERP rotation, lerp position/scale
- When blend completes: switch to target state

## 2. Basic Inverse Kinematics (Two-Bone IK)

### API
```
IK3D.SolveTwoBone(skeleton, animPlayer, rootBone, midBone, endBone, target_vec3, pole_vec3)
```

### Implementation
**File:** New file `src/runtime/graphics/rt_ik3d.c`
Two-bone IK (for arms/legs):
1. Compute chain lengths: `L1 = |mid - root|`, `L2 = |end - mid|`
2. Compute target distance: `D = |target - root|`
3. Use law of cosines to find angles
4. Orient root bone toward target, mid bone by computed angle
5. Apply pole vector to resolve elbow/knee direction

This covers 90% of game IK needs (foot placement, hand reaching, look-at).

## Files Modified
- New: `src/runtime/graphics/rt_animfsm3d.c/h`
- New: `src/runtime/graphics/rt_ik3d.c/h`
- `src/runtime/CMakeLists.txt`
- `src/il/runtime/runtime.def`

## Verification
- State machine: Idle → Walk → Run transitions with smooth crossfade
- Two-bone IK: Character reaches for object — arm bends naturally
- Foot IK: Character on slope — feet match ground angle
