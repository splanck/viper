//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: codegen/x86_64/passes/PeepholePass.cpp
// Purpose: Implement the explicit post-RA peephole pass for the x86-64 pipeline.
// Key invariants:
//   - Runs after register allocation on physical-register MIR.
// Ownership/Lifetime:
//   - Stateless; mutates Module::mir in place via Peephole utilities.
// Links: codegen/x86_64/passes/PeepholePass.hpp,
//        codegen/x86_64/Peephole.hpp
//
//===----------------------------------------------------------------------===//

#include "codegen/x86_64/passes/PeepholePass.hpp"

#include "codegen/x86_64/Backend.hpp"
#include "codegen/x86_64/Peephole.hpp"

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdlib>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace viper::codegen::x64::passes {
namespace {

/// @brief Check whether the codegen-stats environment toggle is active.
/// @details Looks at @c VIPER_CODEGEN_STATS — any non-empty value other
///          than @c "0" enables verbose stats reporting. Used so noisy
///          diagnostic output stays off by default.
/// @return True when stats reporting should be emitted.
[[nodiscard]] bool codegenStatsEnabled() noexcept {
    if (const char *value = std::getenv("VIPER_CODEGEN_STATS"))
        return value[0] != '\0' && value[0] != '0';
    return false;
}

/// @brief Accumulator for MIR shape statistics emitted by the peephole pass.
struct MirStats {
    std::size_t functions = 0;
    std::size_t blocks = 0;
    std::size_t instructions = 0;
    std::size_t calls = 0;
    std::size_t branches = 0;
    std::size_t loads = 0;
    std::size_t stores = 0;
};

/// @brief Predicate: does @p opcode read memory into a register?
/// @details Used only by the optional stats counter — does not change codegen.
[[nodiscard]] bool isLoadOpcode(MOpcode opcode) noexcept {
    return opcode == MOpcode::MOVmr || opcode == MOpcode::MOVSDmr || opcode == MOpcode::MOVUPSmr ||
           opcode == MOpcode::POP;
}

/// @brief Predicate: does @p opcode write a register to memory?
[[nodiscard]] bool isStoreOpcode(MOpcode opcode) noexcept {
    return opcode == MOpcode::MOVrm || opcode == MOpcode::MOVSDrm || opcode == MOpcode::MOVUPSrm ||
           opcode == MOpcode::PUSH;
}

/// @brief Fold per-function MIR statistics into @p stats.
/// @details Used only when the codegen-stats env var is active so the
///          peephole pass can emit a summary at the end. Each accumulator
///          is per-thread when peephole runs in parallel; results are
///          merged under a mutex by the caller.
void accumulateStats(const MFunction &fn, MirStats &stats) {
    ++stats.functions;
    stats.blocks += fn.blocks.size();
    for (const auto &block : fn.blocks) {
        stats.instructions += block.instructions.size();
        for (const auto &instr : block.instructions) {
            if (instr.opcode == MOpcode::CALL)
                ++stats.calls;
            if (instr.opcode == MOpcode::JMP || instr.opcode == MOpcode::JCC ||
                instr.opcode == MOpcode::RET)
                ++stats.branches;
            if (isLoadOpcode(instr.opcode))
                ++stats.loads;
            if (isStoreOpcode(instr.opcode))
                ++stats.stores;
        }
    }
}

} // namespace

/// @brief Run peephole rewrites over every function in @p module.
/// @details Dispatches per-function work either serially (small modules) or
///          across all hardware threads (large modules). Each worker pulls
///          the next function index atomically. Disabled at -O0 to keep
///          compile latency tight.
/// @param module Pipeline state whose @c mir vector is mutated in place.
/// @param diags Diagnostic sink; also receives the optional stats summary.
/// @return True on success or when peepholes are skipped.
bool PeepholePass::run(Module &module, Diagnostics &diags) {
    if (!module.registersAllocated) {
        diags.error("peephole: register allocation must run before backend optimization");
        return false;
    }

    if (module.options.optimizeLevel < 1)
        return true;

    if (module.target == nullptr)
        module.target = &selectTarget(module.options.targetABI);

    const bool collectStats = codegenStatsEnabled();
    std::atomic_size_t total{0};
    MirStats stats{};
    std::mutex statsMutex;
    const std::size_t workerCount = std::min(
        module.mir.size(),
        std::max<std::size_t>(1, static_cast<std::size_t>(std::thread::hardware_concurrency())));
    if (workerCount <= 1) {
        for (auto &fn : module.mir) {
            const std::size_t transformed = runPeepholes(fn, *module.target);
            if (collectStats) {
                total.fetch_add(transformed, std::memory_order_relaxed);
                accumulateStats(fn, stats);
            }
        }
    } else {
        std::atomic_size_t nextIndex{0};
        std::vector<std::thread> workers;
        workers.reserve(workerCount);
        for (std::size_t worker = 0; worker < workerCount; ++worker) {
            workers.emplace_back([&]() {
                MirStats localStats{};
                std::size_t localTotal = 0;
                for (;;) {
                    const std::size_t index = nextIndex.fetch_add(1, std::memory_order_relaxed);
                    if (index >= module.mir.size())
                        break;
                    const std::size_t transformed = runPeepholes(module.mir[index], *module.target);
                    if (collectStats) {
                        localTotal += transformed;
                        accumulateStats(module.mir[index], localStats);
                    }
                }
                if (collectStats) {
                    total.fetch_add(localTotal, std::memory_order_relaxed);
                    std::lock_guard<std::mutex> lock(statsMutex);
                    stats.functions += localStats.functions;
                    stats.blocks += localStats.blocks;
                    stats.instructions += localStats.instructions;
                    stats.calls += localStats.calls;
                    stats.branches += localStats.branches;
                    stats.loads += localStats.loads;
                    stats.stores += localStats.stores;
                }
            });
        }
        for (auto &worker : workers)
            worker.join();
    }

    if (collectStats)
        diags.warning(
            "x86-64 peephole: " + std::to_string(total.load()) + " transformations; mir " +
            std::to_string(stats.functions) + " funcs, " + std::to_string(stats.blocks) +
            " blocks, " + std::to_string(stats.instructions) + " inst, calls=" +
            std::to_string(stats.calls) + ", branches=" + std::to_string(stats.branches) +
            ", loads=" + std::to_string(stats.loads) + ", stores=" + std::to_string(stats.stores));

    return true;
}

} // namespace viper::codegen::x64::passes
