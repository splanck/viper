// File: src/frontends/basic/AstPrinter.hpp
// Purpose: Declares utilities to print BASIC AST nodes.
// Key invariants: None.
// Ownership/Lifetime: Functions do not take ownership of nodes.
// Notes: Uses internal Printer helper for formatting.
// Links: docs/codemap.md
#pragma once

#include "frontends/basic/AST.hpp"
#include <ostream>
#include <string>
#include <string_view>

namespace il::frontends::basic
{
namespace print_stmt
{
struct Context;
} // namespace print_stmt

/// @brief Emits a textual representation of BASIC programs for debugging.
///
/// AstPrinter walks the Program, Stmt, and Expr nodes to produce a
/// human-readable dump using an internal Printer helper to manage
/// indentation.
class AstPrinter
{
  public:
    friend struct print_stmt::Context;
    /// @brief Produce a formatted dump of the given program.
    /// @param prog Program AST to print.
    /// @return String containing the formatted dump.
    std::string dump(const Program &prog);

  private:
    /// @brief Stateful helper that writes lines with indentation.
    struct Printer
    {
        /// Output stream where text is emitted.
        std::ostream &os;

        /// Current indentation level.
        int indent = 0;

        /// @brief Write @p text followed by a newline, honoring indentation.
        /// @param text Line content to write.
        void line(std::string_view text);

        /// @brief RAII guard that decreases indentation on destruction.
        struct Indent
        {
            /// Printer whose indentation is managed.
            Printer &p;

            /// @brief Restore previous indentation level when destroyed.
            ~Indent();
        };

        /// @brief Increase indentation for the next scope.
        /// @return Guard that restores previous indentation when destroyed.
        Indent push();
    };

    /// @brief Holds formatting preferences for AST emission.
    struct PrintStyle
    {
        /// @brief Create a style bound to @p printer.
        explicit PrintStyle(Printer &printer);

        /// @brief Emit the opening delimiter for a statement body.
        void openBody() const;

        /// @brief Emit the closing delimiter for a statement body.
        void closeBody() const;

        /// @brief Ensure elements printed with @p first are separated.
        void separate(bool &first) const;

        /// @brief Write a numbered label prefix for a statement.
        void writeLineNumber(int line) const;

        /// @brief Emit the placeholder used for null optional nodes.
        void writeNull() const;

        /// @brief Emit the prefix used before channel expressions.
        void writeChannelPrefix() const;

        /// @brief Emit the prefix used before argument lists.
        void writeArgsPrefix() const;

        /// @brief Emit the suffix used after argument lists.
        void writeArgsSuffix() const;

        /// @brief Emit the marker indicating trailing newline suppression.
        void writeNoNewlineTag() const;

      private:
        Printer *printer;
    };

    /// @brief Expression visitor producing textual representations.
    struct ExprPrinter;

    /// @brief Statement visitor producing textual representations.
    struct StmtPrinter;

    /// @brief Helper invoked by visitors to serialize expressions.
    static void printExpr(const Expr &expr, Printer &p, PrintStyle &style);

    /// @brief Helper invoked by the dispatcher to serialize statements.
    static void printStmt(const Stmt &stmt, Printer &p, PrintStyle &style);

    /// @brief Dump a statement node to the printer.
    /// @param stmt Statement to serialize.
    /// @param p Printer receiving the output.
    void dump(const Stmt &stmt, Printer &p);

    /// @brief Dump an expression node to the printer.
    /// @param expr Expression to serialize.
    /// @param p Printer receiving the output.
    void dump(const Expr &expr, Printer &p);
};

} // namespace il::frontends::basic
