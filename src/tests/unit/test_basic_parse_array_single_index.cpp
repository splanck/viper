// File: tests/unit/test_basic_parse_array_single_index.cpp
// Purpose: Guard against use-after-move in single-index ArrayExpr parsing (HIGH-2).
// Key invariants: For single-dimensional access, Parser should populate only 'index'
//                 and leave 'indices' empty to avoid moved-from pointers.
// Ownership/Lifetime: Test owns parser and AST.

#include "frontends/basic/Parser.hpp"
#include "support/source_manager.hpp"

#include <cassert>
#include <string>

using namespace il::frontends::basic;
using namespace il::support;

int main()
{
    std::string src = "10 DIM A(2)\n20 LET Y = A(1)\n30 END\n";
    SourceManager sm;
    uint32_t fid = sm.addFile("single_index.bas");
    Parser p(src, fid);
    auto prog = p.parseProgram();

    auto *let = dynamic_cast<LetStmt *>(prog->main[1].get());
    assert(let);
    auto *arr = dynamic_cast<ArrayExpr *>(let->expr.get());
    assert(arr && arr->name == "A");
    // Single-index arrays should use 'index' and keep 'indices' empty
    assert(arr->index);
    assert(arr->indices.empty());
    auto *idx = dynamic_cast<IntExpr *>(arr->index.get());
    assert(idx && idx->value == 1);
    return 0;
}

