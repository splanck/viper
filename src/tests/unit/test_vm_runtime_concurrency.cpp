//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/test_vm_runtime_concurrency.cpp
// Purpose: Verify runtime trap metadata remains isolated per VM under concurrency. 
// Key invariants: Concurrent runtime calls must preserve distinct trap context.
// Ownership/Lifetime: Constructs independent modules per thread and overrides vm_trap.
// Links: docs/codemap.md
//
//===----------------------------------------------------------------------===//

#include "il/build/IRBuilder.hpp"
#include "support/source_location.hpp"
#include "vm/RuntimeBridge.hpp"
#include "vm/VM.hpp"
#include <array>
#include <barrier>
#include <cassert>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace
{
std::barrier<> trapBarrier(2);
std::mutex trapMutex;
std::vector<std::string> trapMessages;
} // namespace

extern "C" void vm_trap(const char *msg)
{
    trapBarrier.arrive_and_wait();
    const auto *ctx = il::vm::RuntimeBridge::activeContext();
    std::ostringstream os;
    os << (msg ? msg : "trap");
    if (ctx)
    {
        os << ' ' << ctx->function << ": " << ctx->block;
        if (ctx->loc.isValid())
            os << " (" << ctx->loc.file_id << ':' << ctx->loc.line << ':' << ctx->loc.column << ')';
    }
    std::lock_guard<std::mutex> lock(trapMutex);
    trapMessages.push_back(os.str());
}

int main()
{
    trapMessages.clear();

    const std::array<std::string, 2> globals = {"g_msg_a", "g_msg_b"};
    const std::array<std::string, 2> messages = {"trap-A", "trap-B"};
    const std::array<std::string, 2> blocks = {"blockA", "blockB"};
    const std::array<il::support::SourceLoc, 2> locs = {
        il::support::SourceLoc{1, 10, 4},
        il::support::SourceLoc{2, 20, 8},
    };

    auto worker = [&](int idx)
    {
        il::core::Module module;
        il::build::IRBuilder builder(module);
        builder.addExtern("rt_trap",
                          il::core::Type(il::core::Type::Kind::Void),
                          {il::core::Type(il::core::Type::Kind::Str)});
        builder.addGlobalStr(globals[idx], messages[idx]);
        auto &fn = builder.startFunction("main", il::core::Type(il::core::Type::Kind::I64), {});
        auto &bb = builder.addBlock(fn, blocks[idx]);
        builder.setInsertPoint(bb);
        auto strVal = builder.emitConstStr(globals[idx], locs[idx]);
        builder.emitCall("rt_trap", {strVal}, std::nullopt, locs[idx]);
        std::optional<il::core::Value> ret = il::core::Value::constInt(0);
        builder.emitRet(ret, locs[idx]);

        il::vm::VM vm(module);
        vm.run();
    };

    std::thread t0(worker, 0);
    std::thread t1(worker, 1);
    t0.join();
    t1.join();

    assert(trapMessages.size() == 2);
    bool seenA = false;
    bool seenB = false;
    for (const auto &entry : trapMessages)
    {
        if (entry.find("trap-A") != std::string::npos)
            seenA = entry.find("blockA") != std::string::npos &&
                    entry.find("(1:10:4)") != std::string::npos;
        if (entry.find("trap-B") != std::string::npos)
            seenB = entry.find("blockB") != std::string::npos &&
                    entry.find("(2:20:8)") != std::string::npos;
    }
    assert(seenA && seenB);
    return 0;
}
