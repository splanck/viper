//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Implements the orchestrating pieces of the BASIC AST printer.  This file ties
// together the high-level `AstPrinter` façade, the `PrintStyle` helpers that
// inject punctuation, and the recursive dump entry points that hand work off to
// the expression/statement printers.  The goal is to keep the public API
// compact: clients construct an `AstPrinter`, call `dump`, and receive a stable
// textual representation for diagnostics or golden tests.
//
//===----------------------------------------------------------------------===//

#include "frontends/basic/AstPrinter.hpp"
#include <sstream>

/// @file
/// @brief Houses the high-level BASIC AST printer implementation.
/// @details This translation unit ties together the @ref AstPrinter façade with
///          helper structures that emit punctuation, indentation, and node
///          formatting.  Expression- and statement-specific logic lives in the
///          dedicated `AstPrint_Expr.cpp` and `AstPrint_Stmt.cpp` visitors; the
///          routines here orchestrate those helpers into a single coherent API.

namespace il::frontends::basic
{

/// @brief Create a print style that forwards delimiters to a printer stream.
/// @details Stores a pointer to the owning @ref Printer so helper methods can
///          write directly to the target output stream without copying or
///          rebuilding shared state.  The style remains lightweight enough to
///          pass by value during recursive printing.
/// @param printer Printer that ultimately receives formatted AST text.
AstPrinter::PrintStyle::PrintStyle(Printer &printer) : printer(&printer) {}

/// @brief Emit the opening delimiter used when printing composite constructs.
/// @details Multi-part statements such as PRINT# wrap their payload inside
///          parentheses for readability.  The helper injects the preceding space
///          so callers do not have to micro-manage whitespace rules.
void AstPrinter::PrintStyle::openBody() const
{
    printer->os << " {";
}

/// @brief Emit the closing delimiter paired with @ref openBody.
/// @details Keeping the implementation centralised prevents mismatched
///          delimiters when formatting policies evolve, as every caller relies
///          on the same helper.
void AstPrinter::PrintStyle::closeBody() const
{
    printer->os << "})";
}

/// @brief Insert a space between list elements on every call after the first.
/// @details The boolean @p first flag is toggled by the helper, letting callers
///          express comma/space separated lists without manual bookkeeping or
///          duplicated conditionals.
/// @param first Indicates whether an element has already been emitted.
void AstPrinter::PrintStyle::separate(bool &first) const
{
    if (!first)
    {
        printer->os << ' ';
    }
    first = false;
}

/// @brief Write a ``<line>:`` prefix to the output stream.
/// @details The printer records original source line numbers with each statement
///          so dumps can correlate textual output with the user's program.
/// @param line One-based source line number to render.
void AstPrinter::PrintStyle::writeLineNumber(int line) const
{
    printer->os << line << ':';
}

/// @brief Emit the canonical ``<null>`` marker for missing optional values.
/// @details Optional constructs—such as absent ELSE branches—use this helper to
///          make absence explicit in dumps, avoiding ambiguity for test fixtures.
void AstPrinter::PrintStyle::writeNull() const
{
    printer->os << "<null>";
}

/// @brief Emit the `` channel=#`` prefix for PRINT# invocations.
/// @details PRINT# statements carry an explicit channel identifier that must be
///          surfaced alongside their payload.  Centralising the prefix ensures
///          consistent spacing across dumps.
void AstPrinter::PrintStyle::writeChannelPrefix() const
{
    printer->os << " channel=#";
}

/// @brief Emit the opening bracket that precedes argument lists.
/// @details Argument dumps use brackets to avoid confusion with statement
///          delimiters.  The helper exists so punctuation changes remain local.
void AstPrinter::PrintStyle::writeArgsPrefix() const
{
    printer->os << " args=[";
}

/// @brief Emit the closing bracket that terminates argument lists.
/// @details Complements @ref writeArgsPrefix, ensuring every call site produces
///          balanced delimiters even when formatting evolves.
void AstPrinter::PrintStyle::writeArgsSuffix() const
{
    printer->os << ']';
}

/// @brief Append the `` no-newline`` suffix for PRINT# statements without EOL.
/// @details The suffix distinguishes between PRINT# invocations that emit an
///          implicit newline and those that omit it, mirroring interpreter
///          semantics for regression tests.
void AstPrinter::PrintStyle::writeNoNewlineTag() const
{
    printer->os << " no-newline";
}

/// @brief Write a line of text to the underlying stream with current indentation.
/// @details Prepends two spaces per indentation level before writing @p text and
///          appends a newline terminator.  The routine intentionally avoids
///          buffering to keep dumps easy to follow in debugger output.
/// @param text Line content to emit.
/// @note Appends a newline character and resets column position.
void AstPrinter::Printer::line(std::string_view text)
{
    for (int i = 0; i < indent; ++i)
        os << "  ";
    os << text << '\n';
}

/// @brief Increase indentation level and return an RAII guard.
/// @details Nested constructs call this helper to ensure indentation is restored
///          automatically even when exceptions or early returns occur.
/// @return Indent object whose destruction restores previous indentation.
AstPrinter::Printer::Indent AstPrinter::Printer::push()
{
    ++indent;
    return Indent{*this};
}

/// @brief Restore the indentation level saved at construction time.
/// @details The destructor is intentionally trivial—decrementing the stored
///          indentation counter—so guards can live on the stack without cost.
AstPrinter::Printer::Indent::~Indent()
{
    --p.indent;
}

/// @brief Serialize an entire BASIC program to a printable string.
/// @details Walks both the procedure list and the main body, printing each
///          statement alongside its recorded line number.  Individual statements
///          delegate to @ref dump so the formatting logic remains centralised.
/// @param prog Program whose procedures and main body are dumped.
/// @returns Concatenated text representation of @p prog.
std::string AstPrinter::dump(const Program &prog)
{
    std::ostringstream os;
    Printer p{os};
    for (auto &stmt : prog.procs)
    {
        std::ostringstream line_os;
        line_os << stmt->line << ": ";
        Printer line_p{line_os};
        dump(*stmt, line_p);
        p.line(line_os.str());
    }
    for (auto &stmt : prog.main)
    {
        std::ostringstream line_os;
        line_os << stmt->line << ": ";
        Printer line_p{line_os};
        dump(*stmt, line_p);
        p.line(line_os.str());
    }
    return os.str();
}

/// @brief Recursively print a statement node and its children.
/// @details Constructs a temporary @ref PrintStyle tied to the provided printer
///          and defers to @ref printStmt, which lives in the statement visitor
///          translation unit.
/// @param stmt Statement to dump.
/// @param p Printer receiving the textual form.
void AstPrinter::dump(const Stmt &stmt, Printer &p)
{
    PrintStyle style{p};
    printStmt(stmt, p, style);
}

/// @brief Print an expression node to the printer.
/// @details Mirrors the statement overload but delegates to @ref printExpr so
///          callers can render subexpressions directly when needed.
/// @param expr Expression to dump.
/// @param p Printer receiving output.
void AstPrinter::dump(const Expr &expr, Printer &p)
{
    PrintStyle style{p};
    printExpr(expr, p, style);
}

} // namespace il::frontends::basic
