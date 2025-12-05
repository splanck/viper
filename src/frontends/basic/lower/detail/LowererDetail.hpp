//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE in the project root for license information.
//
// File: src/frontends/basic/lower/detail/LowererDetail.hpp
//
// Summary:
//   Internal detail header for the BASIC lowering subsystem. Contains shared
//   type definitions and helper declarations used across lowering translation
//   units. NOT part of the public API - should only be included by lower/*.cpp.
//
//===----------------------------------------------------------------------===//

#pragma once

#include "frontends/basic/LowererTypes.hpp"
#include "viper/il/Module.hpp"

#include <functional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace il::frontends::basic
{

class Lowerer;
struct OopLoweringContext;

namespace lower::detail
{

//===----------------------------------------------------------------------===//
// Expression Lowering Helpers
//===----------------------------------------------------------------------===//

/// @brief Helper class coordinating expression lowering operations.
/// @details Encapsulates expression lowering logic including literals, variables,
///          unary/binary operators, calls, and OOP expressions. Works with the
///          visitor pattern to dispatch to specific lowering routines.
/// @invariant All methods preserve Lowerer state consistency.
/// @ownership Borrows Lowerer reference; does not own AST nodes.
class ExprLoweringHelper
{
  public:
    explicit ExprLoweringHelper(Lowerer &lowerer) noexcept;

    /// @brief Lower a variable reference expression.
    [[nodiscard]] RVal lowerVarExpr(const VarExpr &expr);

    /// @brief Lower a unary expression (NOT, negation, etc.).
    [[nodiscard]] RVal lowerUnaryExpr(const UnaryExpr &expr);

    /// @brief Lower a binary expression.
    [[nodiscard]] RVal lowerBinaryExpr(const BinaryExpr &expr);

    /// @brief Lower a builtin function call expression.
    [[nodiscard]] RVal lowerBuiltinCall(const BuiltinCallExpr &expr);

    /// @brief Lower a UBOUND query expression.
    [[nodiscard]] RVal lowerUBoundExpr(const UBoundExpr &expr);

    /// @brief Lower logical (AND/OR) expressions with short-circuiting.
    [[nodiscard]] RVal lowerLogicalBinary(const BinaryExpr &expr);

    /// @brief Lower integer division and modulo with divide-by-zero check.
    [[nodiscard]] RVal lowerDivOrMod(const BinaryExpr &expr);

    /// @brief Lower string concatenation and equality/inequality comparisons.
    [[nodiscard]] RVal lowerStringBinary(const BinaryExpr &expr, RVal lhs, RVal rhs);

    /// @brief Lower numeric arithmetic and comparisons.
    [[nodiscard]] RVal lowerNumericBinary(const BinaryExpr &expr, RVal lhs, RVal rhs);

    /// @brief Lower the power operator by invoking the runtime helper.
    [[nodiscard]] RVal lowerPowBinary(const BinaryExpr &expr, RVal lhs, RVal rhs);

  private:
    Lowerer &lowerer_;
};

//===----------------------------------------------------------------------===//
// Control Flow Lowering Helpers
//===----------------------------------------------------------------------===//

/// @brief Helper class coordinating control flow statement lowering.
/// @details Handles IF, WHILE, DO, FOR, SELECT CASE, GOTO, GOSUB, and related
///          control flow constructs. Manages block creation and branching.
/// @invariant Preserves CFG validity (single terminator per block).
/// @ownership Borrows Lowerer reference; does not own AST nodes.
class ControlLoweringHelper
{
  public:
    explicit ControlLoweringHelper(Lowerer &lowerer) noexcept;

    /// @brief Lower an IF statement with optional ELSEIF/ELSE branches.
    void lowerIf(const IfStmt &stmt);

    /// @brief Lower a WHILE loop.
    void lowerWhile(const WhileStmt &stmt);

    /// @brief Lower a DO loop (DO WHILE/DO UNTIL variants).
    void lowerDo(const DoStmt &stmt);

    /// @brief Lower a FOR loop with bounds and step.
    void lowerFor(const ForStmt &stmt);

    /// @brief Lower a FOR EACH array iteration loop.
    void lowerForEach(const ForEachStmt &stmt);

    /// @brief Lower a SELECT CASE statement.
    void lowerSelectCase(const SelectCaseStmt &stmt);

    /// @brief Lower a NEXT statement (FOR loop increment).
    void lowerNext(const NextStmt &stmt);

    /// @brief Lower an EXIT statement (loop/procedure exit).
    void lowerExit(const ExitStmt &stmt);

    /// @brief Lower a GOTO statement.
    void lowerGoto(const GotoStmt &stmt);

    /// @brief Lower a GOSUB statement.
    void lowerGosub(const GosubStmt &stmt);

    /// @brief Lower a GOSUB RETURN statement.
    void lowerGosubReturn(const ReturnStmt &stmt);

    /// @brief Lower an ON ERROR GOTO handler.
    void lowerOnErrorGoto(const OnErrorGoto &stmt);

    /// @brief Lower a RESUME statement.
    void lowerResume(const Resume &stmt);

    /// @brief Lower an END statement.
    void lowerEnd(const EndStmt &stmt);

    /// @brief Lower a TRY/CATCH statement.
    void lowerTryCatch(const TryCatchStmt &stmt);

  private:
    Lowerer &lowerer_;
};

//===----------------------------------------------------------------------===//
// OOP Lowering Helpers
//===----------------------------------------------------------------------===//

/// @brief Helper class coordinating OOP construct lowering.
/// @details Handles NEW expressions, ME references, member access, method calls,
///          DELETE statements, and class/constructor/method emission.
/// @invariant Maintains class layout consistency during lowering.
/// @ownership Borrows Lowerer reference and OOP context; does not own AST nodes.
class OopLoweringHelper
{
  public:
    explicit OopLoweringHelper(Lowerer &lowerer) noexcept;

    /// @brief Lower a NEW expression allocating a BASIC object instance.
    [[nodiscard]] RVal lowerNewExpr(const NewExpr &expr);
    [[nodiscard]] RVal lowerNewExpr(const NewExpr &expr, OopLoweringContext &ctx);

    /// @brief Lower a ME expression referencing the implicit instance slot.
    [[nodiscard]] RVal lowerMeExpr(const MeExpr &expr);
    [[nodiscard]] RVal lowerMeExpr(const MeExpr &expr, OopLoweringContext &ctx);

    /// @brief Lower a member access reading a field from an object instance.
    [[nodiscard]] RVal lowerMemberAccessExpr(const MemberAccessExpr &expr);
    [[nodiscard]] RVal lowerMemberAccessExpr(const MemberAccessExpr &expr, OopLoweringContext &ctx);

    /// @brief Lower an object method invocation expression.
    [[nodiscard]] RVal lowerMethodCallExpr(const MethodCallExpr &expr);
    [[nodiscard]] RVal lowerMethodCallExpr(const MethodCallExpr &expr, OopLoweringContext &ctx);

    /// @brief Lower a DELETE statement releasing an object reference.
    void lowerDelete(const DeleteStmt &stmt);
    void lowerDelete(const DeleteStmt &stmt, OopLoweringContext &ctx);

    /// @brief Scan program OOP constructs to populate class layouts and runtime requests.
    void scanOOP(const Program &prog);

    /// @brief Emit constructor, destructor, and method bodies for CLASS declarations.
    void emitOopDeclsAndBodies(const Program &prog);

  private:
    Lowerer &lowerer_;
};

//===----------------------------------------------------------------------===//
// Runtime Helpers
//===----------------------------------------------------------------------===//

/// @brief Helper class coordinating runtime statement lowering.
/// @details Handles DIM, REDIM, LET, CONST, STATIC, SWAP, RANDOMIZE, and other
///          runtime-related statements that interact with memory and state.
/// @invariant Preserves symbol table and slot consistency.
/// @ownership Borrows Lowerer reference; does not own AST nodes.
class RuntimeLoweringHelper
{
  public:
    explicit RuntimeLoweringHelper(Lowerer &lowerer) noexcept;

    /// @brief Lower a LET assignment statement.
    void lowerLet(const LetStmt &stmt);

    /// @brief Lower a CONST statement.
    void lowerConst(const ConstStmt &stmt);

    /// @brief Lower a STATIC statement.
    void lowerStatic(const StaticStmt &stmt);

    /// @brief Lower a DIM statement.
    void lowerDim(const DimStmt &stmt);

    /// @brief Lower a REDIM statement.
    void lowerReDim(const ReDimStmt &stmt);

    /// @brief Lower a RANDOMIZE statement.
    void lowerRandomize(const RandomizeStmt &stmt);

    /// @brief Lower a SWAP statement.
    void lowerSwap(const SwapStmt &stmt);

  private:
    Lowerer &lowerer_;
};

} // namespace lower::detail
} // namespace il::frontends::basic
