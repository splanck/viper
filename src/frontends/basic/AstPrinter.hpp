// File: src/frontends/basic/AstPrinter.hpp
// Purpose: Declares utilities to print BASIC AST nodes.
// Key invariants: None.
// Ownership/Lifetime: Functions do not take ownership of nodes.
// Links: docs/class-catalog.md
#pragma once
#include "frontends/basic/AST.hpp"
#include <string>

namespace il::frontends::basic
{

class AstPrinter
{
  public:
    std::string dump(const Program &prog);

  private:
    std::string dump(const Stmt &stmt);
    std::string dump(const Expr &expr);
};

} // namespace il::frontends::basic
