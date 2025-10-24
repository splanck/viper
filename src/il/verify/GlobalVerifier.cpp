//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Implements the verifier responsible for tracking module-level global
// declarations.  The translation unit builds and maintains a map from global
// names to their definitions so duplicate declarations can be diagnosed quickly
// while giving other passes a cheap lookup structure.
//
//===----------------------------------------------------------------------===//
//
/// @file
/// @brief Verification helpers for enforcing global declaration uniqueness.
/// @details The @ref GlobalVerifier caches pointers to module-owned globals in a
///          dense lookup table.  Subsequent queries avoid repeated scans of the
///          module vector while ensuring every symbol is unique within the
///          translation unit.

#include "il/verify/GlobalVerifier.hpp"

#include "il/core/Global.hpp"
#include "il/core/Module.hpp"

using namespace il::core;

namespace il::verify
{
namespace
{
using il::support::Expected;
using il::support::makeError;
} // namespace

/// @brief Expose the cached map from global names to module-owned definitions.
/// @details The verifier records raw pointers to the immutable @ref il::core::Global
///          instances stored inside the module so downstream passes can perform
///          O(1) lookups without rebuilding the index.  The returned reference is
///          valid for the lifetime of the verifier instance.
[[nodiscard]] const GlobalVerifier::GlobalMap &GlobalVerifier::globals() const
{
    return globals_;
}

/// @brief Populate the lookup map and detect duplicate declarations.
/// @details Clears any previous state, iterates over every global declared in the
///          module, and inserts its address into @ref globals_.  Duplicate names
///          trigger an error result containing a diagnostic for the caller to
///          report via the supplied sink.
/// @param module Module whose globals should be indexed.
/// @param sink Diagnostic sink provided by the caller for reporting duplicates.
/// @returns Empty result on success or a populated Expected containing the error.
Expected<void> GlobalVerifier::run(const Module &module, [[maybe_unused]] DiagSink &sink)
{
    globals_.clear();

    for (const auto &global : module.globals)
    {
        if (!globals_.emplace(global.name, &global).second)
            return Expected<void>{makeError({}, "duplicate global @" + global.name)};
    }

    return {};
}

} // namespace il::verify
