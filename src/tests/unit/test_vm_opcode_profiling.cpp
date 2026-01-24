//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/test_vm_opcode_profiling.cpp
// Purpose: Profile VM opcode execution to identify hot opcodes for optimization.
// Key invariants: Profiling data helps prioritize VM optimizations.
// Ownership/Lifetime: Test parses in-memory IL and profiles execution.
// Links: docs/vm-performance.md
//
//===----------------------------------------------------------------------===//

#include "il/api/expected_api.hpp"
#include "il/core/Module.hpp"
#include "il/core/Opcode.hpp"
#include "vm/VM.hpp"
#include <algorithm>
#include <cassert>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#if VIPER_VM_OPCOUNTS

/// @brief Get opcode mnemonic name.
static const char *opcodeName(il::core::Opcode op)
{
    switch (op)
    {
#define IL_OPCODE(NAME, MNEMONIC, ...)                                                             \
    case il::core::Opcode::NAME:                                                                   \
        return MNEMONIC;
#include "il/core/Opcode.def"
        default:
            return "unknown";
    }
}

/// @brief Profile a realistic workload with loops and arithmetic.
static void profileArithmeticLoop()
{
    const char *il = R"(il 0.1
func @main() -> i64 {
entry:
  %sum = alloca 8
  store i64, %sum, 0
  %i = alloca 8
  store i64, %i, 0
  br loop_check()
loop_check:
  %iv = load i64, %i
  %cmp = scmp_lt %iv, 1000
  cbr %cmp, loop_body(%iv), loop_exit()
loop_body(%biv: i64):
  %sv = load i64, %sum
  %new_sum = iadd.ovf %sv, %biv
  store i64, %sum, %new_sum
  %next = iadd.ovf %biv, 1
  store i64, %i, %next
  br loop_check()
loop_exit:
  %result = load i64, %sum
  ret %result
}
)";

    il::core::Module m;
    std::istringstream is(il);
    auto parse = il::api::v2::parse_text_expected(is, m);
    assert(parse);

    il::vm::VM vm(m);
    vm.resetOpcodeCounts();
    int64_t result = vm.run();

    // Expected result: sum of 0..999 = 999*1000/2 = 499500
    assert(result == 499500);

    std::cout << "\n=== Arithmetic Loop Profile (1000 iterations) ===\n";
    auto top = vm.topOpcodes(15);
    for (const auto &[idx, count] : top)
    {
        std::cout << "  " << opcodeName(static_cast<il::core::Opcode>(idx)) << ": " << count
                  << "\n";
    }
}

/// @brief Profile a workload with function calls.
static void profileFunctionCalls()
{
    const char *il = R"(il 0.1
func @helper(i64 %x) -> i64 {
entry(%x0: i64):
  %r = iadd.ovf %x0, 1
  ret %r
}

func @main() -> i64 {
entry:
  %sum = alloca 8
  store i64, %sum, 0
  %i = alloca 8
  store i64, %i, 0
  br loop()
loop:
  %iv = load i64, %i
  %cmp = scmp_lt %iv, 100
  cbr %cmp, body(%iv), done()
body(%biv: i64):
  %called = call @helper(%biv)
  %sv = load i64, %sum
  %ns = iadd.ovf %sv, %called
  store i64, %sum, %ns
  %next = iadd.ovf %biv, 1
  store i64, %i, %next
  br loop()
done:
  %result = load i64, %sum
  ret %result
}
)";

    il::core::Module m;
    std::istringstream is(il);
    auto parse = il::api::v2::parse_text_expected(is, m);
    assert(parse);

    il::vm::VM vm(m);
    vm.resetOpcodeCounts();
    int64_t result = vm.run();

    // Expected: sum of (i+1) for i=0..99 = sum of 1..100 = 100*101/2 = 5050
    assert(result == 5050);

    std::cout << "\n=== Function Call Profile (100 calls) ===\n";
    auto top = vm.topOpcodes(15);
    for (const auto &[idx, count] : top)
    {
        std::cout << "  " << opcodeName(static_cast<il::core::Opcode>(idx)) << ": " << count
                  << "\n";
    }
}

/// @brief Profile memory-intensive workload.
static void profileMemoryOps()
{
    const char *il = R"(il 0.1
func @main() -> i64 {
entry:
  %arr = alloca 800
  %i = alloca 8
  store i64, %i, 0
  br fill_loop()
fill_loop:
  %fi = load i64, %i
  %fcmp = scmp_lt %fi, 100
  cbr %fcmp, fill_body(%fi), sum_init()
fill_body(%fbi: i64):
  %offset = imul.ovf %fbi, 8
  %ptr = gep %arr, %offset
  store i64, %ptr, %fbi
  %fnext = iadd.ovf %fbi, 1
  store i64, %i, %fnext
  br fill_loop()
sum_init:
  %sum = alloca 8
  store i64, %sum, 0
  store i64, %i, 0
  br sum_loop()
sum_loop:
  %si = load i64, %i
  %scmp = scmp_lt %si, 100
  cbr %scmp, sum_body(%si), done()
sum_body(%sbi: i64):
  %soff = imul.ovf %sbi, 8
  %sptr = gep %arr, %soff
  %val = load i64, %sptr
  %sv = load i64, %sum
  %ns = iadd.ovf %sv, %val
  store i64, %sum, %ns
  %snext = iadd.ovf %sbi, 1
  store i64, %i, %snext
  br sum_loop()
done:
  %result = load i64, %sum
  ret %result
}
)";

    il::core::Module m;
    std::istringstream is(il);
    auto parse = il::api::v2::parse_text_expected(is, m);
    assert(parse);

    il::vm::VM vm(m);
    vm.resetOpcodeCounts();
    int64_t result = vm.run();

    // Expected: sum of 0..99 = 99*100/2 = 4950
    assert(result == 4950);

    std::cout << "\n=== Memory Operations Profile (200 load/store ops) ===\n";
    auto top = vm.topOpcodes(15);
    for (const auto &[idx, count] : top)
    {
        std::cout << "  " << opcodeName(static_cast<il::core::Opcode>(idx)) << ": " << count
                  << "\n";
    }
}

int main()
{
    std::cout << "VM Opcode Profiling Report\n";
    std::cout << "==========================\n";

    profileArithmeticLoop();
    profileFunctionCalls();
    profileMemoryOps();

    std::cout << "\n=== Profiling Complete ===\n";
    std::cout << "Hot opcodes identified for optimization priority.\n";

    return 0;
}

#else

int main()
{
    // When VIPER_VM_OPCOUNTS is disabled, just return success
    return 0;
}

#endif
