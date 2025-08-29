#pragma once
#include <ostream>
#include <string>
#include "frontends/basic/AST.h"

namespace il::basic {

class AstPrinter {
public:
  static std::string print(const Program &prog);
  static void printStmt(const Stmt *s, std::ostream &os, bool include_line = true);
  static void printExpr(const Expr *e, std::ostream &os);
};

} // namespace il::basic
