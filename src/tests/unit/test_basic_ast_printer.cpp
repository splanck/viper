// File: tests/unit/test_basic_ast_printer.cpp
// Purpose: Validate BASIC AST printer output for representative nodes.
// Key invariants: Printer emits stable textual form for statements/expressions.
// Ownership/Lifetime: Tests allocate AST nodes with std::unique_ptr.
// Links: docs/codemap.md

#include "frontends/basic/AstPrinter.hpp"
#include <cassert>
#include <iostream>
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

ExprPtr makeFloat(double value)
{
    auto expr = std::make_unique<FloatExpr>();
    expr->value = value;
    return expr;
}

ExprPtr makeString(std::string value)
{
    auto expr = std::make_unique<StringExpr>();
    expr->value = std::move(value);
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

ExprPtr makeArray(std::string name, ExprPtr index)
{
    auto expr = std::make_unique<ArrayExpr>();
    expr->name = std::move(name);
    expr->indices.push_back(std::move(index));
    return expr;
}

} // namespace

// Test strategy: Construct a program containing functions, control-flow
// statements, array accesses, builtin calls, and various expression forms.
// The printer should produce a deterministic textual dump that matches the
// expected snapshot for these representative nodes.
int main()
{
    Program prog;

    auto func = std::make_unique<FunctionDecl>();
    func->line = 5;
    func->name = "FNRESULT";
    func->ret = Type::F64;
    Param paramA;
    paramA.name = "A";
    func->params.push_back(paramA);
    Param paramArr;
    paramArr.name = "ARR";
    paramArr.is_array = true;
    func->params.push_back(paramArr);
    auto funcReturn = std::make_unique<ReturnStmt>();
    funcReturn->line = 501;
    funcReturn->value = makeVar("A");
    func->body.push_back(std::move(funcReturn));
    prog.procs.push_back(std::move(func));

    auto sub = std::make_unique<SubDecl>();
    sub->line = 6;
    sub->name = "DOIT";
    Param paramMsg;
    paramMsg.name = "MSG$";
    sub->params.push_back(paramMsg);
    Param paramValues;
    paramValues.name = "VALUES";
    paramValues.is_array = true;
    sub->params.push_back(paramValues);
    auto subPrint = std::make_unique<PrintStmt>();
    subPrint->line = 601;
    PrintItem subItem;
    subItem.expr = makeString("HELLO");
    subPrint->items.push_back(std::move(subItem));
    sub->body.push_back(std::move(subPrint));
    prog.procs.push_back(std::move(sub));

    auto printStmt = std::make_unique<PrintStmt>();
    printStmt->line = 10;
    PrintItem printItem;
    printItem.expr = makeInt(42);
    printStmt->items.push_back(std::move(printItem));
    PrintItem commaItem;
    commaItem.kind = PrintItem::Kind::Comma;
    printStmt->items.push_back(std::move(commaItem));
    PrintItem floatItem;
    floatItem.expr = makeFloat(3.5);
    printStmt->items.push_back(std::move(floatItem));
    PrintItem stringItem;
    stringItem.expr = makeString("HI");
    printStmt->items.push_back(std::move(stringItem));
    PrintItem semicolonItem;
    semicolonItem.kind = PrintItem::Kind::Semicolon;
    printStmt->items.push_back(std::move(semicolonItem));
    prog.main.push_back(std::move(printStmt));

    auto letStmt = std::make_unique<LetStmt>();
    letStmt->line = 20;
    letStmt->target = makeArray("ARR", makeVar("I"));
    auto builtin = std::make_unique<BuiltinCallExpr>();
    builtin->builtin = BuiltinCallExpr::Builtin::Sqr;
    auto addExpr = std::make_unique<BinaryExpr>();
    addExpr->op = BinaryExpr::Op::Add;
    addExpr->lhs = makeInt(1);
    addExpr->rhs = makeFloat(2.5);
    builtin->args.push_back(std::move(addExpr));
    letStmt->expr = std::move(builtin);
    prog.main.push_back(std::move(letStmt));

    auto dimArr = std::make_unique<DimStmt>();
    dimArr->line = 30;
    dimArr->name = "ARR";
    dimArr->isArray = true;
    dimArr->size = makeInt(10);
    dimArr->type = Type::F64;
    prog.main.push_back(std::move(dimArr));

    auto dimScalar = std::make_unique<DimStmt>();
    dimScalar->line = 35;
    dimScalar->name = "S$";
    dimScalar->isArray = false;
    dimScalar->type = Type::Str;
    prog.main.push_back(std::move(dimScalar));

    auto redim = std::make_unique<ReDimStmt>();
    redim->line = 37;
    redim->name = "ARR";
    redim->size = makeInt(20);
    prog.main.push_back(std::move(redim));

    auto randomize = std::make_unique<RandomizeStmt>();
    randomize->line = 40;
    randomize->seed = makeInt(123);
    prog.main.push_back(std::move(randomize));

    auto input = std::make_unique<InputStmt>();
    input->line = 50;
    input->prompt = makeString("Value?");
    input->vars.push_back("N");
    prog.main.push_back(std::move(input));

    auto ifStmt = std::make_unique<IfStmt>();
    ifStmt->line = 60;
    auto ifCond = std::make_unique<BinaryExpr>();
    ifCond->op = BinaryExpr::Op::Gt;
    ifCond->lhs = makeVar("A");
    ifCond->rhs = makeInt(0);
    ifStmt->cond = std::move(ifCond);
    auto thenList = std::make_unique<StmtList>();
    auto innerLet = std::make_unique<LetStmt>();
    innerLet->line = 61;
    innerLet->target = makeVar("B");
    innerLet->expr = makeBool(true);
    thenList->stmts.push_back(std::move(innerLet));
    auto innerGoto = std::make_unique<GotoStmt>();
    innerGoto->line = 62;
    innerGoto->target = 100;
    thenList->stmts.push_back(std::move(innerGoto));
    ifStmt->then_branch = std::move(thenList);
    IfStmt::ElseIf elseif;
    auto elseifCond = std::make_unique<BinaryExpr>();
    elseifCond->op = BinaryExpr::Op::Lt;
    elseifCond->lhs = makeVar("A");
    elseifCond->rhs = makeInt(0);
    elseif.cond = std::move(elseifCond);
    auto elseifPrint = std::make_unique<PrintStmt>();
    elseifPrint->line = 63;
    PrintItem elseifItem;
    elseifItem.expr = makeString("NEG");
    elseifPrint->items.push_back(std::move(elseifItem));
    elseif.then_branch = std::move(elseifPrint);
    ifStmt->elseifs.push_back(std::move(elseif));
    auto elsePrint = std::make_unique<PrintStmt>();
    elsePrint->line = 64;
    PrintItem elseItem;
    elseItem.expr = makeString("ZERO");
    elsePrint->items.push_back(std::move(elseItem));
    ifStmt->else_branch = std::move(elsePrint);
    prog.main.push_back(std::move(ifStmt));

    auto whileStmt = std::make_unique<WhileStmt>();
    whileStmt->line = 70;
    auto notExpr = std::make_unique<UnaryExpr>();
    notExpr->op = UnaryExpr::Op::LogicalNot;
    notExpr->expr = makeVar("DONE");
    whileStmt->cond = std::move(notExpr);
    auto whilePrint = std::make_unique<PrintStmt>();
    whilePrint->line = 71;
    PrintItem whileItem;
    whileItem.expr = makeInt(1);
    whilePrint->items.push_back(std::move(whileItem));
    whileStmt->body.push_back(std::move(whilePrint));
    prog.main.push_back(std::move(whileStmt));

    auto forStmt = std::make_unique<ForStmt>();
    forStmt->line = 80;
    auto forVarExpr = std::make_unique<VarExpr>();
    forVarExpr->name = "I";
    forStmt->varExpr = std::move(forVarExpr);
    forStmt->start = makeInt(1);
    forStmt->end = makeInt(5);
    forStmt->step = makeInt(2);
    auto forPrint = std::make_unique<PrintStmt>();
    forPrint->line = 81;
    PrintItem forItem;
    forItem.expr = makeVar("I");
    forPrint->items.push_back(std::move(forItem));
    forStmt->body.push_back(std::move(forPrint));
    prog.main.push_back(std::move(forStmt));

    auto doStmt = std::make_unique<DoStmt>();
    doStmt->line = 85;
    doStmt->testPos = DoStmt::TestPos::Post;
    doStmt->condKind = DoStmt::CondKind::Until;
    doStmt->cond = makeVar("DONE");
    auto doPrint = std::make_unique<PrintStmt>();
    doPrint->line = 86;
    PrintItem doItem;
    doItem.expr = makeString("LOOP");
    doPrint->items.push_back(std::move(doItem));
    doStmt->body.push_back(std::move(doPrint));
    prog.main.push_back(std::move(doStmt));

    auto exitStmt = std::make_unique<ExitStmt>();
    exitStmt->line = 87;
    exitStmt->kind = ExitStmt::LoopKind::Do;
    prog.main.push_back(std::move(exitStmt));

    auto nextStmt = std::make_unique<NextStmt>();
    nextStmt->line = 90;
    nextStmt->var = "I";
    prog.main.push_back(std::move(nextStmt));

    auto gotoStmt = std::make_unique<GotoStmt>();
    gotoStmt->line = 100;
    gotoStmt->target = 200;
    prog.main.push_back(std::move(gotoStmt));

    auto returnStmt = std::make_unique<ReturnStmt>();
    returnStmt->line = 110;
    auto callExpr = std::make_unique<CallExpr>();
    callExpr->callee = "FNRESULT";
    callExpr->args.push_back(makeVar("B"));
    callExpr->args.push_back(makeArray("ARR", makeVar("I")));
    returnStmt->value = std::move(callExpr);
    prog.main.push_back(std::move(returnStmt));

    auto endStmt = std::make_unique<EndStmt>();
    endStmt->line = 120;
    prog.main.push_back(std::move(endStmt));

    AstPrinter printer;
    std::string dump = printer.dump(prog);
    const std::string expected =
        "5: (FUNCTION FNRESULT qualifiedName: <null> RET F64 (A ARR()) {501:(RETURN A)})\n"
        "6: (SUB DOIT qualifiedName: <null> (MSG$ VALUES()) {601:(PRINT \"HELLO\")})\n"
        "10: (PRINT 42 , 3.5 \"HI\" ;)\n"
        "20: (LET ARR(I) (SQR (+ 1 2.5)))\n"
        "30: (DIM ARR 10 AS F64)\n"
        "35: (DIM S$ AS STR)\n"
        "37: (REDIM ARR 20)\n"
        "40: (RANDOMIZE 123)\n"
        "50: (INPUT \"Value?\", N)\n"
        "60: (IF (> A 0) THEN (SEQ (LET B TRUE) (GOTO 100)) ELSEIF (< A "
        "0) THEN (PRINT \"NEG\") ELSE (PRINT \"ZERO\"))\n"
        "70: (WHILE (NOT DONE) {71:(PRINT 1)})\n"
        "80: (FOR I = 1 TO 5 STEP 2 {81:(PRINT I)})\n"
        "85: (DO post UNTIL DONE {86:(PRINT \"LOOP\")})\n"
        "87: (EXIT DO)\n"
        "90: (NEXT I)\n"
        "100: (GOTO 200)\n"
        "110: (RETURN (FNRESULT B ARR(I)))\n"
        "120: (END)\n";
    if (dump != expected)
    {
        std::cerr << "ACTUAL:\n" << dump << "\nEXPECTED:\n" << expected << "\n";
    }
    assert(dump == expected);
}
