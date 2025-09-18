// File: tests/unit/test_vm_addr_of.cpp
// Purpose: Verify VM addr_of instruction returns pointer to global string.
// Key invariants: Returned pointer's data matches global initializer.
// Ownership: Test constructs IL module and executes VM.
// Links: docs/il-reference.md

#include "il/io/Parser.hpp"
#include "il/core/Module.hpp"
#include "rt_internal.h"
#include "vm/VM.hpp"
#include <cassert>
#include <sstream>
#include <string>

int main()
{
    const char *il = "il 0.1\n"
                     "global const str @g = \"hi\"\n\n"
                     "func @main() -> i64 {\n"
                     "entry:\n"
                     "  %p = addr_of @g\n"
                     "  %a = alloca 8\n"
                     "  store ptr, %a, %p\n"
                     "  %v = load i64, %a\n"
                     "  ret %v\n"
                     "}\n";

    il::core::Module m;
    std::istringstream is(il);
    std::ostringstream err;
    bool ok = il::io::Parser::parse(is, m, err);
    assert(ok && err.str().empty());

    il::vm::VM vm(m);
    int64_t rv = vm.run();
    rt_string s = reinterpret_cast<rt_string>(static_cast<uintptr_t>(rv));
    assert(s->data == m.globals.front().init.c_str());
    return 0;
}
