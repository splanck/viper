//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/test_il_pass_manager.cpp
// Purpose: Exercise PassManager pipelines, analysis caching, and preservation semantics.
// Key invariants: Custom analyses should only recompute when passes invalidate them.
// Ownership/Lifetime: Test constructs a module in-memory and runs passes locally.
// Links: docs/codemap.md
//
//===----------------------------------------------------------------------===//

#include "il/api/expected_api.hpp"
#include "il/core/Module.hpp"
#include "il/transform/PassManager.hpp"
#include "il/transform/analysis/Liveness.hpp"
#include <cassert>
#include <sstream>
#include <string>
#include <string_view>

using namespace il;

namespace
{
const char *kProgram = R"(il 0.1
func @main() -> i64 {
entry:
  ret 0
}
)";
}

core::Module parseModule()
{
    core::Module module;
    std::istringstream input(kProgram);
    auto parsed = il::api::v2::parse_text_expected(input, module);
    assert(parsed);
    return module;
}

int main()
{
    core::Module module = parseModule();

    transform::PassManager pm;
    pm.addSimplifyCFG();
    std::ostringstream instrumentation;
    pm.setInstrumentationStream(instrumentation);
    pm.setReportPassStatistics(true);

    int functionAnalysisCount = 0;
    pm.registerFunctionAnalysis<int>("count",
                                     [&functionAnalysisCount](core::Module &, core::Function &)
                                     { return ++functionAnalysisCount; });
    int cfgAnalysisCount = 0;
    pm.registerFunctionAnalysis<il::transform::CFGInfo>(
        "cfg",
        [&cfgAnalysisCount](core::Module &moduleRef, core::Function &fnRef)
        {
            ++cfgAnalysisCount;
            return il::transform::buildCFG(moduleRef, fnRef);
        });

    bool seedRan = false;
    pm.registerFunctionPass(
        "seed-analyses",
        [&seedRan](core::Function &fn, transform::AnalysisManager &analysis)
        {
            int &count = analysis.getFunctionResult<int>("count", fn);
            analysis.getFunctionResult<il::transform::CFGInfo>("cfg", fn);
            (void)count;
            seedRan = true;
            transform::PreservedAnalyses preserved;
            preserved.preserveFunction("count");
            preserved.preserveCFG();
            return preserved;
        });

    pm.registerFunctionPass(
        "reuse-cached",
        [](core::Function &fn, transform::AnalysisManager &analysis)
        {
            int &count = analysis.getFunctionResult<int>("count", fn);
            analysis.getFunctionResult<il::transform::CFGInfo>("cfg", fn);
            assert(count == 1);
            transform::PreservedAnalyses preserved;
            preserved.preserveFunction("count");
            preserved.preserveCFG();
            return preserved;
        });

    bool moduleInvalidated = false;
    pm.registerModulePass("module-invalidate",
                          [&moduleInvalidated](core::Module &, transform::AnalysisManager &)
                          {
                              moduleInvalidated = true;
                              return transform::PreservedAnalyses::none();
                          });

    pm.registerFunctionPass(
        "recompute",
        [](core::Function &fn, transform::AnalysisManager &analysis)
        {
            int &valueFirst = analysis.getFunctionResult<int>("count", fn);
            analysis.getFunctionResult<il::transform::CFGInfo>("cfg", fn);
            assert(valueFirst == 2);
            return transform::PreservedAnalyses::none();
        });

    transform::PassManager::Pipeline pipeline = {
        "seed-analyses", "reuse-cached", "module-invalidate", "recompute"};
    pm.registerPipeline("unit", pipeline);

    bool ran = pm.runPipeline(module, "unit");
    assert(ran);
    assert(seedRan);
    assert(moduleInvalidated);
    assert(functionAnalysisCount == 2);
    assert(cfgAnalysisCount == 2);

    auto findPassLine = [&instrumentation](std::string_view id) -> std::string
    {
        std::istringstream reader(instrumentation.str());
        std::string line;
        while (std::getline(reader, line))
        {
            if (line.find(id) != std::string::npos)
                return line;
        }
        return {};
    };

    std::string stats = instrumentation.str();
    assert(!stats.empty());
    assert(stats.find("bb ") != std::string::npos);
    assert(stats.find("inst ") != std::string::npos);

    std::string seedLine = findPassLine("seed-analyses");
    assert(!seedLine.empty());
    assert(seedLine.find("F:2") != std::string::npos);

    std::string reuseLine = findPassLine("reuse-cached");
    assert(!reuseLine.empty());
    assert(reuseLine.find("F:0") != std::string::npos);

    std::string recomputeLine = findPassLine("recompute");
    assert(!recomputeLine.empty());
    assert(recomputeLine.find("F:2") != std::string::npos);

    std::size_t beforeO0 = instrumentation.str().size();
    core::Module moduleO0 = parseModule();
    assert(pm.runPipeline(moduleO0, "O0"));
    std::size_t afterO0 = instrumentation.str().size();
    assert(afterO0 > beforeO0);

    core::Module moduleO1 = parseModule();
    assert(pm.runPipeline(moduleO1, "O1"));

    core::Module moduleO2 = parseModule();
    assert(pm.runPipeline(moduleO2, "O2"));

    std::string statsAfter = instrumentation.str();
    assert(statsAfter.find("simplify-cfg") != std::string::npos);
    assert(statsAfter.find("dce") != std::string::npos);
    assert(statsAfter.find("licm") != std::string::npos || statsAfter.find("inline") != std::string::npos);
    assert(!pm.runPipeline(module, "missing"));

    return 0;
}
