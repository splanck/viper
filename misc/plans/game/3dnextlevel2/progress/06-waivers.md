# Tracked waivers

Canonical waiver location for plan items that cannot be fully automated/closed in
a given pass. Waivers name the gap, reason, owner, fallback coverage, and re-open
condition. Includes re-waivers carried from `3Dnextlevel` (see `../carryover.md`).

| ID | Gap | Reason | Owner | Fallback coverage | Re-open when |
|---|---|---|---|---|---|
| W2-001 | Managed Zia closures cannot drive native callback loops (`run`/`onCollision`/`onUpdate`/streaming callbacks) | Inherited W-001: no VM callback trampoline for managed function objects | Runtime/VM | Manual `tick`/`stepSimulation`/frame loop, `runFramesOnly`, pollable event/handle buffers (authoritative) | A VM callback trampoline lands (carryover CO-2 / D-CB) |
| W2-002 | GPU interactive-framerate proof not in local automated run | Inherited W-002: GPU timing depends on hardware/backend availability | Graphics CI | Software correctness lane + capability-gated GPU smoke | A GPU smoke/timing lane exists on reference hardware |
| W2-003 | Release/reference-hardware software FPS baseline not yet recorded | Inherited W-005: local builds are Debug | Graphics perf | Functional software ctests + Debug-mode measurements | CO-11 Release/reference perf lane is stood up |

## Notes

- New waivers added during implementation must follow this row shape and link a
  re-open condition; never silently omit coverage.
- Carryover items that are *closed* in Phase C move to `done` in
  `01-phase-progress.md` and are removed from this file; only genuinely-deferred
  carryover items remain here as re-waivers.
