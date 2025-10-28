// File: src/frontends/basic/ast/StmtNodes.hpp
// Purpose: Backward-compatible include for legacy BASIC statement consumers.
// Key invariants: Includes the full statement family umbrella to preserve historical semantics.
// Ownership/Lifetime: Statements remain owned via std::unique_ptr managed externally.
// Links: docs/codemap.md
#pragma once

#include "frontends/basic/ast/StmtNodesAll.hpp"
