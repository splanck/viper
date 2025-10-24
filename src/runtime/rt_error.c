// File: src/runtime/rt_error.c
// Purpose: Provides shared constants for the runtime error model.
// Key invariants: Exposes RT_ERROR_NONE as canonical success value.
// Ownership/Lifetime: None; values are static storage duration.
// Links: docs/codemap.md

#include "rt_error.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /// @brief Canonical success error record.
    const RtError RT_ERROR_NONE = {Err_None, 0};

#ifdef __cplusplus
}
#endif
