//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/test_basic_ast_select_case.cpp
// Purpose: Ensure BASIC AST can construct SELECT CASE statements. 
// Key invariants: Node ownership flows through std::unique_ptr containers.
// Ownership/Lifetime: Test owns allocated AST nodes locally.
// Links: docs/codemap.md
//
//===----------------------------------------------------------------------===//

#include "frontends/basic/AST.hpp"

#include <cassert>
#include <memory>
#include <string>
#include <utility>

using namespace il::frontends::basic;

namespace
{
ExprPtr makeInt(int64_t value)
{
    auto expr = std::make_unique<IntExpr>();
    expr->value = value;
    return expr;
}

ExprPtr makeVar(std::string name)
{
    auto expr = std::make_unique<VarExpr>();
    expr->name = std::move(name);
    return expr;
}
} // namespace

int main()
{
    auto select = std::make_unique<SelectCaseStmt>();
    select->line = 100;
    select->selector = makeVar("CHOICE");

    CaseArm arm;
    arm.labels = {1, 2, 3};
    auto print = std::make_unique<PrintStmt>();
    print->line = 101;
    PrintItem item;
    item.expr = makeInt(7);
    print->items.push_back(std::move(item));
    arm.body.push_back(std::move(print));
    select->arms.push_back(std::move(arm));

    auto elseEnd = std::make_unique<EndStmt>();
    elseEnd->line = 999;
    select->elseBody.push_back(std::move(elseEnd));

    assert(select->stmtKind() == Stmt::Kind::SelectCase);
    assert(select->selector != nullptr);
    assert(select->arms.size() == 1);
    assert(select->arms.front().labels.size() == 3);
    assert(select->arms.front().body.size() == 1);
    assert(select->elseBody.size() == 1);

    return 0;
}
