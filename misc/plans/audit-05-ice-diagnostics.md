# Audit Finding #5: Internal Compiler Error Diagnostics

## Problem
5+ sites use `assert()` for internal invariant violations. In release builds, DataflowLiveness non-convergence silently produces wrong code.

## Assert Sites
1. `X64BinaryEncoder.cpp:42` — `assert(reg.isPhys)`
2. `X64BinaryEncoder.cpp:414` — `assert(false && "unhandled MOpcode")`
3. `A64BinaryEncoder.cpp:59` — `assert(op.reg.isPhys)`
4. `AsmEmitter.cpp:859` — `assert(op.reg.isPhys)`
5. `DataflowLiveness.hpp:82-86` — `assert(false && "did not converge"); break;`

## Implementation Plan (1-2 days)

### Step 1: Create reportICE() Utility (1 hour)
Create `src/codegen/common/ICE.hpp`:
```cpp
[[noreturn]] inline void reportICE(const char *file, int line, const std::string &msg) {
    std::cerr << "internal compiler error at " << file << ":" << line << ": " << msg << "\n"
              << "Please report this bug.\n";
    std::abort();
}
#define VIPER_ICE(msg) ::viper::codegen::common::reportICE(__FILE__, __LINE__, (msg))
```

This replaces `assert(false)` with a clean, always-active error report.

### Step 2: Fix DataflowLiveness (CRITICAL, 15 minutes)
`src/codegen/common/ra/DataflowLiveness.hpp:82-86`:
```cpp
// BEFORE (BROKEN in release):
assert(false && "Liveness dataflow did not converge");
break;

// AFTER (always active):
VIPER_ICE("liveness dataflow did not converge after " + std::to_string(maxIter) + " iterations");
```

### Step 3: Replace Encoder Asserts (1-2 hours)
For each encoder assert site, replace with VIPER_ICE providing instruction context:
```cpp
// BEFORE:
assert(reg.isPhys && "virtual register reached binary encoder");

// AFTER:
if (!reg.isPhys) VIPER_ICE("virtual register v" + std::to_string(reg.idOrPhys) + " reached binary encoder");
```

### Step 4: Audit for Other assert(false) Sites (1 hour)
Grep for `assert(false` across all codegen files. Replace any that guard against internal invariant violations with VIPER_ICE.

### Files to Modify
- `src/codegen/common/ICE.hpp` (new)
- `src/codegen/common/ra/DataflowLiveness.hpp:82-86`
- `src/codegen/x86_64/binenc/X64BinaryEncoder.cpp:42,414`
- `src/codegen/aarch64/binenc/A64BinaryEncoder.cpp:59`
- `src/codegen/aarch64/AsmEmitter.cpp:859`
- `src/codegen/aarch64/generated/OpcodeDispatch.inc:28` (regenerate)

### Verification
1. `./scripts/build_viper.sh` — all tests pass in both Debug and Release
2. Feed deliberately broken MIR to encoder — verify clean error message (not raw assert)
3. Build in Release mode — verify liveness non-convergence still errors (not silent)
