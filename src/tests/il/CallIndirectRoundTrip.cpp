//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/il/CallIndirectRoundTrip.cpp
// Purpose: Ensure call.indirect parses, prints, and executes via VM for a simple case.
// Key invariants: Indirect calls resolve by global function name; no args required.
// Ownership/Lifetime: To be documented.
// Links: docs/architecture.md
//
//===----------------------------------------------------------------------===//

#include "il/core/Module.hpp"
#include "il/io/Parser.hpp"
#include "il/io/Serializer.hpp"
#include "tests/TestHarness.hpp"
#include "viper/vm/VM.hpp"
#include <sstream>
#include <string>

using namespace il::core;

TEST(IL, CallIndirectRoundTrip)
{
    // Textual IL with a zero-arg callee and an indirect call.
    const char *text = R"(il 0.2.0
func @callee() -> i64 {
entry:
  ret 7
}
func @main() -> i64 {
entry:
  %t0 = call.indirect @callee
  ret %t0
}
)";

    il::core::Module m;
    std::istringstream iss{text};
    auto parsed = il::io::Parser::parse(iss, m);
    ASSERT_TRUE(parsed && "parse should succeed");

    // Round-trip: serialize and parse again to ensure stability.
    std::string roundTripped = il::io::Serializer::toString(m);
    il::core::Module m2;
    std::istringstream iss2{roundTripped};
    auto parsed2 = il::io::Parser::parse(iss2, m2);
    ASSERT_TRUE(parsed2 && "round-trip parse should succeed");

    // Execute via public Runner façade; expect 7 as exit code
    il::vm::Runner r(m2, {});
    auto exit = r.run();
    ASSERT_EQ(exit, 7);
}

int main(int argc, char **argv)
{
    viper_test::init(&argc, argv);
    return viper_test::run_all_tests();
}
