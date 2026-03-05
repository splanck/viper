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

#include "common/ProcessIsolation.hpp"
#include "il/runtime/signatures/Registry.hpp"
#include "vm/RuntimeBridge.hpp"
#include "vm/VM.hpp"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>

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

int main(int argc, char *argv[])
{
    if (viper::tests::dispatchChild(argc, argv))
        return 0;

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
            std::fprintf(stderr, "VM_ExternRegistryTests: case1 got unexpected result\n");
        }
        assert(res.i64 == 42);

        bool removed = il::vm::RuntimeBridge::unregisterExtern("times2");
        assert(removed);
    }

    // Case 2: Unknown extern -> trap (capture in child).
    {
        auto result = viper::tests::runIsolated(
            []()
            {
                il::vm::RuntimeCallContext ctx{};
                il::vm::Slot arg{};
                arg.i64 = 7;
                (void)il::vm::RuntimeBridge::call(ctx, "times2", {arg}, {}, "", "");
            });
        assert(result.trapped());
        assert(result.stderrText.find("unknown runtime helper 'times2'") != std::string::npos);
    }

    // Case 3: Signature mismatch -> trap (capture in child).
    {
        il::vm::ExternDesc ext;
        ext.name = "times2";
        ext.signature = make_signature("times2", {SigParam::Kind::I64}, {SigParam::Kind::I64});
        ext.fn = reinterpret_cast<void *>(&times2_handler);
        il::vm::RuntimeBridge::registerExtern(ext);

        auto result = viper::tests::runIsolated(
            []()
            {
                il::vm::RuntimeCallContext ctx{};
                // Provide wrong number of args (0 instead of 1)
                (void)il::vm::RuntimeBridge::call(ctx, "times2", {}, {}, "", "");
            });
        assert(result.trapped());
        assert(result.stderrText.find("expected 1 argument(s), got 0") != std::string::npos);

        (void)il::vm::RuntimeBridge::unregisterExtern("times2");
    }

    return 0;
}
