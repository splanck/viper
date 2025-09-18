// File: src/frontends/basic/AST.cpp
// Purpose: Provides out-of-line definitions for BASIC AST nodes.
// Key invariants: None.
// Ownership/Lifetime: Nodes owned via std::unique_ptr.
// Links: docs/class-catalog.md

#include "frontends/basic/AST.hpp"

namespace il::frontends::basic
{

void IntExpr::accept(ExprVisitor &visitor) const
{
    visitor.visit(*this);
}

void FloatExpr::accept(ExprVisitor &visitor) const
{
    visitor.visit(*this);
}

void StringExpr::accept(ExprVisitor &visitor) const
{
    visitor.visit(*this);
}

void BoolExpr::accept(ExprVisitor &visitor) const
{
    visitor.visit(*this);
}

void VarExpr::accept(ExprVisitor &visitor) const
{
    visitor.visit(*this);
}

void ArrayExpr::accept(ExprVisitor &visitor) const
{
    visitor.visit(*this);
}

void UnaryExpr::accept(ExprVisitor &visitor) const
{
    visitor.visit(*this);
}

void BinaryExpr::accept(ExprVisitor &visitor) const
{
    visitor.visit(*this);
}

void BuiltinCallExpr::accept(ExprVisitor &visitor) const
{
    visitor.visit(*this);
}

void CallExpr::accept(ExprVisitor &visitor) const
{
    visitor.visit(*this);
}

void PrintStmt::accept(StmtVisitor &visitor) const
{
    visitor.visit(*this);
}

void LetStmt::accept(StmtVisitor &visitor) const
{
    visitor.visit(*this);
}

void DimStmt::accept(StmtVisitor &visitor) const
{
    visitor.visit(*this);
}

void RandomizeStmt::accept(StmtVisitor &visitor) const
{
    visitor.visit(*this);
}

void IfStmt::accept(StmtVisitor &visitor) const
{
    visitor.visit(*this);
}

void WhileStmt::accept(StmtVisitor &visitor) const
{
    visitor.visit(*this);
}

void ForStmt::accept(StmtVisitor &visitor) const
{
    visitor.visit(*this);
}

void NextStmt::accept(StmtVisitor &visitor) const
{
    visitor.visit(*this);
}

void GotoStmt::accept(StmtVisitor &visitor) const
{
    visitor.visit(*this);
}

void EndStmt::accept(StmtVisitor &visitor) const
{
    visitor.visit(*this);
}

void InputStmt::accept(StmtVisitor &visitor) const
{
    visitor.visit(*this);
}

void ReturnStmt::accept(StmtVisitor &visitor) const
{
    visitor.visit(*this);
}

void FunctionDecl::accept(StmtVisitor &visitor) const
{
    visitor.visit(*this);
}

void SubDecl::accept(StmtVisitor &visitor) const
{
    visitor.visit(*this);
}

void StmtList::accept(StmtVisitor &visitor) const
{
    visitor.visit(*this);
}
}
