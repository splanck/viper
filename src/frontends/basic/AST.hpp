// File: src/frontends/basic/AST.hpp
// Purpose: Umbrella header aggregating BASIC front-end AST node families.
// Key invariants: Preserves legacy include path and exported types.
// Ownership/Lifetime: Nodes remain owned via std::unique_ptr managed by callers.
// Links: docs/codemap.md
#pragma once

#include "frontends/basic/ast/DeclNodes.hpp"
#include "frontends/basic/ast/ExprNodes.hpp"
#include "frontends/basic/ast/NodeFwd.hpp"
#include "frontends/basic/ast/StmtNodesAll.hpp"
