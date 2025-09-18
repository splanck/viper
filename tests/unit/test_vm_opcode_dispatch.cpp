// File: tests/unit/test_vm_opcode_dispatch.cpp
// Purpose: Exercise VM interpreter handlers via a representative IL program.
// Key invariants: Program executes one opcode from each handler group and returns expected sum.
// Ownership: Test parses in-memory IL text and executes the VM in-process.
// Links: docs/il-spec.md

#include "il/core/Module.hpp"
#include "il/io/Parser.hpp"
#include "vm/VM.hpp"
#include <cassert>
#include <sstream>
#include <string>

int main()
{
    const char *il = R"(il 0.1
extern @rt_len(str) -> i64

global const str @g = "hello"

func @bump(i64 %x) -> i64 {
entry(%x0: i64):
  %plus = add %x0, 1
  ret %plus
}

func @main() -> i64 {
entry:
  %base = alloca 24
  %slot0 = gep %base, 0
  %slot1 = gep %base, 8
  %slot2 = gep %base, 16
  %p = addr_of @g
  store ptr, %slot0, %p
  store i64, %slot1, 4
  %load = load i64, %slot1
  %add = add %load, 5
  %sub = sub %add, 1
  %mul = mul %sub, 2
  %xor = xor %mul, 3
  %shl = shl %xor, 1
  %as_float = sitofp %shl
  %fadd = fadd %as_float, 2.5
  %fmul = fmul %fadd, 1.0
  %fsub = fsub %fmul, 0.5
  %fdiv = fdiv %fsub, 1.0
  %back = fptosi %fdiv
  %eq = icmp_eq %back, %shl
  %gt = scmp_gt %shl, %back
  cbr %gt, high(%back), low(%back)
high(%hv: i64):
  br merge(%hv, 0)
low(%lv: i64):
  %z = zext1 %eq
  br merge(%lv, %z)
merge(%val: i64, %flag: i64):
  %trunc = trunc1 %val
  %call = call @bump(%val)
  %fcmp = fcmp_gt %fsub, %as_float
  %str = const_str @g
  %len = call @rt_len(%str)
  %ptr = load ptr, %slot0
  %ptr_bits = load i64, %slot0
  %ptr_nonzero = scmp_gt %ptr_bits, 0
  %sum0 = add %call, %len
  %sum1 = add %sum0, %fcmp
  %sum2 = add %sum1, %ptr_nonzero
  %sum = add %sum2, %trunc
  store i64, %slot2, %sum
  %out = load i64, %slot2
  ret %out
}
)";

    il::core::Module m;
    std::istringstream is(il);
    std::ostringstream err;
    bool ok = il::io::Parser::parse(is, m, err);
    assert(ok && err.str().empty());

    il::vm::VM vm(m);
    int64_t rv = vm.run();
    assert(rv == 48);
    return 0;
}
