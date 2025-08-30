// File: src/il/verify/Verifier.h
// Purpose: Declares IL verifier that checks modules.
// Key invariants: None.
// Ownership/Lifetime: Verifier does not own modules.
// Links: docs/il-spec.md
#pragma once
#include "il/core/Module.h"
#include <ostream>

namespace il::verify {

/// @brief Verifies structural and type rules for a module.
class Verifier {
public:
  /// Verify module. Returns true on success, false and emits diagnostics on err.
  static bool verify(const il::core::Module &m, std::ostream &err);
};

} // namespace il::verify
