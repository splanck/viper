// File: src/frontends/basic/AST.cpp
// Purpose: Provides out-of-line definitions for BASIC AST nodes (MIT License;
//          see LICENSE).
// Key invariants: None.
// Ownership/Lifetime: Nodes owned via std::unique_ptr.
// Links: docs/codemap.md

#include "frontends/basic/AST.hpp"

namespace il::frontends::basic
{

/// @brief Forwards this integer literal node to the visitor for double dispatch.
/// @param visitor Receives the node; ownership remains with the AST.
void IntExpr::accept(ExprVisitor &visitor) const
{
    visitor.visit(*this);
}

void IntExpr::accept(MutExprVisitor &visitor)
{
    visitor.visit(*this);
}

/// @brief Forwards this floating-point literal node to the visitor for double dispatch.
/// @param visitor Receives the node; ownership remains with the AST.
void FloatExpr::accept(ExprVisitor &visitor) const
{
    visitor.visit(*this);
}

void FloatExpr::accept(MutExprVisitor &visitor)
{
    visitor.visit(*this);
}

/// @brief Forwards this string literal node to the visitor for double dispatch.
/// @param visitor Receives the node; ownership remains with the AST.
void StringExpr::accept(ExprVisitor &visitor) const
{
    visitor.visit(*this);
}

void StringExpr::accept(MutExprVisitor &visitor)
{
    visitor.visit(*this);
}

/// @brief Forwards this boolean literal node to the visitor for double dispatch.
/// @param visitor Receives the node; ownership remains with the AST.
void BoolExpr::accept(ExprVisitor &visitor) const
{
    visitor.visit(*this);
}

void BoolExpr::accept(MutExprVisitor &visitor)
{
    visitor.visit(*this);
}

/// @brief Forwards this variable reference node to the visitor for double dispatch.
/// @param visitor Receives the node; ownership remains with the AST.
void VarExpr::accept(ExprVisitor &visitor) const
{
    visitor.visit(*this);
}

void VarExpr::accept(MutExprVisitor &visitor)
{
    visitor.visit(*this);
}

/// @brief Forwards this array element access node to the visitor for double dispatch.
/// @param visitor Receives the node; ownership remains with the AST.
void ArrayExpr::accept(ExprVisitor &visitor) const
{
    visitor.visit(*this);
}

void ArrayExpr::accept(MutExprVisitor &visitor)
{
    visitor.visit(*this);
}

/// @brief Forwards this LBOUND query node to the visitor for double dispatch.
/// @param visitor Receives the node; ownership remains with the AST.
void LBoundExpr::accept(ExprVisitor &visitor) const
{
    visitor.visit(*this);
}

void LBoundExpr::accept(MutExprVisitor &visitor)
{
    visitor.visit(*this);
}

/// @brief Forwards this UBOUND query node to the visitor for double dispatch.
/// @param visitor Receives the node; ownership remains with the AST.
void UBoundExpr::accept(ExprVisitor &visitor) const
{
    visitor.visit(*this);
}

void UBoundExpr::accept(MutExprVisitor &visitor)
{
    visitor.visit(*this);
}

/// @brief Forwards this unary operation node to the visitor for double dispatch.
/// @param visitor Receives the node; ownership remains with the AST.
void UnaryExpr::accept(ExprVisitor &visitor) const
{
    visitor.visit(*this);
}

void UnaryExpr::accept(MutExprVisitor &visitor)
{
    visitor.visit(*this);
}

/// @brief Forwards this binary operation node to the visitor for double dispatch.
/// @param visitor Receives the node; ownership remains with the AST.
void BinaryExpr::accept(ExprVisitor &visitor) const
{
    visitor.visit(*this);
}

void BinaryExpr::accept(MutExprVisitor &visitor)
{
    visitor.visit(*this);
}

/// @brief Forwards this builtin call node to the visitor for double dispatch.
/// @param visitor Receives the node; ownership remains with the AST.
void BuiltinCallExpr::accept(ExprVisitor &visitor) const
{
    visitor.visit(*this);
}

void BuiltinCallExpr::accept(MutExprVisitor &visitor)
{
    visitor.visit(*this);
}

/// @brief Forwards this user-defined FUNCTION/SUB call node to the visitor for double dispatch.
/// @param visitor Receives the node; ownership remains with the AST.
void CallExpr::accept(ExprVisitor &visitor) const
{
    visitor.visit(*this);
}

void CallExpr::accept(MutExprVisitor &visitor)
{
    visitor.visit(*this);
}

#if VIPER_ENABLE_OOP
/// @brief Forwards this NEW expression node to the visitor for double dispatch.
/// @param visitor Receives the node; ownership remains with the AST.
void NewExpr::accept(ExprVisitor &visitor) const
{
    visitor.visit(*this);
}

void NewExpr::accept(MutExprVisitor &visitor)
{
    visitor.visit(*this);
}

/// @brief Forwards this ME expression node to the visitor for double dispatch.
/// @param visitor Receives the node; ownership remains with the AST.
void MeExpr::accept(ExprVisitor &visitor) const
{
    visitor.visit(*this);
}

void MeExpr::accept(MutExprVisitor &visitor)
{
    visitor.visit(*this);
}

/// @brief Forwards this member access expression node to the visitor.
/// @param visitor Receives the node; ownership remains with the AST.
void MemberAccessExpr::accept(ExprVisitor &visitor) const
{
    visitor.visit(*this);
}

void MemberAccessExpr::accept(MutExprVisitor &visitor)
{
    visitor.visit(*this);
}

/// @brief Forwards this method call expression node to the visitor.
/// @param visitor Receives the node; ownership remains with the AST.
void MethodCallExpr::accept(ExprVisitor &visitor) const
{
    visitor.visit(*this);
}

void MethodCallExpr::accept(MutExprVisitor &visitor)
{
    visitor.visit(*this);
}
#endif

/// @brief Forwards this label statement node to the visitor for double dispatch.
/// @param visitor Receives the node; ownership remains with the AST.
void LabelStmt::accept(StmtVisitor &visitor) const
{
    visitor.visit(*this);
}

void LabelStmt::accept(MutStmtVisitor &visitor)
{
    visitor.visit(*this);
}

/// @brief Forwards this print statement node to the visitor for double dispatch.
/// @param visitor Receives the node; ownership remains with the AST.
void PrintStmt::accept(StmtVisitor &visitor) const
{
    visitor.visit(*this);
}

void PrintStmt::accept(MutStmtVisitor &visitor)
{
    visitor.visit(*this);
}

/// @brief Forwards this PRINT # statement node to the visitor for double dispatch.
/// @param visitor Receives the node; ownership remains with the AST.
void PrintChStmt::accept(StmtVisitor &visitor) const
{
    visitor.visit(*this);
}

void PrintChStmt::accept(MutStmtVisitor &visitor)
{
    visitor.visit(*this);
}

/// @brief Forwards this CALL statement node to the visitor for double dispatch.
/// @param visitor Receives the node; ownership remains with the AST.
void CallStmt::accept(StmtVisitor &visitor) const
{
    visitor.visit(*this);
}

void CallStmt::accept(MutStmtVisitor &visitor)
{
    visitor.visit(*this);
}

/// @brief Forwards this CLS statement node to the visitor for double dispatch.
/// @param visitor Receives the node; ownership remains with the AST.
void ClsStmt::accept(StmtVisitor &visitor) const
{
    visitor.visit(*this);
}

void ClsStmt::accept(MutStmtVisitor &visitor)
{
    visitor.visit(*this);
}

/// @brief Forwards this COLOR statement node to the visitor for double dispatch.
/// @param visitor Receives the node; ownership remains with the AST.
void ColorStmt::accept(StmtVisitor &visitor) const
{
    visitor.visit(*this);
}

void ColorStmt::accept(MutStmtVisitor &visitor)
{
    visitor.visit(*this);
}

/// @brief Forwards this LOCATE statement node to the visitor for double dispatch.
/// @param visitor Receives the node; ownership remains with the AST.
void LocateStmt::accept(StmtVisitor &visitor) const
{
    visitor.visit(*this);
}

void LocateStmt::accept(MutStmtVisitor &visitor)
{
    visitor.visit(*this);
}

/// @brief Forwards this let statement node to the visitor for double dispatch.
/// @param visitor Receives the node; ownership remains with the AST.
void LetStmt::accept(StmtVisitor &visitor) const
{
    visitor.visit(*this);
}

void LetStmt::accept(MutStmtVisitor &visitor)
{
    visitor.visit(*this);
}

/// @brief Forwards this dimension statement node to the visitor for double dispatch.
/// @param visitor Receives the node; ownership remains with the AST.
void DimStmt::accept(StmtVisitor &visitor) const
{
    visitor.visit(*this);
}

void DimStmt::accept(MutStmtVisitor &visitor)
{
    visitor.visit(*this);
}

/// @brief Forwards this REDIM statement node to the visitor for double dispatch.
/// @param visitor Receives the node; ownership remains with the AST.
void ReDimStmt::accept(StmtVisitor &visitor) const
{
    visitor.visit(*this);
}

void ReDimStmt::accept(MutStmtVisitor &visitor)
{
    visitor.visit(*this);
}

/// @brief Forwards this randomize statement node to the visitor for double dispatch.
/// @param visitor Receives the node; ownership remains with the AST.
void RandomizeStmt::accept(StmtVisitor &visitor) const
{
    visitor.visit(*this);
}

void RandomizeStmt::accept(MutStmtVisitor &visitor)
{
    visitor.visit(*this);
}

/// @brief Forwards this conditional statement node to the visitor for double dispatch.
/// @param visitor Receives the node; ownership remains with the AST.
void IfStmt::accept(StmtVisitor &visitor) const
{
    visitor.visit(*this);
}

void IfStmt::accept(MutStmtVisitor &visitor)
{
    visitor.visit(*this);
}

/// @brief Forwards this SELECT CASE statement node to the visitor for double dispatch.
/// @param visitor Receives the node; ownership remains with the AST.
void SelectCaseStmt::accept(StmtVisitor &visitor) const
{
    visitor.visit(*this);
}

void SelectCaseStmt::accept(MutStmtVisitor &visitor)
{
    visitor.visit(*this);
}

/// @brief Forwards this while loop node to the visitor for double dispatch.
/// @param visitor Receives the node; ownership remains with the AST.
void WhileStmt::accept(StmtVisitor &visitor) const
{
    visitor.visit(*this);
}

void WhileStmt::accept(MutStmtVisitor &visitor)
{
    visitor.visit(*this);
}

/// @brief Forwards this DO loop node to the visitor for double dispatch.
/// @param visitor Receives the node; ownership remains with the AST.
void DoStmt::accept(StmtVisitor &visitor) const
{
    visitor.visit(*this);
}

void DoStmt::accept(MutStmtVisitor &visitor)
{
    visitor.visit(*this);
}

/// @brief Forwards this for loop node to the visitor for double dispatch.
/// @param visitor Receives the node; ownership remains with the AST.
void ForStmt::accept(StmtVisitor &visitor) const
{
    visitor.visit(*this);
}

void ForStmt::accept(MutStmtVisitor &visitor)
{
    visitor.visit(*this);
}

/// @brief Forwards this next statement node to the visitor for double dispatch.
/// @param visitor Receives the node; ownership remains with the AST.
void NextStmt::accept(StmtVisitor &visitor) const
{
    visitor.visit(*this);
}

void NextStmt::accept(MutStmtVisitor &visitor)
{
    visitor.visit(*this);
}

/// @brief Forwards this EXIT statement node to the visitor for double dispatch.
/// @param visitor Receives the node; ownership remains with the AST.
void ExitStmt::accept(StmtVisitor &visitor) const
{
    visitor.visit(*this);
}

void ExitStmt::accept(MutStmtVisitor &visitor)
{
    visitor.visit(*this);
}

/// @brief Forwards this goto statement node to the visitor for double dispatch.
/// @param visitor Receives the node; ownership remains with the AST.
void GotoStmt::accept(StmtVisitor &visitor) const
{
    visitor.visit(*this);
}

void GotoStmt::accept(MutStmtVisitor &visitor)
{
    visitor.visit(*this);
}

/// @brief Forwards this GOSUB statement node to the visitor for double dispatch.
/// @param visitor Receives the node; ownership remains with the AST.
void GosubStmt::accept(StmtVisitor &visitor) const
{
    visitor.visit(*this);
}

void GosubStmt::accept(MutStmtVisitor &visitor)
{
    visitor.visit(*this);
}

/// @brief Forwards this OPEN statement node to the visitor for double dispatch.
/// @param visitor Receives the node; ownership remains with the AST.
void OpenStmt::accept(StmtVisitor &visitor) const
{
    visitor.visit(*this);
}

void OpenStmt::accept(MutStmtVisitor &visitor)
{
    visitor.visit(*this);
}

/// @brief Forwards this CLOSE statement node to the visitor for double dispatch.
/// @param visitor Receives the node; ownership remains with the AST.
void CloseStmt::accept(StmtVisitor &visitor) const
{
    visitor.visit(*this);
}

void CloseStmt::accept(MutStmtVisitor &visitor)
{
    visitor.visit(*this);
}

/// @brief Forwards this SEEK statement node to the visitor for double dispatch.
/// @param visitor Receives the node; ownership remains with the AST.
void SeekStmt::accept(StmtVisitor &visitor) const
{
    visitor.visit(*this);
}

void SeekStmt::accept(MutStmtVisitor &visitor)
{
    visitor.visit(*this);
}

/// @brief Forward this ON ERROR GOTO statement node to the visitor.
/// @param visitor Receives the node; ownership remains with the AST.
void OnErrorGoto::accept(StmtVisitor &visitor) const
{
    visitor.visit(*this);
}

void OnErrorGoto::accept(MutStmtVisitor &visitor)
{
    visitor.visit(*this);
}

/// @brief Forward this RESUME statement node to the visitor.
/// @param visitor Receives the node; ownership remains with the AST.
void Resume::accept(StmtVisitor &visitor) const
{
    visitor.visit(*this);
}

void Resume::accept(MutStmtVisitor &visitor)
{
    visitor.visit(*this);
}

/// @brief Forwards this end statement node to the visitor for double dispatch.
/// @param visitor Receives the node; ownership remains with the AST.
void EndStmt::accept(StmtVisitor &visitor) const
{
    visitor.visit(*this);
}

void EndStmt::accept(MutStmtVisitor &visitor)
{
    visitor.visit(*this);
}

/// @brief Forwards this input statement node to the visitor for double dispatch.
/// @param visitor Receives the node; ownership remains with the AST.
void InputStmt::accept(StmtVisitor &visitor) const
{
    visitor.visit(*this);
}

void InputStmt::accept(MutStmtVisitor &visitor)
{
    visitor.visit(*this);
}

/// @brief Forwards this INPUT # statement node to the visitor for double dispatch.
/// @param visitor Receives the node; ownership remains with the AST.
void InputChStmt::accept(StmtVisitor &visitor) const
{
    visitor.visit(*this);
}

void InputChStmt::accept(MutStmtVisitor &visitor)
{
    visitor.visit(*this);
}

/// @brief Forwards this LINE INPUT # statement node to the visitor for double dispatch.
/// @param visitor Receives the node; ownership remains with the AST.
void LineInputChStmt::accept(StmtVisitor &visitor) const
{
    visitor.visit(*this);
}

void LineInputChStmt::accept(MutStmtVisitor &visitor)
{
    visitor.visit(*this);
}

/// @brief Forwards this return statement node to the visitor for double dispatch.
/// @param visitor Receives the node; ownership remains with the AST.
void ReturnStmt::accept(StmtVisitor &visitor) const
{
    visitor.visit(*this);
}

void ReturnStmt::accept(MutStmtVisitor &visitor)
{
    visitor.visit(*this);
}

/// @brief Forwards this function declaration node to the visitor for double dispatch.
/// @param visitor Receives the node; ownership remains with the AST.
void FunctionDecl::accept(StmtVisitor &visitor) const
{
    visitor.visit(*this);
}

void FunctionDecl::accept(MutStmtVisitor &visitor)
{
    visitor.visit(*this);
}

/// @brief Forwards this subroutine declaration node to the visitor for double dispatch.
/// @param visitor Receives the node; ownership remains with the AST.
void SubDecl::accept(StmtVisitor &visitor) const
{
    visitor.visit(*this);
}

void SubDecl::accept(MutStmtVisitor &visitor)
{
    visitor.visit(*this);
}

/// @brief Forwards this statement list node to the visitor for double dispatch.
/// @param visitor Receives the node; ownership remains with the AST.
void StmtList::accept(StmtVisitor &visitor) const
{
    visitor.visit(*this);
}

void StmtList::accept(MutStmtVisitor &visitor)
{
    visitor.visit(*this);
}

#if VIPER_ENABLE_OOP

/// @brief Forwards this DELETE statement node to the visitor for double dispatch.
/// @param visitor Receives the node; ownership remains with the AST.
void DeleteStmt::accept(StmtVisitor &visitor) const
{
    visitor.visit(*this);
}

void DeleteStmt::accept(MutStmtVisitor &visitor)
{
    visitor.visit(*this);
}

/// @brief Forwards this constructor declaration node to the visitor.
/// @param visitor Receives the node; ownership remains with the AST.
void ConstructorDecl::accept(StmtVisitor &visitor) const
{
    visitor.visit(*this);
}

void ConstructorDecl::accept(MutStmtVisitor &visitor)
{
    visitor.visit(*this);
}

/// @brief Forwards this destructor declaration node to the visitor.
/// @param visitor Receives the node; ownership remains with the AST.
void DestructorDecl::accept(StmtVisitor &visitor) const
{
    visitor.visit(*this);
}

void DestructorDecl::accept(MutStmtVisitor &visitor)
{
    visitor.visit(*this);
}

/// @brief Forwards this method declaration node to the visitor for double dispatch.
/// @param visitor Receives the node; ownership remains with the AST.
void MethodDecl::accept(StmtVisitor &visitor) const
{
    visitor.visit(*this);
}

void MethodDecl::accept(MutStmtVisitor &visitor)
{
    visitor.visit(*this);
}

/// @brief Forwards this CLASS declaration node to the visitor for double dispatch.
/// @param visitor Receives the node; ownership remains with the AST.
void ClassDecl::accept(StmtVisitor &visitor) const
{
    visitor.visit(*this);
}

void ClassDecl::accept(MutStmtVisitor &visitor)
{
    visitor.visit(*this);
}

/// @brief Forwards this TYPE declaration node to the visitor for double dispatch.
/// @param visitor Receives the node; ownership remains with the AST.
void TypeDecl::accept(StmtVisitor &visitor) const
{
    visitor.visit(*this);
}

void TypeDecl::accept(MutStmtVisitor &visitor)
{
    visitor.visit(*this);
}

#endif
} // namespace il::frontends::basic
