// File: src/frontends/basic/AstPrinter.hpp
// Purpose: Declares utilities to print BASIC AST nodes.
// Key invariants: None.
// Ownership/Lifetime: Functions do not take ownership of nodes.
// Notes: Uses internal Printer helper for formatting.
// Links: docs/class-catalog.md
#pragma once

#include "frontends/basic/AST.hpp"
#include <ostream>
#include <string>
#include <string_view>

namespace il::frontends::basic
{

class AstPrinter
{
  public:
    std::string dump(const Program &prog);

  private:
    struct Printer
    {
        std::ostream &os;
        int indent = 0;
        void line(std::string_view text);

        struct Indent
        {
            Printer &p;

            ~Indent()
            {
                --p.indent;
            }
        };

        Indent push();
    };

    void dump(const Stmt &stmt, Printer &p);
    void dump(const Expr &expr, Printer &p);
};

} // namespace il::frontends::basic
