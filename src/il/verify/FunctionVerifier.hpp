// File: src/il/verify/FunctionVerifier.hpp
// Purpose: Declares verifier for functions.
// Key invariants: None.
// Ownership/Lifetime: Verifier does not own functions.
// Links: docs/il-spec.md
#pragma once

#include "il/core/Extern.hpp"
#include "il/core/Function.hpp"
#include <ostream>
#include <string>
#include <unordered_map>

namespace il::verify
{

/// @brief Verifies structural and type rules for a function.
class FunctionVerifier
{
  public:
    /// @brief Verify function @p fn against the IL specification.
    /// @param fn Function to verify.
    /// @param externs Previously gathered extern signatures for calls.
    /// @param funcs Map of all functions for resolving references.
    /// @param err Stream receiving diagnostic messages.
    /// @return True if the function passes all checks; false otherwise.
    bool verify(const il::core::Function &fn,
                const std::unordered_map<std::string, const il::core::Extern *> &externs,
                const std::unordered_map<std::string, const il::core::Function *> &funcs,
                std::ostream &err);
};

} // namespace il::verify
