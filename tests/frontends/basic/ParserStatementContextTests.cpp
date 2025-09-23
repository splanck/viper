// File: tests/frontends/basic/ParserStatementContextTests.cpp
// Purpose: Validate BASIC parser statement context helpers for colon chains and nested flows.
// Key invariants: StatementContext centralizes separator handling without altering AST shape.
// Ownership/Lifetime: Test owns parser/source manager objects and inspects resulting AST.
// Links: docs/class-catalog.md

#include "frontends/basic/Parser.hpp"
#include "support/source_manager.hpp"
#include <cassert>
#include <string>

using namespace il::frontends::basic;
using namespace il::support;

int main()
{
    {
        std::string src = "10 PRINT 1: LET X = 5\n20 END\n";
        SourceManager sm;
        uint32_t fid = sm.addFile("colon.bas");
        Parser p(src, fid);
        auto prog = p.parseProgram();
        assert(prog);
        assert(prog->main.size() == 2);
        auto *list = dynamic_cast<StmtList *>(prog->main[0].get());
        assert(list);
        assert(list->stmts.size() == 2);
        assert(dynamic_cast<PrintStmt *>(list->stmts[0].get()));
        assert(dynamic_cast<LetStmt *>(list->stmts[1].get()));
    }

    {
        std::string src =
            "10 WHILE FLAG\n"
            "20 FOR I = 1 TO 3\n"
            "30 PRINT I: IF I = 2 THEN PRINT 99\n"
            "40 NEXT I\n"
            "50 WEND\n"
            "60 END\n";
        SourceManager sm;
        uint32_t fid = sm.addFile("nested.bas");
        Parser p(src, fid);
        auto prog = p.parseProgram();
        assert(prog);
        assert(prog->main.size() == 2);
        auto *whileStmt = dynamic_cast<WhileStmt *>(prog->main[0].get());
        assert(whileStmt);
        assert(whileStmt->body.size() == 1);
        auto *forStmt = dynamic_cast<ForStmt *>(whileStmt->body[0].get());
        assert(forStmt);
        assert(forStmt->body.size() == 2);
        assert(dynamic_cast<PrintStmt *>(forStmt->body[0].get()));
        auto *innerIf = dynamic_cast<IfStmt *>(forStmt->body[1].get());
        assert(innerIf);
        assert(innerIf->then_branch);
        assert(dynamic_cast<PrintStmt *>(innerIf->then_branch.get()));
    }

    {
        std::string src =
            "10 IF FLAG THEN\n"
            "20 PRINT 1\n"
            "30 ELSE\n"
            "40 PRINT 2\n"
            "50 END\n";
        SourceManager sm;
        uint32_t fid = sm.addFile("ifnewlines.bas");
        Parser p(src, fid);
        auto prog = p.parseProgram();
        assert(prog);
        assert(prog->main.size() == 2);
        auto *ifStmt = dynamic_cast<IfStmt *>(prog->main[0].get());
        assert(ifStmt);
        assert(ifStmt->then_branch);
        assert(dynamic_cast<PrintStmt *>(ifStmt->then_branch.get()));
        assert(ifStmt->else_branch);
        assert(dynamic_cast<PrintStmt *>(ifStmt->else_branch.get()));
    }

    return 0;
}
