// File: src/frontends/basic/AstPrinter.cpp
// Purpose: Implements BASIC AST printer orchestration and formatting helpers.
// Key invariants: None.
// Ownership/Lifetime: Printer does not own AST nodes.
// Notes: Delegates expression/statement printing to dedicated translation units.
// Links: docs/codemap.md

#include "frontends/basic/AstPrinter.hpp"
#include <sstream>

namespace il::frontends::basic
{

AstPrinter::PrintStyle::PrintStyle(Printer &printer) : printer(&printer) {}

void AstPrinter::PrintStyle::openBody() const
{
    printer->os << " {";
}

void AstPrinter::PrintStyle::closeBody() const
{
    printer->os << "})";
}

void AstPrinter::PrintStyle::separate(bool &first) const
{
    if (!first)
    {
        printer->os << ' ';
    }
    first = false;
}

void AstPrinter::PrintStyle::writeLineNumber(int line) const
{
    printer->os << line << ':';
}

void AstPrinter::PrintStyle::writeNull() const
{
    printer->os << "<null>";
}

void AstPrinter::PrintStyle::writeChannelPrefix() const
{
    printer->os << " channel=#";
}

void AstPrinter::PrintStyle::writeArgsPrefix() const
{
    printer->os << " args=[";
}

void AstPrinter::PrintStyle::writeArgsSuffix() const
{
    printer->os << ']';
}

void AstPrinter::PrintStyle::writeNoNewlineTag() const
{
    printer->os << " no-newline";
}

/// @brief Write a line of text to the underlying stream with current indentation.
/// @param text Line content to emit.
/// @note Appends a newline character and resets column position.
void AstPrinter::Printer::line(std::string_view text)
{
    for (int i = 0; i < indent; ++i)
        os << "  ";
    os << text << '\n';
}

/// @brief Increase indentation level and return RAII guard.
/// @return Indent object whose destruction restores previous indentation.
AstPrinter::Printer::Indent AstPrinter::Printer::push()
{
    ++indent;
    return Indent{*this};
}

/// @brief Restore the indentation level saved at construction time.
AstPrinter::Printer::Indent::~Indent()
{
    --p.indent;
}

/// @brief Serialize an entire BASIC program to a printable string.
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
/// @param stmt Statement to dump.
/// @param p Printer receiving the textual form.
void AstPrinter::dump(const Stmt &stmt, Printer &p)
{
    PrintStyle style{p};
    printStmt(stmt, p, style);
}

/// @brief Print an expression node to the printer.
/// @param expr Expression to dump.
/// @param p Printer receiving output.
void AstPrinter::dump(const Expr &expr, Printer &p)
{
    PrintStyle style{p};
    printExpr(expr, p, style);
}

} // namespace il::frontends::basic

