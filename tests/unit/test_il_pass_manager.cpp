// File: tests/unit/test_il_pass_manager.cpp
// Purpose: Exercise PassManager pipelines, analysis caching, and preservation semantics.
// Key invariants: Custom analyses should only recompute when passes invalidate them.
// Ownership: Test constructs a module in-memory and runs passes locally.
// Links: docs/codemap.md

#include "il/api/expected_api.hpp"
#include "il/core/Module.hpp"
#include "il/transform/PassManager.hpp"
#include <cassert>
#include <sstream>
#include <string>

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

int main()
{
    core::Module module;
    std::istringstream input(kProgram);
    auto parsed = il::api::v2::parse_text_expected(input, module);
    assert(parsed);

    transform::PassManager pm;

    int functionAnalysisCount = 0;
    pm.registerFunctionAnalysis<int>("count",
                                     [&functionAnalysisCount](core::Module &, core::Function &)
                                     { return ++functionAnalysisCount; });

    bool functionPassRun = false;
    bool modulePassRun = false;

    pm.registerFunctionPass("mark-function",
                            [&functionPassRun](core::Function &, transform::AnalysisManager &)
                            {
                                functionPassRun = true;
                                return transform::PreservedAnalyses::none();
                            });

    pm.registerFunctionPass("check-preserve",
                            [](core::Function &fn, transform::AnalysisManager &analysis)
                            {
                                int &first = analysis.getFunctionResult<int>("count", fn);
                                int &second = analysis.getFunctionResult<int>("count", fn);
                                assert(first == 1 && second == 1);
                                transform::PreservedAnalyses preserved;
                                preserved.preserveFunction("count");
                                return preserved;
                            });

    pm.registerFunctionPass("check-reuse",
                            [](core::Function &fn, transform::AnalysisManager &analysis)
                            {
                                int &value = analysis.getFunctionResult<int>("count", fn);
                                assert(value == 1);
                                return transform::PreservedAnalyses::none();
                            });

    pm.registerFunctionPass("check-recompute",
                            [](core::Function &fn, transform::AnalysisManager &analysis)
                            {
                                int &valueFirst = analysis.getFunctionResult<int>("count", fn);
                                int &valueSecond = analysis.getFunctionResult<int>("count", fn);
                                assert(valueFirst == 2 && valueSecond == 2);
                                return transform::PreservedAnalyses::none();
                            });

    pm.registerModulePass("mark-module",
                          [&modulePassRun](core::Module &, transform::AnalysisManager &)
                          {
                              modulePassRun = true;
                              return transform::PreservedAnalyses::all();
                          });

    transform::PassManager::Pipeline pipeline = {
        "mark-function", "check-preserve", "check-reuse", "check-recompute", "mark-module"};
    pm.registerPipeline("unit", pipeline);

    bool ran = pm.runPipeline(module, "unit");
    assert(ran);
    assert(!pm.runPipeline(module, "missing"));
    assert(functionPassRun);
    assert(modulePassRun);
    assert(functionAnalysisCount == 2);

    return 0;
}
