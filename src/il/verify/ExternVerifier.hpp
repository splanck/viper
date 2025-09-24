// File: src/il/verify/ExternVerifier.hpp
// Purpose: Declares the module-level verifier for extern declarations.
// Key invariants: Collected extern descriptors remain valid while the source module lives.
// Ownership/Lifetime: Stores pointers into the provided module; does not own declarations.
// Links: docs/il-guide.md#reference
#pragma once

#include "il/verify/DiagSink.hpp"

#include "support/diag_expected.hpp"

#include <string>
#include <unordered_map>

namespace il::core
{
struct Module;
struct Extern;
} // namespace il::core

namespace il::verify
{

/// @brief Validates extern declarations and records them for downstream passes.
class ExternVerifier
{
  public:
    using ExternMap = std::unordered_map<std::string, const il::core::Extern *>;

    /// @brief Access verified extern descriptors keyed by symbol name.
    [[nodiscard]] const ExternMap &externs() const
    {
        return externs_;
    }

    /// @brief Verify extern declarations in @p module and populate the lookup map.
    /// @param module Module whose extern table should be validated.
    /// @param sink Diagnostic sink receiving advisory messages (currently unused).
    /// @return Empty on success; diagnostic describing the first failure otherwise.
    il::support::Expected<void> run(const il::core::Module &module, DiagSink &sink);

  private:
    ExternMap externs_;
};

} // namespace il::verify
