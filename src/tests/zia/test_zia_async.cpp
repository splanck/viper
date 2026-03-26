//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Unit tests for Zia async/await lowering and typing.
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

/// @brief Has diag containing.
bool hasDiagContaining(const DiagnosticEngine &diag, const std::string &needle)
{
    for (const auto &d : diag.diagnostics())
    {
        if (d.message.find(needle) != std::string::npos)
            return true;
    }
    return false;
}

/// @brief Compile source.
CompilerResult compileSource(const std::string &source, const std::string &path = "async_test.zia")
{
    SourceManager sm;
    CompilerInput input{.source = source, .path = path};
    CompilerOptions opts{};
    return compile(input, opts, sm);
}

/// @brief Has function.
bool hasFunction(const il::core::Module &mod, const std::string &name)
{
    for (const auto &fn : mod.functions)
    {
        if (fn.name == name)
            return true;
    }
    return false;
}

bool hasDirectCall(const il::core::Module &mod,
                   const std::string &fnName,
                   const std::string &callee)
{
    for (const auto &fn : mod.functions)
    {
        if (fn.name != fnName)
            continue;
        for (const auto &block : fn.blocks)
        {
            for (const auto &instr : block.instructions)
            {
                if (instr.op == il::core::Opcode::Call && instr.callee == callee)
                    return true;
            }
        }
    }
    return false;
}

/// @brief Has indirect call.
bool hasIndirectCall(const il::core::Module &mod, const std::string &fnName)
{
    for (const auto &fn : mod.functions)
    {
        if (fn.name != fnName)
            continue;
        for (const auto &block : fn.blocks)
        {
            for (const auto &instr : block.instructions)
            {
                if (instr.op == il::core::Opcode::CallIndirect)
                    return true;
            }
        }
    }
    return false;
}

TEST(ZiaAsync, AsyncFunctionLowersToWorkerAndWrapper)
{
    const std::string src = R"(module Test;

/// @brief Fetch data.
async func fetchData(name: String, retries: Integer) -> String {
    return name;
}

/// @brief Start.
func start() {
    var future = fetchData("viper", 2);
}
)";

    auto result = compileSource(src, "async_wrapper.zia");
    ASSERT_TRUE(result.succeeded());
    EXPECT_TRUE(hasFunction(result.module, "fetchData"));
    EXPECT_TRUE(hasFunction(result.module, "fetchData__async_worker"));
    EXPECT_TRUE(hasDirectCall(result.module, "fetchData", "Viper.Threads.Async.Run"));
}

TEST(ZiaAsync, AwaitUsesFutureGetAndUnboxesKnownPayload)
{
    const std::string src = R"(module Test;

/// @brief Fetch data.
async func fetchData() -> String {
    return "ready";
}

/// @brief Start.
func start() {
    var value: String = await fetchData();
}
)";

    auto result = compileSource(src, "async_await_string.zia");
    ASSERT_TRUE(result.succeeded());
    EXPECT_TRUE(hasDirectCall(result.module, "main", "Viper.Threads.Future.Get"));
}

TEST(ZiaAsync, AwaitRejectsNonFutureOperands)
{
    const std::string src = R"(module Test;

/// @brief Start.
func start() {
    var value = await "not a future";
}
)";

    auto result = compileSource(src);
    EXPECT_FALSE(result.succeeded());
    EXPECT_TRUE(hasDiagContaining(result.diagnostics, "`await` expects Viper.Threads.Future"));
}

TEST(ZiaAsync, AsyncWorkerInvokesBodyThroughGeneratedFunction)
{
    const std::string src = R"(module Test;

/// @brief Add one.
async func addOne(value: Integer) -> Integer {
    return value + 1;
}

/// @brief Start.
func start() {
    var future = addOne(41);
}
)";

    auto result = compileSource(src, "async_worker_body.zia");
    ASSERT_TRUE(result.succeeded());
    EXPECT_FALSE(hasIndirectCall(result.module, "addOne__async_worker"));
}

} // namespace

int main(int argc, char **argv)
{
    viper_test::init(&argc, argv);
    return viper_test::run_all_tests();
}
