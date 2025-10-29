// File: src/frontends/basic/ast/StmtNodesAll.hpp
// Purpose: Convenience umbrella header aggregating all BASIC statement node families.
// Key invariants: Provides a single include for legacy translation units that require
//                 every statement definition.
// Ownership/Lifetime: Nodes continue to follow ownership semantics defined in their
//                     respective family headers.
// Links: docs/codemap.md
#pragma once

#include "frontends/basic/ast/StmtBase.hpp"
#include "frontends/basic/ast/StmtControl.hpp"
#include "frontends/basic/ast/StmtDecl.hpp"
#include "frontends/basic/ast/StmtExpr.hpp"
