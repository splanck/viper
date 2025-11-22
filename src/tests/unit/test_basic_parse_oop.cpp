//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/test_basic_parse_oop.cpp
// Purpose: Ensure OOP-specific expressions and statements parse into expected AST nodes. 
// Key invariants: NEW/ME expressions and DELETE statement are recognized with OOP always enabled.
// Ownership/Lifetime: Test owns parser instance and resulting AST.
// Links: docs/codemap.md
//
//===----------------------------------------------------------------------===//

#include "frontends/basic/Parser.hpp"
#include "support/source_manager.hpp"
#include <cassert>
#include <string>

using namespace il::frontends::basic;
using namespace il::support;

int main()
{
    // NEW without arguments produces NewExpr with empty arg list.
    {
        std::string src = "10 LET O = NEW Foo()\n20 END\n";
        SourceManager sm;
        uint32_t fid = sm.addFile("test.bas");
        Parser p(src, fid);
        auto prog = p.parseProgram();
        auto *let = dynamic_cast<LetStmt *>(prog->main[0].get());
        auto *alloc = dynamic_cast<NewExpr *>(let->expr.get());
        assert(alloc);
        assert(alloc->className == "FOO");
        assert(alloc->args.empty());
    }

    // NEW with arguments preserves order and count.
    {
        std::string src = "10 LET O = NEW Foo(1, 2)\n20 END\n";
        SourceManager sm;
        uint32_t fid = sm.addFile("test.bas");
        Parser p(src, fid);
        auto prog = p.parseProgram();
        auto *let = dynamic_cast<LetStmt *>(prog->main[0].get());
        auto *alloc = dynamic_cast<NewExpr *>(let->expr.get());
        assert(alloc);
        assert(alloc->args.size() == 2);
        auto *first = dynamic_cast<IntExpr *>(alloc->args[0].get());
        auto *second = dynamic_cast<IntExpr *>(alloc->args[1].get());
        assert(first && first->value == 1);
        assert(second && second->value == 2);
    }

    // ME keyword parses into MeExpr.
    {
        std::string src = "10 LET O = ME\n20 END\n";
        SourceManager sm;
        uint32_t fid = sm.addFile("test.bas");
        Parser p(src, fid);
        auto prog = p.parseProgram();
        auto *let = dynamic_cast<LetStmt *>(prog->main[0].get());
        auto *me = dynamic_cast<MeExpr *>(let->expr.get());
        assert(me);
    }

    // DELETE statement captures target expression.
    {
        std::string src = "10 DELETE O\n20 END\n";
        SourceManager sm;
        uint32_t fid = sm.addFile("test.bas");
        Parser p(src, fid);
        auto prog = p.parseProgram();
        auto *del = dynamic_cast<DeleteStmt *>(prog->main[0].get());
        assert(del);
        auto *target = dynamic_cast<VarExpr *>(del->target.get());
        assert(target && target->name == "O");
    }
    return 0;
}
