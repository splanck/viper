//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/VM_ExternRegistryTests.cpp
// Purpose: Test runtime extern registration, canonicalization, and error traps. 
// Key invariants: To be documented.
// Ownership/Lifetime: To be documented.
// Links: docs/architecture.md
//
//===----------------------------------------------------------------------===//

#include "il/runtime/signatures/Registry.hpp"
#include "vm/RuntimeBridge.hpp"
#include "vm/VM.hpp"

#include <cassert>
#include <cstring>
#include <string>
#include <sys/wait.h>
#include <unistd.h>

using il::runtime::signatures::make_signature;
using il::runtime::signatures::SigParam;

static int64_t times2(int64_t x)
{
    return x * 2;
}

static void times2_handler(void **args, void *result)
{
    const auto ptr = args ? reinterpret_cast<const int64_t *>(args[0]) : nullptr;
    const int64_t x = ptr ? *ptr : 0;
    const int64_t y = times2(x);
    if (result)
        *reinterpret_cast<int64_t *>(result) = y;
}

static std::string read_child_stderr_and_wait(pid_t pid, int fd)
{
    std::string out;
    char buf[512];
    for (;;)
    {
        ssize_t n = read(fd, buf, sizeof(buf));
        if (n > 0)
            out.append(buf, buf + n);
        else
            break;
    }
    int status = 0;
    (void)waitpid(pid, &status, 0);
    return out;
}

int main()
{
    // Case 1: Register extern and invoke successfully.
    {
        il::vm::ExternDesc ext;
        ext.name = "Times2"; // check canonicalization
        ext.signature = make_signature("times2", {SigParam::Kind::I64}, {SigParam::Kind::I64});
        ext.fn = reinterpret_cast<void *>(&times2_handler);
        il::vm::RuntimeBridge::registerExtern(ext);

        il::vm::RuntimeCallContext ctx{};
        il::vm::Slot arg{};
        arg.i64 = 21;
        il::vm::Slot res = il::vm::RuntimeBridge::call(ctx, "times2", {arg}, {}, "", "");
        // Debug: ensure we see failures clearly if any
        if (res.i64 != 42)
        {
            const char *msg = "VM_ExternRegistryTests: case1 got unexpected result\n";
            (void)!write(2, msg, strlen(msg));
        }
        assert(res.i64 == 42);

        bool removed = il::vm::RuntimeBridge::unregisterExtern("times2");
        assert(removed);
    }

    // Case 2: Unknown extern -> trap (capture in child).
    {
        int fds[2];
        assert(pipe(fds) == 0);
        pid_t pid = fork();
        assert(pid >= 0);
        if (pid == 0)
        {
            close(fds[0]);
            dup2(fds[1], 2);
            il::vm::RuntimeCallContext ctx{};
            il::vm::Slot arg{};
            arg.i64 = 7;
            (void)il::vm::RuntimeBridge::call(ctx, "times2", {arg}, {}, "", "");
            _exit(0);
        }
        close(fds[1]);
        std::string out = read_child_stderr_and_wait(pid, fds[0]);
        assert(out.find("unknown runtime helper 'times2'") != std::string::npos);
    }

    // Case 3: Signature mismatch -> trap (capture in child).
    {
        il::vm::ExternDesc ext;
        ext.name = "times2";
        ext.signature = make_signature("times2", {SigParam::Kind::I64}, {SigParam::Kind::I64});
        ext.fn = reinterpret_cast<void *>(&times2_handler);
        il::vm::RuntimeBridge::registerExtern(ext);

        int fds[2];
        assert(pipe(fds) == 0);
        pid_t pid = fork();
        assert(pid >= 0);
        if (pid == 0)
        {
            close(fds[0]);
            dup2(fds[1], 2);
            il::vm::RuntimeCallContext ctx{};
            // Provide wrong number of args (0 instead of 1)
            (void)il::vm::RuntimeBridge::call(ctx, "times2", {}, {}, "", "");
            _exit(0);
        }
        close(fds[1]);
        std::string out = read_child_stderr_and_wait(pid, fds[0]);
        assert(out.find("expected 1 argument(s), got 0") != std::string::npos);

        (void)il::vm::RuntimeBridge::unregisterExtern("times2");
    }

    return 0;
}
