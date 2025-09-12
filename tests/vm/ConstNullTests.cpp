// File: tests/vm/ConstNullTests.cpp
// Purpose: Verify const_null writes a null pointer to the destination register.
// Key invariants: execFunction returns Slot with nullptr for const_null result.
// Ownership/Lifetime: Test constructs IL module and VM instance.
// Links: docs/testing.md
#include <array>
#include <cassert>
#include <cstdint>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#define private public
#include "vm/VM.hpp"
#undef private
#include "il/io/Parser.hpp"

int main()
{
    const char *il = R"il(il 0.1

func @main() -> ptr {
entry:
  %p = const_null
  ret %p
}
)il";
    std::istringstream is(il);
    il::core::Module m;
    std::ostringstream err;
    bool ok = il::io::Parser::parse(is, m, err);
    assert(ok);
    il::vm::VM vm(m);
    il::vm::Slot res = vm.execFunction(m.functions[0], {});
    assert(res.ptr == nullptr);
    return 0;
}
