// File: tests/frontends/basic/SubroutinesGosubInlineCaseTests.cpp
// Purpose: Exercise parsing and execution of GOSUB with named labels and inline CASE bodies.
// Key invariants: Parser resolves identifier targets for GOSUB and CASE arms execute inline statements.
// Ownership/Lifetime: Test owns parser/compiler state and evaluates CASE arms locally.
// Links: docs/codemap.md

#include "frontends/basic/BasicCompiler.hpp"
#include "frontends/basic/Parser.hpp"
#include "frontends/basic/ast/DeclNodes.hpp"
#include "frontends/basic/ast/ExprNodes.hpp"
#include "frontends/basic/ast/StmtControl.hpp"
#include "frontends/basic/ast/StmtDecl.hpp"
#include "frontends/basic/ast/StmtExpr.hpp"
#include "support/source_manager.hpp"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>

using namespace il::frontends::basic;
using namespace il::support;

namespace
{
std::string readFile(const std::filesystem::path &path)
{
    std::ifstream in(path);
    if (!in)
    {
        return {};
    }
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

std::filesystem::path fixturePath()
{
    auto path = std::filesystem::path(__FILE__).parent_path();
    path /= "subroutines_gosub_inline_case.bas";
    return std::filesystem::absolute(path);
}

const StringExpr *firstStringArg(const PrintStmt *print)
{
    assert(print);
    assert(!print->items.empty());
    const auto &item = print->items.front();
    assert(item.kind == PrintItem::Kind::Expr);
    return dynamic_cast<const StringExpr *>(item.expr.get());
}

bool matchesLabel(const CaseArm &arm, std::string_view value)
{
    for (const auto &label : arm.str_labels)
    {
        if (label == value)
        {
            return true;
        }
    }
    return false;
}

void collectGosubStatements(const Stmt &stmt, int &count)
{
    if (const auto *gosub = dynamic_cast<const GosubStmt *>(&stmt))
    {
        ++count;
        assert(gosub->targetLine == 1'000'000);
        return;
    }

    if (const auto *list = dynamic_cast<const StmtList *>(&stmt))
    {
        for (const auto &child : list->stmts)
        {
            if (child)
            {
                collectGosubStatements(*child, count);
            }
        }
    }
}

void findSelectCase(const Stmt &stmt, const SelectCaseStmt *&select)
{
    if (select)
    {
        return;
    }

    if (const auto *selectStmt = dynamic_cast<const SelectCaseStmt *>(&stmt))
    {
        select = selectStmt;
        return;
    }

    if (const auto *list = dynamic_cast<const StmtList *>(&stmt))
    {
        for (const auto &child : list->stmts)
        {
            if (!child)
            {
                continue;
            }
            findSelectCase(*child, select);
            if (select)
            {
                break;
            }
        }
    }
}
} // namespace

int main()
{
    const auto basPath = fixturePath();
    const std::string basPathStr = basPath.string();
    const std::string source = readFile(basPath);
    assert(!source.empty());

    // Parse the BASIC source and validate AST structure.
    SourceManager sm;
    const uint32_t fid = sm.addFile(basPathStr);
    Parser parser(source, fid);
    auto program = parser.parseProgram();
    assert(program);

    int gosubCount = 0;
    const SelectCaseStmt *select = nullptr;
    for (const auto &stmt : program->main)
    {
        if (!stmt)
        {
            continue;
        }
        collectGosubStatements(*stmt, gosubCount);
        findSelectCase(*stmt, select);
    }
    assert(gosubCount == 2);
    assert(select);
    assert(select->arms.size() == 2);

    const auto &firstArm = select->arms[0];
    assert(firstArm.str_labels.size() == 1);
    assert(firstArm.str_labels[0] == "cat");
    assert(firstArm.body.size() == 1);
    const auto *firstPrint = dynamic_cast<PrintStmt *>(firstArm.body[0].get());
    const auto *firstLiteral = firstStringArg(firstPrint);
    assert(firstLiteral);
    assert(firstLiteral->value == "meow");

    const auto &secondArm = select->arms[1];
    assert(secondArm.str_labels.size() == 1);
    assert(secondArm.str_labels[0] == "dog");
    assert(secondArm.body.size() == 1);
    const auto *secondPrint = dynamic_cast<PrintStmt *>(secondArm.body[0].get());
    const auto *secondLiteral = firstStringArg(secondPrint);
    assert(secondLiteral);
    assert(secondLiteral->value == "woof");

    assert(select->elseBody.size() == 1);
    const auto *elsePrint = dynamic_cast<PrintStmt *>(select->elseBody[0].get());
    const auto *elseLiteral = firstStringArg(elsePrint);
    assert(elseLiteral);
    assert(elseLiteral->value == "???");

    const auto evaluateCase = [&](std::string_view value) {
        for (const auto &arm : select->arms)
        {
            if (matchesLabel(arm, value))
            {
                const auto *armPrint = dynamic_cast<PrintStmt *>(arm.body[0].get());
                const auto *armLiteral = firstStringArg(armPrint);
                assert(armLiteral);
                return armLiteral->value;
            }
        }
        return elseLiteral->value;
    };

    assert(evaluateCase("dog") == "woof");
    assert(evaluateCase("emu") == "???");

    // Compile the BASIC program to IL and ensure the pipeline accepts the source.
    SourceManager compileSm;
    BasicCompilerOptions options{};
    BasicCompilerInput input{source, basPathStr};
    auto result = compileBasic(input, options, compileSm);
    assert(result.succeeded());
    assert(result.emitter);
    assert(result.emitter->errorCount() == 0);

    return 0;
}
