// File: tests/frontends/basic/ParserInputPromptEscapeTests.cpp
// Purpose: Ensure INPUT prompt literals decode escape sequences like other strings.
// Key invariants: Parser normalizes escapes so AST stores actual newline characters.
// Ownership/Lifetime: Test owns parser and AST resources; no shared state.
// Links: docs/codemap.md

#include "frontends/basic/Parser.hpp"
#include "support/source_manager.hpp"

#include <cassert>
#include <string>

using namespace il::frontends::basic;
using namespace il::support;

int main()
{
    const std::string src =
        "10 INPUT \"Ready?\\n\", A$\n"
        "20 END\n";

    SourceManager sm;
    const uint32_t fid = sm.addFile("input_prompt_escape.bas");

    Parser parser(src, fid);
    auto program = parser.parseProgram();
    assert(program);

    assert(program->main.size() == 2);
    auto *inputStmt = dynamic_cast<InputStmt *>(program->main[0].get());
    assert(inputStmt);
    assert(inputStmt->prompt);

    auto *promptExpr = dynamic_cast<StringExpr *>(inputStmt->prompt.get());
    assert(promptExpr);
    assert(promptExpr->value == "Ready?\n");
    assert(promptExpr->value.size() == 7);
    assert(promptExpr->value.back() == '\n');

    return 0;
}
