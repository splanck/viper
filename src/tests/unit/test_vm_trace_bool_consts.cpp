//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/test_vm_trace_bool_consts.cpp
// Purpose: Ensure VM IL traces render boolean constants using textual literals. 
// Key invariants: Trace output spells const.i1 operands as "true"/"false" to
// Ownership/Lifetime: Test constructs IL in-memory and executes the VM with tracing
// Links: docs/il-guide.md#reference
//
//===----------------------------------------------------------------------===//

#include "il/api/expected_api.hpp"
#include "il/core/Module.hpp"
#include "viper/vm/debug/Debug.hpp"
#include "vm/VM.hpp"

#include <cassert>
#include <iostream>
#include <sstream>
#include <string>

int main()
{
    const char *src = R"(il 0.1
func @main() -> i64 {
entry:
  %slot = alloca 1
  store i1, %slot, false
  %val = load i1, %slot
  cbr true, then(%val), other(%val)
then(%flag:i1):
  %ext_then = zext1 %flag
  ret %ext_then
other(%flag:i1):
  %ext_else = zext1 %flag
  ret %ext_else
}
)";

    std::istringstream in(src);
    il::core::Module module;
    auto parsed = il::api::v2::parse_text_expected(in, module);
    assert(parsed);

    il::vm::TraceConfig traceCfg;
    traceCfg.mode = il::vm::TraceConfig::IL;

    std::ostringstream captured;
    auto *oldErr = std::cerr.rdbuf(captured.rdbuf());

    il::vm::VM vm(module, traceCfg);
    const auto result = vm.run();

    std::cerr.rdbuf(oldErr);

    assert(result == 0);

    const std::string trace = captured.str();
    assert(trace.find("false") != std::string::npos);
    assert(trace.find("true") != std::string::npos);

    return 0;
}
