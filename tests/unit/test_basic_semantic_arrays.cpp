// File: tests/unit/test_basic_semantic_arrays.cpp
// Purpose: Verify BASIC semantic analyzer handles array declarations, resizing,
//          indexing, and bound intrinsics.
// Key invariants: DIM establishes array type metadata, REDIM validates existing
//                 arrays, invalid indexes report errors, and bound intrinsics
//                 respect zero-based semantics.
// Ownership/Lifetime: Tests own parser, analyzer, and lowerer instances.
// Links: docs/codemap.md

#include "frontends/basic/DiagnosticEmitter.hpp"
#include "frontends/basic/Lowerer.hpp"
#include "frontends/basic/Parser.hpp"
#include "frontends/basic/SemanticAnalyzer.hpp"
#include "support/source_manager.hpp"
#include <cassert>
#include <string>

using namespace il::frontends::basic;
using namespace il::support;

int main()
{
    // DIM infers array type information.
    {
        const std::string src = "10 DIM A(5)\n20 END\n";
        SourceManager sm;
        uint32_t fid = sm.addFile("dim_array.bas");
        Parser parser(src, fid);
        auto prog = parser.parseProgram();
        assert(prog);

        DiagnosticEngine de;
        DiagnosticEmitter em(de, sm);
        em.addSource(fid, src);
        SemanticAnalyzer sema(em);
        sema.analyze(*prog);

        assert(em.errorCount() == 0);
        auto arrTy = sema.lookupVarType("A");
        assert(arrTy.has_value());
        assert(*arrTy == SemanticAnalyzer::Type::ArrayInt);
    }

    // REDIM succeeds for known arrays and fails for unknown names.
    {
        const std::string src = "10 DIM A(5)\n20 REDIM A(10)\n30 END\n";
        SourceManager sm;
        uint32_t fid = sm.addFile("redim_ok.bas");
        Parser parser(src, fid);
        auto prog = parser.parseProgram();
        assert(prog);

        DiagnosticEngine de;
        DiagnosticEmitter em(de, sm);
        em.addSource(fid, src);
        SemanticAnalyzer sema(em);
        sema.analyze(*prog);

        assert(em.errorCount() == 0);
        auto arrTy = sema.lookupVarType("A");
        assert(arrTy.has_value());
        assert(*arrTy == SemanticAnalyzer::Type::ArrayInt);
    }
    {
        const std::string src = "10 REDIM B(3)\n20 END\n";
        SourceManager sm;
        uint32_t fid = sm.addFile("redim_fail.bas");
        Parser parser(src, fid);
        auto prog = parser.parseProgram();
        assert(prog);

        DiagnosticEngine de;
        DiagnosticEmitter em(de, sm);
        em.addSource(fid, src);
        SemanticAnalyzer sema(em);
        sema.analyze(*prog);

        assert(em.errorCount() == 1);
    }

    // Using a string index on an integer array reports a type error.
    {
        const std::string src = "10 DIM A(2)\n20 PRINT A(\"X\")\n30 END\n";
        SourceManager sm;
        uint32_t fid = sm.addFile("array_index.bas");
        Parser parser(src, fid);
        auto prog = parser.parseProgram();
        assert(prog);

        DiagnosticEngine de;
        DiagnosticEmitter em(de, sm);
        em.addSource(fid, src);
        SemanticAnalyzer sema(em);
        sema.analyze(*prog);

        assert(em.errorCount() == 1);
    }

    // LBOUND is constant zero and UBOUND yields an integer result.
    {
        const std::string src = "10 DIM A(4)\n20 LET L = LBOUND(A)\n30 END\n";
        SourceManager sm;
        uint32_t fid = sm.addFile("lbound.bas");
        Parser parser(src, fid);
        auto prog = parser.parseProgram();
        assert(prog);

        DiagnosticEngine de;
        DiagnosticEmitter em(de, sm);
        em.addSource(fid, src);
        SemanticAnalyzer sema(em);
        sema.analyze(*prog);
        assert(em.errorCount() == 0);

        const auto *letStmt = dynamic_cast<LetStmt *>(prog->main[1].get());
        assert(letStmt);
        const uint32_t letSourceLine = letStmt->loc.line;

        Lowerer lowerer;
        il::core::Module module = lowerer.lowerProgram(*prog);
        const il::core::Function *mainFn = nullptr;
        for (const auto &fn : module.functions)
        {
            if (fn.name == "main")
            {
                mainFn = &fn;
                break;
            }
        }
        assert(mainFn);

        bool sawStoreZero = false;
        for (const auto &block : mainFn->blocks)
        {
            for (const auto &instr : block.instructions)
            {
                if (instr.op == il::core::Opcode::Store && instr.loc.line == letSourceLine &&
                    instr.operands.size() == 2)
                {
                    const auto &value = instr.operands[1];
                    if (value.kind == il::core::Value::Kind::ConstInt && value.i64 == 0)
                    {
                        sawStoreZero = true;
                    }
                }
            }
        }
        assert(sawStoreZero);
    }
    {
        const std::string src = "10 DIM A(6)\n20 LET S$ = UBOUND(A)\n30 END\n";
        SourceManager sm;
        uint32_t fid = sm.addFile("ubound.bas");
        Parser parser(src, fid);
        auto prog = parser.parseProgram();
        assert(prog);

        DiagnosticEngine de;
        DiagnosticEmitter em(de, sm);
        em.addSource(fid, src);
        SemanticAnalyzer sema(em);
        sema.analyze(*prog);

        assert(em.errorCount() == 1);
    }

    return 0;
}
