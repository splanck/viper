# Tracked waivers

Canonical waiver location for plan items that cannot be fully automated/closed in
a given pass. Waivers name the gap, reason, owner, fallback coverage, and re-open
condition. Includes re-waivers carried from `3Dnextlevel` (see `../carryover.md`).

| ID | Gap | Reason | Owner | Fallback coverage | Re-open when |
|---|---|---|---|---|---|
| W2-001 | Managed Zia closures cannot drive native callback loops (`run`/`onCollision`/`onUpdate`/streaming callbacks) | Inherited W-001: no VM callback trampoline for managed function objects | Runtime/VM | Manual `tick`/`stepSimulation`/frame loop, `runFramesOnly`, pollable event/handle buffers (authoritative) | A VM callback trampoline lands (carryover CO-2 / D-CB) |
| W2-002 | Cross-platform GPU interactive-framerate proof is not in local automated run | Inherited W-002: GPU timing depends on hardware/backend availability | Graphics CI | Release macOS Metal smoke + Metal `perf_probe.zia`; software correctness lane | GPU smoke/timing lanes exist on Windows/Linux reference hardware |
| W2-003 | Windows/Linux Release/reference-hardware software FPS baselines not yet recorded | Local macOS Release baseline is recorded, but this workstation cannot produce Windows/Linux hardware evidence | Graphics perf | Release macOS Apple M4 Max software/Metal baseline + functional software ctests | Windows/Linux Release perf lanes are run on named reference hosts |
| W2-004 | Basis supercompression, Draco, and meshopt are not Phase 12 gates | High-effort from-scratch decoders are not required to prove the small streamed-world architecture | Graphics assets | KTX2/precompressed backend blocks, software RGBA fallback, ordinary glTF meshes, glTF camera/multi-scene tests | Phase 11b/import-depth scope is explicitly approved |

## Notes

- New waivers added during implementation must follow this row shape and link a
  re-open condition; never silently omit coverage.
- Carryover items that are *closed* in Phase C move to `done` in
  `01-phase-progress.md` and are removed from this file; only genuinely-deferred
  carryover items remain here as re-waivers.
