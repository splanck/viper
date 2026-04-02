//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/zia/test_zia_http_server.cpp
// Purpose: Ensure Zia HttpServer routes lower to real handler bindings.
//
//===----------------------------------------------------------------------===//

#include "frontends/zia/Compiler.hpp"
#include "il/core/Extern.hpp"
#include "il/core/Module.hpp"
#include "il/core/Opcode.hpp"
#include "support/source_manager.hpp"
#include "tests/TestHarness.hpp"

#include <algorithm>
#include <string>

namespace {

bool hasExtern(const il::core::Module &module, std::string_view name) {
    return std::any_of(module.externs.begin(), module.externs.end(), [&](const il::core::Extern &e) {
        return e.name == name;
    });
}

int countCallsTo(const il::core::Module &module, std::string_view name) {
    int count = 0;
    for (const auto &fn : module.functions) {
        for (const auto &bb : fn.blocks) {
            for (const auto &ins : bb.instructions) {
                if (ins.op == il::core::Opcode::Call && ins.callee == name)
                    ++count;
            }
        }
    }
    return count;
}

} // namespace

TEST(ZiaHttpServerBinding, EmitsBindHandlerForLiteralRouteTag) {
    il::support::SourceManager sm;
    const std::string source = R"(
module Test;

func handlePing(req: Viper.Network.ServerReq, res: Viper.Network.ServerRes) {
    res.Send("pong");
}

func start() {
    var server = new Viper.Network.HttpServer(8080);
    server.Get("/ping", "handlePing");
}
)";

    il::frontends::zia::CompilerInput input{.source = source, .path = "http_server_binding.zia"};
    il::frontends::zia::CompilerOptions opts{};
    auto result = il::frontends::zia::compile(input, opts, sm);
    ASSERT_TRUE(result.succeeded());
    EXPECT_TRUE(hasExtern(result.module, "Viper.Network.HttpServer.Get"));
    EXPECT_TRUE(hasExtern(result.module, "Viper.Network.HttpServer.BindHandler"));
    EXPECT_TRUE(hasExtern(result.module, "Viper.Network.ServerRes.Send"));
    EXPECT_EQ(countCallsTo(result.module, "Viper.Network.HttpServer.BindHandler"), 1);
}

int main() {
    return viper_test::run_all_tests();
}
