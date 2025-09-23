// File: tests/unit/test_basic_loc.cpp
// Purpose: Ensure BASIC AST and IL instructions retain source locations.
// Key invariants: Locations must match expected columns.
// Ownership: Test owns constructed AST and module.
// Links: docs/codemap.md

#include "frontends/basic/Lowerer.hpp"
#include "frontends/basic/Parser.hpp"
#include "support/source_manager.hpp"
#include <cassert>

using namespace il::frontends::basic;
using il::support::SourceManager;

int main()
{
    SourceManager sm;
    uint32_t fid = sm.addFile("test.bas");
    std::string src = "PRINT 1+2\n";
    Parser p(src, fid);
    auto prog = p.parseProgram();
    assert(prog->main.size() == 1);
    auto *ps = dynamic_cast<PrintStmt *>(prog->main[0].get());
    assert(ps);
    assert(ps->loc.file_id == fid && ps->loc.line == 1 && ps->loc.column == 1);
    assert(ps->items.size() == 1);
    const auto &item = ps->items[0];
    assert(item.kind == PrintItem::Kind::Expr);
    auto *bin = dynamic_cast<BinaryExpr *>(item.expr.get());
    assert(bin);
    assert(bin->loc.column == 8);
    auto *lhs = dynamic_cast<IntExpr *>(bin->lhs.get());
    auto *rhs = dynamic_cast<IntExpr *>(bin->rhs.get());
    assert(lhs && rhs);
    assert(lhs->loc.column == 7);
    assert(rhs->loc.column == 9);

    Lowerer low;
    il::core::Module m = low.lower(*prog);
    bool foundAdd = false;
    for (const auto &fn : m.functions)
    {
        for (const auto &bb : fn.blocks)
        {
            for (const auto &in : bb.instructions)
            {
                if (in.op == il::core::Opcode::Add)
                {
                    assert(in.loc.line == 1 && in.loc.column == 8);
                    foundAdd = true;
                }
            }
        }
    }
    assert(foundAdd);
    return 0;
}
