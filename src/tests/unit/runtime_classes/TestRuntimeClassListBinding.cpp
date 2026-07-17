//===----------------------------------------------------------------------===//
// Part of the Zanna project, under the GNU GPL v3.
// File: tests/unit/runtime_classes/TestRuntimeClassListBinding.cpp
// Purpose: Ensure instance calls to Zanna.Collections.List bind to externs.
//===----------------------------------------------------------------------===//
#include "frontends/basic/BasicCompiler.hpp"
#include "il/core/Extern.hpp"
#include "il/core/Module.hpp"
#include "support/source_manager.hpp"
#include "tests/TestHarness.hpp"

#include <algorithm>
#include <string>

namespace {
[[nodiscard]] bool hasExtern(const il::core::Module &module, std::string_view name) {
    const auto &externs = module.externs;
    return std::any_of(
        externs.begin(), externs.end(), [&](const il::core::Extern &e) { return e.name == name; });
}
} // namespace

TEST(RuntimeClassListBinding, EmitsListExterns) {
    il::support::SourceManager sm;
    il::frontends::basic::BasicCompilerOptions opts{};
    const char *kSrc = R"BASIC(
10 DIM l AS Zanna.Collections.List
20 l = NEW Zanna.Collections.List()
30 l.Push(l)
40 PRINT l.Count
50 l.RemoveAt(0)
60 l.Clear()
70 PRINT l.Get(0)
80 l.Set(0, l)
90 END
)BASIC";
    std::string source(kSrc);
    il::frontends::basic::BasicCompilerInput input{source, "list_binding.bas"};
    auto result = il::frontends::basic::compileBasic(input, opts, sm);
    ASSERT_TRUE(result.succeeded());
    EXPECT_TRUE(hasExtern(result.module, "Zanna.Collections.List.New"));
    EXPECT_TRUE(hasExtern(result.module, "Zanna.Collections.List.Push"));
    EXPECT_TRUE(hasExtern(result.module, "Zanna.Collections.List.get_Count"));
    EXPECT_TRUE(hasExtern(result.module, "Zanna.Collections.List.RemoveAt"));
    EXPECT_TRUE(hasExtern(result.module, "Zanna.Collections.List.Clear"));
    EXPECT_TRUE(hasExtern(result.module, "Zanna.Collections.List.Get"));
    EXPECT_TRUE(hasExtern(result.module, "Zanna.Collections.List.Set"));
}

int main(int argc, char **argv) {
    zanna_test::init(&argc, argv);
    return zanna_test::run_all_tests();
}
