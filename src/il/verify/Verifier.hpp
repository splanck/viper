// File: src/il/verify/Verifier.hpp
// Purpose: Declares IL verifier that checks modules.
// Key invariants: None.
// Ownership/Lifetime: Verifier does not own modules.
// Links: docs/il-spec.md
#pragma once
#include "il/core/Module.hpp"
#include <ostream>

namespace il::verify
{

/// @brief Verifies structural and type rules for a module.
class Verifier
{
  public:
    /// @brief Verify module @p m against the IL specification.
    /// @param m Module to verify.
    /// @param err Stream receiving diagnostic messages.
    /// @return True if verification succeeds; false otherwise.
    static bool verify(const il::core::Module &m, std::ostream &err);
};

} // namespace il::verify
