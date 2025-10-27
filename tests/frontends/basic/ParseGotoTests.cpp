// File: tests/frontends/basic/ParseGotoTests.cpp
// Purpose: Validate parsing and AST inspection of BASIC GOTO statements.
// Key invariants: GOTO accepts numeric and named labels, recording shared line ids.
// Ownership/Lifetime: Test owns parser, AST, and source manager instances.
// Links: docs/codemap.md

#include "frontends/basic/Parser.hpp"
#include "frontends/basic/ast/StmtNodes.hpp"
#include "support/source_manager.hpp"

#include <cassert>
#include <string>

using namespace il::frontends::basic;
using namespace il::support;

namespace
{
const GotoStmt *findGoto(const Program &program)
{
    for (const auto &stmt : program.main)
    {
        if (auto *gotoStmt = dynamic_cast<GotoStmt *>(stmt.get()))
            return gotoStmt;
    }
    return nullptr;
}

const Stmt *findStmtWithLine(const Program &program, int line)
{
    for (const auto &stmt : program.main)
    {
        if (!stmt)
            continue;
        if (stmt->line == line)
            return stmt.get();
        if (auto *list = dynamic_cast<StmtList *>(stmt.get()))
        {
            for (const auto &inner : list->stmts)
            {
                if (inner && inner->line == line)
                    return inner.get();
            }
        }
    }
    return nullptr;
}
} // namespace

int main()
{
    {
        SourceManager sm;
        uint32_t fid = sm.addFile("goto_numeric.bas");
        Parser parser("10 GOTO 200\n20 END\n", fid);
        auto program = parser.parseProgram();
        assert(program);

        auto *gotoStmt = findGoto(*program);
        assert(gotoStmt);
        assert(gotoStmt->target == 200);
    }

    {
        const std::string src = "10 GOTO Speak\n"
                                "20 END\n"
                                "Speak:\n"
                                "PRINT 1\n"
                                "END\n";
        SourceManager sm;
        uint32_t fid = sm.addFile("goto_label.bas");
        Parser parser(src, fid);
        auto program = parser.parseProgram();
        assert(program);

        auto *gotoStmt = findGoto(*program);
        assert(gotoStmt);

        auto *targetStmt = findStmtWithLine(*program, gotoStmt->target);
        assert(targetStmt);
        assert(targetStmt->line == gotoStmt->target);
    }

    return 0;
}
