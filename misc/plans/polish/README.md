# Viper Platform Polish — Implementation Plans

Plans to address all stubbed, incomplete, and basic implementations identified
during the comprehensive platform audit (2026-03-29). Validated against actual
codebase on 2026-03-29.

## Status After Validation

- **13 actionable plans** (2 removed: Plan 14 already implemented behind #ifdef,
  Plan 15 Windows SafeI64 already complete)
- Plans renumbered and corrected with exact file:line references

## Implementation Phases

```
Phase 1 — Native EH (Platform Credibility):
  01-aarch64-native-eh.md          [L]  setjmp/longjmp EH for AArch64
  02-trap-message-forwarding.md    [S]  Forward throw messages in native code
  03-error-field-extraction.md     [M]  ErrGetKind/Code via runtime bridge + TLS

Phase 2 — Codegen Stability:
  04-regpool-graceful-spill.md     [M]  Graceful fallback on register exhaustion
  05-lowering-diagnostics.md       [S]  Add warnings to 41 silent return-false paths
  06-aarch64-loop-hoist-fix.md     [L]  Fix hoistLoopConstants — needs dominator analysis

Phase 3 — Codegen Performance:
  07-spill-slot-reuse.md           [M]  Fix cross-block liveness for slot sharing
  08-dead-fp-store-ordering.md     [S]  Renumber pass 4.85 → 1.8 (before pass 1.9)
  09-m-series-scheduler.md         [S]  Fix FP divide latency (3→10 cycles) + others

Phase 4 — Debug & Diagnostics:
  10-dwarf-debug-info.md           [L]  Complete DWARF v5 (Phase 1 only: CU + function DIEs)

Phase 5 — Runtime Gaps:
  11-http-keepalive.md             [L]  HTTP/1.1 keep-alive — requires thread model refactor
  12-aes-timing-hardening.md       [M]  ARM Crypto Extensions for constant-time AES
  13-keyboard-i18n.md              [S]  UCKeyTranslate on macOS for non-US keyboards
```

## Removed Plans (Already Complete)

- ~~14-gui-widget-stubs.md~~ — All 7 functions ARE implemented behind
  `#ifdef VIPER_ENABLE_GRAPHICS`. The "stubs" are only in the `#else` path
  for no-graphics builds. No work needed.

- ~~15-windows-safe-i64.md~~ — Windows SafeI64 IS fully implemented using
  `InterlockedExchange64`/`InterlockedCompareExchange64` at `rt_safe_i64.c:39-96`.
  The comment at line 19 ("Win32 support not yet implemented") is stale.
  **Action: Update the stale comment only.**

## Summary

| Phase | Plans | Complexity | Focus |
|-------|-------|-----------|-------|
| 1 | 3 | 1L + 1M + 1S | Native exception handling |
| 2 | 3 | 1L + 1M + 1S | Codegen robustness |
| 3 | 3 | 1M + 2S | Codegen performance |
| 4 | 1 | 1L | Debug experience |
| 5 | 3 | 1L + 1M + 1S | Runtime quality |
| **Total** | **13** | **4L + 3M + 6S** | |

## Documentation Updates Required

Each plan should update the following docs when implemented:
- `/docs/release_notes/Viper_Release_Notes_0_2_4.md` — Add to release notes
- `/docs/codemap.md` — Update if new files added
- `/misc/plans/zia_bugs_20260329.md` — Cross-reference resolved items
- `/docs/zia-reference.md` — Update if language behavior changes
