// File: tests/unit/test_vm_entry_arg_mismatch.cpp
// Purpose: Ensure VM traps when entry frame argument counts do not match block parameters.
// Key invariants: Calling a function with mismatched argument count emits InvalidOperation trap.
// Ownership: Builds synthetic module and executes VM in forked child to capture diagnostics.
// Links: docs/codemap.md

#include "VMTestHook.hpp"
#include "il/build/IRBuilder.hpp"
#include "vm/VM.hpp"

#include <cassert>
#include <optional>
#include <string>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

using namespace il::core;

namespace
{
std::string captureTrap(Module &module,
                        const Function &fn,
                        const std::vector<il::vm::Slot> &args)
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
        il::vm::VMTestHook::run(vm, fn, args);
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
    return std::string(buffer);
}
} // namespace

int main()
{
    Module module;
    il::build::IRBuilder builder(module);

    builder.startFunction("too_many_args", Type(Type::Kind::Void), {});
    auto &tooManyEntry = builder.createBlock(module.functions.back(), "entry");
    builder.setInsertPoint(tooManyEntry);
    builder.emitRet(std::optional<Value>{}, {1, 1, 1});

    builder.startFunction("too_few_args", Type(Type::Kind::Void), {});
    auto &tooFewEntry = builder.createBlock(
        module.functions.back(),
        "entry",
        std::vector<Param>{{"p0", Type(Type::Kind::I64), 0}});
    builder.setInsertPoint(tooFewEntry);
    builder.emitRet(std::optional<Value>{}, {1, 1, 1});

    auto &tooManyFn = module.functions.front();
    auto &tooFewFn = module.functions.back();

    il::vm::Slot slot{};
    slot.i64 = 42;

    const std::string extraDiag = captureTrap(module, tooManyFn, {slot});
    assert(extraDiag.find("Trap @too_many_args#0 line -1: InvalidOperation (code=0)") !=
           std::string::npos);
    assert(extraDiag.find("argument count mismatch") != std::string::npos);

    const std::string missingDiag = captureTrap(module, tooFewFn, {});
    assert(missingDiag.find("Trap @too_few_args#0 line -1: InvalidOperation (code=0)") !=
           std::string::npos);
    assert(missingDiag.find("argument count mismatch") != std::string::npos);

    return 0;
}
