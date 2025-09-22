// File: src/il/verify/Verifier.hpp
// Purpose: Declares IL verifier that checks modules.
// Key invariants: None.
// Ownership/Lifetime: Verifier does not own modules.
// Links: docs/il-spec.md
#pragma once

#include "support/diag_expected.hpp"

namespace il::core
{
struct Module;
}

namespace il::verify
{

/// @brief Verifies structural and type rules for a module.
class Verifier
{
  public:
    /// @brief Verify module @p m against the IL specification.
    /// @param m Module to verify.
    /// @return Expected success or diagnostic on failure.
    static il::support::Expected<void> verify(const il::core::Module &m);
};

} // namespace il::verify
