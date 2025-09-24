// File: tests/unit/test_basic_parse_loops.cpp
// Purpose: Verify loop parsing shares body handling without regressing NEXT/WEND semantics.
// Key invariants: Loop bodies exclude terminators and preserve nested statement line numbers.
// Ownership/Lifetime: Test owns parser, AST, and source manager instances.
// Links: docs/codemap.md

#include "frontends/basic/Parser.hpp"
#include "support/source_manager.hpp"
#include <cassert>
#include <string>

using namespace il::frontends::basic;
using namespace il::support;

int main()
{
    {
        std::string src = "10 FOR I = 1 TO 3\n20 PRINT I\n30 NEXT I\n40 END\n";
        SourceManager sm;
        uint32_t fid = sm.addFile("loops.bas");
        Parser p(src, fid);
        auto prog = p.parseProgram();
        assert(prog);
        assert(prog->main.size() == 2);
        auto *forStmt = dynamic_cast<ForStmt *>(prog->main[0].get());
        assert(forStmt);
        assert(forStmt->body.size() == 1);
        auto *print = dynamic_cast<PrintStmt *>(forStmt->body[0].get());
        assert(print);
        assert(print->line == 20);
        auto *endStmt = dynamic_cast<EndStmt *>(prog->main[1].get());
        assert(endStmt);
    }

    {
        std::string src = "10 WHILE X\n20 PRINT X\n30 WEND\n40 END\n";
        SourceManager sm;
        uint32_t fid = sm.addFile("loops.bas");
        Parser p(src, fid);
        auto prog = p.parseProgram();
        assert(prog);
        assert(prog->main.size() == 2);
        auto *whileStmt = dynamic_cast<WhileStmt *>(prog->main[0].get());
        assert(whileStmt);
        assert(whileStmt->body.size() == 1);
        auto *print = dynamic_cast<PrintStmt *>(whileStmt->body[0].get());
        assert(print);
        assert(print->line == 20);
        auto *endStmt = dynamic_cast<EndStmt *>(prog->main[1].get());
        assert(endStmt);
    }

    {
        std::string src = "10 EXIT FOR\n20 EXIT WHILE\n30 EXIT DO\n40 END\n";
        SourceManager sm;
        uint32_t fid = sm.addFile("loops.bas");
        Parser p(src, fid);
        auto prog = p.parseProgram();
        assert(prog);
        assert(prog->main.size() == 4);
        auto *exitFor = dynamic_cast<ExitStmt *>(prog->main[0].get());
        assert(exitFor);
        assert(exitFor->kind == ExitStmt::LoopKind::For);
        auto *exitWhile = dynamic_cast<ExitStmt *>(prog->main[1].get());
        assert(exitWhile);
        assert(exitWhile->kind == ExitStmt::LoopKind::While);
        auto *exitDo = dynamic_cast<ExitStmt *>(prog->main[2].get());
        assert(exitDo);
        assert(exitDo->kind == ExitStmt::LoopKind::Do);
        auto *endStmt = dynamic_cast<EndStmt *>(prog->main[3].get());
        assert(endStmt);
    }

    return 0;
}
