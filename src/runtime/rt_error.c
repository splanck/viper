//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Declares the canonical success sentinel used by the runtime error reporting
// infrastructure.  Centralising the definition ensures both the VM and native
// runtimes share a single representation, avoiding discrepancies when checking
// for the absence of errors.  The constants in this translation unit live in
// static storage and therefore never require explicit initialisation by
// embedding applications.
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Provides the canonical success @ref RtError instance.
/// @details Runtime subsystems treat @ref RT_ERROR_NONE as a universal "no
///          error" token.  Defining it out-of-line guarantees a single storage
///          location even when multiple components include @ref rt_error.h.

#include "rt_error.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /// @brief Canonical success error record shared across the runtime.
    /// @details Initialises the discriminant to @ref Err_None and clears the
    ///          auxiliary payload.  Because the object resides in static
    ///          storage, every consumer observes the same address when checking
    ///          for pointer identity or performing atomic replacements.
    const RtError RT_ERROR_NONE = {Err_None, 0};

#ifdef __cplusplus
}
#endif
