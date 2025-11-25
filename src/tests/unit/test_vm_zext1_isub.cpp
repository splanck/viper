//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/test_vm_zext1_isub.cpp
// Purpose: Ensure the VM handles boolean materialisation via zext1 and isub.
// Key invariants: Zero-extension results are canonical 0/1 and subtraction yields -1/0 without
// Ownership/Lifetime: Parses IL text at runtime and executes it through the VM interpreter.
// Links: docs/il-guide.md#reference
//
//===----------------------------------------------------------------------===//

#include "il/api/expected_api.hpp"
#include "il/core/Module.hpp"
#include "vm/VM.hpp"

#include <cassert>
#include <sstream>
#include <string>

int main()
{
    const char *il = R"(il 0.1

func @materialize(i1 %flag) -> i64 {
entry(%flag0: i1):
  %z = zext1 %flag0
  %neg = sub 0, %z
  ret %neg
}

func @main() -> i64 {
entry:
  %true_flag = icmp_eq 1, 1
  %false_flag = icmp_eq 0, 1
  %neg_true = call @materialize(%true_flag)
  %neg_false = call @materialize(%false_flag)
  %true_ok = icmp_eq %neg_true, -1
  %false_ok = icmp_eq %neg_false, 0
  %true_i64 = zext1 %true_ok
  %false_i64 = zext1 %false_ok
  %sum = add %true_i64, %false_i64
  ret %sum
}
)";

    il::core::Module m;
    std::istringstream is(il);
    auto parse = il::api::v2::parse_text_expected(is, m);
    assert(parse);

    il::vm::VM vm(m);
    int64_t rv = vm.run();
    assert(rv == 2);
    return 0;
}
