// File: tests/frontends/basic/ParserStatementContextTests.cpp
// Purpose: Validate BASIC parser statement sequencing helper for colon chains and nested flows.
// Key invariants: StatementSequencer centralizes separator handling without altering AST shape.
// Ownership/Lifetime: Test owns parser/source manager objects and inspects resulting AST.
// Links: docs/codemap.md

#include "frontends/basic/DiagnosticEmitter.hpp"
#include "frontends/basic/Parser.hpp"
#include "support/diagnostics.hpp"
#include "support/source_manager.hpp"
#include <cassert>
#include <sstream>
#include <string>

using namespace il::frontends::basic;
using namespace il::support;

int main()
{
    {
        std::string src = "PRINT 123\nEND\n";
        SourceManager sm;
        uint32_t fid = sm.addFile("single_line.bas");
        Parser p(src, fid);
        StatementSequencer seq(p);
        assert(seq.lastSeparator() == StatementSequencer::SeparatorKind::None);
        seq.skipLineBreaks();
        assert(seq.lastSeparator() == StatementSequencer::SeparatorKind::None);
        auto prog = p.parseProgram();
        assert(prog);
        assert(prog->main.size() == 2);
        assert(dynamic_cast<PrintStmt *>(prog->main[0].get()));
        assert(dynamic_cast<EndStmt *>(prog->main[1].get()));
    }

    {
        std::string src = "PRINT 1\nPRINT 2: PRINT 3\nEND\n";
        SourceManager sm;
        uint32_t fid = sm.addFile("multiline.bas");
        Parser p(src, fid);
        auto prog = p.parseProgram();
        assert(prog);
        assert(prog->main.size() == 3);
        auto *first = dynamic_cast<PrintStmt *>(prog->main[0].get());
        assert(first);
        auto *list = dynamic_cast<StmtList *>(prog->main[1].get());
        assert(list);
        assert(list->stmts.size() == 2);
        assert(dynamic_cast<PrintStmt *>(list->stmts[0].get()));
        assert(dynamic_cast<PrintStmt *>(list->stmts[1].get()));
    }

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
        std::string src = "PRINT 1:: PRINT 2: END\n";
        SourceManager sm;
        uint32_t fid = sm.addFile("double_colon.bas");
        Parser p(src, fid);
        auto prog = p.parseProgram();
        assert(prog);
        assert(prog->main.size() == 1);
        auto *list = dynamic_cast<StmtList *>(prog->main[0].get());
        assert(list);
        assert(list->stmts.size() == 3);
        assert(dynamic_cast<PrintStmt *>(list->stmts[0].get()));
        assert(dynamic_cast<PrintStmt *>(list->stmts[1].get()));
        assert(dynamic_cast<EndStmt *>(list->stmts[2].get()));
    }

    {
        std::string src = "10 PRINT 1:20 LET X = 5\n30 END\n";
        SourceManager sm;
        uint32_t fid = sm.addFile("line-label.bas");
        Parser p(src, fid);
        auto prog = p.parseProgram();
        assert(prog);
        assert(prog->main.size() == 3);
        auto *first = dynamic_cast<PrintStmt *>(prog->main[0].get());
        assert(first);
        assert(first->line == 10);
        auto *second = dynamic_cast<LetStmt *>(prog->main[1].get());
        assert(second);
        assert(second->line == 20);
    }

    {
        std::string src = "200 REM comment\n210 PRINT X\n";
        SourceManager sm;
        uint32_t fid = sm.addFile("line-break-number.bas");
        Parser p(src, fid);
        auto prog = p.parseProgram();
        assert(prog);
        assert(prog->main.size() == 2);
        auto *label = dynamic_cast<LabelStmt *>(prog->main[0].get());
        assert(label);
        assert(label->line == 200);
        assert(label->loc.isValid());
        auto *print = dynamic_cast<PrintStmt *>(prog->main[1].get());
        assert(print);
        assert(print->line == 210);
    }

    {
        std::string src = "300\n310 PRINT 1\n";
        SourceManager sm;
        uint32_t fid = sm.addFile("blank-line.bas");
        Parser p(src, fid);
        auto prog = p.parseProgram();
        assert(prog);
        assert(prog->main.size() == 2);
        auto *label = dynamic_cast<LabelStmt *>(prog->main[0].get());
        assert(label);
        assert(label->line == 300);
        assert(label->loc.isValid());
        auto *print = dynamic_cast<PrintStmt *>(prog->main[1].get());
        assert(print);
        assert(print->line == 310);
    }

    {
        std::string src = "100 REM a\n110 REM b\n120 PRINT \"ok\"\n";
        SourceManager sm;
        uint32_t fid = sm.addFile("label-chain.bas");
        Parser p(src, fid);
        auto prog = p.parseProgram();
        assert(prog);
        assert(prog->main.size() == 3);
        auto *label100 = dynamic_cast<LabelStmt *>(prog->main[0].get());
        assert(label100);
        assert(label100->line == 100);
        auto *label110 = dynamic_cast<LabelStmt *>(prog->main[1].get());
        assert(label110);
        assert(label110->line == 110);
        auto *print = dynamic_cast<PrintStmt *>(prog->main[2].get());
        assert(print);
        assert(print->line == 120);
    }

    {
        std::string src = "10 WHILE FLAG\n"
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
        std::string src = "Speak: PRINT 1\nEND\n";
        SourceManager sm;
        uint32_t fid = sm.addFile("named_label.bas");
        Parser p(src, fid);
        auto prog = p.parseProgram();
        assert(prog);
        assert(prog->main.size() == 2);
        auto *printStmt = dynamic_cast<PrintStmt *>(prog->main[0].get());
        assert(printStmt);
        assert(printStmt->line == 1'000'000);
        auto *endStmt = dynamic_cast<EndStmt *>(prog->main[1].get());
        assert(endStmt);
    }

    {
        std::string src = "Whisper:\nEND\n";
        SourceManager sm;
        uint32_t fid = sm.addFile("named_label_only.bas");
        Parser p(src, fid);
        auto prog = p.parseProgram();
        assert(prog);
        assert(prog->main.size() == 2);
        auto *labelStmt = dynamic_cast<LabelStmt *>(prog->main[0].get());
        assert(labelStmt);
        assert(labelStmt->line == 1'000'000);
        auto *endStmt = dynamic_cast<EndStmt *>(prog->main[1].get());
        assert(endStmt);
    }

    {
        std::string src = "Echo:\nEcho:\n";
        SourceManager sm;
        uint32_t fid = sm.addFile("named_label_duplicate.bas");

        DiagnosticEngine de;
        DiagnosticEmitter emitter(de, sm);
        emitter.addSource(fid, src);

        Parser parser(src, fid, &emitter);
        auto program = parser.parseProgram();
        assert(program);
        assert(emitter.errorCount() == 1);

        std::ostringstream oss;
        emitter.printAll(oss);
        std::string output = oss.str();
        assert(output.find("error[B0001]") != std::string::npos);
        assert(output.find("label 'ECHO' already defined") != std::string::npos);
    }

    {
        std::string src = "10 IF FLAG THEN\n"
                          "20 PRINT 1\n"
                          "30 ELSE\n"
                          "40 PRINT 2\n"
                          "50 END IF\n"
                          "60 END\n";
        SourceManager sm;
        uint32_t fid = sm.addFile("ifnewlines.bas");
        Parser p(src, fid);
        auto prog = p.parseProgram();
        assert(prog);
        assert(prog->main.size() == 2);
        auto *ifStmt = dynamic_cast<IfStmt *>(prog->main[0].get());
        assert(ifStmt);
        assert(ifStmt->then_branch);
        auto *thenList = dynamic_cast<StmtList *>(ifStmt->then_branch.get());
        assert(thenList);
        assert(thenList->stmts.size() == 1);
        assert(dynamic_cast<PrintStmt *>(thenList->stmts[0].get()));
        assert(ifStmt->else_branch);
        auto *elseList = dynamic_cast<StmtList *>(ifStmt->else_branch.get());
        assert(elseList);
        assert(elseList->stmts.size() == 1);
        assert(dynamic_cast<PrintStmt *>(elseList->stmts[0].get()));
    }

    {
        std::string src = "10 IF FLAG THEN\n"
                          "20 PRINT 1\n"
                          "30 ELSEIF OTHER THEN\n"
                          "40 PRINT 2\n"
                          "50 ELSE\n"
                          "60 PRINT 3\n"
                          "70 END IF\n"
                          "80 END\n";
        SourceManager sm;
        uint32_t fid = sm.addFile("ifelseif.bas");
        Parser p(src, fid);
        auto prog = p.parseProgram();
        assert(prog);
        assert(prog->main.size() == 2);
        auto *ifStmt = dynamic_cast<IfStmt *>(prog->main[0].get());
        assert(ifStmt);
        assert(ifStmt->then_branch);
        auto *block = dynamic_cast<StmtList *>(ifStmt->then_branch.get());
        assert(block);
        assert(block->line == 10);
        assert(block->stmts.size() == 1);
        assert(dynamic_cast<PrintStmt *>(block->stmts[0].get()));
        assert(ifStmt->elseifs.size() == 1);
        assert(ifStmt->elseifs[0].then_branch);
        auto *elseifBlock = dynamic_cast<StmtList *>(ifStmt->elseifs[0].then_branch.get());
        assert(elseifBlock);
        assert(elseifBlock->line == 10);
        assert(elseifBlock->stmts.size() == 1);
        assert(dynamic_cast<PrintStmt *>(elseifBlock->stmts[0].get()));
        assert(ifStmt->else_branch);
        auto *elseBlock = dynamic_cast<StmtList *>(ifStmt->else_branch.get());
        assert(elseBlock);
        assert(elseBlock->line == 10);
        assert(elseBlock->stmts.size() == 1);
        assert(dynamic_cast<PrintStmt *>(elseBlock->stmts[0].get()));
    }

    {
        std::string src = "10 IF FLAG THEN PRINT 1 ELSEIF OTHER THEN PRINT 2 ELSE PRINT 3\n"
                          "20 END\n";
        SourceManager sm;
        uint32_t fid = sm.addFile("ifinline.bas");
        Parser p(src, fid);
        auto prog = p.parseProgram();
        assert(prog);
        assert(prog->main.size() == 2);
        auto *ifStmt = dynamic_cast<IfStmt *>(prog->main[0].get());
        assert(ifStmt);
        assert(ifStmt->then_branch);
        assert(dynamic_cast<PrintStmt *>(ifStmt->then_branch.get()));
        assert(ifStmt->elseifs.size() == 1);
        assert(dynamic_cast<PrintStmt *>(ifStmt->elseifs[0].then_branch.get()));
        assert(ifStmt->else_branch);
        assert(dynamic_cast<PrintStmt *>(ifStmt->else_branch.get()));
    }

    {
        std::string src = "10 IF FLAG THEN PRINT 1: PRINT 2\n"
                          "20 END\n";
        SourceManager sm;
        uint32_t fid = sm.addFile("ifcolon.bas");
        Parser p(src, fid);
        auto prog = p.parseProgram();
        assert(prog);
        assert(prog->main.size() == 2);
        auto *list = dynamic_cast<StmtList *>(prog->main[0].get());
        assert(list);
        assert(list->stmts.size() == 2);
        auto *ifStmt = dynamic_cast<IfStmt *>(list->stmts[0].get());
        assert(ifStmt);
        assert(dynamic_cast<PrintStmt *>(list->stmts[1].get()));
    }

    {
        std::string src = "10 IF FLAG THEN\n"
                          "20 PRINT 1\n"
                          "30 END\n";
        SourceManager sm;
        uint32_t fid = sm.addFile("ifendmissing.bas");
        DiagnosticEngine de;
        DiagnosticEmitter emitter(de, sm);
        emitter.addSource(fid, src);
        Parser p(src, fid, &emitter);
        auto prog = p.parseProgram();
        assert(prog);
        assert(emitter.errorCount() == 1);
        std::ostringstream oss;
        emitter.printAll(oss);
        std::string output = oss.str();
        assert(output.find("error[B0004]") != std::string::npos);
        assert(output.find("missing END IF") != std::string::npos);
    }

    {
        std::string src = "10 DO WHILE FLAG\n"
                          "20 PRINT 1\n"
                          "30 LOOP\n"
                          "40 END\n";
        SourceManager sm;
        uint32_t fid = sm.addFile("dowhile.bas");
        Parser p(src, fid);
        auto prog = p.parseProgram();
        assert(prog);
        assert(prog->main.size() == 2);
        auto *doStmt = dynamic_cast<DoStmt *>(prog->main[0].get());
        assert(doStmt);
        assert(doStmt->condKind == DoStmt::CondKind::While);
        assert(doStmt->testPos == DoStmt::TestPos::Pre);
        assert(doStmt->cond);
        auto *var = dynamic_cast<VarExpr *>(doStmt->cond.get());
        assert(var);
        assert(var->name == "FLAG");
        assert(doStmt->body.size() == 1);
        assert(dynamic_cast<PrintStmt *>(doStmt->body[0].get()));
    }

    {
        std::string src = "10 DO\n"
                          "20 PRINT 1\n"
                          "30 LOOP UNTIL DONE\n"
                          "40 END\n";
        SourceManager sm;
        uint32_t fid = sm.addFile("dountil.bas");
        Parser p(src, fid);
        auto prog = p.parseProgram();
        assert(prog);
        assert(prog->main.size() == 2);
        auto *doStmt = dynamic_cast<DoStmt *>(prog->main[0].get());
        assert(doStmt);
        assert(doStmt->condKind == DoStmt::CondKind::Until);
        assert(doStmt->testPos == DoStmt::TestPos::Post);
        assert(doStmt->cond);
        auto *var = dynamic_cast<VarExpr *>(doStmt->cond.get());
        assert(var);
        assert(var->name == "DONE");
        assert(doStmt->body.size() == 1);
        assert(dynamic_cast<PrintStmt *>(doStmt->body[0].get()));
    }

    {
        std::string src = "10 DO: LOOP\n"
                          "20 END\n";
        SourceManager sm;
        uint32_t fid = sm.addFile("doloop.bas");
        Parser p(src, fid);
        auto prog = p.parseProgram();
        assert(prog);
        assert(prog->main.size() == 2);
        auto *doStmt = dynamic_cast<DoStmt *>(prog->main[0].get());
        assert(doStmt);
        assert(doStmt->condKind == DoStmt::CondKind::None);
        assert(!doStmt->cond);
        assert(doStmt->body.empty());
    }

    return 0;
}
