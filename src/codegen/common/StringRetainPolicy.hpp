//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: codegen/common/StringRetainPolicy.hpp
// Purpose: Decide when backends may skip the defensive rt_str_retain_maybe on
//          a string-typed call result.
// Key invariants:
//   - Elision is legal only when the callee is known to transfer an owned
//     reference (classifyRuntimeOwnership returnsOwned); the transferred +1
//     already covers the frontends' single balancing release/consume.
//   - Unknown, indirect, and user-defined callees keep the retain.
//   - Load-path retains are never elided here: they carry the ownership
//     transfer for slot-resident strings (consuming callees, returns).
// Ownership/Lifetime: Header-only policy; no state beyond the env hatch.
// Links: docs/il-passes.md, src/il/runtime/RuntimeOwnership.hpp
//
//===----------------------------------------------------------------------===//
#pragma once

#include "il/runtime/RuntimeOwnership.hpp"
#include "il/runtime/RuntimeSignatures.hpp"

#include <cstdlib>
#include <string_view>

namespace viper::codegen {

/// @brief True when the backend may skip the defensive retain on the string
///        result of a call to @p callee.
/// @details The retain exists to mirror the VM's "every register def owns a
///          reference" model without its balancing frame-teardown release, so
///          on every balanced frontend path it is a pure leak. When the callee
///          is classified as transferring ownership of its result, the
///          transferred reference already keeps the value alive until the
///          frontend's release/consume, and the retain can be dropped.
///          `VIPER_NO_RETAIN_ELIDE=1` restores the unconditional retain.
[[nodiscard]] inline bool shouldElideStringResultRetain(std::string_view callee) {
    if (callee.empty())
        return false;
    if (std::getenv("VIPER_NO_RETAIN_ELIDE") != nullptr)
        return false;
    // Registered runtime helpers transfer when classified returnsOwned.
    // Everything else is an IL-defined function (module or cross-module),
    // and the IL string return convention is transfer-everywhere: frontends
    // mint the caller's owned reference on every return path and the VM's
    // return-retain enforces the same. Indirect calls (empty callee) keep
    // the retain above.
    if (il::runtime::findRuntimeSignature(callee) == nullptr)
        return true;
    return il::runtime::classifyRuntimeOwnership(callee).returnsOwned;
}

/// @brief True when @p callee is an explicit string release entry point.
[[nodiscard]] inline bool isStringReleaseCallee(std::string_view callee) {
    return callee == "rt_str_release_maybe" || callee == "rt_str_release" ||
           callee == "rt_memory_release_str" || callee == "Viper.String.ReleaseMaybe" ||
           callee == "Viper.Memory.ReleaseStr" || callee == "Viper.Runtime.Unsafe.ReleaseStr";
}

/// @brief True when the load-path retain for a string load may be skipped.
/// @details The defensive load retain exists so consuming callees and
///          explicit releases can spend a reference without destroying the
///          slot's own one. When the loaded value's ONLY use is an explicit
///          release call, the retain+release pair is a native no-op that
///          silently keeps the displaced value alive forever; skipping the
///          retain lets the frontend's release actually free it. Any other
///          use keeps the retain.
[[nodiscard]] inline bool shouldElideLoadRetainForRelease() {
    return std::getenv("VIPER_NO_RETAIN_ELIDE") == nullptr;
}

} // namespace viper::codegen
