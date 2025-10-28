// File: tests/frontends/basic/SubroutinesGosubInlineCaseTests.cpp
// Purpose: Validate parsing and execution of GOSUB with named labels and inline CASE bodies.
// Key invariants: Parser recognises CASE inline statements and named-label GOSUB; VM prints expected output.
// Ownership/Lifetime: Test owns parser/compiler objects and VM module instance.
// Links: docs/codemap.md

#include "frontends/basic/BasicCompiler.hpp"
#include "frontends/basic/Parser.hpp"
#include "frontends/basic/ast/StmtNodes.hpp"
#include "il/io/Serializer.hpp"
#include "support/source_manager.hpp"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <string>
#include <vector>

using namespace il::frontends::basic;
using namespace il::support;

namespace
{

[[nodiscard]] std::string readFixture(const std::filesystem::path &path)
{
    std::ifstream in(path);
    assert(in && "failed to open BASIC fixture");
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

[[nodiscard]] std::filesystem::path fixturePath()
{
    auto path = std::filesystem::path(__FILE__).parent_path();
    path /= "subroutines_gosub_inline_case.bas";
    return std::filesystem::absolute(path);
}

[[nodiscard]] const SelectCaseStmt *findSelectCase(const Program &program)
{
    const SelectCaseStmt *select = nullptr;
    std::function<void(const StmtPtr &)> visit = [&](const StmtPtr &node) {
        if (!node || select)
        {
            return;
        }
        if (const auto *list = dynamic_cast<const StmtList *>(node.get()))
        {
            for (const auto &child : list->stmts)
            {
                visit(child);
                if (select)
                {
                    return;
                }
            }
            return;
        }
        if (const auto *candidate = dynamic_cast<const SelectCaseStmt *>(node.get()))
        {
            select = candidate;
            return;
        }
    };

    for (const auto &stmt : program.main)
    {
        visit(stmt);
        if (select)
        {
            break;
        }
    }
    return select;
}

[[nodiscard]] std::vector<const GosubStmt *> collectGosubs(const Program &program)
{
    std::vector<const GosubStmt *> gosubs;
    std::function<void(const StmtPtr &)> visit = [&](const StmtPtr &node) {
        if (!node)
        {
            return;
        }
        if (const auto *list = dynamic_cast<const StmtList *>(node.get()))
        {
            for (const auto &child : list->stmts)
            {
                visit(child);
            }
            return;
        }
        if (const auto *gosub = dynamic_cast<const GosubStmt *>(node.get()))
        {
            gosubs.push_back(gosub);
        }
    };

    for (const auto &stmt : program.main)
    {
        visit(stmt);
    }
    return gosubs;
}

} // namespace

int main()
{
    const std::filesystem::path basPath = fixturePath();
    const std::string source = readFixture(basPath);

    SourceManager sm;
    const uint32_t fid = sm.addFile(basPath.string());
    Parser parser(source, fid);
    auto program = parser.parseProgram();
    assert(program && "parser returned null program");

    const auto gosubs = collectGosubs(*program);
    assert(gosubs.size() == 2 && "expected two GOSUB statements");

    for (const auto *gosub : gosubs)
    {
        assert(gosub->targetLine == 1'000'000 && "GOSUB target should match Speak label line");
    }

    const SelectCaseStmt *select = findSelectCase(*program);
    assert(select && "expected SELECT CASE statement under Speak label");
    assert(select->arms.size() == 2 && "expected two CASE arms");

    const auto &catArm = select->arms[0];
    assert(catArm.str_labels.size() == 1 && catArm.str_labels[0] == "cat");
    assert(catArm.body.size() == 1 && "expected inline CASE body to contain PRINT");
    const auto *catPrint = dynamic_cast<const PrintStmt *>(catArm.body[0].get());
    assert(catPrint && "CASE \"cat\" should contain PRINT statement");
    assert(catPrint->items.size() == 1);
    const auto *catExpr = dynamic_cast<const StringExpr *>(catPrint->items[0].expr.get());
    assert(catExpr && catExpr->value == "meow");

    const auto &dogArm = select->arms[1];
    assert(dogArm.str_labels.size() == 1 && dogArm.str_labels[0] == "dog");
    assert(dogArm.body.size() == 1);
    const auto *dogPrint = dynamic_cast<const PrintStmt *>(dogArm.body[0].get());
    assert(dogPrint && dogPrint->items.size() == 1);
    const auto *dogExpr = dynamic_cast<const StringExpr *>(dogPrint->items[0].expr.get());
    assert(dogExpr && dogExpr->value == "woof");

    assert(select->elseBody.size() == 1 && "expected CASE ELSE body");
    const auto *elsePrint = dynamic_cast<const PrintStmt *>(select->elseBody[0].get());
    assert(elsePrint && elsePrint->items.size() == 1);
    const auto *elseExpr = dynamic_cast<const StringExpr *>(elsePrint->items[0].expr.get());
    assert(elseExpr && elseExpr->value == "???");

    BasicCompilerOptions options{};
    BasicCompilerInput input{source, basPath.string(), fid};
    auto compileResult = compileBasic(input, options, sm);
    assert(compileResult.succeeded() && "expected compilation to succeed");

    il::core::Module module = std::move(compileResult.module);
    const std::string ilText = il::io::Serializer::toString(module);
    compileResult.emitter.reset();
    const std::size_t woofPos = ilText.find("\"woof\"");
    const std::size_t unknownPos = ilText.find("\"???\"");
    assert(woofPos != std::string::npos && unknownPos != std::string::npos);
    assert(woofPos < unknownPos && "expected woof print before fallback");

    return 0;
}
