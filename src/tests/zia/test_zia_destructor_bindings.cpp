//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//

#include "frontends/zia/Compiler.hpp"
#include "il/core/Opcode.hpp"
#include "support/source_manager.hpp"
#include "tests/TestHarness.hpp"

#include <string>

using namespace il::frontends::zia;
using namespace il::support;

namespace
{

static bool hasCallee(const il::core::Module &mod, const std::string &fnName, const std::string &callee)
{
    for (const auto &fn : mod.functions)
    {
        if (fn.name != fnName)
            continue;
        for (const auto &bb : fn.blocks)
        {
            for (const auto &in : bb.instructions)
            {
                if (in.op == il::core::Opcode::Call && in.callee == callee)
                    return true;
            }
        }
    }
    return false;
}

TEST(ZiaDestructorBindings, BoundRuntimeSymbolsResolveInDeinit)
{
    SourceManager sm;
    const std::string source = R"(
module Test;

bind Viper.Terminal;

var released: Boolean = false;

entity Resource {
    deinit {
        released = true;
        Say("deinit");
    }
}

func start() {
    {
        var r = new Resource();
    }
}
)";

    CompilerInput input{.source = source, .path = "test_destructor_bindings.zia"};
    CompilerOptions opts{};
    auto result = compile(input, opts, sm);

    if (!result.succeeded())
    {
        for (const auto &d : result.diagnostics.diagnostics())
        {
            std::cerr << "  [" << (d.severity == Severity::Error ? "ERROR" : "WARN") << "] "
                      << d.message << "\n";
        }
    }

    ASSERT_TRUE(result.succeeded());
    EXPECT_TRUE(hasCallee(result.module, "Resource.__dtor", "Viper.Terminal.Say"));
}

} // namespace

int main(int argc, char **argv)
{
    viper_test::init(&argc, argv);
    return viper_test::run_all_tests();
}
