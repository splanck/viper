// File: src/il/verify/ModuleVerifier.hpp
// Purpose: Declares verifier for modules.
// Key invariants: None.
// Ownership/Lifetime: Verifier does not own modules.
// Links: docs/il-spec.md
#pragma once

#include "il/core/Module.hpp"
#include <ostream>
#include <unordered_map>

namespace il::verify
{

/// @brief Verifies structural and type rules for a module.
class ModuleVerifier
{
  public:
    /// @brief Verify module @p m against the IL specification.
    /// @param m Module to verify.
    /// @param err Stream receiving diagnostic messages.
    /// @return True if verification succeeds; false otherwise.
    bool verify(const il::core::Module &m, std::ostream &err);

  private:
    bool verifyExterns(const il::core::Module &m,
                       std::ostream &err,
                       std::unordered_map<std::string, const il::core::Extern *> &externs);

    bool verifyGlobals(const il::core::Module &m,
                       std::ostream &err,
                       std::unordered_map<std::string, const il::core::Global *> &globals);
};

} // namespace il::verify
