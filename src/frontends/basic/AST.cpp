//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE in the project root for license information.
//
//===----------------------------------------------------------------------===//
//
// Defines the visitor entry points for each BASIC AST node.  The translation
// unit implements the accept methods declared on expressions and statements,
// completing the double-dispatch wiring required by the visitor interfaces.
// Each overload forwards to the corresponding visitor's visit method while
// preserving node ownership within the AST.
//
//===----------------------------------------------------------------------===//

#include "frontends/basic/AST.hpp"

namespace il::frontends::basic
{

/// @brief Forwards this integer literal node to the visitor for double dispatch.
/// @details Implements the visitor pattern by invoking the matching visit overload on the provided
///          visitor. The const overload preserves read-only access to the node while still performing double dispatch.
/// @param visitor Receives the node; ownership remains with the AST.
void IntExpr::accept(ExprVisitor &visitor) const
{
    visitor.visit(*this);
}


/// @brief Forwards this expression node to a mutating visitor for double dispatch.
/// @details Invokes the corresponding MutExprVisitor::visit overload, allowing the visitor to rewrite
///          the node in place while the AST retains ownership.
/// @param visitor Mutating visitor that receives the node.
void IntExpr::accept(MutExprVisitor &visitor)
{
    visitor.visit(*this);
}

/// @brief Forwards this floating-point literal node to the visitor for double dispatch.
/// @details Implements the visitor pattern by invoking the matching visit overload on the provided
///          visitor. The const overload preserves read-only access to the node while still performing double dispatch.
/// @param visitor Receives the node; ownership remains with the AST.
void FloatExpr::accept(ExprVisitor &visitor) const
{
    visitor.visit(*this);
}


/// @brief Forwards this expression node to a mutating visitor for double dispatch.
/// @details Invokes the corresponding MutExprVisitor::visit overload, allowing the visitor to rewrite
///          the node in place while the AST retains ownership.
/// @param visitor Mutating visitor that receives the node.
void FloatExpr::accept(MutExprVisitor &visitor)
{
    visitor.visit(*this);
}

/// @brief Forwards this string literal node to the visitor for double dispatch.
/// @details Implements the visitor pattern by invoking the matching visit overload on the provided
///          visitor. The const overload preserves read-only access to the node while still performing double dispatch.
/// @param visitor Receives the node; ownership remains with the AST.
void StringExpr::accept(ExprVisitor &visitor) const
{
    visitor.visit(*this);
}


/// @brief Forwards this expression node to a mutating visitor for double dispatch.
/// @details Invokes the corresponding MutExprVisitor::visit overload, allowing the visitor to rewrite
///          the node in place while the AST retains ownership.
/// @param visitor Mutating visitor that receives the node.
void StringExpr::accept(MutExprVisitor &visitor)
{
    visitor.visit(*this);
}

/// @brief Forwards this boolean literal node to the visitor for double dispatch.
/// @details Implements the visitor pattern by invoking the matching visit overload on the provided
///          visitor. The const overload preserves read-only access to the node while still performing double dispatch.
/// @param visitor Receives the node; ownership remains with the AST.
void BoolExpr::accept(ExprVisitor &visitor) const
{
    visitor.visit(*this);
}


/// @brief Forwards this expression node to a mutating visitor for double dispatch.
/// @details Invokes the corresponding MutExprVisitor::visit overload, allowing the visitor to rewrite
///          the node in place while the AST retains ownership.
/// @param visitor Mutating visitor that receives the node.
void BoolExpr::accept(MutExprVisitor &visitor)
{
    visitor.visit(*this);
}

/// @brief Forwards this variable reference node to the visitor for double dispatch.
/// @details Implements the visitor pattern by invoking the matching visit overload on the provided
///          visitor. The const overload preserves read-only access to the node while still performing double dispatch.
/// @param visitor Receives the node; ownership remains with the AST.
void VarExpr::accept(ExprVisitor &visitor) const
{
    visitor.visit(*this);
}


/// @brief Forwards this expression node to a mutating visitor for double dispatch.
/// @details Invokes the corresponding MutExprVisitor::visit overload, allowing the visitor to rewrite
///          the node in place while the AST retains ownership.
/// @param visitor Mutating visitor that receives the node.
void VarExpr::accept(MutExprVisitor &visitor)
{
    visitor.visit(*this);
}

/// @brief Forwards this array element access node to the visitor for double dispatch.
/// @details Implements the visitor pattern by invoking the matching visit overload on the provided
///          visitor. The const overload preserves read-only access to the node while still performing double dispatch.
/// @param visitor Receives the node; ownership remains with the AST.
void ArrayExpr::accept(ExprVisitor &visitor) const
{
    visitor.visit(*this);
}


/// @brief Forwards this expression node to a mutating visitor for double dispatch.
/// @details Invokes the corresponding MutExprVisitor::visit overload, allowing the visitor to rewrite
///          the node in place while the AST retains ownership.
/// @param visitor Mutating visitor that receives the node.
void ArrayExpr::accept(MutExprVisitor &visitor)
{
    visitor.visit(*this);
}

/// @brief Forwards this LBOUND query node to the visitor for double dispatch.
/// @details Implements the visitor pattern by invoking the matching visit overload on the provided
///          visitor. The const overload preserves read-only access to the node while still performing double dispatch.
/// @param visitor Receives the node; ownership remains with the AST.
void LBoundExpr::accept(ExprVisitor &visitor) const
{
    visitor.visit(*this);
}


/// @brief Forwards this expression node to a mutating visitor for double dispatch.
/// @details Invokes the corresponding MutExprVisitor::visit overload, allowing the visitor to rewrite
///          the node in place while the AST retains ownership.
/// @param visitor Mutating visitor that receives the node.
void LBoundExpr::accept(MutExprVisitor &visitor)
{
    visitor.visit(*this);
}

/// @brief Forwards this UBOUND query node to the visitor for double dispatch.
/// @details Implements the visitor pattern by invoking the matching visit overload on the provided
///          visitor. The const overload preserves read-only access to the node while still performing double dispatch.
/// @param visitor Receives the node; ownership remains with the AST.
void UBoundExpr::accept(ExprVisitor &visitor) const
{
    visitor.visit(*this);
}


/// @brief Forwards this expression node to a mutating visitor for double dispatch.
/// @details Invokes the corresponding MutExprVisitor::visit overload, allowing the visitor to rewrite
///          the node in place while the AST retains ownership.
/// @param visitor Mutating visitor that receives the node.
void UBoundExpr::accept(MutExprVisitor &visitor)
{
    visitor.visit(*this);
}

/// @brief Forwards this unary operation node to the visitor for double dispatch.
/// @details Implements the visitor pattern by invoking the matching visit overload on the provided
///          visitor. The const overload preserves read-only access to the node while still performing double dispatch.
/// @param visitor Receives the node; ownership remains with the AST.
void UnaryExpr::accept(ExprVisitor &visitor) const
{
    visitor.visit(*this);
}


/// @brief Forwards this expression node to a mutating visitor for double dispatch.
/// @details Invokes the corresponding MutExprVisitor::visit overload, allowing the visitor to rewrite
///          the node in place while the AST retains ownership.
/// @param visitor Mutating visitor that receives the node.
void UnaryExpr::accept(MutExprVisitor &visitor)
{
    visitor.visit(*this);
}

/// @brief Forwards this binary operation node to the visitor for double dispatch.
/// @details Implements the visitor pattern by invoking the matching visit overload on the provided
///          visitor. The const overload preserves read-only access to the node while still performing double dispatch.
/// @param visitor Receives the node; ownership remains with the AST.
void BinaryExpr::accept(ExprVisitor &visitor) const
{
    visitor.visit(*this);
}


/// @brief Forwards this expression node to a mutating visitor for double dispatch.
/// @details Invokes the corresponding MutExprVisitor::visit overload, allowing the visitor to rewrite
///          the node in place while the AST retains ownership.
/// @param visitor Mutating visitor that receives the node.
void BinaryExpr::accept(MutExprVisitor &visitor)
{
    visitor.visit(*this);
}

/// @brief Forwards this builtin call node to the visitor for double dispatch.
/// @details Implements the visitor pattern by invoking the matching visit overload on the provided
///          visitor. The const overload preserves read-only access to the node while still performing double dispatch.
/// @param visitor Receives the node; ownership remains with the AST.
void BuiltinCallExpr::accept(ExprVisitor &visitor) const
{
    visitor.visit(*this);
}


/// @brief Forwards this expression node to a mutating visitor for double dispatch.
/// @details Invokes the corresponding MutExprVisitor::visit overload, allowing the visitor to rewrite
///          the node in place while the AST retains ownership.
/// @param visitor Mutating visitor that receives the node.
void BuiltinCallExpr::accept(MutExprVisitor &visitor)
{
    visitor.visit(*this);
}

/// @brief Forwards this user-defined FUNCTION/SUB call node to the visitor for double dispatch.
/// @details Implements the visitor pattern by invoking the matching visit overload on the provided
///          visitor. The const overload preserves read-only access to the node while still performing double dispatch.
/// @param visitor Receives the node; ownership remains with the AST.
void CallExpr::accept(ExprVisitor &visitor) const
{
    visitor.visit(*this);
}


/// @brief Forwards this expression node to a mutating visitor for double dispatch.
/// @details Invokes the corresponding MutExprVisitor::visit overload, allowing the visitor to rewrite
///          the node in place while the AST retains ownership.
/// @param visitor Mutating visitor that receives the node.
void CallExpr::accept(MutExprVisitor &visitor)
{
    visitor.visit(*this);
}

/// @brief Forwards this NEW expression node to the visitor for double dispatch.
/// @details Implements the visitor pattern by invoking the matching visit overload on the provided
///          visitor. The const overload preserves read-only access to the node while still performing double dispatch.
/// @param visitor Receives the node; ownership remains with the AST.
void NewExpr::accept(ExprVisitor &visitor) const
{
    visitor.visit(*this);
}


/// @brief Forwards this expression node to a mutating visitor for double dispatch.
/// @details Invokes the corresponding MutExprVisitor::visit overload, allowing the visitor to rewrite
///          the node in place while the AST retains ownership.
/// @param visitor Mutating visitor that receives the node.
void NewExpr::accept(MutExprVisitor &visitor)
{
    visitor.visit(*this);
}

/// @brief Forwards this ME expression node to the visitor for double dispatch.
/// @details Implements the visitor pattern by invoking the matching visit overload on the provided
///          visitor. The const overload preserves read-only access to the node while still performing double dispatch.
/// @param visitor Receives the node; ownership remains with the AST.
void MeExpr::accept(ExprVisitor &visitor) const
{
    visitor.visit(*this);
}


/// @brief Forwards this expression node to a mutating visitor for double dispatch.
/// @details Invokes the corresponding MutExprVisitor::visit overload, allowing the visitor to rewrite
///          the node in place while the AST retains ownership.
/// @param visitor Mutating visitor that receives the node.
void MeExpr::accept(MutExprVisitor &visitor)
{
    visitor.visit(*this);
}

/// @brief Forwards this member access expression node to the visitor.
/// @details Implements the visitor pattern by invoking the matching visit overload on the provided
///          visitor. The const overload preserves read-only access to the node while still performing double dispatch.
/// @param visitor Receives the node; ownership remains with the AST.
void MemberAccessExpr::accept(ExprVisitor &visitor) const
{
    visitor.visit(*this);
}


/// @brief Forwards this expression node to a mutating visitor for double dispatch.
/// @details Invokes the corresponding MutExprVisitor::visit overload, allowing the visitor to rewrite
///          the node in place while the AST retains ownership.
/// @param visitor Mutating visitor that receives the node.
void MemberAccessExpr::accept(MutExprVisitor &visitor)
{
    visitor.visit(*this);
}

/// @brief Forwards this method call expression node to the visitor.
/// @details Implements the visitor pattern by invoking the matching visit overload on the provided
///          visitor. The const overload preserves read-only access to the node while still performing double dispatch.
/// @param visitor Receives the node; ownership remains with the AST.
void MethodCallExpr::accept(ExprVisitor &visitor) const
{
    visitor.visit(*this);
}


/// @brief Forwards this expression node to a mutating visitor for double dispatch.
/// @details Invokes the corresponding MutExprVisitor::visit overload, allowing the visitor to rewrite
///          the node in place while the AST retains ownership.
/// @param visitor Mutating visitor that receives the node.
void MethodCallExpr::accept(MutExprVisitor &visitor)
{
    visitor.visit(*this);
}

/// @brief Forwards this label statement node to the visitor for double dispatch.
/// @details Implements the visitor pattern by invoking the matching visit overload on the provided
///          visitor. The const overload preserves read-only access to the node while still performing double dispatch.
/// @param visitor Receives the node; ownership remains with the AST.
void LabelStmt::accept(StmtVisitor &visitor) const
{
    visitor.visit(*this);
}


/// @brief Forwards this statement node to a mutating visitor for double dispatch.
/// @details Calls the appropriate MutStmtVisitor::visit overload so the visitor can edit the
///          statement without taking ownership.
/// @param visitor Mutating visitor that receives the node.
void LabelStmt::accept(MutStmtVisitor &visitor)
{
    visitor.visit(*this);
}

/// @brief Forwards this print statement node to the visitor for double dispatch.
/// @details Implements the visitor pattern by invoking the matching visit overload on the provided
///          visitor. The const overload preserves read-only access to the node while still performing double dispatch.
/// @param visitor Receives the node; ownership remains with the AST.
void PrintStmt::accept(StmtVisitor &visitor) const
{
    visitor.visit(*this);
}


/// @brief Forwards this statement node to a mutating visitor for double dispatch.
/// @details Calls the appropriate MutStmtVisitor::visit overload so the visitor can edit the
///          statement without taking ownership.
/// @param visitor Mutating visitor that receives the node.
void PrintStmt::accept(MutStmtVisitor &visitor)
{
    visitor.visit(*this);
}

/// @brief Forwards this PRINT # statement node to the visitor for double dispatch.
/// @details Implements the visitor pattern by invoking the matching visit overload on the provided
///          visitor. The const overload preserves read-only access to the node while still performing double dispatch.
/// @param visitor Receives the node; ownership remains with the AST.
void PrintChStmt::accept(StmtVisitor &visitor) const
{
    visitor.visit(*this);
}


/// @brief Forwards this statement node to a mutating visitor for double dispatch.
/// @details Calls the appropriate MutStmtVisitor::visit overload so the visitor can edit the
///          statement without taking ownership.
/// @param visitor Mutating visitor that receives the node.
void PrintChStmt::accept(MutStmtVisitor &visitor)
{
    visitor.visit(*this);
}

/// @brief Forwards this CALL statement node to the visitor for double dispatch.
/// @details Implements the visitor pattern by invoking the matching visit overload on the provided
///          visitor. The const overload preserves read-only access to the node while still performing double dispatch.
/// @param visitor Receives the node; ownership remains with the AST.
void CallStmt::accept(StmtVisitor &visitor) const
{
    visitor.visit(*this);
}


/// @brief Forwards this statement node to a mutating visitor for double dispatch.
/// @details Calls the appropriate MutStmtVisitor::visit overload so the visitor can edit the
///          statement without taking ownership.
/// @param visitor Mutating visitor that receives the node.
void CallStmt::accept(MutStmtVisitor &visitor)
{
    visitor.visit(*this);
}

/// @brief Forwards this CLS statement node to the visitor for double dispatch.
/// @details Implements the visitor pattern by invoking the matching visit overload on the provided
///          visitor. The const overload preserves read-only access to the node while still performing double dispatch.
/// @param visitor Receives the node; ownership remains with the AST.
void ClsStmt::accept(StmtVisitor &visitor) const
{
    visitor.visit(*this);
}


/// @brief Forwards this statement node to a mutating visitor for double dispatch.
/// @details Calls the appropriate MutStmtVisitor::visit overload so the visitor can edit the
///          statement without taking ownership.
/// @param visitor Mutating visitor that receives the node.
void ClsStmt::accept(MutStmtVisitor &visitor)
{
    visitor.visit(*this);
}

/// @brief Forwards this COLOR statement node to the visitor for double dispatch.
/// @details Implements the visitor pattern by invoking the matching visit overload on the provided
///          visitor. The const overload preserves read-only access to the node while still performing double dispatch.
/// @param visitor Receives the node; ownership remains with the AST.
void ColorStmt::accept(StmtVisitor &visitor) const
{
    visitor.visit(*this);
}


/// @brief Forwards this statement node to a mutating visitor for double dispatch.
/// @details Calls the appropriate MutStmtVisitor::visit overload so the visitor can edit the
///          statement without taking ownership.
/// @param visitor Mutating visitor that receives the node.
void ColorStmt::accept(MutStmtVisitor &visitor)
{
    visitor.visit(*this);
}

/// @brief Forwards this LOCATE statement node to the visitor for double dispatch.
/// @details Implements the visitor pattern by invoking the matching visit overload on the provided
///          visitor. The const overload preserves read-only access to the node while still performing double dispatch.
/// @param visitor Receives the node; ownership remains with the AST.
void LocateStmt::accept(StmtVisitor &visitor) const
{
    visitor.visit(*this);
}


/// @brief Forwards this statement node to a mutating visitor for double dispatch.
/// @details Calls the appropriate MutStmtVisitor::visit overload so the visitor can edit the
///          statement without taking ownership.
/// @param visitor Mutating visitor that receives the node.
void LocateStmt::accept(MutStmtVisitor &visitor)
{
    visitor.visit(*this);
}

/// @brief Forwards this let statement node to the visitor for double dispatch.
/// @details Implements the visitor pattern by invoking the matching visit overload on the provided
///          visitor. The const overload preserves read-only access to the node while still performing double dispatch.
/// @param visitor Receives the node; ownership remains with the AST.
void LetStmt::accept(StmtVisitor &visitor) const
{
    visitor.visit(*this);
}


/// @brief Forwards this statement node to a mutating visitor for double dispatch.
/// @details Calls the appropriate MutStmtVisitor::visit overload so the visitor can edit the
///          statement without taking ownership.
/// @param visitor Mutating visitor that receives the node.
void LetStmt::accept(MutStmtVisitor &visitor)
{
    visitor.visit(*this);
}

/// @brief Forwards this dimension statement node to the visitor for double dispatch.
/// @details Implements the visitor pattern by invoking the matching visit overload on the provided
///          visitor. The const overload preserves read-only access to the node while still performing double dispatch.
/// @param visitor Receives the node; ownership remains with the AST.
void DimStmt::accept(StmtVisitor &visitor) const
{
    visitor.visit(*this);
}


/// @brief Forwards this statement node to a mutating visitor for double dispatch.
/// @details Calls the appropriate MutStmtVisitor::visit overload so the visitor can edit the
///          statement without taking ownership.
/// @param visitor Mutating visitor that receives the node.
void DimStmt::accept(MutStmtVisitor &visitor)
{
    visitor.visit(*this);
}

/// @brief Forwards this REDIM statement node to the visitor for double dispatch.
/// @details Implements the visitor pattern by invoking the matching visit overload on the provided
///          visitor. The const overload preserves read-only access to the node while still performing double dispatch.
/// @param visitor Receives the node; ownership remains with the AST.
void ReDimStmt::accept(StmtVisitor &visitor) const
{
    visitor.visit(*this);
}


/// @brief Forwards this statement node to a mutating visitor for double dispatch.
/// @details Calls the appropriate MutStmtVisitor::visit overload so the visitor can edit the
///          statement without taking ownership.
/// @param visitor Mutating visitor that receives the node.
void ReDimStmt::accept(MutStmtVisitor &visitor)
{
    visitor.visit(*this);
}

/// @brief Forwards this randomize statement node to the visitor for double dispatch.
/// @details Implements the visitor pattern by invoking the matching visit overload on the provided
///          visitor. The const overload preserves read-only access to the node while still performing double dispatch.
/// @param visitor Receives the node; ownership remains with the AST.
void RandomizeStmt::accept(StmtVisitor &visitor) const
{
    visitor.visit(*this);
}


/// @brief Forwards this statement node to a mutating visitor for double dispatch.
/// @details Calls the appropriate MutStmtVisitor::visit overload so the visitor can edit the
///          statement without taking ownership.
/// @param visitor Mutating visitor that receives the node.
void RandomizeStmt::accept(MutStmtVisitor &visitor)
{
    visitor.visit(*this);
}

/// @brief Forwards this conditional statement node to the visitor for double dispatch.
/// @details Implements the visitor pattern by invoking the matching visit overload on the provided
///          visitor. The const overload preserves read-only access to the node while still performing double dispatch.
/// @param visitor Receives the node; ownership remains with the AST.
void IfStmt::accept(StmtVisitor &visitor) const
{
    visitor.visit(*this);
}


/// @brief Forwards this statement node to a mutating visitor for double dispatch.
/// @details Calls the appropriate MutStmtVisitor::visit overload so the visitor can edit the
///          statement without taking ownership.
/// @param visitor Mutating visitor that receives the node.
void IfStmt::accept(MutStmtVisitor &visitor)
{
    visitor.visit(*this);
}

/// @brief Forwards this SELECT CASE statement node to the visitor for double dispatch.
/// @details Implements the visitor pattern by invoking the matching visit overload on the provided
///          visitor. The const overload preserves read-only access to the node while still performing double dispatch.
/// @param visitor Receives the node; ownership remains with the AST.
void SelectCaseStmt::accept(StmtVisitor &visitor) const
{
    visitor.visit(*this);
}


/// @brief Forwards this statement node to a mutating visitor for double dispatch.
/// @details Calls the appropriate MutStmtVisitor::visit overload so the visitor can edit the
///          statement without taking ownership.
/// @param visitor Mutating visitor that receives the node.
void SelectCaseStmt::accept(MutStmtVisitor &visitor)
{
    visitor.visit(*this);
}

/// @brief Forwards this while loop node to the visitor for double dispatch.
/// @details Implements the visitor pattern by invoking the matching visit overload on the provided
///          visitor. The const overload preserves read-only access to the node while still performing double dispatch.
/// @param visitor Receives the node; ownership remains with the AST.
void WhileStmt::accept(StmtVisitor &visitor) const
{
    visitor.visit(*this);
}


/// @brief Forwards this statement node to a mutating visitor for double dispatch.
/// @details Calls the appropriate MutStmtVisitor::visit overload so the visitor can edit the
///          statement without taking ownership.
/// @param visitor Mutating visitor that receives the node.
void WhileStmt::accept(MutStmtVisitor &visitor)
{
    visitor.visit(*this);
}

/// @brief Forwards this DO loop node to the visitor for double dispatch.
/// @details Implements the visitor pattern by invoking the matching visit overload on the provided
///          visitor. The const overload preserves read-only access to the node while still performing double dispatch.
/// @param visitor Receives the node; ownership remains with the AST.
void DoStmt::accept(StmtVisitor &visitor) const
{
    visitor.visit(*this);
}


/// @brief Forwards this statement node to a mutating visitor for double dispatch.
/// @details Calls the appropriate MutStmtVisitor::visit overload so the visitor can edit the
///          statement without taking ownership.
/// @param visitor Mutating visitor that receives the node.
void DoStmt::accept(MutStmtVisitor &visitor)
{
    visitor.visit(*this);
}

/// @brief Forwards this for loop node to the visitor for double dispatch.
/// @details Implements the visitor pattern by invoking the matching visit overload on the provided
///          visitor. The const overload preserves read-only access to the node while still performing double dispatch.
/// @param visitor Receives the node; ownership remains with the AST.
void ForStmt::accept(StmtVisitor &visitor) const
{
    visitor.visit(*this);
}


/// @brief Forwards this statement node to a mutating visitor for double dispatch.
/// @details Calls the appropriate MutStmtVisitor::visit overload so the visitor can edit the
///          statement without taking ownership.
/// @param visitor Mutating visitor that receives the node.
void ForStmt::accept(MutStmtVisitor &visitor)
{
    visitor.visit(*this);
}

/// @brief Forwards this next statement node to the visitor for double dispatch.
/// @details Implements the visitor pattern by invoking the matching visit overload on the provided
///          visitor. The const overload preserves read-only access to the node while still performing double dispatch.
/// @param visitor Receives the node; ownership remains with the AST.
void NextStmt::accept(StmtVisitor &visitor) const
{
    visitor.visit(*this);
}


/// @brief Forwards this statement node to a mutating visitor for double dispatch.
/// @details Calls the appropriate MutStmtVisitor::visit overload so the visitor can edit the
///          statement without taking ownership.
/// @param visitor Mutating visitor that receives the node.
void NextStmt::accept(MutStmtVisitor &visitor)
{
    visitor.visit(*this);
}

/// @brief Forwards this EXIT statement node to the visitor for double dispatch.
/// @details Implements the visitor pattern by invoking the matching visit overload on the provided
///          visitor. The const overload preserves read-only access to the node while still performing double dispatch.
/// @param visitor Receives the node; ownership remains with the AST.
void ExitStmt::accept(StmtVisitor &visitor) const
{
    visitor.visit(*this);
}


/// @brief Forwards this statement node to a mutating visitor for double dispatch.
/// @details Calls the appropriate MutStmtVisitor::visit overload so the visitor can edit the
///          statement without taking ownership.
/// @param visitor Mutating visitor that receives the node.
void ExitStmt::accept(MutStmtVisitor &visitor)
{
    visitor.visit(*this);
}

/// @brief Forwards this goto statement node to the visitor for double dispatch.
/// @details Implements the visitor pattern by invoking the matching visit overload on the provided
///          visitor. The const overload preserves read-only access to the node while still performing double dispatch.
/// @param visitor Receives the node; ownership remains with the AST.
void GotoStmt::accept(StmtVisitor &visitor) const
{
    visitor.visit(*this);
}


/// @brief Forwards this statement node to a mutating visitor for double dispatch.
/// @details Calls the appropriate MutStmtVisitor::visit overload so the visitor can edit the
///          statement without taking ownership.
/// @param visitor Mutating visitor that receives the node.
void GotoStmt::accept(MutStmtVisitor &visitor)
{
    visitor.visit(*this);
}

/// @brief Forwards this GOSUB statement node to the visitor for double dispatch.
/// @details Implements the visitor pattern by invoking the matching visit overload on the provided
///          visitor. The const overload preserves read-only access to the node while still performing double dispatch.
/// @param visitor Receives the node; ownership remains with the AST.
void GosubStmt::accept(StmtVisitor &visitor) const
{
    visitor.visit(*this);
}


/// @brief Forwards this statement node to a mutating visitor for double dispatch.
/// @details Calls the appropriate MutStmtVisitor::visit overload so the visitor can edit the
///          statement without taking ownership.
/// @param visitor Mutating visitor that receives the node.
void GosubStmt::accept(MutStmtVisitor &visitor)
{
    visitor.visit(*this);
}

/// @brief Forwards this OPEN statement node to the visitor for double dispatch.
/// @details Implements the visitor pattern by invoking the matching visit overload on the provided
///          visitor. The const overload preserves read-only access to the node while still performing double dispatch.
/// @param visitor Receives the node; ownership remains with the AST.
void OpenStmt::accept(StmtVisitor &visitor) const
{
    visitor.visit(*this);
}


/// @brief Forwards this statement node to a mutating visitor for double dispatch.
/// @details Calls the appropriate MutStmtVisitor::visit overload so the visitor can edit the
///          statement without taking ownership.
/// @param visitor Mutating visitor that receives the node.
void OpenStmt::accept(MutStmtVisitor &visitor)
{
    visitor.visit(*this);
}

/// @brief Forwards this CLOSE statement node to the visitor for double dispatch.
/// @details Implements the visitor pattern by invoking the matching visit overload on the provided
///          visitor. The const overload preserves read-only access to the node while still performing double dispatch.
/// @param visitor Receives the node; ownership remains with the AST.
void CloseStmt::accept(StmtVisitor &visitor) const
{
    visitor.visit(*this);
}


/// @brief Forwards this statement node to a mutating visitor for double dispatch.
/// @details Calls the appropriate MutStmtVisitor::visit overload so the visitor can edit the
///          statement without taking ownership.
/// @param visitor Mutating visitor that receives the node.
void CloseStmt::accept(MutStmtVisitor &visitor)
{
    visitor.visit(*this);
}

/// @brief Forwards this SEEK statement node to the visitor for double dispatch.
/// @details Implements the visitor pattern by invoking the matching visit overload on the provided
///          visitor. The const overload preserves read-only access to the node while still performing double dispatch.
/// @param visitor Receives the node; ownership remains with the AST.
void SeekStmt::accept(StmtVisitor &visitor) const
{
    visitor.visit(*this);
}


/// @brief Forwards this statement node to a mutating visitor for double dispatch.
/// @details Calls the appropriate MutStmtVisitor::visit overload so the visitor can edit the
///          statement without taking ownership.
/// @param visitor Mutating visitor that receives the node.
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


/// @brief Forwards this statement node to a mutating visitor for double dispatch.
/// @details Calls the appropriate MutStmtVisitor::visit overload so the visitor can edit the
///          statement without taking ownership.
/// @param visitor Mutating visitor that receives the node.
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


/// @brief Forwards this statement node to a mutating visitor for double dispatch.
/// @details Calls the appropriate MutStmtVisitor::visit overload so the visitor can edit the
///          statement without taking ownership.
/// @param visitor Mutating visitor that receives the node.
void Resume::accept(MutStmtVisitor &visitor)
{
    visitor.visit(*this);
}

/// @brief Forwards this end statement node to the visitor for double dispatch.
/// @details Implements the visitor pattern by invoking the matching visit overload on the provided
///          visitor. The const overload preserves read-only access to the node while still performing double dispatch.
/// @param visitor Receives the node; ownership remains with the AST.
void EndStmt::accept(StmtVisitor &visitor) const
{
    visitor.visit(*this);
}


/// @brief Forwards this statement node to a mutating visitor for double dispatch.
/// @details Calls the appropriate MutStmtVisitor::visit overload so the visitor can edit the
///          statement without taking ownership.
/// @param visitor Mutating visitor that receives the node.
void EndStmt::accept(MutStmtVisitor &visitor)
{
    visitor.visit(*this);
}

/// @brief Forwards this input statement node to the visitor for double dispatch.
/// @details Implements the visitor pattern by invoking the matching visit overload on the provided
///          visitor. The const overload preserves read-only access to the node while still performing double dispatch.
/// @param visitor Receives the node; ownership remains with the AST.
void InputStmt::accept(StmtVisitor &visitor) const
{
    visitor.visit(*this);
}


/// @brief Forwards this statement node to a mutating visitor for double dispatch.
/// @details Calls the appropriate MutStmtVisitor::visit overload so the visitor can edit the
///          statement without taking ownership.
/// @param visitor Mutating visitor that receives the node.
void InputStmt::accept(MutStmtVisitor &visitor)
{
    visitor.visit(*this);
}

/// @brief Forwards this INPUT # statement node to the visitor for double dispatch.
/// @details Implements the visitor pattern by invoking the matching visit overload on the provided
///          visitor. The const overload preserves read-only access to the node while still performing double dispatch.
/// @param visitor Receives the node; ownership remains with the AST.
void InputChStmt::accept(StmtVisitor &visitor) const
{
    visitor.visit(*this);
}


/// @brief Forwards this statement node to a mutating visitor for double dispatch.
/// @details Calls the appropriate MutStmtVisitor::visit overload so the visitor can edit the
///          statement without taking ownership.
/// @param visitor Mutating visitor that receives the node.
void InputChStmt::accept(MutStmtVisitor &visitor)
{
    visitor.visit(*this);
}

/// @brief Forwards this LINE INPUT # statement node to the visitor for double dispatch.
/// @details Implements the visitor pattern by invoking the matching visit overload on the provided
///          visitor. The const overload preserves read-only access to the node while still performing double dispatch.
/// @param visitor Receives the node; ownership remains with the AST.
void LineInputChStmt::accept(StmtVisitor &visitor) const
{
    visitor.visit(*this);
}


/// @brief Forwards this statement node to a mutating visitor for double dispatch.
/// @details Calls the appropriate MutStmtVisitor::visit overload so the visitor can edit the
///          statement without taking ownership.
/// @param visitor Mutating visitor that receives the node.
void LineInputChStmt::accept(MutStmtVisitor &visitor)
{
    visitor.visit(*this);
}

/// @brief Forwards this return statement node to the visitor for double dispatch.
/// @details Implements the visitor pattern by invoking the matching visit overload on the provided
///          visitor. The const overload preserves read-only access to the node while still performing double dispatch.
/// @param visitor Receives the node; ownership remains with the AST.
void ReturnStmt::accept(StmtVisitor &visitor) const
{
    visitor.visit(*this);
}


/// @brief Forwards this statement node to a mutating visitor for double dispatch.
/// @details Calls the appropriate MutStmtVisitor::visit overload so the visitor can edit the
///          statement without taking ownership.
/// @param visitor Mutating visitor that receives the node.
void ReturnStmt::accept(MutStmtVisitor &visitor)
{
    visitor.visit(*this);
}

/// @brief Forwards this function declaration node to the visitor for double dispatch.
/// @details Implements the visitor pattern by invoking the matching visit overload on the provided
///          visitor. The const overload preserves read-only access to the node while still performing double dispatch.
/// @param visitor Receives the node; ownership remains with the AST.
void FunctionDecl::accept(StmtVisitor &visitor) const
{
    visitor.visit(*this);
}


/// @brief Forwards this statement node to a mutating visitor for double dispatch.
/// @details Calls the appropriate MutStmtVisitor::visit overload so the visitor can edit the
///          statement without taking ownership.
/// @param visitor Mutating visitor that receives the node.
void FunctionDecl::accept(MutStmtVisitor &visitor)
{
    visitor.visit(*this);
}

/// @brief Forwards this subroutine declaration node to the visitor for double dispatch.
/// @details Implements the visitor pattern by invoking the matching visit overload on the provided
///          visitor. The const overload preserves read-only access to the node while still performing double dispatch.
/// @param visitor Receives the node; ownership remains with the AST.
void SubDecl::accept(StmtVisitor &visitor) const
{
    visitor.visit(*this);
}


/// @brief Forwards this statement node to a mutating visitor for double dispatch.
/// @details Calls the appropriate MutStmtVisitor::visit overload so the visitor can edit the
///          statement without taking ownership.
/// @param visitor Mutating visitor that receives the node.
void SubDecl::accept(MutStmtVisitor &visitor)
{
    visitor.visit(*this);
}

/// @brief Forwards this statement list node to the visitor for double dispatch.
/// @details Implements the visitor pattern by invoking the matching visit overload on the provided
///          visitor. The const overload preserves read-only access to the node while still performing double dispatch.
/// @param visitor Receives the node; ownership remains with the AST.
void StmtList::accept(StmtVisitor &visitor) const
{
    visitor.visit(*this);
}


/// @brief Forwards this statement node to a mutating visitor for double dispatch.
/// @details Calls the appropriate MutStmtVisitor::visit overload so the visitor can edit the
///          statement without taking ownership.
/// @param visitor Mutating visitor that receives the node.
void StmtList::accept(MutStmtVisitor &visitor)
{
    visitor.visit(*this);
}


/// @brief Forwards this DELETE statement node to the visitor for double dispatch.
/// @details Implements the visitor pattern by invoking the matching visit overload on the provided
///          visitor. The const overload preserves read-only access to the node while still performing double dispatch.
/// @param visitor Receives the node; ownership remains with the AST.
void DeleteStmt::accept(StmtVisitor &visitor) const
{
    visitor.visit(*this);
}


/// @brief Forwards this statement node to a mutating visitor for double dispatch.
/// @details Calls the appropriate MutStmtVisitor::visit overload so the visitor can edit the
///          statement without taking ownership.
/// @param visitor Mutating visitor that receives the node.
void DeleteStmt::accept(MutStmtVisitor &visitor)
{
    visitor.visit(*this);
}

/// @brief Forwards this constructor declaration node to the visitor.
/// @details Implements the visitor pattern by invoking the matching visit overload on the provided
///          visitor. The const overload preserves read-only access to the node while still performing double dispatch.
/// @param visitor Receives the node; ownership remains with the AST.
void ConstructorDecl::accept(StmtVisitor &visitor) const
{
    visitor.visit(*this);
}


/// @brief Forwards this statement node to a mutating visitor for double dispatch.
/// @details Calls the appropriate MutStmtVisitor::visit overload so the visitor can edit the
///          statement without taking ownership.
/// @param visitor Mutating visitor that receives the node.
void ConstructorDecl::accept(MutStmtVisitor &visitor)
{
    visitor.visit(*this);
}

/// @brief Forwards this destructor declaration node to the visitor.
/// @details Implements the visitor pattern by invoking the matching visit overload on the provided
///          visitor. The const overload preserves read-only access to the node while still performing double dispatch.
/// @param visitor Receives the node; ownership remains with the AST.
void DestructorDecl::accept(StmtVisitor &visitor) const
{
    visitor.visit(*this);
}


/// @brief Forwards this statement node to a mutating visitor for double dispatch.
/// @details Calls the appropriate MutStmtVisitor::visit overload so the visitor can edit the
///          statement without taking ownership.
/// @param visitor Mutating visitor that receives the node.
void DestructorDecl::accept(MutStmtVisitor &visitor)
{
    visitor.visit(*this);
}

/// @brief Forwards this method declaration node to the visitor for double dispatch.
/// @details Implements the visitor pattern by invoking the matching visit overload on the provided
///          visitor. The const overload preserves read-only access to the node while still performing double dispatch.
/// @param visitor Receives the node; ownership remains with the AST.
void MethodDecl::accept(StmtVisitor &visitor) const
{
    visitor.visit(*this);
}


/// @brief Forwards this statement node to a mutating visitor for double dispatch.
/// @details Calls the appropriate MutStmtVisitor::visit overload so the visitor can edit the
///          statement without taking ownership.
/// @param visitor Mutating visitor that receives the node.
void MethodDecl::accept(MutStmtVisitor &visitor)
{
    visitor.visit(*this);
}

/// @brief Forwards this CLASS declaration node to the visitor for double dispatch.
/// @details Implements the visitor pattern by invoking the matching visit overload on the provided
///          visitor. The const overload preserves read-only access to the node while still performing double dispatch.
/// @param visitor Receives the node; ownership remains with the AST.
void ClassDecl::accept(StmtVisitor &visitor) const
{
    visitor.visit(*this);
}


/// @brief Forwards this statement node to a mutating visitor for double dispatch.
/// @details Calls the appropriate MutStmtVisitor::visit overload so the visitor can edit the
///          statement without taking ownership.
/// @param visitor Mutating visitor that receives the node.
void ClassDecl::accept(MutStmtVisitor &visitor)
{
    visitor.visit(*this);
}

/// @brief Forwards this TYPE declaration node to the visitor for double dispatch.
/// @details Implements the visitor pattern by invoking the matching visit overload on the provided
///          visitor. The const overload preserves read-only access to the node while still performing double dispatch.
/// @param visitor Receives the node; ownership remains with the AST.
void TypeDecl::accept(StmtVisitor &visitor) const
{
    visitor.visit(*this);
}


/// @brief Forwards this statement node to a mutating visitor for double dispatch.
/// @details Calls the appropriate MutStmtVisitor::visit overload so the visitor can edit the
///          statement without taking ownership.
/// @param visitor Mutating visitor that receives the node.
void TypeDecl::accept(MutStmtVisitor &visitor)
{
    visitor.visit(*this);
}

} // namespace il::frontends::basic
