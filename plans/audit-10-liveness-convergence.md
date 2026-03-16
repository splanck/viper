# Audit Finding #10: Liveness Non-Convergence Silent Miscompilation

## Problem
`DataflowLiveness.hpp:82-86`: `assert(false); break;` — in release builds, assert is stripped and loop exits with incomplete liveness data, silently producing wrong register allocations.

## Implementation Plan (1 hour)

### The Fix
Replace the assert-guarded break with an unconditional error:

**File:** `src/codegen/common/ra/DataflowLiveness.hpp:82-86`

```cpp
// BEFORE (BROKEN in release):
if (++iteration > maxIter)
{
    assert(false && "Liveness dataflow did not converge");
    break;
}

// AFTER (always safe):
if (++iteration > maxIter)
{
    // This MUST be unconditional — in release builds, assert() is stripped
    // and the break would silently produce wrong liveness data, causing
    // the register allocator to miscompile.
    VIPER_ICE("liveness dataflow did not converge after " +
              std::to_string(maxIter) + " iterations");
}
```

Where `VIPER_ICE` is the macro from Finding #5 (`codegen/common/ICE.hpp`). If Finding #5 hasn't been implemented yet, use:
```cpp
std::cerr << "internal compiler error: liveness dataflow did not converge\n";
std::abort();
```

### Dependency
- If done standalone: inline the error (no dependency)
- If done after Finding #5: use `VIPER_ICE` macro from `codegen/common/ICE.hpp`

### Files to Modify
- `src/codegen/common/ra/DataflowLiveness.hpp:82-86`

### Verification
1. `./scripts/build_viper.sh` — all tests pass in both Debug AND Release
2. Create test with pathological CFG approaching maxIter — verify it converges normally
3. Temporarily set maxIter=1, verify clean error message (not silent wrong code)
4. Build in Release mode — grep binary for "did not converge" string to confirm it's present
