// File: src/runtime/rt_error.c
// Purpose: Provides shared constants for the runtime error model.
// Key invariants: Exposes RT_ERROR_NONE as canonical success value.
// Ownership/Lifetime: None; values are static storage duration.
// Links: docs/codemap.md

#include "rt_error.h"

#ifdef __cplusplus
extern "C" {
#endif

    /// @brief Canonical success error record.
    const RtError RT_ERROR_NONE = { Err_None, 0 };

    /// @brief Sign-extend a 32-bit runtime error code to 64 bits.
    /// @param code Error code produced by helpers returning int32_t.
    /// @return Sign-extended 64-bit representation of @p code.
    int64_t rt_err_i32_to_i64(int32_t code)
    {
        return (int64_t)code;
    }

#ifdef __cplusplus
}
#endif

