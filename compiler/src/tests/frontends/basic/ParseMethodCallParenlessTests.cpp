//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/frontends/basic/ParseMethodCallParenlessTests.cpp
// Purpose: Ensure BASIC parser accepts zero-arg method calls without parentheses in statement
// Key invariants: To be documented.
// Ownership/Lifetime: To be documented.
// Links: docs/architecture.md
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
    // Program defines a simple class with a zero-arg SUB, then calls it both
    // with and without parentheses to exercise parser behavior.
    const std::string src = "10 CLASS C\n"
                            "20   SUB INC()\n"
                            "30   END SUB\n"
                            "40 END CLASS\n"
                            "50 DIM X AS C\n"
                            "60 LET X = NEW C()\n"
                            "70 X.INC\n"   // paren-less method call (statement form)
                            "80 X.INC()\n" // canonical method call with parentheses
                            "90 END\n";

    SourceManager sm;
    uint32_t fid = sm.addFile("method_parenless.bas");

    Parser parser(src, fid);
    auto program = parser.parseProgram();
    assert(program);

    // We expect lines 70 and 80 to be CallStmt.
    // The main sequence contains: DIM, LET, CallStmt, CallStmt, END (plus any implicit labels).
    bool sawParenless = false;
    bool sawParened = false;
    for (const auto &stmt : program->main)
    {
        if (auto *callStmt = dynamic_cast<CallStmt *>(stmt.get()))
        {
            if (auto *m = dynamic_cast<MethodCallExpr *>(callStmt->call.get()))
            {
                if (m->method == "INC")
                {
                    // We don't distinguish the two syntactic forms in AST here,
                    // but seeing two method-call statements for INC verifies both were accepted.
                    // Flip flags in order of first/second sighting.
                    if (!sawParenless)
                        sawParenless = true;
                    else
                        sawParened = true;
                }
            }
        }
    }

    assert(sawParenless);
    assert(sawParened);
    return 0;
}
