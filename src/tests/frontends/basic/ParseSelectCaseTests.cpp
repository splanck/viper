//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/frontends/basic/ParseSelectCaseTests.cpp
// Purpose: Validate parsing of BASIC SELECT CASE statements and diagnostics. 
// Key invariants: SELECT CASE requires integer labels, unique CASE ELSE, and END SELECT.
// Ownership/Lifetime: Tests own parser, diagnostic engine, and AST instances.
// Links: docs/codemap.md
//
//===----------------------------------------------------------------------===//

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
        const std::string src = "10 SELECT CASE X\n"
                                "20 CASE 1\n"
                                "30 END SELECT\n";
        SourceManager sm;
        const uint32_t fid = sm.addFile("single_label.bas");
        Parser parser(src, fid);
        auto prog = parser.parseProgram();
        assert(prog);
        assert(prog->main.size() == 1);
        auto *select = dynamic_cast<SelectCaseStmt *>(prog->main[0].get());
        assert(select);
        assert(select->arms.size() == 1);
        assert(select->arms[0].labels.size() == 1);
        assert(select->arms[0].labels[0] == 1);
        assert(select->arms[0].body.empty());
    }

    {
        const std::string src = "10 SELECT CASE X\n"
                                "20 CASE 1, 2, 3\n"
                                "30 END SELECT\n";
        SourceManager sm;
        const uint32_t fid = sm.addFile("multi_label.bas");
        Parser parser(src, fid);
        auto prog = parser.parseProgram();
        assert(prog);
        assert(prog->main.size() == 1);
        auto *select = dynamic_cast<SelectCaseStmt *>(prog->main[0].get());
        assert(select);
        assert(select->arms.size() == 1);
        assert(select->arms[0].labels.size() == 3);
        assert(select->arms[0].labels[0] == 1);
        assert(select->arms[0].labels[1] == 2);
        assert(select->arms[0].labels[2] == 3);
    }

    {
        const std::string src = "10 SELECT CASE X\n"
                                "20 CASE 1, 2\n"
                                "30 PRINT 1\n"
                                "40 CASE 3\n"
                                "50 PRINT 3\n"
                                "60 CASE ELSE\n"
                                "70 PRINT 0\n"
                                "80 END SELECT\n";
        SourceManager sm;
        const uint32_t fid = sm.addFile("multi_line.bas");
        Parser parser(src, fid);
        auto prog = parser.parseProgram();
        assert(prog);
        assert(prog->main.size() == 1);
        auto *select = dynamic_cast<SelectCaseStmt *>(prog->main[0].get());
        assert(select);
        assert(select->arms.size() == 2);
        assert(select->arms[0].body.size() == 1);
        assert(dynamic_cast<PrintStmt *>(select->arms[0].body[0].get()));
        assert(select->arms[1].body.size() == 1);
        assert(dynamic_cast<PrintStmt *>(select->arms[1].body[0].get()));
        assert(select->elseBody.size() == 1);
        assert(dynamic_cast<PrintStmt *>(select->elseBody[0].get()));
    }

    {
        const std::string src = "10 SELECT CASE X\n"
                                "20 CASE 1: PRINT 1: PRINT 2\n"
                                "30 CASE 2\n"
                                "40 PRINT 3\n"
                                "50 CASE ELSE: PRINT 0\n"
                                "60 END SELECT\n";
        SourceManager sm;
        const uint32_t fid = sm.addFile("inline_case.bas");
        Parser parser(src, fid);
        auto prog = parser.parseProgram();
        assert(prog);
        assert(prog->main.size() == 1);
        auto *select = dynamic_cast<SelectCaseStmt *>(prog->main[0].get());
        assert(select);
        assert(select->arms.size() == 2);
        assert(select->arms[0].body.size() == 2);
        assert(dynamic_cast<PrintStmt *>(select->arms[0].body[0].get()));
        assert(dynamic_cast<PrintStmt *>(select->arms[0].body[1].get()));
        assert(select->arms[1].body.size() == 1);
        assert(dynamic_cast<PrintStmt *>(select->arms[1].body[0].get()));
        assert(select->elseBody.size() == 1);
        assert(dynamic_cast<PrintStmt *>(select->elseBody[0].get()));
    }

    {
        const std::string src = "10 SELECT CASE X\n"
                                "20 CASE A\n"
                                "30 PRINT 1\n"
                                "40 END SELECT\n";
        SourceManager sm;
        const uint32_t fid = sm.addFile("bad_label.bas");
        DiagnosticEngine de;
        DiagnosticEmitter emitter(de, sm);
        emitter.addSource(fid, src);
        Parser parser(src, fid, &emitter);
        auto prog = parser.parseProgram();
        assert(prog);
        assert(emitter.errorCount() >= 1);
        std::ostringstream oss;
        emitter.printAll(oss);
        const std::string output = oss.str();
        assert(output.find("integer literals") != std::string::npos);
    }

    {
        const std::string src = "10 SELECT CASE X\n"
                                "20 CASE\n"
                                "30 END SELECT\n";
        SourceManager sm;
        const uint32_t fid = sm.addFile("missing_label.bas");
        DiagnosticEngine de;
        DiagnosticEmitter emitter(de, sm);
        emitter.addSource(fid, src);
        Parser parser(src, fid, &emitter);
        auto prog = parser.parseProgram();
        assert(prog);
        std::ostringstream oss;
        emitter.printAll(oss);
        const std::string output = oss.str();
        const std::string expected = "missing_label.bas:2:4: error[ERR_Case_EmptyLabelList]: CASE "
                                     "arm requires at least one label\n"
                                     "20 CASE\n"
                                     "   ^^^^\n";
        assert(output == expected);
    }

    {
        const std::string src = "10 SELECT CASE X\n"
                                "20 CASE 1, \"x\"\n"
                                "30 END SELECT\n";
        SourceManager sm;
        const uint32_t fid = sm.addFile("mixed_label.bas");
        DiagnosticEngine de;
        DiagnosticEmitter emitter(de, sm);
        emitter.addSource(fid, src);
        Parser parser(src, fid, &emitter);
        auto prog = parser.parseProgram();
        assert(prog);
        assert(emitter.errorCount() == 0);
        auto *select = dynamic_cast<SelectCaseStmt *>(prog->main[0].get());
        assert(select);
        assert(select->arms.size() == 1);
        assert(select->arms[0].labels.size() == 1);
        assert(select->arms[0].labels[0] == 1);
        assert(select->arms[0].str_labels.size() == 1);
        assert(select->arms[0].str_labels[0] == "x");
    }

    {
        const std::string src = "10 SELECT CASE X\n"
                                "20 CASE \"A\"\n"
                                "30 PRINT \"x\"\n"
                                "40 END SELECT\n";
        SourceManager sm;
        const uint32_t fid = sm.addFile("non_integer_label.bas");
        DiagnosticEngine de;
        DiagnosticEmitter emitter(de, sm);
        emitter.addSource(fid, src);
        Parser parser(src, fid, &emitter);
        auto prog = parser.parseProgram();
        assert(prog);
        assert(emitter.errorCount() == 0);
        auto *select = dynamic_cast<SelectCaseStmt *>(prog->main[0].get());
        assert(select);
        assert(select->arms.size() == 1);
        assert(select->arms[0].labels.empty());
        assert(select->arms[0].str_labels.size() == 1);
        assert(select->arms[0].str_labels[0] == "A");
    }

    {
        const std::string src = "10 SELECT CASE X\n"
                                "20 CASE 1\n"
                                "30 PRINT 1\n"
                                "40 CASE ELSE\n"
                                "50 PRINT 0\n"
                                "60 CASE ELSE\n"
                                "70 PRINT 2\n"
                                "80 END SELECT\n";
        SourceManager sm;
        const uint32_t fid = sm.addFile("dup_else.bas");
        DiagnosticEngine de;
        DiagnosticEmitter emitter(de, sm);
        emitter.addSource(fid, src);
        Parser parser(src, fid, &emitter);
        auto prog = parser.parseProgram();
        assert(prog);
        std::ostringstream oss;
        emitter.printAll(oss);
        const std::string output = oss.str();
        const std::string expected =
            "dup_else.bas:6:9: error[ERR_SelectCase_DuplicateElse]: duplicate CASE ELSE\n"
            "60 CASE ELSE\n"
            "        ^^^^\n";
        assert(output == expected);
    }

    {
        const std::string src = "10 SELECT CASE X\n"
                                "20 CASE ELSE\n"
                                "30 PRINT 0\n"
                                "40 END SELECT\n";
        SourceManager sm;
        const uint32_t fid = sm.addFile("else_without_case.bas");
        DiagnosticEngine de;
        DiagnosticEmitter emitter(de, sm);
        emitter.addSource(fid, src);
        Parser parser(src, fid, &emitter);
        auto prog = parser.parseProgram();
        assert(prog);
        std::ostringstream oss;
        emitter.printAll(oss);
        const std::string output = oss.str();
        assert(output.find("CASE ELSE requires a preceding CASE arm") != std::string::npos);
    }

    {
        const std::string src = "10 SELECT CASE X\n"
                                "20 CASE 1\n"
                                "30 PRINT 1\n";
        SourceManager sm;
        const uint32_t fid = sm.addFile("missing_end.bas");
        DiagnosticEngine de;
        DiagnosticEmitter emitter(de, sm);
        emitter.addSource(fid, src);
        Parser parser(src, fid, &emitter);
        auto prog = parser.parseProgram();
        assert(prog);
        std::ostringstream oss;
        emitter.printAll(oss);
        const std::string output = oss.str();
        const std::string expected = "missing_end.bas:1:4: error[ERR_SelectCase_MissingEndSelect]: "
                                     "SELECT CASE missing END SELECT terminator\n"
                                     "10 SELECT CASE X\n"
                                     "   ^^^^^^\n";
        assert(output == expected);
    }

    {
        const std::string src = "10 SELECT CASE X\n"
                                "20 CASE 1\n"
                                "30 PRINT 1\n"
                                "40 CASE ELSE\n"
                                "50 PRINT 0\n"
                                "60 PRINT 2\n"
                                "70 END SELECT\n";
        SourceManager sm;
        const uint32_t fid = sm.addFile("else_body.bas");
        Parser parser(src, fid);
        auto prog = parser.parseProgram();
        assert(prog);
        assert(prog->main.size() == 1);
        auto *select = dynamic_cast<SelectCaseStmt *>(prog->main[0].get());
        assert(select);
        assert(select->elseBody.size() == 2);
    }

    {
        const std::string src = "10 SELECT CASE X\n"
                                "20 CASE 1\n"
                                "30 PRINT 1\n"
                                "40 CASE ELSE\n"
                                "50 PRINT 0\n"
                                "60 CASE ELSE\n"
                                "70 PRINT 2\n"
                                "80 END SELECT\n";
        SourceManager sm;
        const uint32_t fid = sm.addFile("dup_else_body.bas");
        DiagnosticEngine de;
        DiagnosticEmitter emitter(de, sm);
        emitter.addSource(fid, src);
        Parser parser(src, fid, &emitter);
        auto prog = parser.parseProgram();
        assert(prog);
        std::ostringstream oss;
        emitter.printAll(oss);
        const std::string output = oss.str();
        const std::string expected =
            "dup_else_body.bas:6:9: error[ERR_SelectCase_DuplicateElse]: duplicate CASE ELSE\n"
            "60 CASE ELSE\n"
            "        ^^^^\n";
        assert(output == expected);
        auto *select = dynamic_cast<SelectCaseStmt *>(prog->main[0].get());
        assert(select);
        assert(select->elseBody.size() == 1);
    }

    {
        const std::string src = "10 SELECT CASE X\n"
                                "20 CASE 1\n"
                                "30 CASE ELSE: PRINT \"a\"\n"
                                "40 CASE ELSE: PRINT \"b\"\n"
                                "50 END SELECT\n";
        SourceManager sm;
        const uint32_t fid = sm.addFile("duplicate_case_else.bas");
        DiagnosticEngine de;
        DiagnosticEmitter emitter(de, sm);
        emitter.addSource(fid, src);
        Parser parser(src, fid, &emitter);
        auto prog = parser.parseProgram();
        assert(prog);
        std::ostringstream oss;
        emitter.printAll(oss);
        const std::string expected =
            "duplicate_case_else.bas:4:9: error[ERR_SelectCase_DuplicateElse]: duplicate CASE "
            "ELSE\n"
            "40 CASE ELSE: PRINT \"b\"\n"
            "        ^^^^\n";
        assert(oss.str() == expected);
    }

    return 0;
}
