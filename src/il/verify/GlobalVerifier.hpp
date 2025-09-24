// File: src/il/verify/GlobalVerifier.hpp
// Purpose: Declares verification utilities for module global declarations.
// Key invariants: Global names must be unique; pointers remain valid for module lifetime.
// Ownership/Lifetime: Stores pointers back into the inspected module only for lookup reuse.
// Links: docs/il-guide.md#reference
#pragma once

#include "il/verify/DiagSink.hpp"

#include "support/diag_expected.hpp"

#include <string>
#include <unordered_map>

namespace il::core
{
struct Module;
struct Global;
} // namespace il::core

namespace il::verify
{

/// @brief Ensures module global declarations obey uniqueness rules.
class GlobalVerifier
{
  public:
    using GlobalMap = std::unordered_map<std::string, const il::core::Global *>;

    /// @brief Access the verified global lookup table.
    [[nodiscard]] const GlobalMap &globals() const
    {
        return globals_;
    }

    /// @brief Verify globals in @p module and populate the lookup table.
    /// @param module Module to inspect.
    /// @param sink Diagnostic sink receiving advisory output (currently unused).
    /// @return Empty Expected on success; diagnostic payload when verification fails.
    il::support::Expected<void> run(const il::core::Module &module, DiagSink &sink);

  private:
    GlobalMap globals_;
};

} // namespace il::verify
