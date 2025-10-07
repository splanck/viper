// File: tests/vm/BranchArgMismatchTests.cpp
// Purpose: Ensure the VM traps when a branch supplies the wrong number of arguments.
// Key invariants: Branch argument count mismatches produce InvalidOperation traps mentioning the callee block.
// Ownership/Lifetime: Constructs an in-memory module executed in a subprocess to capture diagnostics.
// Links: docs/il-guide.md#reference

#include "il/build/IRBuilder.hpp"
#include "vm/VM.hpp"

#include <cassert>
#include <optional>
#include <string>
#include <sys/wait.h>
#include <unistd.h>

using namespace il::core;

namespace
{
std::string captureTrap(Module &module)
{
    int fds[2];
    assert(pipe(fds) == 0);
    pid_t pid = fork();
    assert(pid >= 0);
    if (pid == 0)
    {
        close(fds[0]);
        dup2(fds[1], 2);
        il::vm::VM vm(module);
        vm.run();
        _exit(0);
    }
    close(fds[1]);
    char buffer[512];
    ssize_t n = read(fds[0], buffer, sizeof(buffer) - 1);
    if (n < 0)
        n = 0;
    buffer[n] = '\0';
    close(fds[0]);
    int status = 0;
    waitpid(pid, &status, 0);
    assert(WIFEXITED(status) && WEXITSTATUS(status) == 1);
    return std::string(buffer);
}

} // namespace

int main()
{
    Module module;
    il::build::IRBuilder builder(module);
    auto &fn = builder.startFunction("main", Type(Type::Kind::I64), {});
    builder.addBlock(fn, "entry");
    builder.createBlock(fn,
                        "target",
                        {Param{"x", Type(Type::Kind::I64), 0}});
    auto &entry = fn.blocks.front();
    auto &target = fn.blocks.back();
    assert(target.params.size() == 1);

    builder.setInsertPoint(entry);
    builder.br(target, {Value::constInt(42)});
    auto &brInstr = entry.instructions.back();
    brInstr.loc = {1, 1, 1};
    brInstr.brArgs[0].clear();

    builder.setInsertPoint(target);
    builder.emitRet(std::optional<Value>(Value::constInt(0)), {1, 2, 1});

    const std::string diag = captureTrap(module);
    const bool hasMessage = diag.find("branch argument count mismatch") != std::string::npos;
    assert(hasMessage && "expected branch argument mismatch diagnostic");

    const bool mentionsTarget = diag.find("'target'") != std::string::npos;
    assert(mentionsTarget && "expected diagnostic to mention callee block label");

    const bool mentionsCounts = diag.find("expected 1, got 0") != std::string::npos;
    assert(mentionsCounts && "expected diagnostic to report argument counts");

    return 0;
}
