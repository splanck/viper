//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/test_vm_trap_loc.cpp
// Purpose: Verify VM trap messages include instruction source locations.
// Key invariants: Trap output must reference function, block, and location.
// Ownership/Lifetime: Test constructs IL module and executes VM.
// Links: docs/codemap.md
//
//===----------------------------------------------------------------------===//

#include "il/build/IRBuilder.hpp"
#include "vm/VM.hpp"
#include <cassert>
#include <string>
#include "tests/common/WaitCompat.hpp"
#include "tests/common/PosixCompat.h"

int main()
{
    il::core::Module m;
    il::build::IRBuilder b(m);
    auto &fn = b.startFunction("main", il::core::Type(il::core::Type::Kind::I64), {});
    auto &bb = b.addBlock(fn, "entry");
    b.setInsertPoint(bb);
    il::core::Instr in;
    in.op = il::core::Opcode::Trap;
    in.type = il::core::Type(il::core::Type::Kind::Void);
    in.loc = {1, 1, 1};
    bb.instructions.push_back(in);
    int fds[2];
    assert(pipe(fds) == 0);
    pid_t pid = fork();
    assert(pid >= 0);
    if (pid == 0)
    {
        close(fds[0]);
        dup2(fds[1], 2);
        il::vm::VM vm(m);
        vm.run();
        _exit(0);
    }
    close(fds[1]);
    char buf[256];
    ssize_t n = read(fds[0], buf, sizeof(buf) - 1);
    if (n > 0)
        buf[n] = '\0';
    else
        buf[0] = '\0';
    int status = 0;
    waitpid(pid, &status, 0);
    std::string out(buf);
    // Format: "Trap @function:block#ip line N: Kind (code=C)"
    bool ok = out.find("Trap @main:entry#0 line 1: DomainError (code=0)") != std::string::npos;
    assert(ok);
    return 0;
}
