// File: src/frontends/basic/ast/StmtExpr.hpp
// Purpose: Defines BASIC statement nodes that primarily manipulate expressions or IO.
// Key invariants: Expression-owned members are non-null when documented and retain
//                 ownership semantics described by ExprPtr/LValuePtr aliases.
// Ownership/Lifetime: Statements own child expressions through unique_ptr wrappers.
// Links: docs/codemap.md
#pragma once

#include "frontends/basic/ast/ExprNodes.hpp"
#include "frontends/basic/ast/StmtBase.hpp"

#include <string>
#include <utility>
#include <vector>

namespace il::frontends::basic
{

/// @brief Item within a PRINT statement.
struct PrintItem
{
    /// Kind of item to output.
    enum class Kind
    {
        Expr,      ///< Expression to print.
        Comma,     ///< Insert a space.
        Semicolon, ///< Insert nothing.
    } kind{Kind::Expr};

    /// Expression value when @ref kind == Kind::Expr; owned.
    ExprPtr expr;
};

/// @brief PRINT statement outputting a sequence of expressions and separators.
/// Trailing semicolon suppresses the automatic newline.
/// @invariant items.size() > 0
struct PrintStmt : Stmt
{
    [[nodiscard]] constexpr Kind stmtKind() const noexcept override
    {
        return Kind::Print;
    }

    /// Items printed in order; unless the last item is a semicolon, a newline is appended.
    std::vector<PrintItem> items;
    void accept(StmtVisitor &visitor) const override;
    void accept(MutStmtVisitor &visitor) override;
};

/// @brief PRINT # statement that outputs to a file channel.
struct PrintChStmt : Stmt
{
    [[nodiscard]] constexpr Kind stmtKind() const noexcept override
    {
        return Kind::PrintCh;
    }
    enum class Mode
    {
        Print,
        Write,
    } mode{Mode::Print};
    /// Channel expression evaluated to select the file handle; owned and non-null.
    ExprPtr channelExpr;

    /// Expressions printed to the channel, separated by commas in source.
    std::vector<ExprPtr> args;

    /// True when a trailing newline should be emitted after printing.
    bool trailingNewline{true};

    void accept(StmtVisitor &visitor) const override;
    void accept(MutStmtVisitor &visitor) override;
};

/// @brief BEEP statement emitting a bell/beep sound.
struct BeepStmt : Stmt
{
    [[nodiscard]] constexpr Kind stmtKind() const noexcept override
    {
        return Kind::Beep;
    }

    void accept(StmtVisitor &visitor) const override;
    void accept(MutStmtVisitor &visitor) override;
};

/// @brief CALL statement invoking a user-defined SUB or instance method.
struct CallStmt : Stmt
{
    [[nodiscard]] constexpr Kind stmtKind() const noexcept override
    {
        return Kind::Call;
    }

    /// Invocation expression with side effects (SUB or instance method).
    /// May be a CallExpr or a MethodCallExpr.
    ExprPtr call;
    void accept(StmtVisitor &visitor) const override;
    void accept(MutStmtVisitor &visitor) override;
};

/// @brief CLS statement clearing the screen.
struct ClsStmt : Stmt
{
    [[nodiscard]] constexpr Kind stmtKind() const noexcept override
    {
        return Kind::Cls;
    }

    void accept(StmtVisitor &visitor) const override;
    void accept(MutStmtVisitor &visitor) override;
};

/// @brief COLOR statement changing the palette.
struct ColorStmt : Stmt
{
    [[nodiscard]] constexpr Kind stmtKind() const noexcept override
    {
        return Kind::Color;
    }

    /// Foreground color expression; may be null when omitted.
    ExprPtr fg;

    /// Background color expression; may be null when omitted.
    ExprPtr bg;

    void accept(StmtVisitor &visitor) const override;
    void accept(MutStmtVisitor &visitor) override;
};

/// @brief SLEEP statement blocking for a duration in milliseconds.
struct SleepStmt : Stmt
{
    [[nodiscard]] constexpr Kind stmtKind() const noexcept override
    {
        return Kind::Sleep;
    }

    /// Millisecond duration expression; owned and non-null.
    ExprPtr ms;

    void accept(StmtVisitor &visitor) const override;
    void accept(MutStmtVisitor &visitor) override;
};

/// @brief LOCATE statement moving the cursor.
struct LocateStmt : Stmt
{
    [[nodiscard]] constexpr Kind stmtKind() const noexcept override
    {
        return Kind::Locate;
    }

    /// Row expression (1-based); owned and non-null.
    ExprPtr row;

    /// Column expression (1-based); owned and non-null.
    ExprPtr col;

    /// Optional cursor visibility expression.
    ExprPtr cursor;

    /// Optional start scan line expression.
    ExprPtr start;

    /// Optional stop scan line expression.
    ExprPtr stop;

    void accept(StmtVisitor &visitor) const override;
    void accept(MutStmtVisitor &visitor) override;
};

/// @brief CURSOR statement controlling cursor visibility.
struct CursorStmt : Stmt
{
    [[nodiscard]] constexpr Kind stmtKind() const noexcept override
    {
        return Kind::Cursor;
    }

    /// True for CURSOR ON, false for CURSOR OFF.
    bool visible{true};

    void accept(StmtVisitor &visitor) const override;
    void accept(MutStmtVisitor &visitor) override;
};

/// @brief ALTSCREEN statement controlling alternate screen buffer.
struct AltScreenStmt : Stmt
{
    [[nodiscard]] constexpr Kind stmtKind() const noexcept override
    {
        return Kind::AltScreen;
    }

    /// True for ALTSCREEN ON, false for ALTSCREEN OFF.
    bool enable{true};

    void accept(StmtVisitor &visitor) const override;
    void accept(MutStmtVisitor &visitor) override;
};

/// @brief LET statement assigning to an lvalue.
struct LetStmt : Stmt
{
    [[nodiscard]] constexpr Kind stmtKind() const noexcept override
    {
        return Kind::Let;
    }

    /// Left-hand side receiving the assignment; owned and non-null.
    LValuePtr target;

    /// Right-hand side expression; owned and non-null.
    ExprPtr expr;

    void accept(StmtVisitor &visitor) const override;
    void accept(MutStmtVisitor &visitor) override;
};

/// @brief CONST statement declaring a constant.
/// @invariant Initializer expression must be non-null.
struct ConstStmt : Stmt
{
    [[nodiscard]] constexpr Kind stmtKind() const noexcept override
    {
        return Kind::Const;
    }

    /// Constant name being declared.
    std::string name;

    /// Initializer expression; owned and non-null.
    ExprPtr initializer;

    /// Declared BASIC type for this constant.
    Type type{Type::I64};

    void accept(StmtVisitor &visitor) const override;
    void accept(MutStmtVisitor &visitor) override;
};

/// @brief DIM statement declaring a variable or array.
struct DimStmt : Stmt
{
    [[nodiscard]] constexpr Kind stmtKind() const noexcept override
    {
        return Kind::Dim;
    }

    /// Array name being declared.
    std::string name;

    /// Number of elements to allocate when @ref isArray is true; may be null for scalars.
    /// For multi-dimensional arrays, use @ref dimensions instead.
    /// @deprecated Use @ref dimensions for multi-dimensional support.
    ExprPtr size;

    /// Dimension sizes for multi-dimensional arrays (owned).
    /// For single-dimensional arrays, this contains one element (backward compatible).
    /// Empty for scalar declarations.
    std::vector<ExprPtr> dimensions;

    /// Declared BASIC type for this DIM.
    Type type{Type::I64};

    /// True when DIM declares an array; false for scalar declarations.
    bool isArray{true};
    void accept(StmtVisitor &visitor) const override;
    void accept(MutStmtVisitor &visitor) override;
};

/// @brief STATIC statement declaring a persistent procedure-local variable.
struct StaticStmt : Stmt
{
    [[nodiscard]] constexpr Kind stmtKind() const noexcept override
    {
        return Kind::Static;
    }

    /// Variable name being declared.
    std::string name;

    /// Declared BASIC type for this STATIC variable.
    Type type{Type::I64};

    void accept(StmtVisitor &visitor) const override;
    void accept(MutStmtVisitor &visitor) override;
};

/// @brief SHARED statement declaring that names refer to module-level storage.
/// @details Classic BASIC uses SHARED within procedures to indicate that listed
///          variables refer to module-level bindings. In this implementation the
///          analyser already allows accessing module-level variables from
///          procedures, so this statement is effectively a no-op and primarily
///          exists for compatibility and better diagnostics.
struct SharedStmt : Stmt
{
    std::vector<std::string> names; ///< Names listed in the SHARED statement.

    [[nodiscard]] Kind stmtKind() const noexcept override { return Kind::Shared; }
    void accept(StmtVisitor &visitor) const override { visitor.visit(*this); }
    void accept(MutStmtVisitor &visitor) override { visitor.visit(*this); }
};

/// @brief REDIM statement resizing an existing array.
struct ReDimStmt : Stmt
{
    [[nodiscard]] constexpr Kind stmtKind() const noexcept override
    {
        return Kind::ReDim;
    }

    /// Array name whose storage is being reallocated.
    std::string name;

    /// Number of elements in the resized array; owned and non-null.
    ExprPtr size;

    void accept(StmtVisitor &visitor) const override;
    void accept(MutStmtVisitor &visitor) override;
};

/// @brief SWAP statement for exchanging values of two variables.
struct SwapStmt : Stmt
{
    [[nodiscard]] constexpr Kind stmtKind() const noexcept override
    {
        return Kind::Swap;
    }

    /// First variable to swap.
    LValuePtr lhs;

    /// Second variable to swap.
    LValuePtr rhs;

    void accept(StmtVisitor &visitor) const override;
    void accept(MutStmtVisitor &visitor) override;
};

/// @brief RANDOMIZE statement seeding the pseudo-random generator.
struct RandomizeStmt : Stmt
{
    [[nodiscard]] constexpr Kind stmtKind() const noexcept override
    {
        return Kind::Randomize;
    }

    /// Numeric seed expression, truncated to i64; owned and non-null.
    ExprPtr seed;
    void accept(StmtVisitor &visitor) const override;
    void accept(MutStmtVisitor &visitor) override;
};

/// @brief OPEN statement configuring a file channel.
struct OpenStmt : Stmt
{
    [[nodiscard]] constexpr Kind stmtKind() const noexcept override
    {
        return Kind::Open;
    }

    /// File path expression; owned and non-null.
    ExprPtr pathExpr;

    /// File mode keyword.
    enum class Mode
    {
        Input,
        Output,
        Append,
        Binary,
        Random,
    } mode{Mode::Input};

    /// File number expression; owned and non-null.
    ExprPtr channelExpr;

    void accept(StmtVisitor &visitor) const override;
    void accept(MutStmtVisitor &visitor) override;
};

/// @brief CLOSE statement closing a file channel.
struct CloseStmt : Stmt
{
    [[nodiscard]] constexpr Kind stmtKind() const noexcept override
    {
        return Kind::Close;
    }

    /// Optional file channel expression; null closes all.
    ExprPtr channelExpr;
    void accept(StmtVisitor &visitor) const override;
    void accept(MutStmtVisitor &visitor) override;
};

/// @brief SEEK statement moving a file position.
struct SeekStmt : Stmt
{
    [[nodiscard]] constexpr Kind stmtKind() const noexcept override
    {
        return Kind::Seek;
    }

    /// File channel expression; owned and non-null.
    ExprPtr channelExpr;

    /// Absolute file position expression.
    ExprPtr positionExpr;

    void accept(StmtVisitor &visitor) const override;
    void accept(MutStmtVisitor &visitor) override;
};

/// @brief INPUT statement to read from stdin into a variable, optionally displaying a prompt.
struct InputStmt : Stmt
{
    [[nodiscard]] constexpr Kind stmtKind() const noexcept override
    {
        return Kind::Input;
    }

    /// Optional prompt string literal (nullptr if absent).
    ExprPtr prompt;

    /// Target variable names (each may end with '$').
    std::vector<std::string> vars;
    void accept(StmtVisitor &visitor) const override;
    void accept(MutStmtVisitor &visitor) override;
};

/// @brief Reference to a BASIC identifier together with its source location.
struct NameRef
{
    /// Identifier text, including optional type suffix.
    Identifier name;

    /// Source location where the identifier appeared.
    il::support::SourceLoc loc{};
};

/// @brief INPUT # statement reading a field from a file channel.
struct InputChStmt : Stmt
{
    [[nodiscard]] constexpr Kind stmtKind() const noexcept override
    {
        return Kind::InputCh;
    }

    /// Numeric file channel identifier following '#'.
    int channel{0};

    /// Variable receiving the parsed field.
    NameRef target;

    void accept(StmtVisitor &visitor) const override;
    void accept(MutStmtVisitor &visitor) override;
};

/// @brief LINE INPUT # statement reading an entire line from a file channel.
struct LineInputChStmt : Stmt
{
    [[nodiscard]] constexpr Kind stmtKind() const noexcept override
    {
        return Kind::LineInputCh;
    }

    /// Channel expression evaluated to select the file handle; owned and non-null.
    ExprPtr channelExpr;

    /// Destination lvalue that receives the read line.
    LValuePtr targetVar;

    void accept(StmtVisitor &visitor) const override;
    void accept(MutStmtVisitor &visitor) override;
};

/// @brief DELETE statement releasing an object reference.
struct DeleteStmt : Stmt
{
    [[nodiscard]] constexpr Kind stmtKind() const noexcept override
    {
        return Kind::Delete;
    }

    /// Expression evaluating to the instance to delete.
    ExprPtr target;
    void accept(StmtVisitor &visitor) const override;
    void accept(MutStmtVisitor &visitor) override;
};

} // namespace il::frontends::basic
