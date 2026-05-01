//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/codegen/x86_64/passes/PeepholePass.cpp
// Purpose: Implement the explicit post-RA peephole pass for the x86-64 pipeline.
//
//===----------------------------------------------------------------------===//

#include "codegen/x86_64/passes/PeepholePass.hpp"

#include "codegen/x86_64/Backend.hpp"
#include "codegen/x86_64/Peephole.hpp"

#include <cstddef>
#include <cstdlib>
#include <string>

namespace viper::codegen::x64::passes {
namespace {

[[nodiscard]] bool codegenStatsEnabled() noexcept {
    if (const char *value = std::getenv("VIPER_CODEGEN_STATS"))
        return value[0] != '\0' && value[0] != '0';
    return false;
}

struct MirStats {
    std::size_t functions = 0;
    std::size_t blocks = 0;
    std::size_t instructions = 0;
    std::size_t calls = 0;
    std::size_t branches = 0;
    std::size_t loads = 0;
    std::size_t stores = 0;
};

[[nodiscard]] bool isLoadOpcode(MOpcode opcode) noexcept {
    return opcode == MOpcode::MOVmr || opcode == MOpcode::MOVSDmr ||
           opcode == MOpcode::MOVUPSmr || opcode == MOpcode::POP;
}

[[nodiscard]] bool isStoreOpcode(MOpcode opcode) noexcept {
    return opcode == MOpcode::MOVrm || opcode == MOpcode::MOVSDrm ||
           opcode == MOpcode::MOVUPSrm || opcode == MOpcode::PUSH;
}

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

bool PeepholePass::run(Module &module, Diagnostics &diags) {
    if (!module.registersAllocated) {
        diags.error("peephole: register allocation must run before backend optimization");
        return false;
    }

    if (module.options.optimizeLevel < 1)
        return true;

    if (module.target == nullptr)
        module.target = &selectTarget(module.options.targetABI);

    std::size_t total = 0;
    MirStats stats{};
    for (auto &fn : module.mir) {
        total += runPeepholes(fn, *module.target);
        accumulateStats(fn, stats);
    }

    if (codegenStatsEnabled())
        diags.warning("x86-64 peephole: " + std::to_string(total) + " transformations; mir " +
                      std::to_string(stats.functions) + " funcs, " +
                      std::to_string(stats.blocks) + " blocks, " +
                      std::to_string(stats.instructions) + " inst, calls=" +
                      std::to_string(stats.calls) + ", branches=" +
                      std::to_string(stats.branches) + ", loads=" +
                      std::to_string(stats.loads) + ", stores=" + std::to_string(stats.stores));

    return true;
}

} // namespace viper::codegen::x64::passes
