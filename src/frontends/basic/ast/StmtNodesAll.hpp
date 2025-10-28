// File: src/frontends/basic/ast/StmtNodesAll.hpp
// Purpose: Convenience umbrella include aggregating all BASIC statement families.
// Key invariants: Mirrors legacy StmtNodes.hpp API surface for broad consumers.
// Ownership/Lifetime: Statements remain owned via std::unique_ptr managed by callers.
// Links: docs/codemap.md
#pragma once

#include "frontends/basic/ast/StmtBase.hpp"
#include "frontends/basic/ast/StmtControl.hpp"
#include "frontends/basic/ast/StmtDecl.hpp"
#include "frontends/basic/ast/StmtExpr.hpp"

