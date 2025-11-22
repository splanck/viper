//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/test_basic_loc.cpp
// Purpose: Ensure BASIC AST and IL instructions retain source locations. 
// Key invariants: Locations must match expected columns.
// Ownership/Lifetime: Test owns constructed AST and module.
// Links: docs/codemap.md
//
//===----------------------------------------------------------------------===//

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
    std::string src = "LET X = 1\nPRINT X+2\n";
    Parser p(src, fid);
    auto prog = p.parseProgram();
    assert(prog->main.size() == 2);
    auto *ps = dynamic_cast<PrintStmt *>(prog->main[1].get());
    assert(ps);
    assert(ps->loc.file_id == fid && ps->loc.line == 2 && ps->loc.column == 1);
    assert(ps->items.size() == 1);
    const auto &item = ps->items[0];
    assert(item.kind == PrintItem::Kind::Expr);
    auto *bin = dynamic_cast<BinaryExpr *>(item.expr.get());
    assert(bin);
    assert(bin->loc.line == 2 && bin->loc.column == 8);
    auto *lhsVar = dynamic_cast<VarExpr *>(bin->lhs.get());
    auto *rhs = dynamic_cast<IntExpr *>(bin->rhs.get());
    assert(lhsVar && rhs);
    assert(lhsVar->loc.column == 7);
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
                if (in.op == il::core::Opcode::IAddOvf)
                {
                    assert(in.loc.line == 2 && in.loc.column == 8);
                    foundAdd = true;
                }
            }
        }
    }
    assert(foundAdd);
    return 0;
}
