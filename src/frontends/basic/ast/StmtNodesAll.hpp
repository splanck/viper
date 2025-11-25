//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: frontends/basic/ast/StmtNodesAll.hpp
// Purpose: Convenience umbrella header aggregating all BASIC statement node families.
// Key invariants: Provides a single include for legacy translation units that require
// Ownership/Lifetime: Nodes continue to follow ownership semantics defined in their
// Links: docs/codemap.md
//
//===----------------------------------------------------------------------===//

#pragma once

#include "frontends/basic/ast/StmtBase.hpp"
#include "frontends/basic/ast/StmtControl.hpp"
#include "frontends/basic/ast/StmtDecl.hpp"
#include "frontends/basic/ast/StmtExpr.hpp"
