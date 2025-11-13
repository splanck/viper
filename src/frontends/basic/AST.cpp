//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Provides the out-of-line accept/dispatch helpers for BASIC AST nodes.  The
// translation unit exists so the header remains lightweight while still
// centralising visitor entry points for every concrete node type.
//
//===----------------------------------------------------------------------===//
//
/// @file
/// @brief Implements visitor forwarding shims for the BASIC AST hierarchy.
/// @details Each AST class exposes two overloads of @c accept: one for
///          read-only visitors and one for mutable visitors.  Keeping the
///          definitions out-of-line avoids repeatedly instantiating the same
///          forwarding code across translation units that include @c AST.hpp.

#include "frontends/basic/AST.hpp"

namespace il::frontends::basic
{

/// @brief Forward a const expression node to a visitor implementation.
/// @details Wraps the polymorphic @c accept call so clients can invoke
///          `visit(expr, visitor)` without naming the exact derived type.
/// @param expr Expression node to visit.
/// @param visitor Visitor receiving the node.
void visit(const Expr &expr, ExprVisitor &visitor)
{
    expr.accept(visitor);
}

/// @brief Forward a mutable expression node to a visitor implementation.
/// @details Enables uniform visitation over mutable AST nodes by deferring to
///          the node's @c accept overload.
/// @param expr Expression node to visit and potentially mutate.
/// @param visitor Visitor receiving the node.
void visit(Expr &expr, MutExprVisitor &visitor)
{
    expr.accept(visitor);
}

/// @brief Forward a const statement node to a visitor implementation.
/// @details Used when traversing immutable ASTs; delegates to @c accept to
///          perform the double-dispatch.
/// @param stmt Statement node to visit.
/// @param visitor Visitor receiving the node.
void visit(const Stmt &stmt, StmtVisitor &visitor)
{
    stmt.accept(visitor);
}

/// @brief Forward a mutable statement node to a visitor implementation.
/// @details Invokes the node's @c accept overload so visitors can mutate the
///          statement in-place.
/// @param stmt Statement node to visit and potentially mutate.
/// @param visitor Visitor receiving the node.
void visit(Stmt &stmt, MutStmtVisitor &visitor)
{
    stmt.accept(visitor);
}

/// @brief Forwards this integer literal node to the visitor for double dispatch.
/// @param visitor Receives the node; ownership remains with the AST.
void IntExpr::accept(ExprVisitor &visitor) const
{
    visitor.visit(*this);
}

/// @brief Dispatch this integer literal node to a mutable visitor.
/// @param visitor Receives the node and may mutate it in-place.
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

/// @brief Dispatch this floating-point literal node to a mutable visitor.
/// @param visitor Receives the node and may rewrite it.
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

/// @brief Dispatch this string literal node to a mutable visitor.
/// @param visitor Receives the node and may mutate it.
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

/// @brief Dispatch this boolean literal node to a mutable visitor.
/// @param visitor Receives the node and may mutate it.
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

/// @brief Dispatch this variable reference node to a mutable visitor.
/// @param visitor Receives the node and may mutate it.
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

/// @brief Dispatch this array element access node to a mutable visitor.
/// @param visitor Receives the node and may mutate indices or base expression.
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

/// @brief Dispatch this LBOUND query node to a mutable visitor.
/// @param visitor Receives the node and may rewrite its operand.
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

/// @brief Dispatch this UBOUND query node to a mutable visitor.
/// @param visitor Receives the node and may rewrite its operand.
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

/// @brief Dispatch this unary operation node to a mutable visitor.
/// @param visitor Receives the node and may update its operand/operator.
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

/// @brief Dispatch this binary operation node to a mutable visitor.
/// @param visitor Receives the node and may rewrite its operands or operator.
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

/// @brief Dispatch this builtin call node to a mutable visitor.
/// @param visitor Receives the node and may mutate arguments.
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

/// @brief Dispatch this user-defined call node to a mutable visitor.
/// @param visitor Receives the node and may rewrite callee or arguments.
void CallExpr::accept(MutExprVisitor &visitor)
{
    visitor.visit(*this);
}

/// @brief Forwards this NEW expression node to the visitor for double dispatch.
/// @param visitor Receives the node; ownership remains with the AST.
void NewExpr::accept(ExprVisitor &visitor) const
{
    visitor.visit(*this);
}

/// @brief Dispatch this NEW expression node to a mutable visitor.
/// @param visitor Receives the node and may mutate constructor arguments.
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

/// @brief Dispatch this ME expression node to a mutable visitor.
/// @param visitor Receives the node and may update bindings.
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

/// @brief Dispatch this member access expression node to a mutable visitor.
/// @param visitor Receives the node and may rewrite the receiver or member name.
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

/// @brief Dispatch this method call expression node to a mutable visitor.
/// @param visitor Receives the node and may mutate the receiver or argument list.
void MethodCallExpr::accept(MutExprVisitor &visitor)
{
    visitor.visit(*this);
}

/// @brief Forwards this IS expression node to the visitor.
void IsExpr::accept(ExprVisitor &visitor) const
{
    visitor.visit(*this);
}

/// @brief Dispatch this IS expression node to a mutable visitor.
void IsExpr::accept(MutExprVisitor &visitor)
{
    visitor.visit(*this);
}

/// @brief Forwards this AS expression node to the visitor.
void AsExpr::accept(ExprVisitor &visitor) const
{
    visitor.visit(*this);
}

/// @brief Dispatch this AS expression node to a mutable visitor.
void AsExpr::accept(MutExprVisitor &visitor)
{
    visitor.visit(*this);
}

/// @brief Forwards this label statement node to the visitor for double dispatch.
/// @param visitor Receives the node; ownership remains with the AST.
void LabelStmt::accept(StmtVisitor &visitor) const
{
    visitor.visit(*this);
}

/// @brief Dispatch this label statement node to a mutable visitor.
/// @param visitor Receives the node and may adjust its metadata.
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

/// @brief Dispatch this print statement node to a mutable visitor.
/// @param visitor Receives the node and may edit arguments.
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

/// @brief Dispatch this PRINT # statement node to a mutable visitor.
/// @param visitor Receives the node and may edit channel or arguments.
void PrintChStmt::accept(MutStmtVisitor &visitor)
{
    visitor.visit(*this);
}

/// @brief Forwards this BEEP statement node to the visitor for double dispatch.
/// @param visitor Receives the node; ownership remains with the AST.
void BeepStmt::accept(StmtVisitor &visitor) const
{
    visitor.visit(*this);
}

/// @brief Dispatch this BEEP statement node to a mutable visitor.
/// @param visitor Receives the node.
void BeepStmt::accept(MutStmtVisitor &visitor)
{
    visitor.visit(*this);
}

/// @brief Forwards this CALL statement node to the visitor for double dispatch.
/// @param visitor Receives the node; ownership remains with the AST.
void CallStmt::accept(StmtVisitor &visitor) const
{
    visitor.visit(*this);
}

/// @brief Dispatch this CALL statement node to a mutable visitor.
/// @param visitor Receives the node and may rewrite the callee or arguments.
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

/// @brief Dispatch this CLS statement node to a mutable visitor.
/// @param visitor Receives the node and may modify screen options.
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

/// @brief Dispatch this COLOR statement node to a mutable visitor.
/// @param visitor Receives the node and may update colour arguments.
void ColorStmt::accept(MutStmtVisitor &visitor)
{
    visitor.visit(*this);
}

/// @brief Forwards this SLEEP statement node to the visitor for double dispatch.
/// @param visitor Receives the node; ownership remains with the AST.
void SleepStmt::accept(StmtVisitor &visitor) const
{
    visitor.visit(*this);
}

/// @brief Dispatch this SLEEP statement node to a mutable visitor.
/// @param visitor Receives the node and may update the duration expression.
void SleepStmt::accept(MutStmtVisitor &visitor)
{
    visitor.visit(*this);
}

/// @brief Forwards this LOCATE statement node to the visitor for double dispatch.
/// @param visitor Receives the node; ownership remains with the AST.
void LocateStmt::accept(StmtVisitor &visitor) const
{
    visitor.visit(*this);
}

/// @brief Dispatch this LOCATE statement node to a mutable visitor.
/// @param visitor Receives the node and may mutate cursor targets.
void LocateStmt::accept(MutStmtVisitor &visitor)
{
    visitor.visit(*this);
}

/// @brief Forwards this CURSOR statement node to the visitor for double dispatch.
/// @param visitor Receives the node; ownership remains with the AST.
void CursorStmt::accept(StmtVisitor &visitor) const
{
    visitor.visit(*this);
}

/// @brief Dispatch this CURSOR statement node to a mutable visitor.
/// @param visitor Receives the node and may update visibility flag.
void CursorStmt::accept(MutStmtVisitor &visitor)
{
    visitor.visit(*this);
}

/// @brief Forwards this ALTSCREEN statement node to the visitor for double dispatch.
/// @param visitor Receives the node; ownership remains with the AST.
void AltScreenStmt::accept(StmtVisitor &visitor) const
{
    visitor.visit(*this);
}

/// @brief Dispatch this ALTSCREEN statement node to a mutable visitor.
/// @param visitor Receives the node and may update enable flag.
void AltScreenStmt::accept(MutStmtVisitor &visitor)
{
    visitor.visit(*this);
}

/// @brief Forwards this let statement node to the visitor for double dispatch.
/// @param visitor Receives the node; ownership remains with the AST.
void LetStmt::accept(StmtVisitor &visitor) const
{
    visitor.visit(*this);
}

/// @brief Dispatch this LET statement node to a mutable visitor.
/// @param visitor Receives the node and may rewrite the assigned expression.
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

/// @brief Dispatch this DIM statement node to a mutable visitor.
/// @param visitor Receives the node and may alter the declared array metadata.
void DimStmt::accept(MutStmtVisitor &visitor)
{
    visitor.visit(*this);
}

/// @brief Forwards this CONST statement node to the visitor for double dispatch.
/// @param visitor Receives the node; ownership remains with the AST.
void ConstStmt::accept(StmtVisitor &visitor) const
{
    visitor.visit(*this);
}

/// @brief Dispatch this CONST statement node to a mutable visitor.
/// @param visitor Receives the node and may alter the declared constant metadata.
void ConstStmt::accept(MutStmtVisitor &visitor)
{
    visitor.visit(*this);
}

/// @brief Forwards this STATIC statement node to the visitor for double dispatch.
/// @param visitor Receives the node; ownership remains with the AST.
void StaticStmt::accept(StmtVisitor &visitor) const
{
    visitor.visit(*this);
}

/// @brief Dispatch this STATIC statement node to a mutable visitor.
/// @param visitor Receives the node and may alter the declared variable metadata.
void StaticStmt::accept(MutStmtVisitor &visitor)
{
    visitor.visit(*this);
}

// SharedStmt accept methods are defined inline in the header.

/// @brief Forwards this REDIM statement node to the visitor for double dispatch.
/// @param visitor Receives the node; ownership remains with the AST.
void ReDimStmt::accept(StmtVisitor &visitor) const
{
    visitor.visit(*this);
}

/// @brief Dispatch this REDIM statement node to a mutable visitor.
/// @param visitor Receives the node and may update extents or target symbol.
void ReDimStmt::accept(MutStmtVisitor &visitor)
{
    visitor.visit(*this);
}

/// @brief Dispatch this SWAP statement node to a visitor.
/// @param visitor Visitor to receive this SWAP statement.
void SwapStmt::accept(StmtVisitor &visitor) const
{
    visitor.visit(*this);
}

/// @brief Dispatch this SWAP statement node to a mutable visitor.
/// @param visitor Mutable visitor to receive this SWAP statement.
void SwapStmt::accept(MutStmtVisitor &visitor)
{
    visitor.visit(*this);
}

/// @brief Forwards this randomize statement node to the visitor for double dispatch.
/// @param visitor Receives the node; ownership remains with the AST.
void RandomizeStmt::accept(StmtVisitor &visitor) const
{
    visitor.visit(*this);
}

/// @brief Dispatch this RANDOMIZE statement node to a mutable visitor.
/// @param visitor Receives the node and may mutate seed expression.
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

/// @brief Dispatch this IF statement node to a mutable visitor.
/// @param visitor Receives the node and may mutate predicates or branches.
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

/// @brief Dispatch this SELECT CASE statement node to a mutable visitor.
/// @param visitor Receives the node and may adjust selector or branches.
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

/// @brief Dispatch this WHILE statement node to a mutable visitor.
/// @param visitor Receives the node and may mutate the loop predicate.
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

/// @brief Dispatch this DO statement node to a mutable visitor.
/// @param visitor Receives the node and may update loop condition or body.
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

/// @brief Dispatch this FOR loop node to a mutable visitor.
/// @param visitor Receives the node and may rewrite bounds or step.
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

/// @brief Dispatch this NEXT statement node to a mutable visitor.
/// @param visitor Receives the node and may adjust control variable metadata.
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

/// @brief Dispatch this EXIT statement node to a mutable visitor.
/// @param visitor Receives the node and may adjust loop or procedure target.
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

/// @brief Dispatch this GOTO statement node to a mutable visitor.
/// @param visitor Receives the node and may retarget the jump label.
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

/// @brief Dispatch this GOSUB statement node to a mutable visitor.
/// @param visitor Receives the node and may retarget the subroutine label.
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

/// @brief Dispatch this OPEN statement node to a mutable visitor.
/// @param visitor Receives the node and may update mode or path expressions.
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

/// @brief Dispatch this CLOSE statement node to a mutable visitor.
/// @param visitor Receives the node and may adjust channel identifiers.
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

/// @brief Dispatch this SEEK statement node to a mutable visitor.
/// @param visitor Receives the node and may alter channel or position expressions.
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

/// @brief Dispatch this ON ERROR GOTO statement node to a mutable visitor.
/// @param visitor Receives the node and may rewrite the target label.
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

/// @brief Dispatch this RESUME statement node to a mutable visitor.
/// @param visitor Receives the node and may retarget the resume label.
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

/// @brief Dispatch this END statement node to a mutable visitor.
/// @param visitor Receives the node and may mutate associated expression data.
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

/// @brief Dispatch this INPUT statement node to a mutable visitor.
/// @param visitor Receives the node and may adjust prompts or destinations.
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

/// @brief Dispatch this INPUT # statement node to a mutable visitor.
/// @param visitor Receives the node and may mutate channel or destination list.
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

/// @brief Dispatch this LINE INPUT # statement node to a mutable visitor.
/// @param visitor Receives the node and may update channel or destination.
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

/// @brief Dispatch this RETURN statement node to a mutable visitor.
/// @param visitor Receives the node and may adjust the target label.
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

/// @brief Dispatch this FUNCTION declaration node to a mutable visitor.
/// @param visitor Receives the node and may mutate the procedure body.
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

/// @brief Dispatch this SUB declaration node to a mutable visitor.
/// @param visitor Receives the node and may mutate the procedure body.
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

/// @brief Dispatch this statement list node to a mutable visitor.
/// @param visitor Receives the node and may reorder or mutate child statements.
void StmtList::accept(MutStmtVisitor &visitor)
{
    visitor.visit(*this);
}

/// @brief Forwards this DELETE statement node to the visitor for double dispatch.
/// @param visitor Receives the node; ownership remains with the AST.
void DeleteStmt::accept(StmtVisitor &visitor) const
{
    visitor.visit(*this);
}

/// @brief Dispatch this DELETE statement node to a mutable visitor.
/// @param visitor Receives the node and may update the target expression.
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

/// @brief Dispatch this constructor declaration node to a mutable visitor.
/// @param visitor Receives the node and may modify parameter or body data.
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

/// @brief Dispatch this destructor declaration node to a mutable visitor.
/// @param visitor Receives the node and may mutate its body.
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

/// @brief Dispatch this method declaration node to a mutable visitor.
/// @param visitor Receives the node and may mutate parameters or body.
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

/// @brief Dispatch this CLASS declaration node to a mutable visitor.
/// @param visitor Receives the node and may edit members or base list.
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

/// @brief Dispatch this TYPE declaration node to a mutable visitor.
/// @param visitor Receives the node and may mutate field definitions.
void TypeDecl::accept(MutStmtVisitor &visitor)
{
    visitor.visit(*this);
}

/// @brief Forwards this INTERFACE declaration node to the visitor.
void InterfaceDecl::accept(StmtVisitor &visitor) const
{
    visitor.visit(*this);
}

/// @brief Dispatch this INTERFACE declaration node to a mutable visitor.
void InterfaceDecl::accept(MutStmtVisitor &visitor)
{
    visitor.visit(*this);
}

} // namespace il::frontends::basic
