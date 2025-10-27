// File: tests/frontends/basic/ParserControlConstructTests.cpp
// Purpose: Validate statement registry dispatch for control-flow constructs.
// Key invariants: IF/WHILE/FOR/SELECT statements parse via registry handlers and
//                 produce nested bodies matching source layout.
// Ownership/Lifetime: Tests create parsers on the stack and inspect Program ASTs.
// Links: docs/codemap.md

#include "frontends/basic/AST.hpp"
#include "frontends/basic/Parser.hpp"
#include "support/source_manager.hpp"

#include <cassert>
#include <functional>
#include <string>
#include <vector>

using namespace il::frontends::basic;
using il::support::SourceManager;

namespace
{
struct Scenario
{
    std::string name;
    std::string source;
    std::function<void(const Program &)> verify;
};

void verifyIf(const Program &program)
{
    assert(!program.main.empty());
    auto *ifStmt = dynamic_cast<IfStmt *>(program.main[0].get());
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

void verifyWhile(const Program &program)
{
    assert(!program.main.empty());
    auto *whileStmt = dynamic_cast<WhileStmt *>(program.main[0].get());
    assert(whileStmt);
    assert(whileStmt->body.size() == 1);
    assert(dynamic_cast<PrintStmt *>(whileStmt->body[0].get()));
}

void verifyFor(const Program &program)
{
    assert(!program.main.empty());
    auto *forStmt = dynamic_cast<ForStmt *>(program.main[0].get());
    assert(forStmt);
    assert(forStmt->body.size() == 1);
    assert(dynamic_cast<PrintStmt *>(forStmt->body[0].get()));
}

void verifySelect(const Program &program)
{
    assert(!program.main.empty());
    auto *select = dynamic_cast<SelectCaseStmt *>(program.main[0].get());
    assert(select);
    assert(select->arms.size() == 1);
    assert(select->arms[0].body.size() == 1);
    assert(dynamic_cast<PrintStmt *>(select->arms[0].body[0].get()));
    assert(select->elseBody.size() == 1);
    assert(dynamic_cast<PrintStmt *>(select->elseBody[0].get()));
}

std::vector<Scenario> buildScenarios()
{
    return {
        {"if_nested",
         "IF 1 THEN\nPRINT 1\nELSE\nPRINT 2\nEND IF\nEND\n",
         verifyIf},
        {"while_nested",
         "WHILE 1\nPRINT 1\nWEND\nEND\n",
         verifyWhile},
        {"for_nested",
         "FOR I = 1 TO 3\nPRINT I\nNEXT I\nEND\n",
         verifyFor},
        {"select_nested",
         "SELECT CASE 1\nCASE 1\nPRINT 1\nCASE ELSE\nPRINT 2\nEND SELECT\nEND\n",
         verifySelect},
    };
}
} // namespace

int main()
{
    for (const auto &scenario : buildScenarios())
    {
        SourceManager sm;
        uint32_t fid = sm.addFile(scenario.name + ".bas");
        Parser parser(scenario.source, fid);
        auto program = parser.parseProgram();
        assert(program);
        scenario.verify(*program);
    }
    return 0;
}

