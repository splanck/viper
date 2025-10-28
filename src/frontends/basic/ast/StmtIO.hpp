// File: src/frontends/basic/ast/StmtIO.hpp
// Purpose: Defines BASIC I/O and console interaction statements.
// Key invariants: Channel expressions and I/O targets are owned via unique_ptr when present.
// Ownership/Lifetime: Statement children follow the AST unique_ptr ownership scheme.
// Links: docs/codemap.md
#pragma once

#include "frontends/basic/ast/StmtBase.hpp"

#include <string>
#include <vector>

namespace il::frontends::basic
{

/// @brief Pseudo statement that only carries a line label.
struct LabelStmt : Stmt
{
    [[nodiscard]] Kind stmtKind() const override
    {
        return Kind::Label;
    }

    void accept(StmtVisitor &visitor) const override;
    void accept(MutStmtVisitor &visitor) override;
};

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
    [[nodiscard]] Kind stmtKind() const override
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
    [[nodiscard]] Kind stmtKind() const override
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

/// @brief CLS statement clearing the screen.
struct ClsStmt : Stmt
{
    [[nodiscard]] Kind stmtKind() const override
    {
        return Kind::Cls;
    }

    void accept(StmtVisitor &visitor) const override;
    void accept(MutStmtVisitor &visitor) override;
};

/// @brief COLOR statement changing the palette.
struct ColorStmt : Stmt
{
    [[nodiscard]] Kind stmtKind() const override
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

/// @brief LOCATE statement moving the cursor.
struct LocateStmt : Stmt
{
    [[nodiscard]] Kind stmtKind() const override
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

/// @brief OPEN statement configuring a file channel.
struct OpenStmt : Stmt
{
    [[nodiscard]] Kind stmtKind() const override
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
    [[nodiscard]] Kind stmtKind() const override
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
    [[nodiscard]] Kind stmtKind() const override
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

/// @brief Reference to a BASIC identifier together with its source location.
struct NameRef
{
    /// Identifier text, including optional type suffix.
    Identifier name;

    /// Source location where the identifier appeared.
    il::support::SourceLoc loc{};
};

/// @brief INPUT statement to read from stdin into a variable, optionally displaying a prompt.
struct InputStmt : Stmt
{
    [[nodiscard]] Kind stmtKind() const override
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

/// @brief INPUT # statement reading a field from a file channel.
struct InputChStmt : Stmt
{
    [[nodiscard]] Kind stmtKind() const override
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
    [[nodiscard]] Kind stmtKind() const override
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

} // namespace il::frontends::basic
