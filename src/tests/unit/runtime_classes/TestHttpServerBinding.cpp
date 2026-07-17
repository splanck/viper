//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/runtime_classes/TestHttpServerBinding.cpp
// Purpose: Ensure BASIC HttpServer routes emit real handler bindings.
//
//===----------------------------------------------------------------------===//

#include "frontends/basic/BasicCompiler.hpp"
#include "il/core/Extern.hpp"
#include "il/core/Module.hpp"
#include "il/core/Opcode.hpp"
#include "support/source_manager.hpp"
#include "tests/TestHarness.hpp"

#include <algorithm>
#include <string>

namespace {

bool hasExtern(const il::core::Module &module, std::string_view name) {
    return std::any_of(module.externs.begin(),
                       module.externs.end(),
                       [&](const il::core::Extern &e) { return e.name == name; });
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

TEST(RuntimeClassBinding, EmitsHttpServerBindHandlerForLiteralRouteTag) {
    il::support::SourceManager sm;
    il::frontends::basic::BasicCompilerOptions opts{};
    const char *kSrc = R"BASIC(
10 DIM server AS Zanna.Network.HttpServer
20 server = NEW Zanna.Network.HttpServer(8080)
30 server.Get("/ping", "HandlePing")
40 END

SUB HandlePing(req AS Zanna.Network.ServerReq, res AS Zanna.Network.ServerRes)
    res.Send("pong")
END SUB
)BASIC";

    std::string source(kSrc);
    il::frontends::basic::BasicCompilerInput input{source, "http_server_binding.bas"};
    auto result = il::frontends::basic::compileBasic(input, opts, sm);
    ASSERT_TRUE(result.succeeded());
    EXPECT_TRUE(hasExtern(result.module, "Zanna.Network.HttpServer.Get"));
    EXPECT_TRUE(hasExtern(result.module, "Zanna.Network.HttpServer.BindHandler"));
    EXPECT_TRUE(hasExtern(result.module, "Zanna.Network.ServerRes.Send"));
    EXPECT_EQ(countCallsTo(result.module, "Zanna.Network.HttpServer.BindHandler"), 1);
}

TEST(RuntimeClassBinding, EmitsHttpsServerBindHandlerForLiteralRouteTag) {
    il::support::SourceManager sm;
    il::frontends::basic::BasicCompilerOptions opts{};
    const char *kSrc = R"BASIC(
10 DIM server AS Zanna.Network.HttpsServer
20 server = NEW Zanna.Network.HttpsServer(8443, "cert.pem", "key.pem")
30 server.Get("/ping", "HandlePing")
40 END

SUB HandlePing(req AS Zanna.Network.ServerReq, res AS Zanna.Network.ServerRes)
    res.Send("pong")
END SUB
)BASIC";

    std::string source(kSrc);
    il::frontends::basic::BasicCompilerInput input{source, "https_server_binding.bas"};
    auto result = il::frontends::basic::compileBasic(input, opts, sm);
    ASSERT_TRUE(result.succeeded());
    EXPECT_TRUE(hasExtern(result.module, "Zanna.Network.HttpsServer.Get"));
    EXPECT_TRUE(hasExtern(result.module, "Zanna.Network.HttpsServer.BindHandler"));
    EXPECT_TRUE(hasExtern(result.module, "Zanna.Network.ServerRes.Send"));
    EXPECT_EQ(countCallsTo(result.module, "Zanna.Network.HttpsServer.BindHandler"), 1);
}

TEST(RuntimeClassBinding, EmitsWssServerBroadcastExterns) {
    il::support::SourceManager sm;
    il::frontends::basic::BasicCompilerOptions opts{};
    const char *kSrc = R"BASIC(
10 DIM server AS Zanna.Network.WssServer
20 server = NEW Zanna.Network.WssServer(8443, "cert.pem", "key.pem")
30 server.Broadcast("hello")
40 server.Stop()
)BASIC";

    std::string source(kSrc);
    il::frontends::basic::BasicCompilerInput input{source, "wss_server_binding.bas"};
    auto result = il::frontends::basic::compileBasic(input, opts, sm);
    ASSERT_TRUE(result.succeeded());
    EXPECT_TRUE(hasExtern(result.module, "Zanna.Network.WssServer.Broadcast"));
    EXPECT_TRUE(hasExtern(result.module, "Zanna.Network.WssServer.Stop"));
    EXPECT_EQ(countCallsTo(result.module, "Zanna.Network.WssServer.Broadcast"), 1);
}

int main(int argc, char **argv) {
    zanna_test::init(&argc, argv);
    return zanna_test::run_all_tests();
}
