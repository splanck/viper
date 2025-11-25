//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/frontends/basic/ParseCallStatementTests.cpp
// Purpose: Verify BASIC parser accepts CALL statements invoking SUB routines.
// Key invariants: Identifier followed by parentheses lowers to CallStmt.
// Ownership/Lifetime: Test owns parser and AST; no shared state.
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
    const std::string src = "10 SUB GREET(N$)\n"
                            "20 PRINT \"Hi, \"; N$\n"
                            "30 END SUB\n"
                            "40 GREET(\"Alice\")\n"
                            "50 END\n";

    SourceManager sm;
    uint32_t fid = sm.addFile("call_stmt.bas");

    Parser parser(src, fid);
    auto program = parser.parseProgram();
    assert(program);

    assert(program->main.size() == 2);
    auto *callStmt = dynamic_cast<CallStmt *>(program->main[0].get());
    assert(callStmt);
    auto *call = dynamic_cast<CallExpr *>(callStmt->call.get());
    assert(call);
    assert(call->callee == "GREET");
    assert(call->args.size() == 1);
    auto *arg0 = dynamic_cast<StringExpr *>(call->args[0].get());
    assert(arg0);
    assert(arg0->value == "Alice");

    auto *endStmt = dynamic_cast<EndStmt *>(program->main[1].get());
    assert(endStmt);

    return 0;
}
