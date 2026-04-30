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

#include <sstream>
#include <unordered_set>

namespace viper::codegen::aarch64::passes {
namespace {

[[nodiscard]] bool isBranchTargetOpcode(MOpcode opcode) noexcept {
    return opcode == MOpcode::Br || opcode == MOpcode::BCond || opcode == MOpcode::Cbz ||
           opcode == MOpcode::Cbnz;
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

    for (auto &fn : module.mir) {
        [[maybe_unused]] auto stats = runPeephole(fn, module.ti);
        pruneUnusedCalleeSaved(fn);
        if (!validateFunction(fn, diags))
            return false;
    }
    return true;
}

} // namespace viper::codegen::aarch64::passes
