//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/test_basic_ast_mutation.cpp
// Purpose: Regression tests ensuring AST mutation passes update nodes correctly. 
// Key invariants: Const folder short-circuits and loop folding mutate AST; semantic
// Ownership/Lifetime: Test owns AST objects and infrastructure locally.
// Links: docs/codemap.md
//
//===----------------------------------------------------------------------===//

#include "frontends/basic/AST.hpp"
#include "frontends/basic/ConstFolder.hpp"
#include "frontends/basic/DiagnosticEmitter.hpp"
#include "frontends/basic/SemanticAnalyzer.hpp"
#include "support/source_manager.hpp"
#include <cassert>
#include <memory>
#include <string>

using namespace il::frontends::basic;
using namespace il::support;

namespace
{
ExprPtr makeInt(long long value)
{
    auto expr = std::make_unique<IntExpr>();
    expr->value = value;
    return expr;
}

ExprPtr makeBool(bool value)
{
    auto expr = std::make_unique<BoolExpr>();
    expr->value = value;
    return expr;
}

ExprPtr makeVar(std::string name)
{
    auto expr = std::make_unique<VarExpr>();
    expr->name = std::move(name);
    return expr;
}

ExprPtr makeString(std::string value)
{
    auto expr = std::make_unique<StringExpr>();
    expr->value = std::move(value);
    return expr;
}
} // namespace

int main()
{
    // ConstFolder short-circuits logical AND without mutating the RHS.
    {
        Program prog;
        auto let = std::make_unique<LetStmt>();
        let->target = makeVar("X");
        auto andExpr = std::make_unique<BinaryExpr>();
        andExpr->op = BinaryExpr::Op::LogicalAndShort;
        andExpr->lhs = makeBool(false);
        auto divExpr = std::make_unique<BinaryExpr>();
        divExpr->op = BinaryExpr::Op::Div;
        divExpr->lhs = makeInt(1);
        divExpr->rhs = makeInt(0);
        andExpr->rhs = std::move(divExpr);
        let->expr = std::move(andExpr);
        prog.main.push_back(std::move(let));

        foldConstants(prog);

        auto *foldedLet = dynamic_cast<LetStmt *>(prog.main[0].get());
        assert(foldedLet);
        auto *boolExpr = dynamic_cast<BoolExpr *>(foldedLet->expr.get());
        assert(boolExpr && boolExpr->value == false);
    }

    // ConstFolder rewrites expressions inside loop bodies.
    {
        Program prog;
        auto loop = std::make_unique<ForStmt>();
        auto varExpr = std::make_unique<VarExpr>();
        varExpr->name = "I";
        loop->varExpr = std::move(varExpr);
        loop->start = makeInt(0);
        loop->end = makeInt(1);
        auto print = std::make_unique<PrintStmt>();
        PrintItem item;
        item.kind = PrintItem::Kind::Expr;
        auto sum = std::make_unique<BinaryExpr>();
        sum->op = BinaryExpr::Op::Add;
        sum->lhs = makeInt(1);
        sum->rhs = makeInt(2);
        item.expr = std::move(sum);
        print->items.push_back(std::move(item));
        loop->body.push_back(std::move(print));
        prog.main.push_back(std::move(loop));

        foldConstants(prog);

        auto *foldedLoop = dynamic_cast<ForStmt *>(prog.main[0].get());
        assert(foldedLoop);
        auto *foldedPrint = dynamic_cast<PrintStmt *>(foldedLoop->body[0].get());
        assert(foldedPrint);
        auto *intExpr = dynamic_cast<IntExpr *>(foldedPrint->items[0].expr.get());
        assert(intExpr && intExpr->value == 3);
    }

    // SemanticAnalyzer rewrites scoped identifiers consistently.
    {
        auto prog = std::make_unique<Program>();
        auto sub = std::make_unique<SubDecl>();
        sub->name = "P";

        auto dimArr = std::make_unique<DimStmt>();
        dimArr->name = "ARR";
        dimArr->isArray = true;
        dimArr->size = makeInt(5);
        sub->body.push_back(std::move(dimArr));

        auto dimName = std::make_unique<DimStmt>();
        dimName->name = "NAME$";
        dimName->isArray = false;
        dimName->type = Type::Str;
        sub->body.push_back(std::move(dimName));

        auto dimI = std::make_unique<DimStmt>();
        dimI->name = "I";
        dimI->isArray = false;
        dimI->type = Type::I64;
        sub->body.push_back(std::move(dimI));

        auto input = std::make_unique<InputStmt>();
        input->prompt = makeString("?");
        input->vars.push_back("NAME$");
        sub->body.push_back(std::move(input));

        auto loop = std::make_unique<ForStmt>();
        auto loopVar = std::make_unique<VarExpr>();
        loopVar->name = "I";
        loop->varExpr = std::move(loopVar);
        loop->start = makeInt(1);
        loop->end = makeInt(3);
        sub->body.push_back(std::move(loop));

        prog->procs.push_back(std::move(sub));

        DiagnosticEngine engine;
        SourceManager sm;
        DiagnosticEmitter emitter(engine, sm);
        SemanticAnalyzer sema(emitter);
        sema.analyze(*prog);
        assert(engine.errorCount() == 0);

        auto *analyzedSub = dynamic_cast<SubDecl *>(prog->procs[0].get());
        assert(analyzedSub);
        auto *arrDecl = dynamic_cast<DimStmt *>(analyzedSub->body[0].get());
        auto *nameDecl = dynamic_cast<DimStmt *>(analyzedSub->body[1].get());
        auto *iDecl = dynamic_cast<DimStmt *>(analyzedSub->body[2].get());
        auto *inputStmt = dynamic_cast<InputStmt *>(analyzedSub->body[3].get());
        auto *forStmt = dynamic_cast<ForStmt *>(analyzedSub->body[4].get());
        assert(arrDecl && nameDecl && iDecl && inputStmt && forStmt);
        assert(arrDecl->name == "ARR_0");
        assert(nameDecl->name == "NAME$_1");
        assert(iDecl->name == "I_2");
        assert(inputStmt->vars.size() == 1);
        assert(inputStmt->vars[0] == "NAME$_1");
        auto *forVar = dynamic_cast<VarExpr *>(forStmt->varExpr.get());
        assert(forVar && forVar->name == "I_2");
    }

    return 0;
}
