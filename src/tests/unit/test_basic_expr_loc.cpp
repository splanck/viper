// File: tests/unit/test_basic_expr_loc.cpp
// Purpose: Verify BASIC expression nodes record source locations.
// Key invariants: Line and column values must match token positions.
// Ownership: Test owns parsed AST.
// Links: docs/codemap.md

#include "frontends/basic/Parser.hpp"
#include "support/source_manager.hpp"
#include <cassert>

using namespace il::frontends::basic;
using il::support::SourceManager;

int main()
{
    SourceManager sm;
    uint32_t fid = sm.addFile("expr.bas");
    std::string src = "PRINT 1+2*3\n";
    Parser p(src, fid);
    auto prog = p.parseProgram();
    assert(prog->main.size() == 1);
    auto *ps = dynamic_cast<PrintStmt *>(prog->main[0].get());
    assert(ps);
    assert(ps->items.size() == 1);
    const auto &item = ps->items[0];
    assert(item.kind == PrintItem::Kind::Expr);
    auto *add = dynamic_cast<BinaryExpr *>(item.expr.get());
    assert(add);
    assert(add->loc.file_id == fid && add->loc.line == 1 && add->loc.column == 8);
    auto *lhs = dynamic_cast<IntExpr *>(add->lhs.get());
    auto *mul = dynamic_cast<BinaryExpr *>(add->rhs.get());
    assert(lhs && mul);
    assert(lhs->loc.column == 7);
    assert(mul->loc.column == 10);
    auto *mulL = dynamic_cast<IntExpr *>(mul->lhs.get());
    auto *mulR = dynamic_cast<IntExpr *>(mul->rhs.get());
    assert(mulL && mulR);
    assert(mulL->loc.column == 9);
    assert(mulR->loc.column == 11);
    return 0;
}
