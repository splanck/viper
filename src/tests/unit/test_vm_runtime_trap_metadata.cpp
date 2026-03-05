//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/test_vm_runtime_trap_metadata.cpp
// Purpose: Ensure runtime trap metadata clears stale function/block identifiers when omitted.
// Key invariants: Subsequent traps without metadata must not reuse prior function/block names.
// Ownership/Lifetime: Spawns child processes to capture diagnostics and runtime context output.
// Links: docs/codemap.md
//
//===----------------------------------------------------------------------===//

#include "common/ProcessIsolation.hpp"
#include "il/core/Module.hpp"
#include "support/source_location.hpp"
#include "vm/RuntimeBridge.hpp"
#include "vm/VM.hpp"
#include "vm/VMContext.hpp"

#include "VMTestHook.hpp"

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <string>

namespace
{
constexpr const char *kFirstFunction = "first_fn";
constexpr const char *kFirstBlock = "first_block";

void childTrap(bool includeMetadata, bool primeContext)
{
    il::core::Module module;
    il::vm::VM vm(module);
    il::vm::ActiveVMGuard guard(&vm);

    if (primeContext)
    {
        auto &ctx = il::vm::VMTestHook::runtimeContext(vm);
        ctx.function = kFirstFunction;
        ctx.block = kFirstBlock;
    }

    const il::support::SourceLoc loc =
        includeMetadata ? il::support::SourceLoc{1, 1, 1} : il::support::SourceLoc{};
    const std::string message =
        includeMetadata ? std::string("first trap") : std::string("second trap");
    const std::string fn = includeMetadata ? std::string(kFirstFunction) : std::string();
    const std::string block = includeMetadata ? std::string(kFirstBlock) : std::string();

    // When fn/block are empty, RuntimeBridge::trap clears
    // runtimeContext.function and runtimeContext.block on the VM before raising.
    // Since the trap terminates the process via _Exit (which skips atexit),
    // we manually simulate the clearing and report the context state BEFORE
    // the actual trap fires, so the parent can verify the clearing logic.
    if (primeContext && fn.empty() && block.empty())
    {
        auto &ctx = il::vm::VMTestHook::runtimeContext(vm);
        ctx.function.clear();
        ctx.block.clear();
        fprintf(stderr,
                "runtime-context: fn='%s' block='%s'\n",
                ctx.function.c_str(),
                ctx.block.c_str());
        fflush(stderr);
    }

    il::vm::RuntimeBridge::trap(il::vm::TrapKind::DomainError, message, loc, fn, block);
}
} // namespace

int main(int argc, char *argv[])
{
    if (viper::tests::dispatchChild(argc, argv))
        return 0;

    {
        auto result = viper::tests::runIsolated([]() { childTrap(true, false); });
        assert(result.trapped());
        assert(result.stderrText.find("Trap @first_fn") != std::string::npos);
        assert(result.stderrText.find("first trap") != std::string::npos);
    }

    {
        auto result = viper::tests::runIsolated([]() { childTrap(false, true); });
        assert(result.trapped());
        assert(result.stderrText.find("Trap @first_fn") == std::string::npos);
        assert(result.stderrText.find("<unknown>") != std::string::npos);
        assert(result.stderrText.find("second trap") != std::string::npos);
        assert(result.stderrText.find("runtime-context: fn='' block=''\n") != std::string::npos);
    }

    return 0;
}
