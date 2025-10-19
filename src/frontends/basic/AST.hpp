// File: src/frontends/basic/AST.hpp
// Purpose: Umbrella header re-exporting BASIC AST node families.
// Key invariants: Preserves public AST API while minimizing compilation dependencies.
// Ownership/Lifetime: Nodes remain owned via std::unique_ptr handles declared in NodeFwd.hpp.
// Links: docs/codemap.md
#pragma once

#include "frontends/basic/ast/NodeFwd.hpp"
#include "frontends/basic/ast/ExprNodes.hpp"
#include "frontends/basic/ast/StmtNodes.hpp"
#include "frontends/basic/ast/DeclNodes.hpp"

