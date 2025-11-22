//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/test_basic_parse_do.cpp
// Purpose: Verify BASIC parser handles all DO/EXIT loop forms and preserves nesting details. 
// Key invariants: DO loop conditions map to correct enum/test position and nested statements keep
// Ownership/Lifetime: To be documented.
// Links: docs/architecture.md
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
    {
        std::string src = "10 DO WHILE X < 10\n"
                          "20 PRINT X\n"
                          "30 LOOP\n"
                          "40 END\n";
        SourceManager sm;
        uint32_t fid = sm.addFile("do_while.bas");
        Parser p(src, fid);
        auto prog = p.parseProgram();
        assert(prog);
        assert(prog->main.size() == 2);
        auto *doStmt = dynamic_cast<DoStmt *>(prog->main[0].get());
        assert(doStmt);
        assert(doStmt->line == 10);
        assert(doStmt->condKind == DoStmt::CondKind::While);
        assert(doStmt->testPos == DoStmt::TestPos::Pre);
        auto *cond = dynamic_cast<BinaryExpr *>(doStmt->cond.get());
        assert(cond);
        assert(cond->op == BinaryExpr::Op::Lt);
        auto *lhsVar = dynamic_cast<VarExpr *>(cond->lhs.get());
        assert(lhsVar);
        assert(lhsVar->name == "X");
        auto *rhsInt = dynamic_cast<IntExpr *>(cond->rhs.get());
        assert(rhsInt);
        assert(rhsInt->value == 10);
        assert(doStmt->body.size() == 1);
        auto *print = dynamic_cast<PrintStmt *>(doStmt->body[0].get());
        assert(print);
        assert(print->line == 20);
    }

    {
        std::string src = "10 DO UNTIL X = 0\n"
                          "20 PRINT X\n"
                          "30 LOOP\n"
                          "40 END\n";
        SourceManager sm;
        uint32_t fid = sm.addFile("do_until.bas");
        Parser p(src, fid);
        auto prog = p.parseProgram();
        assert(prog);
        assert(prog->main.size() == 2);
        auto *doStmt = dynamic_cast<DoStmt *>(prog->main[0].get());
        assert(doStmt);
        assert(doStmt->condKind == DoStmt::CondKind::Until);
        assert(doStmt->testPos == DoStmt::TestPos::Pre);
        auto *cond = dynamic_cast<BinaryExpr *>(doStmt->cond.get());
        assert(cond);
        assert(cond->op == BinaryExpr::Op::Eq);
        auto *lhsVar = dynamic_cast<VarExpr *>(cond->lhs.get());
        assert(lhsVar);
        assert(lhsVar->name == "X");
        auto *rhsInt = dynamic_cast<IntExpr *>(cond->rhs.get());
        assert(rhsInt);
        assert(rhsInt->value == 0);
        assert(doStmt->body.size() == 1);
        auto *print = dynamic_cast<PrintStmt *>(doStmt->body[0].get());
        assert(print);
        assert(print->line == 20);
    }

    {
        std::string src = "10 DO\n"
                          "20 PRINT X\n"
                          "30 LOOP WHILE X <> 0\n"
                          "40 END\n";
        SourceManager sm;
        uint32_t fid = sm.addFile("do_loop_while.bas");
        Parser p(src, fid);
        auto prog = p.parseProgram();
        assert(prog);
        assert(prog->main.size() == 2);
        auto *doStmt = dynamic_cast<DoStmt *>(prog->main[0].get());
        assert(doStmt);
        assert(doStmt->condKind == DoStmt::CondKind::While);
        assert(doStmt->testPos == DoStmt::TestPos::Post);
        auto *cond = dynamic_cast<BinaryExpr *>(doStmt->cond.get());
        assert(cond);
        assert(cond->op == BinaryExpr::Op::Ne);
        auto *lhsVar = dynamic_cast<VarExpr *>(cond->lhs.get());
        assert(lhsVar);
        assert(lhsVar->name == "X");
        auto *rhsInt = dynamic_cast<IntExpr *>(cond->rhs.get());
        assert(rhsInt);
        assert(rhsInt->value == 0);
        assert(doStmt->body.size() == 1);
        auto *print = dynamic_cast<PrintStmt *>(doStmt->body[0].get());
        assert(print);
        assert(print->line == 20);
    }

    {
        std::string src = "10 DO\n"
                          "20 PRINT X\n"
                          "30 LOOP UNTIL DONE\n"
                          "40 END\n";
        SourceManager sm;
        uint32_t fid = sm.addFile("do_loop_until.bas");
        Parser p(src, fid);
        auto prog = p.parseProgram();
        assert(prog);
        assert(prog->main.size() == 2);
        auto *doStmt = dynamic_cast<DoStmt *>(prog->main[0].get());
        assert(doStmt);
        assert(doStmt->condKind == DoStmt::CondKind::Until);
        assert(doStmt->testPos == DoStmt::TestPos::Post);
        auto *cond = dynamic_cast<VarExpr *>(doStmt->cond.get());
        assert(cond);
        assert(cond->name == "DONE");
        assert(doStmt->body.size() == 1);
        auto *print = dynamic_cast<PrintStmt *>(doStmt->body[0].get());
        assert(print);
        assert(print->line == 20);
    }

    {
        std::string src = "10 DO\n"
                          "20 PRINT X\n"
                          "30 LOOP\n"
                          "40 END\n";
        SourceManager sm;
        uint32_t fid = sm.addFile("do_loop_none.bas");
        Parser p(src, fid);
        auto prog = p.parseProgram();
        assert(prog);
        assert(prog->main.size() == 2);
        auto *doStmt = dynamic_cast<DoStmt *>(prog->main[0].get());
        assert(doStmt);
        assert(doStmt->condKind == DoStmt::CondKind::None);
        assert(doStmt->cond == nullptr);
        assert(doStmt->body.size() == 1);
        auto *print = dynamic_cast<PrintStmt *>(doStmt->body[0].get());
        assert(print);
        assert(print->line == 20);
    }

    {
        std::string src = "10 EXIT DO\n"
                          "20 EXIT WHILE\n"
                          "30 EXIT FOR\n"
                          "40 END\n";
        SourceManager sm;
        uint32_t fid = sm.addFile("exit_kinds.bas");
        Parser p(src, fid);
        auto prog = p.parseProgram();
        assert(prog);
        assert(prog->main.size() == 4);
        auto *exitDo = dynamic_cast<ExitStmt *>(prog->main[0].get());
        assert(exitDo);
        assert(exitDo->kind == ExitStmt::LoopKind::Do);
        auto *exitWhile = dynamic_cast<ExitStmt *>(prog->main[1].get());
        assert(exitWhile);
        assert(exitWhile->kind == ExitStmt::LoopKind::While);
        auto *exitFor = dynamic_cast<ExitStmt *>(prog->main[2].get());
        assert(exitFor);
        assert(exitFor->kind == ExitStmt::LoopKind::For);
    }

    {
        std::string src = "10 DO\n"
                          "20 WHILE FLAG\n"
                          "30 PRINT FLAG\n"
                          "40 WEND\n"
                          "50 LOOP\n"
                          "60 END\n";
        SourceManager sm;
        uint32_t fid = sm.addFile("do_while_nested.bas");
        Parser p(src, fid);
        auto prog = p.parseProgram();
        assert(prog);
        assert(prog->main.size() == 2);
        auto *doStmt = dynamic_cast<DoStmt *>(prog->main[0].get());
        assert(doStmt);
        assert(doStmt->body.size() == 1);
        auto *whileStmt = dynamic_cast<WhileStmt *>(doStmt->body[0].get());
        assert(whileStmt);
        assert(whileStmt->line == 20);
        assert(whileStmt->body.size() == 1);
        auto *print = dynamic_cast<PrintStmt *>(whileStmt->body[0].get());
        assert(print);
        assert(print->line == 30);
    }

    return 0;
}
