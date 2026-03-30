# POLISH-09: Apple M-Series Scheduler Latency Tuning

## Context
**Validated complete latency table** at `SchedulerPass.cpp:50-88`:

| Current | Opcodes | Cycles |
|---------|---------|--------|
| Loads (L1) | LdrRegFpImm, LdrRegBaseImm, LdrFprFpImm, LdpRegFpImm, etc. | 4 |
| Int mul/div | MulRRR, SDivRRR, UDivRRR, MSubRRRR, MAddRRRR | 3 |
| FP arith | FAddRRR, FSubRRR, **FMulRRR, FDivRRR** | **3** |
| Conversions | SCvtF, FCvtZS, UCvtF, FCvtZU | 3 |
| Everything else | default | 1 |

**Critical finding:** FP divide (`FDivRRR`) is at 3 cycles — the same as FP add.
Real Apple M1 FP divide is **10-15 cycles**. Similarly, integer divide (`SDivRRR`)
is at 3 cycles but is actually **7-12 cycles** on M1.

The scheduler algorithm is greedy list scheduling with critical-path
prioritization. It runs AFTER register allocation on physical registers.

**Complexity: S** | **Priority: P3**

## Design

Update `instrLatency()` at `SchedulerPass.cpp:50-88`:

```cpp
static int instrLatency(MOpcode opc) {
    switch (opc) {
        // Loads: L1 hit ~4 cycles (keep current)
        case MOpcode::LdrRegFpImm:
        case MOpcode::LdrRegBaseImm:
        case MOpcode::LdrFprFpImm:
        case MOpcode::LdrFprBaseImm:
        case MOpcode::LdpRegFpImm:
        case MOpcode::LdpFprFpImm:
            return 4;

        // Integer multiply: 3 cycles (correct for M1)
        case MOpcode::MulRRR:
        case MOpcode::MAddRRRR:
        case MOpcode::MSubRRRR:
            return 3;

        // Integer divide: 7 cycles (was 3; M1 is 7-12 cycles)
        case MOpcode::SDivRRR:
        case MOpcode::UDivRRR:
            return 7;

        // FP add/sub: 3 cycles (correct for M1)
        case MOpcode::FAddRRR:
        case MOpcode::FSubRRR:
            return 3;

        // FP multiply: 4 cycles (was 3; M1 is 4 cycles)
        case MOpcode::FMulRRR:
            return 4;

        // FP divide: 10 cycles (was 3; M1 is 10-15 cycles)
        case MOpcode::FDivRRR:
            return 10;

        // Conversions: 4 cycles (was 3)
        case MOpcode::SCvtF:
        case MOpcode::FCvtZS:
        case MOpcode::UCvtF:
        case MOpcode::FCvtZU:
            return 4;

        default:
            return 1;
    }
}
```

### Key Changes
| Operation | Was | Now | Real M1 |
|-----------|-----|-----|---------|
| Integer divide | 3 | **7** | 7-12 |
| FP multiply | 3 | **4** | 4 |
| FP divide | 3 | **10** | 10-15 |
| Conversions | 3 | **4** | 3-4 |

### Files to Modify

| File | Change |
|------|--------|
| `src/codegen/aarch64/passes/SchedulerPass.cpp:50-88` | Update latency values |

## Documentation Updates
- `docs/release_notes/Viper_Release_Notes_0_2_4.md`
- `docs/codegen/aarch64.md` — Document Apple M-series latency model

## Verification
Run benchmarks with FP divide and integer divide heavy code. Verify scheduler
produces better instruction ordering (fewer pipeline stalls).
