//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: codegen/aarch64/passes/PeepholePass.cpp
// Purpose: Peephole optimisation pass for the AArch64 modular pipeline.
//
// Runs the AArch64 peephole optimizer on each MIR function after register
// allocation.  Peephole failures are silently ignored — the pass always
// returns true (non-fatal).
//
//===----------------------------------------------------------------------===//

#include "codegen/aarch64/passes/PeepholePass.hpp"

#include "codegen/aarch64/Peephole.hpp"

#include <cstddef>
#include <cstdlib>
#include <sstream>
#include <string>
#include <unordered_set>

namespace viper::codegen::aarch64::passes {
namespace {

[[nodiscard]] bool isBranchTargetOpcode(MOpcode opcode) noexcept {
    return opcode == MOpcode::Br || opcode == MOpcode::BCond || opcode == MOpcode::Cbz ||
           opcode == MOpcode::Cbnz;
}

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

[[nodiscard]] bool isCallOpcode(MOpcode opcode) noexcept {
    return opcode == MOpcode::Bl || opcode == MOpcode::Blr;
}

[[nodiscard]] bool isBranchOpcode(MOpcode opcode) noexcept {
    return opcode == MOpcode::Br || opcode == MOpcode::BCond || opcode == MOpcode::Cbz ||
           opcode == MOpcode::Cbnz || opcode == MOpcode::Ret;
}

[[nodiscard]] bool isLoadOpcode(MOpcode opcode) noexcept {
    return opcode == MOpcode::LdrRegFpImm || opcode == MOpcode::LdrRegBaseImm ||
           opcode == MOpcode::LdrFprFpImm || opcode == MOpcode::LdrFprBaseImm ||
           opcode == MOpcode::LdpRegFpImm || opcode == MOpcode::LdpFprFpImm;
}

[[nodiscard]] bool isStoreOpcode(MOpcode opcode) noexcept {
    return opcode == MOpcode::StrRegFpImm || opcode == MOpcode::StrRegBaseImm ||
           opcode == MOpcode::StrRegSpImm || opcode == MOpcode::StrFprFpImm ||
           opcode == MOpcode::StrFprBaseImm || opcode == MOpcode::StrFprSpImm ||
           opcode == MOpcode::StpRegFpImm || opcode == MOpcode::StpFprFpImm ||
           opcode == MOpcode::PhiStoreGPR || opcode == MOpcode::PhiStoreFPR;
}

void accumulateStats(const MFunction &fn, MirStats &stats) {
    ++stats.functions;
    stats.blocks += fn.blocks.size();
    for (const auto &block : fn.blocks) {
        stats.instructions += block.instrs.size();
        for (const auto &instr : block.instrs) {
            if (isCallOpcode(instr.opc))
                ++stats.calls;
            if (isBranchOpcode(instr.opc))
                ++stats.branches;
            if (isLoadOpcode(instr.opc))
                ++stats.loads;
            if (isStoreOpcode(instr.opc))
                ++stats.stores;
        }
    }
}

[[nodiscard]] bool isHardTerminator(MOpcode opcode) noexcept {
    return opcode == MOpcode::Br || opcode == MOpcode::Ret;
}

[[nodiscard]] bool validateFunction(const MFunction &fn, Diagnostics &diags) {
    std::unordered_set<std::string> labels;
    for (const auto &block : fn.blocks)
        labels.insert(block.name);

    for (const auto &block : fn.blocks) {
        bool seenTerminator = false;
        for (std::size_t ii = 0; ii < block.instrs.size(); ++ii) {
            const auto &instr = block.instrs[ii];

            if (seenTerminator) {
                std::ostringstream msg;
                msg << "aarch64 peephole: non-terminator after terminator in function '"
                    << fn.name << "', block '" << block.name << "'";
                diags.error(msg.str());
                return false;
            }
            if (isHardTerminator(instr.opc))
                seenTerminator = true;

            for (const auto &op : instr.ops) {
                if (op.kind == MOperand::Kind::Reg && !op.reg.isPhys) {
                    std::ostringstream msg;
                    msg << "aarch64 peephole: virtual register remains in function '" << fn.name
                        << "', block '" << block.name << "'";
                    diags.error(msg.str());
                    return false;
                }
            }

            if (!isBranchTargetOpcode(instr.opc))
                continue;
            if (instr.opc == MOpcode::Br) {
                if (instr.ops.empty() || instr.ops[0].kind != MOperand::Kind::Label)
                    continue;
                if (labels.count(instr.ops[0].label) != 0)
                    continue;
            } else {
                if (instr.ops.size() < 2 || instr.ops[1].kind != MOperand::Kind::Label)
                    continue;
                if (labels.count(instr.ops[1].label) != 0)
                    continue;
            }

            std::ostringstream msg;
            msg << "aarch64 peephole: branch to missing block in function '" << fn.name
                << "', block '" << block.name << "'";
            diags.error(msg.str());
            return false;
        }
    }
    return true;
}

} // namespace

bool PeepholePass::run(AArch64Module &module, Diagnostics &diags) {
    if (module.ti == nullptr) {
        diags.error("aarch64 peephole: target info is required");
        return false;
    }

    int total = 0;
    MirStats stats{};
    for (auto &fn : module.mir) {
        auto peepholeStats = runPeephole(fn, module.ti);
        total += peepholeStats.total();
        pruneUnusedCalleeSaved(fn);
        if (!validateFunction(fn, diags))
            return false;
        accumulateStats(fn, stats);
    }

    if (codegenStatsEnabled())
        diags.warning("aarch64 peephole: " + std::to_string(total) + " transformations; mir " +
                      std::to_string(stats.functions) + " funcs, " +
                      std::to_string(stats.blocks) + " blocks, " +
                      std::to_string(stats.instructions) + " inst, calls=" +
                      std::to_string(stats.calls) + ", branches=" +
                      std::to_string(stats.branches) + ", loads=" +
                      std::to_string(stats.loads) + ", stores=" + std::to_string(stats.stores));

    return true;
}

} // namespace viper::codegen::aarch64::passes
