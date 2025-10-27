//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
// File: src/frontends/basic/lower/AstVisitor.hpp
// Purpose: Declares a lightweight visitor interface shared by BASIC lowering
//          components to decouple node traversal from lowering state.
// Key invariants: Each lowering visitor processes one AST family per call and
//                 never mutates ownership of AST nodes.
// Ownership/Lifetime: Visitors borrow AST nodes and Lowerer context; no node
//                     ownership is transferred.
// Links: docs/codemap.md, docs/basic-language.md
//
//===----------------------------------------------------------------------===//

#pragma once

#include "frontends/basic/ast/NodeFwd.hpp"

namespace il::frontends::basic::lower
{

namespace AST = ::il::frontends::basic;

/// @brief Shared visitor interface for lowering helpers.
/// @details Implementations forward to AST-specific visitors while keeping
///          the Lowerer orchestration decoupled from concrete traversal logic.
struct AstVisitor
{
    virtual ~AstVisitor() = default;

    /// @brief Visit an expression node and translate it through the bound
    ///        lowering helper.
    virtual void visitExpr(const AST::Expr &) = 0;

    /// @brief Visit a statement node and translate it through the bound
    ///        lowering helper.
    virtual void visitStmt(const AST::Stmt &) = 0;
};

} // namespace il::frontends::basic::lower
