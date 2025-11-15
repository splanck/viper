// File: tests/frontends/basic/ParseSelectCasePositiveTests.cpp
// Purpose: Ensure SELECT CASE with ELSE parses correctly from BASIC source file.
// Key invariants: Parser builds SelectCaseStmt with expected arms and else body.
// Ownership/Lifetime: Test owns parser, source manager, and AST result.
// Links: docs/codemap.md

#include "frontends/basic/Parser.hpp"
#include "support/source_manager.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
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

std::filesystem::path selectCaseFixturePath()
{
    auto path = std::filesystem::path(__FILE__).parent_path();
    path /= "../../parser/basic/select_case_ok.bas";
    return std::filesystem::absolute(path);
}

std::string normalizeForParser(const std::string &source)
{
    static constexpr std::string_view kLeadingAssignment = "10 X = 2";
    if (source.rfind(kLeadingAssignment, 0) != 0)
    {
        return source;
    }

    std::string normalized = source;
    normalized.replace(0, kLeadingAssignment.size(), "10 LET X = 2");
    return normalized;
}
} // namespace

int main()
{
    const auto basPath = selectCaseFixturePath();
    const std::string source = readFile(basPath);
    if (source.empty())
    {
        std::cerr << "error: BASIC source fixture is empty at " << basPath << '\n';
        return 1;
    }

    if (source.rfind("10 X = 2", 0) != 0)
    {
        std::cerr << "error: expected fixture to begin with '10 X = 2'\n";
        return 1;
    }

    const std::string parseSource = normalizeForParser(source);

    SourceManager sm;
    const uint32_t fid = sm.addFile(basPath.string());
    Parser parser(parseSource, fid);
    auto program = parser.parseProgram();
    if (!program)
    {
        std::cerr << "error: parser returned null program\n";
        return 1;
    }

    if (program->main.size() != 2)
    {
        std::cerr << "error: expected two statements in main, got " << program->main.size() << '\n';
        return 1;
    }

    const auto selectCount = std::count_if(
        program->main.begin(),
        program->main.end(),
        [](const StmtPtr &stmt) { return dynamic_cast<SelectCaseStmt *>(stmt.get()) != nullptr; });
    if (selectCount != 1)
    {
        std::cerr << "error: expected exactly one SelectCaseStmt, found " << selectCount << '\n';
        return 1;
    }

    auto *select = dynamic_cast<SelectCaseStmt *>(program->main[1].get());
    if (!select)
    {
        std::cerr << "error: second statement is not SelectCaseStmt\n";
        return 1;
    }

    if (select->arms.size() != 2)
    {
        std::cerr << "error: expected two CASE arms, got " << select->arms.size() << '\n';
        return 1;
    }

    const auto &firstArm = select->arms[0];
    if (firstArm.labels.size() != 2 || firstArm.labels[0] != 1 || firstArm.labels[1] != 3)
    {
        std::cerr << "error: first CASE labels expected [1,3]\n";
        return 1;
    }

    const auto &secondArm = select->arms[1];
    if (secondArm.labels.size() != 1 || secondArm.labels[0] != 2)
    {
        std::cerr << "error: second CASE labels expected [2]\n";
        return 1;
    }

    if (select->elseBody.empty())
    {
        std::cerr << "error: expected CASE ELSE body to be present\n";
        return 1;
    }

    return 0;
}
