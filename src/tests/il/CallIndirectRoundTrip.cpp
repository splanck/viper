// File: tests/il/CallIndirectRoundTrip.cpp
// Purpose: Ensure call.indirect parses, prints, and executes via VM for a simple case.
// Key invariants: Indirect calls resolve by global function name; no args required.

#include "il/core/Module.hpp"
#include "il/io/Parser.hpp"
#include "il/io/Serializer.hpp"
#include "viper/vm/VM.hpp"
#include <cassert>
#include <sstream>
#include <string>

using namespace il::core;

int main()
{
    // Textual IL with a zero-arg callee and an indirect call.
    const char *text = R"(il 0.1.2
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
    assert(parsed && "parse should succeed");

    // Round-trip: serialize and parse again to ensure stability.
    std::string roundTripped = il::io::Serializer::toString(m);
    il::core::Module m2;
    std::istringstream iss2{roundTripped};
    auto parsed2 = il::io::Parser::parse(iss2, m2);
    assert(parsed2 && "round-trip parse should succeed");

    // Execute via public Runner façade; expect 7 as exit code
    il::vm::Runner r(m2, {});
    auto exit = r.run();
    assert(exit == 7);
    return 0;
}
