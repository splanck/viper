// File: tests/unit/test_vm_switch_block_label.cpp
// Purpose: Verify switch traps record the executing block label in diagnostics.
// Key invariants: handleSwitchI32 must attribute out-of-range traps to the active block.
// Ownership: Constructs a synthetic module and triggers a trap in a forked child process.
// Links: docs/codemap.md

#include "VMTestHook.hpp"
#include "il/build/IRBuilder.hpp"
#include "vm/OpHandlers_Control.hpp"
#include "vm/VMContext.hpp"

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <sys/wait.h>
#include <unistd.h>

using namespace il::core;

namespace
{
constexpr const char *kFunctionName = "main";
constexpr const char *kTrapBlockLabel = "trap";

Instr makeSwitchInstr()
{
    Instr instr;
    instr.op = Opcode::SwitchI32;
    instr.type = Type(Type::Kind::Void);
    instr.operands.push_back(Value::constInt(0));
    instr.loc = {1, 1, 1};
    return instr;
}

il::vm::VM *gTrapVm = nullptr;

void reportRuntimeContext()
{
    if (!gTrapVm)
        return;
    const auto &ctx = il::vm::VMTestHook::runtimeContext(*gTrapVm);
    std::fprintf(
        stderr, "runtime-context: fn='%s' block='%s'\n", ctx.function.c_str(), ctx.block.c_str());
}
} // namespace

int main()
{
    Module module;
    il::build::IRBuilder builder(module);

    Function &fn = builder.startFunction(kFunctionName, Type(Type::Kind::I64), {});
    BasicBlock &entry = builder.createBlock(fn, "entry");
    BasicBlock &trapBlock = builder.createBlock(fn, kTrapBlockLabel);

    builder.setInsertPoint(entry);
    builder.br(trapBlock);

    builder.setInsertPoint(trapBlock);
    trapBlock.instructions.push_back(makeSwitchInstr());
    trapBlock.terminated = true;

    int fds[2];
    assert(pipe(fds) == 0);
    pid_t pid = fork();
    assert(pid >= 0);
    if (pid == 0)
    {
        close(fds[0]);
        dup2(fds[1], 2);
        close(fds[1]);

        il::vm::VM vm(module);
        il::vm::ActiveVMGuard guard(&vm);
        gTrapVm = &vm;
        std::atexit(reportRuntimeContext);

        auto state = il::vm::VMTestHook::prepare(vm, fn);
        state.bb = &trapBlock;
        state.ip = 0;

        const Instr &switchInstr = trapBlock.instructions.front();
        il::vm::VMTestHook::setContext(vm, state.fr, state.bb, state.ip, switchInstr);
        il::vm::detail::control::handleSwitchI32(
            vm, state.fr, switchInstr, state.blocks, state.bb, state.ip);

        _exit(0); // Unreachable but placates compilers.
    }

    close(fds[1]);
    char buffer[512];
    ssize_t n = read(fds[0], buffer, sizeof(buffer) - 1);
    if (n < 0)
        n = 0;
    buffer[n] = '\0';
    const std::string diag(buffer);
    close(fds[0]);

    int status = 0;
    waitpid(pid, &status, 0);
    if (!WIFEXITED(status))
    {
        std::fprintf(
            stderr, "switch-block-label: child terminated abnormally (status=%d)\n", status);
        if (WIFSIGNALED(status))
        {
            std::fprintf(stderr, "switch-block-label: received signal=%d\n", WTERMSIG(status));
        }
        if (!diag.empty())
            std::fprintf(stderr, "switch-block-label: stderr: %s", diag.c_str());
        assert(false && "switch-block-label child did not exit normally");
    }
    assert(WEXITSTATUS(status) == 1);

    assert(diag.find("switch target out of range") != std::string::npos);
    assert(diag.find("runtime-context: fn='main' block='trap'\n") != std::string::npos);
    assert(diag.find("block='entry'") == std::string::npos);

    return 0;
}
