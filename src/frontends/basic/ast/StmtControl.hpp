//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: frontends/basic/ast/StmtControl.hpp
// Purpose: Defines BASIC control-flow oriented statement nodes.
// Key invariants: Control statements maintain ownership of nested bodies via
// Ownership/Lifetime: Statements own nested statements through unique_ptr wrappers.
// Links: docs/codemap.md
//
//===----------------------------------------------------------------------===//

#pragma once

#include "frontends/basic/SelectModel.hpp"
#include "frontends/basic/ast/ExprNodes.hpp"
#include "frontends/basic/ast/StmtBase.hpp"

#include "support/source_location.hpp"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace il::frontends::basic
{

/// @brief Pseudo statement that only carries a line label.
struct LabelStmt : Stmt
{
    [[nodiscard]] constexpr Kind stmtKind() const noexcept override
    {
        return Kind::Label;
    }

    void accept(StmtVisitor &visitor) const override;
    void accept(MutStmtVisitor &visitor) override;
};

/// @brief IF statement with optional ELSEIF chain and ELSE branch.
struct IfStmt : Stmt
{
    [[nodiscard]] constexpr Kind stmtKind() const noexcept override
    {
        return Kind::If;
    }

    /// @brief ELSEIF arm.
    struct ElseIf
    {
        /// Condition expression controlling this arm; owned and non-null.
        ExprPtr cond;

        /// Executed when @ref cond evaluates to true; owned and non-null.
        StmtPtr then_branch;
    };

    /// Initial IF condition; owned and non-null.
    ExprPtr cond;

    /// THEN branch when @ref cond is true; owned and non-null.
    StmtPtr then_branch;

    /// Zero or more ELSEIF arms evaluated in order.
    std::vector<ElseIf> elseifs;

    /// Optional trailing ELSE branch (may be null) executed when no condition matched.
    StmtPtr else_branch;
    void accept(StmtVisitor &visitor) const override;
    void accept(MutStmtVisitor &visitor) override;
};

/// @brief Arm within a SELECT CASE statement.
struct CaseArm
{
    /// @brief Relational guard matched by the arm.
    struct CaseRel
    {
        /// @brief Relational operation kind.
        enum Op
        {
            LT, ///< Selector must be less than rhs.
            LE, ///< Selector must be less than or equal to rhs.
            EQ, ///< Selector must equal rhs.
            GE, ///< Selector must be greater than or equal to rhs.
            GT  ///< Selector must be greater than rhs.
        };

        /// @brief Relational operator applied to the selector.
        Op op{EQ};

        /// @brief Right-hand-side integer operand compared against the selector.
        std::int64_t rhs{0};
    };

    /// @brief Literal labels matched by the arm.
    std::vector<std::int64_t> labels;

    /// @brief String literal labels matched by the arm when the selector is a string.
    std::vector<std::string> str_labels;

    /// @brief Inclusive integer ranges matched by the arm.
    std::vector<std::pair<std::int64_t, std::int64_t>> ranges;

    /// @brief Relational comparisons matched by the arm.
    std::vector<CaseRel> rels;

    /// @brief Statements executed when the labels match.
    std::vector<StmtPtr> body;

    /// @brief Source range covering the CASE keyword and its labels.
    il::support::SourceRange range{};

    /// @brief Length of the CASE keyword lexeme for diagnostics.
    uint32_t caseKeywordLength = 0;
};

/// @brief SELECT CASE statement with zero or more CASE arms and optional ELSE body.
struct SelectCaseStmt : Stmt
{
    [[nodiscard]] constexpr Kind stmtKind() const noexcept override
    {
        return Kind::SelectCase;
    }

    /// @brief Expression whose value selects a CASE arm; owned and non-null.
    ExprPtr selector;

    /// @brief Ordered CASE arms evaluated sequentially.
    std::vector<CaseArm> arms;

    /// @brief Statements executed when no CASE label matches; empty when absent.
    std::vector<StmtPtr> elseBody;

    /// @brief Source range spanning the SELECT CASE header.
    il::support::SourceRange range{};

    /// @brief Normalised model describing CASE labels and ranges.
    SelectModel model{};

    void accept(StmtVisitor &visitor) const override;
    void accept(MutStmtVisitor &visitor) override;
};

/// @brief TRY/CATCH statement with optional catch variable.
struct TryCatchStmt : Stmt
{
    [[nodiscard]] constexpr Kind stmtKind() const noexcept override
    {
        return Kind::TryCatch;
    }

    /// @brief Statements executed under the TRY region.
    std::vector<StmtPtr> tryBody;

    /// @brief Optional catch variable name (binds error code as i64 when present).
    std::optional<std::string> catchVar;

    /// @brief Statements executed when an error is caught.
    std::vector<StmtPtr> catchBody;

    /// @brief Source range covering the TRY…CATCH header for diagnostics.
    il::support::SourceRange header{};

    void accept(StmtVisitor &visitor) const override;
    void accept(MutStmtVisitor &visitor) override;
};

/// @brief WHILE ... WEND loop statement.
struct WhileStmt : Stmt
{
    [[nodiscard]] constexpr Kind stmtKind() const noexcept override
    {
        return Kind::While;
    }

    /// Loop continuation condition; owned and non-null.
    ExprPtr cond;

    /// Body statements executed while @ref cond is true.
    std::vector<StmtPtr> body;
    void accept(StmtVisitor &visitor) const override;
    void accept(MutStmtVisitor &visitor) override;
};

/// @brief DO ... LOOP statement supporting WHILE and UNTIL tests.
struct DoStmt : Stmt
{
    [[nodiscard]] constexpr Kind stmtKind() const noexcept override
    {
        return Kind::Do;
    }
    /// Condition kind controlling loop continuation.
    enum class CondKind
    {
        None,  ///< No explicit condition; loop runs until EXIT.
        While, ///< Continue while condition evaluates to true.
        Until, ///< Continue until condition evaluates to true.
    } condKind{CondKind::None};

    /// Whether condition is evaluated before or after executing the body.
    enum class TestPos
    {
        Pre,  ///< Evaluate condition before each iteration.
        Post, ///< Evaluate condition after executing the body.
    } testPos{TestPos::Pre};

    /// Continuation condition; null when @ref condKind == CondKind::None.
    ExprPtr cond;

    /// Ordered statements forming the loop body.
    std::vector<StmtPtr> body;
    void accept(StmtVisitor &visitor) const override;
    void accept(MutStmtVisitor &visitor) override;
};

/// @brief FOR ... NEXT loop statement.
struct ForStmt : Stmt
{
    [[nodiscard]] constexpr Kind stmtKind() const noexcept override
    {
        return Kind::For;
    }

    /// Loop variable expression (lvalue) controlling the iteration.
    /// Can be a VarExpr (simple variable), MemberAccessExpr (object member),
    /// or ArrayExpr (array element). Owned and non-null.
    /// BUG-081 fix: Changed from std::string to ExprPtr to support object members.
    ExprPtr varExpr;

    /// Initial value assigned to loop variable; owned and non-null.
    ExprPtr start;

    /// Loop end value; owned and non-null.
    ExprPtr end;

    /// Optional step expression; null means 1.
    ExprPtr step;

    /// Body statements executed each iteration.
    std::vector<StmtPtr> body;
    void accept(StmtVisitor &visitor) const override;
    void accept(MutStmtVisitor &visitor) override;
};

/// @brief FOR EACH ... IN ... NEXT loop statement for array iteration.
/// @details Iterates over all elements of an array, assigning each element
///          to the loop variable in sequence. The loop runs from the first
///          to the last element of the array.
struct ForEachStmt : Stmt
{
    [[nodiscard]] constexpr Kind stmtKind() const noexcept override
    {
        return Kind::ForEach;
    }

    /// Name of the element variable receiving each array element.
    std::string elementVar;

    /// Name of the array being iterated.
    std::string arrayName;

    /// Body statements executed for each element.
    std::vector<StmtPtr> body;

    void accept(StmtVisitor &visitor) const override;
    void accept(MutStmtVisitor &visitor) override;
};

/// @brief NEXT statement closing a FOR.
struct NextStmt : Stmt
{
    [[nodiscard]] constexpr Kind stmtKind() const noexcept override
    {
        return Kind::Next;
    }

    /// Loop variable after NEXT.
    std::string var;
    void accept(StmtVisitor &visitor) const override;
    void accept(MutStmtVisitor &visitor) override;
};

/// @brief EXIT statement leaving the innermost enclosing loop.
struct ExitStmt : Stmt
{
    [[nodiscard]] constexpr Kind stmtKind() const noexcept override
    {
        return Kind::Exit;
    }
    /// Loop type targeted by this EXIT.
    enum class LoopKind
    {
        For,      ///< EXIT FOR
        While,    ///< EXIT WHILE
        Do,       ///< EXIT DO
        Sub,      ///< EXIT SUB
        Function, ///< EXIT FUNCTION
    } kind{LoopKind::While};

    void accept(StmtVisitor &visitor) const override;
    void accept(MutStmtVisitor &visitor) override;
};

/// @brief GOTO statement transferring control to a line number.
struct GotoStmt : Stmt
{
    [[nodiscard]] constexpr Kind stmtKind() const noexcept override
    {
        return Kind::Goto;
    }

    /// Target line number to jump to.
    int target{0};
    void accept(StmtVisitor &visitor) const override;
    void accept(MutStmtVisitor &visitor) override;
};

/// @brief GOSUB statement invoking a line label as a subroutine.
struct GosubStmt : Stmt
{
    [[nodiscard]] constexpr Kind stmtKind() const noexcept override
    {
        return Kind::Gosub;
    }

    /// Target line number to branch to.
    int targetLine{0};
    void accept(StmtVisitor &visitor) const override;
    void accept(MutStmtVisitor &visitor) override;
};

/// @brief ON ERROR GOTO statement configuring error handler target.
struct OnErrorGoto : Stmt
{
    [[nodiscard]] constexpr Kind stmtKind() const noexcept override
    {
        return Kind::OnErrorGoto;
    }

    /// Destination line for error handler when @ref toZero is false.
    int target{0};

    /// True when the statement uses "GOTO 0" to disable the handler.
    bool toZero{false};
    void accept(StmtVisitor &visitor) const override;
    void accept(MutStmtVisitor &visitor) override;
};

/// @brief RESUME statement controlling error-handler resumption.
struct Resume : Stmt
{
    [[nodiscard]] constexpr Kind stmtKind() const noexcept override
    {
        return Kind::Resume;
    }
    /// Resumption strategy following an error handler.
    enum class Mode
    {
        Same,  ///< Resume execution at the failing statement.
        Next,  ///< Resume at the statement following the failure site.
        Label, ///< Resume at a labeled line.
    } mode{Mode::Same};

    /// Target line label when @ref mode == Mode::Label.
    int target{0};
    void accept(StmtVisitor &visitor) const override;
    void accept(MutStmtVisitor &visitor) override;
};

/// @brief END statement terminating program execution.
struct EndStmt : Stmt
{
    [[nodiscard]] constexpr Kind stmtKind() const noexcept override
    {
        return Kind::End;
    }

    void accept(StmtVisitor &visitor) const override;
    void accept(MutStmtVisitor &visitor) override;
};

/// @brief RETURN statement optionally yielding a value.
struct ReturnStmt : Stmt
{
    [[nodiscard]] constexpr Kind stmtKind() const noexcept override
    {
        return Kind::Return;
    }

    /// Expression whose value is returned; null when no expression is provided.
    ExprPtr value;

    /// True when this RETURN exits a GOSUB (top-level RETURN without a value).
    bool isGosubReturn{false};
    void accept(StmtVisitor &visitor) const override;
    void accept(MutStmtVisitor &visitor) override;
};

} // namespace il::frontends::basic
