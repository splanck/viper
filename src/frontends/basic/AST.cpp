// File: src/frontends/basic/AST.cpp
// Purpose: Provides the anchor translation unit for BASIC AST helpers (MIT
//          License; see LICENSE).
// Key invariants: None.
// Ownership/Lifetime: Nodes owned via std::unique_ptr.
// Links: docs/codemap.md

#include "frontends/basic/AST.hpp"

// Trivial visitor forwarding methods live inline in ast/* headers to improve
// compile times. Add non-trivial AST utilities here when they require
// out-of-line definitions.
