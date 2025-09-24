// File: tests/unit/test_basic_parse_array_var.cpp
// Purpose: Verify Parser distinguishes variable and array references.
// Key invariants: Identifier without parentheses yields VarExpr; with index yields ArrayExpr.
// Ownership/Lifetime: Test owns parser and AST.
// Links: docs/codemap.md

#include "frontends/basic/Parser.hpp"
#include "support/source_manager.hpp"
#include <cassert>
#include <string>

using namespace il::frontends::basic;
using namespace il::support;

int main()
{
    // Variable reference
    {
        std::string src = "10 LET Y = X\n20 END\n";
        SourceManager sm;
        uint32_t fid = sm.addFile("test.bas");
        Parser p(src, fid);
        auto prog = p.parseProgram();
        auto *let = dynamic_cast<LetStmt *>(prog->main[0].get());
        auto *var = dynamic_cast<VarExpr *>(let->expr.get());
        assert(var && var->name == "X");
    }

    // Array reference
    {
        std::string src = "10 DIM A(2)\n20 LET Y = A(1)\n30 END\n";
        SourceManager sm;
        uint32_t fid = sm.addFile("test.bas");
        Parser p(src, fid);
        auto prog = p.parseProgram();
        auto *let = dynamic_cast<LetStmt *>(prog->main[1].get());
        auto *arr = dynamic_cast<ArrayExpr *>(let->expr.get());
        assert(arr && arr->name == "A");
        auto *idx = dynamic_cast<IntExpr *>(arr->index.get());
        assert(idx && idx->value == 1);
    }

    // REDIM statement
    {
        std::string src = "10 DIM A(2)\n20 REDIM A(4)\n30 END\n";
        SourceManager sm;
        uint32_t fid = sm.addFile("test.bas");
        Parser p(src, fid);
        auto prog = p.parseProgram();
        auto *redim = dynamic_cast<ReDimStmt *>(prog->main[1].get());
        assert(redim && redim->name == "A");
        auto *size = dynamic_cast<IntExpr *>(redim->size.get());
        assert(size && size->value == 4);
    }

    return 0;
}
