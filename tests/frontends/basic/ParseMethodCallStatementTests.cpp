// File: tests/frontends/basic/ParseMethodCallStatementTests.cpp
// Purpose: Ensure BASIC parser recognizes object method call statements.
// Key invariants: Method invocation lowers to CallStmt with MethodCallExpr base/selector.
// Ownership/Lifetime: Test constructs parser/AST per run; no shared resources.
// Links: docs/codemap.md

#include "frontends/basic/Parser.hpp"
#include "support/source_manager.hpp"

#include <cassert>
#include <string>

using namespace il::frontends::basic;
using namespace il::support;

int main()
{
    const std::string src = "10 o.INC()\n20 END\n";
    SourceManager sm;
    auto diag = std::make_unique<DiagnosticEmitter>(&sm);
    Parser p(sm, *diag);
    auto program = p.parseString(src);
    assert(program);
    assert(program->main.size() == 2);

    auto *callStmt = dynamic_cast<CallStmt *>(program->main[0].get());
    assert(callStmt && callStmt->call);

    auto *mcall = dynamic_cast<MethodCallExpr *>(callStmt->call.get());
    assert(mcall);
    auto *base = dynamic_cast<VarExpr *>(mcall->base.get());
    assert(base && base->name == "o");
    assert(mcall->method == "INC");
    return 0;
}
