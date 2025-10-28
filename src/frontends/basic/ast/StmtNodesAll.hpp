// File: src/frontends/basic/ast/StmtNodesAll.hpp
// Purpose: Convenience umbrella header aggregating all BASIC statement node headers.
// Key invariants: Provides a stable include for legacy translation units needing full AST coverage.
// Ownership/Lifetime: Statements remain owned by callers through std::unique_ptr.
// Links: docs/codemap.md
#pragma once

#include "frontends/basic/ast/StmtBase.hpp"
#include "frontends/basic/ast/StmtControl.hpp"
#include "frontends/basic/ast/StmtIO.hpp"
#include "frontends/basic/ast/StmtRuntime.hpp"

