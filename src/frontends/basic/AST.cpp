// File: src/frontends/basic/AST.cpp
// Purpose: Provides out-of-line definitions for BASIC AST nodes (MIT License;
//          see LICENSE).
// Key invariants: None.
// Ownership/Lifetime: Nodes owned via std::unique_ptr.
// Links: docs/class-catalog.md

#include "frontends/basic/AST.hpp"

namespace il::frontends::basic
{

/// @brief Forwards this integer literal node to the visitor for double dispatch.
/// @param visitor Receives the node; ownership remains with the AST.
void IntExpr::accept(ExprVisitor &visitor) const
{
    visitor.visit(*this);
}

/// @brief Forwards this floating-point literal node to the visitor for double dispatch.
/// @param visitor Receives the node; ownership remains with the AST.
void FloatExpr::accept(ExprVisitor &visitor) const
{
    visitor.visit(*this);
}

/// @brief Forwards this string literal node to the visitor for double dispatch.
/// @param visitor Receives the node; ownership remains with the AST.
void StringExpr::accept(ExprVisitor &visitor) const
{
    visitor.visit(*this);
}

/// @brief Forwards this boolean literal node to the visitor for double dispatch.
/// @param visitor Receives the node; ownership remains with the AST.
void BoolExpr::accept(ExprVisitor &visitor) const
{
    visitor.visit(*this);
}

/// @brief Forwards this variable reference node to the visitor for double dispatch.
/// @param visitor Receives the node; ownership remains with the AST.
void VarExpr::accept(ExprVisitor &visitor) const
{
    visitor.visit(*this);
}

/// @brief Forwards this array element access node to the visitor for double dispatch.
/// @param visitor Receives the node; ownership remains with the AST.
void ArrayExpr::accept(ExprVisitor &visitor) const
{
    visitor.visit(*this);
}

/// @brief Forwards this unary operation node to the visitor for double dispatch.
/// @param visitor Receives the node; ownership remains with the AST.
void UnaryExpr::accept(ExprVisitor &visitor) const
{
    visitor.visit(*this);
}

/// @brief Forwards this binary operation node to the visitor for double dispatch.
/// @param visitor Receives the node; ownership remains with the AST.
void BinaryExpr::accept(ExprVisitor &visitor) const
{
    visitor.visit(*this);
}

/// @brief Forwards this builtin call node to the visitor for double dispatch.
/// @param visitor Receives the node; ownership remains with the AST.
void BuiltinCallExpr::accept(ExprVisitor &visitor) const
{
    visitor.visit(*this);
}

/// @brief Forwards this user-defined FUNCTION/SUB call node to the visitor for double dispatch.
/// @param visitor Receives the node; ownership remains with the AST.
void CallExpr::accept(ExprVisitor &visitor) const
{
    visitor.visit(*this);
}

/// @brief Forwards this print statement node to the visitor for double dispatch.
/// @param visitor Receives the node; ownership remains with the AST.
void PrintStmt::accept(StmtVisitor &visitor) const
{
    visitor.visit(*this);
}

/// @brief Forwards this let statement node to the visitor for double dispatch.
/// @param visitor Receives the node; ownership remains with the AST.
void LetStmt::accept(StmtVisitor &visitor) const
{
    visitor.visit(*this);
}

/// @brief Forwards this dimension statement node to the visitor for double dispatch.
/// @param visitor Receives the node; ownership remains with the AST.
void DimStmt::accept(StmtVisitor &visitor) const
{
    visitor.visit(*this);
}

/// @brief Forwards this randomize statement node to the visitor for double dispatch.
/// @param visitor Receives the node; ownership remains with the AST.
void RandomizeStmt::accept(StmtVisitor &visitor) const
{
    visitor.visit(*this);
}

/// @brief Forwards this conditional statement node to the visitor for double dispatch.
/// @param visitor Receives the node; ownership remains with the AST.
void IfStmt::accept(StmtVisitor &visitor) const
{
    visitor.visit(*this);
}

/// @brief Forwards this while loop node to the visitor for double dispatch.
/// @param visitor Receives the node; ownership remains with the AST.
void WhileStmt::accept(StmtVisitor &visitor) const
{
    visitor.visit(*this);
}

/// @brief Forwards this for loop node to the visitor for double dispatch.
/// @param visitor Receives the node; ownership remains with the AST.
void ForStmt::accept(StmtVisitor &visitor) const
{
    visitor.visit(*this);
}

/// @brief Forwards this next statement node to the visitor for double dispatch.
/// @param visitor Receives the node; ownership remains with the AST.
void NextStmt::accept(StmtVisitor &visitor) const
{
    visitor.visit(*this);
}

/// @brief Forwards this goto statement node to the visitor for double dispatch.
/// @param visitor Receives the node; ownership remains with the AST.
void GotoStmt::accept(StmtVisitor &visitor) const
{
    visitor.visit(*this);
}

/// @brief Forwards this end statement node to the visitor for double dispatch.
/// @param visitor Receives the node; ownership remains with the AST.
void EndStmt::accept(StmtVisitor &visitor) const
{
    visitor.visit(*this);
}

/// @brief Forwards this input statement node to the visitor for double dispatch.
/// @param visitor Receives the node; ownership remains with the AST.
void InputStmt::accept(StmtVisitor &visitor) const
{
    visitor.visit(*this);
}

/// @brief Forwards this return statement node to the visitor for double dispatch.
/// @param visitor Receives the node; ownership remains with the AST.
void ReturnStmt::accept(StmtVisitor &visitor) const
{
    visitor.visit(*this);
}

/// @brief Forwards this function declaration node to the visitor for double dispatch.
/// @param visitor Receives the node; ownership remains with the AST.
void FunctionDecl::accept(StmtVisitor &visitor) const
{
    visitor.visit(*this);
}

/// @brief Forwards this subroutine declaration node to the visitor for double dispatch.
/// @param visitor Receives the node; ownership remains with the AST.
void SubDecl::accept(StmtVisitor &visitor) const
{
    visitor.visit(*this);
}

/// @brief Forwards this statement list node to the visitor for double dispatch.
/// @param visitor Receives the node; ownership remains with the AST.
void StmtList::accept(StmtVisitor &visitor) const
{
    visitor.visit(*this);
}
} // namespace il::frontends::basic
