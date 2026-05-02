//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: codegen/aarch64/passes/RegAllocPass.cpp
// Purpose: Register allocation pass for the AArch64 modular pipeline.
//
// Runs the linear-scan register allocator on every MIR function produced by
// LoweringPass.  After this pass, all virtual registers are replaced with
// physical AArch64 registers and spill/reload code has been inserted.
//
//===----------------------------------------------------------------------===//

#include "codegen/aarch64/passes/RegAllocPass.hpp"

#include "codegen/aarch64/Coalescer.hpp"
#include "codegen/aarch64/RegAllocLinear.hpp"

#include <algorithm>
#include <atomic>
#include <exception>
#include <mutex>
#include <thread>
#include <vector>

namespace viper::codegen::aarch64::passes {

bool RegAllocPass::run(AArch64Module &module, Diagnostics &diags) {
    if (!module.ti) {
        diags.error("RegAllocPass: ti must be non-null");
        return false;
    }

    std::string firstError;
    std::mutex errorMutex;

    auto allocateOne = [&](std::size_t index) {
        auto &fn = module.mir[index];
        try {
            // Coalesce MovRR/FMovRR between virtual registers before register
            // allocation to reduce register pressure and eliminate redundant copies.
            coalesce(fn);
            [[maybe_unused]] auto result = allocate(fn, *module.ti);
        } catch (const std::exception &ex) {
            std::lock_guard<std::mutex> lock(errorMutex);
            if (firstError.empty())
                firstError = "AArch64 register allocation failed for function '" + fn.name +
                             "': " + ex.what();
        }
    };

    const std::size_t functionCount = module.mir.size();
    const std::size_t workerCount =
        std::min(functionCount,
                 std::max<std::size_t>(
                     1, static_cast<std::size_t>(std::thread::hardware_concurrency())));
    if (workerCount <= 1) {
        for (std::size_t i = 0; i < functionCount; ++i)
            allocateOne(i);
    } else {
        std::atomic_size_t nextIndex{0};
        std::vector<std::thread> workers;
        workers.reserve(workerCount);
        for (std::size_t worker = 0; worker < workerCount; ++worker) {
            workers.emplace_back([&]() {
                for (;;) {
                    const std::size_t index =
                        nextIndex.fetch_add(1, std::memory_order_relaxed);
                    if (index >= functionCount)
                        break;
                    allocateOne(index);
                }
            });
        }
        for (auto &worker : workers)
            worker.join();
    }

    if (!firstError.empty()) {
        diags.error("V-CG-AARCH64-REGALLOC", firstError);
        return false;
    }

    return true;
}

} // namespace viper::codegen::aarch64::passes
