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
#include <string_view>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

using namespace il::core;

namespace
{
std::string captureTrap(Module &module, const Function &fn, const std::vector<il::vm::Slot> &args)
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

bool trapHeaderMatches(const std::string &diag, std::string_view function, std::string_view kind)
{
    std::string_view view(diag);
    if (const auto newline = view.find('\n'); newline != std::string_view::npos)
        view = view.substr(0, newline);

    constexpr std::string_view kPrefix = "Trap @";
    if (!view.starts_with(kPrefix))
        return false;
    view.remove_prefix(kPrefix.size());

    if (view.substr(0, function.size()) != function)
        return false;
    view.remove_prefix(function.size());

    if (view.empty() || view.front() != '#')
        return false;

    const auto colonPos = view.find(':');
    if (colonPos == std::string_view::npos || colonPos + 2 > view.size())
        return false;

    const auto headerSuffix = view.substr(0, colonPos);
    if (headerSuffix.find(" line ") == std::string_view::npos)
        return false;
    if (view[colonPos + 1] != ' ')
        return false;

    view.remove_prefix(colonPos + 2);
    if (view.substr(0, kind.size()) != kind)
        return false;
    if (view.size() <= kind.size() || view[kind.size()] != ' ')
        return false;
    if (view.find("(code=") == std::string_view::npos)
        return false;

    return true;
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
        module.functions.back(), "entry", std::vector<Param>{{"p0", Type(Type::Kind::I64), 0}});
    builder.setInsertPoint(tooFewEntry);
    builder.emitRet(std::optional<Value>{}, {1, 1, 1});

    auto &tooManyFn = module.functions.front();
    auto &tooFewFn = module.functions.back();

    il::vm::Slot slot{};
    slot.i64 = 42;

    const std::string extraDiag = captureTrap(module, tooManyFn, {slot});
    assert(trapHeaderMatches(extraDiag, "too_many_args", "InvalidOperation"));
    assert(extraDiag.find("argument count mismatch") != std::string::npos);

    const std::string missingDiag = captureTrap(module, tooFewFn, {});
    assert(trapHeaderMatches(missingDiag, "too_few_args", "InvalidOperation"));
    assert(missingDiag.find("argument count mismatch") != std::string::npos);

    return 0;
}
