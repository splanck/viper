//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/test_basic_semantic.cpp
// Purpose: Unit test verifying BASIC semantic analyzer and lowerer handle a
// Key invariants: Analyzer collects symbols/labels and lowering produces
// Ownership/Lifetime: Test owns all objects locally.
// Links: docs/codemap.md
//
//===----------------------------------------------------------------------===//

#include "frontends/basic/DiagnosticEmitter.hpp"
#include "frontends/basic/Lowerer.hpp"
#include "frontends/basic/Parser.hpp"
#include "frontends/basic/SemanticAnalyzer.hpp"
#include "support/source_manager.hpp"
#include <array>
#include <cassert>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>

using namespace il::frontends::basic;
using namespace il::support;

int main()
{
    std::ostringstream srcBuilder;
    srcBuilder << "100 FUNCTION F(N)\n"
               << "110 RETURN N + 1\n"
               << "120 END FUNCTION\n"
               << "200 SUB P(Q())\n"
               << "210 PRINT LEN(\"SUB\")\n"
               << "220 END SUB\n";

    constexpr int kExtraProcedures = 16;

    srcBuilder << "1000 DIM A(5)\n"
               << "1010 DIM FLAG AS BOOLEAN\n"
               << "1020 DIM S$\n"
               << "1030 LET FLAG = TRUE\n"
               << "1035 LET FLAG = NOT FLAG\n"
               << "1040 LET X = 3\n"
               << "1050 LET Y# = 1.5\n"
               << "1060 RANDOMIZE 42: PRINT LEN(\"HI\"), A(X)\n"
               << "1070 IF FLAG THEN LET X = X + 1 ELSEIF X > 1 THEN LET X = X - 1 ELSE PRINT "
                  "\"ZERO\": PRINT \"TAIL\"\n"
               << "1080 WHILE X > 0\n"
               << "1090 PRINT LEN(\"HI\"), A(X)\n"
               << "1100 LET X = X - 1: PRINT X\n"
               << "1110 WEND\n"
               << "1120 FOR I = 1 TO 3\n"
               << "1130 LET A(I) = I\n"
               << "1140 NEXT I\n"
               << "1150 INPUT \"Value?\", S$\n"
               << "1160 PRINT F(X)\n"
               << "1170 GOTO 2000\n"
               << "1180 END\n"
               << "2000 PRINT \"DONE\";\n";

    std::string src = srcBuilder.str();
    SourceManager sm;
    uint32_t fid = sm.addFile("test.bas");
    Parser p(src, fid);
    auto prog = p.parseProgram();

    for (int i = 0; i < kExtraProcedures; ++i)
    {
        auto fn = std::make_unique<FunctionDecl>();
        fn->line = 3000 + i * 10;
        fn->name = "EXTRA_FN" + std::to_string(i);
        Param param;
        param.name = "LARGE_FN_ARG_" + std::to_string(i);
        fn->params.push_back(param);

        auto fnDim = std::make_unique<DimStmt>();
        fnDim->line = fn->line + 1;
        fnDim->name = "LARGE_FN_DIM_" + std::to_string(i);
        auto fnDimSize = std::make_unique<IntExpr>();
        fnDimSize->value = 5;
        fnDim->size = std::move(fnDimSize);
        fn->body.push_back(std::move(fnDim));

        auto fnLet = std::make_unique<LetStmt>();
        fnLet->line = fn->line + 2;
        auto fnTarget = std::make_unique<VarExpr>();
        fnTarget->name = "LARGE_FN_LOCAL_" + std::to_string(i);
        fnLet->target = std::move(fnTarget);
        auto fnExpr = std::make_unique<VarExpr>();
        fnExpr->name = param.name;
        fnLet->expr = std::move(fnExpr);
        fn->body.push_back(std::move(fnLet));

        auto fnInput = std::make_unique<InputStmt>();
        fnInput->line = fn->line + 3;
        auto fnPrompt = std::make_unique<StringExpr>();
        fnPrompt->value = "?";
        fnInput->prompt = std::move(fnPrompt);
        fnInput->vars.push_back("LARGE_FN_INPUT_" + std::to_string(i) + "$");
        fn->body.push_back(std::move(fnInput));

        auto fnReturn = std::make_unique<ReturnStmt>();
        fnReturn->line = fn->line + 4;
        auto retExpr = std::make_unique<VarExpr>();
        retExpr->name = param.name;
        fnReturn->value = std::move(retExpr);
        fn->body.push_back(std::move(fnReturn));

        prog->procs.push_back(std::move(fn));
    }
    for (int i = 0; i < kExtraProcedures; ++i)
    {
        auto sub = std::make_unique<SubDecl>();
        sub->line = 4000 + i * 10;
        sub->name = "EXTRA_SUB" + std::to_string(i);
        Param param;
        param.name = "LARGE_SUB_ARG_" + std::to_string(i);
        sub->params.push_back(param);

        auto subDim = std::make_unique<DimStmt>();
        subDim->line = sub->line + 1;
        subDim->name = "LARGE_SUB_DIM_" + std::to_string(i);
        auto subDimSize = std::make_unique<IntExpr>();
        subDimSize->value = 3;
        subDim->size = std::move(subDimSize);
        sub->body.push_back(std::move(subDim));

        auto subLet = std::make_unique<LetStmt>();
        subLet->line = sub->line + 2;
        auto subTarget = std::make_unique<VarExpr>();
        subTarget->name = "LARGE_SUB_LOCAL_" + std::to_string(i);
        subLet->target = std::move(subTarget);
        auto subExpr = std::make_unique<VarExpr>();
        subExpr->name = param.name;
        subLet->expr = std::move(subExpr);
        sub->body.push_back(std::move(subLet));

        auto subInput = std::make_unique<InputStmt>();
        subInput->line = sub->line + 3;
        auto subPrompt = std::make_unique<StringExpr>();
        subPrompt->value = "!";
        subInput->prompt = std::move(subPrompt);
        subInput->vars.push_back("LARGE_SUB_INPUT_" + std::to_string(i) + "$");
        sub->body.push_back(std::move(subInput));

        prog->procs.push_back(std::move(sub));
    }

    DiagnosticEngine de;
    DiagnosticEmitter em(de, sm);
    em.addSource(fid, src);
    SemanticAnalyzer sema(em);
    sema.analyze(*prog);
    assert(em.errorCount() == 0);
    assert(em.warningCount() == 0);
    auto arrTy = sema.lookupVarType("A");
    assert(arrTy.has_value());
    assert(*arrTy == SemanticAnalyzer::Type::ArrayInt);
    assert(sema.symbols().count("A") == 1);
    assert(sema.symbols().count("FLAG") == 1);
    assert(sema.symbols().count("S$") == 1);
    assert(sema.symbols().count("X") == 1);
    assert(sema.symbols().count("Y#") == 1);
    assert(sema.symbols().count("I") == 1);
    assert(sema.labels().count(1000) == 1);
    assert(sema.labels().count(1070) == 1);
    assert(sema.labels().count(2000) == 1);
    assert(sema.labelRefs().count(2000) == 1);
    assert(sema.procs().count("F") == 1);
    assert(sema.procs().count("P") == 1);
    assert(sema.procs().at("F").params.size() == 1);
    assert(sema.procs().at("P").params.size() == 1);

    bool hasStmtList = false;
    for (const auto &stmt : prog->main)
    {
        if (dynamic_cast<const StmtList *>(stmt.get()))
        {
            hasStmtList = true;
            break;
        }
    }
    assert(hasStmtList);

    Lowerer lowerer;
    il::core::Module module = lowerer.lowerProgram(*prog);
    bool sawMain = false;
    bool sawFunction = false;
    bool sawSub = false;
    for (const auto &fn : module.functions)
    {
        if (fn.name == "main")
            sawMain = true;
        else if (fn.name == "F")
            sawFunction = true;
        else if (fn.name == "P")
            sawSub = true;
    }
    assert(sawMain && sawFunction && sawSub);

    const std::array<std::string_view, 8> forbiddenPrefixes = {"LARGE_FN_DIM_",
                                                               "LARGE_FN_LOCAL_",
                                                               "LARGE_FN_INPUT_",
                                                               "LARGE_FN_ARG_",
                                                               "LARGE_SUB_DIM_",
                                                               "LARGE_SUB_LOCAL_",
                                                               "LARGE_SUB_INPUT_",
                                                               "LARGE_SUB_ARG_"};
    for (const auto &name : sema.symbols())
    {
        for (auto prefix : forbiddenPrefixes)
            assert(name.rfind(prefix, 0) != 0);
    }

    {
        std::string redimSrc = "10 DIM X AS INT\n20 REDIM X(5)\n30 END\n";
        SourceManager smRedim;
        uint32_t fidRedim = smRedim.addFile("redim.bas");
        Parser parserRedim(redimSrc, fidRedim);
        auto progRedim = parserRedim.parseProgram();
        DiagnosticEngine deRedim;
        DiagnosticEmitter emRedim(deRedim, smRedim);
        emRedim.addSource(fidRedim, redimSrc);
        SemanticAnalyzer semaRedim(emRedim);
        semaRedim.analyze(*progRedim);
        assert(emRedim.errorCount() == 1);
    }

    {
        std::string indexSrc = "10 DIM A(2)\n20 PRINT A(1.5)\n30 END\n";
        SourceManager smIndex;
        uint32_t fidIndex = smIndex.addFile("index.bas");
        Parser parserIndex(indexSrc, fidIndex);
        auto progIndex = parserIndex.parseProgram();
        DiagnosticEngine deIndex;
        DiagnosticEmitter emIndex(deIndex, smIndex);
        emIndex.addSource(fidIndex, indexSrc);
        SemanticAnalyzer semaIndex(emIndex);
        semaIndex.analyze(*progIndex);
        assert(emIndex.errorCount() == 0);
        assert(emIndex.warningCount() == 1);
        std::ostringstream diag;
        emIndex.printAll(diag);
        assert(diag.str().find("warning[B2002]") != std::string::npos);
    }
    return 0;
}
