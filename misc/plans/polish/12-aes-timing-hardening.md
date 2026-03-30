# POLISH-12: Constant-Time AES via ARM Crypto Extensions

## Context
**Validated:** `rt_aes.c:18-20` (in `src/runtime/text/rt_aes.c`) states:
```
// This implementation is NOT hardened against cache-timing attacks; do not
// use in contexts requiring constant-time cryptographic guarantees.
```

**Validated:** NO hardware crypto acceleration used anywhere in the runtime.
No references to `arm_neon.h`, `CommonCrypto`, `wmmintrin.h`, or any
`CRYPTO_HARDWARE` feature flags.

The file is ~1000 LOC of pure C T-table AES. Table lookups are cache-line
dependent, leaking key material via timing side channels.

**Complexity: M** | **Priority: P3**

## Design

### Apple Silicon: ARM Crypto Extensions (Always Available)

All Apple Silicon (M1+) has ARMv8 Crypto Extensions. Use NEON intrinsics:

```c
#include <arm_neon.h>

static inline uint8x16_t aes_enc_round(uint8x16_t state, uint8x16_t key) {
    state = vaeseq_u8(state, key);   // AES single-round encrypt
    state = vaesmcq_u8(state);        // MixColumns
    return state;
}

static inline uint8x16_t aes_dec_round(uint8x16_t state, uint8x16_t key) {
    state = vaesdq_u8(state, key);   // AES single-round decrypt
    state = vaesimcq_u8(state);       // Inverse MixColumns
    return state;
}
```

### Detection
```c
#if defined(__aarch64__) && defined(__ARM_FEATURE_CRYPTO)
    #define RT_HAS_AES_HW 1
#elif defined(__x86_64__) && defined(__AES__)
    #define RT_HAS_AES_HW 1
#else
    #define RT_HAS_AES_HW 0
#endif
```

### Implementation Strategy
1. Add `rt_aes_hw.c` with ARM Crypto / AES-NI implementations
2. In `rt_aes.c`, dispatch to HW path when available
3. Keep existing T-table as fallback for platforms without HW
4. Add `RT_HAS_AES_HW` to `rt_platform.h`

### Files to Modify

| File | Change |
|------|--------|
| NEW: `src/runtime/text/rt_aes_hw.c` | ARM Crypto Extensions + AES-NI |
| `src/runtime/text/rt_aes.c` | Dispatch to HW path |
| `src/runtime/core/rt_platform.h` | Add `RT_HAS_AES_HW` macro |
| `src/runtime/CMakeLists.txt` | Compile rt_aes_hw.c with `-march=armv8-a+crypto` |

## Documentation Updates
- `docs/release_notes/Viper_Release_Notes_0_2_4.md`
- `docs/viperlib/crypto.md` — Note hardware acceleration

## Verification
Run NIST FIPS-197 test vectors through both HW and SW paths.
Benchmark: HW path should be 5-20x faster.
