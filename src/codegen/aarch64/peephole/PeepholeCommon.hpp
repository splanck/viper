//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: codegen/aarch64/peephole/PeepholeCommon.hpp
// Purpose: Shared utility functions and types for AArch64 peephole sub-passes.
//
// Key invariants:
//   - Small queries (3-30 LOC) live inline here for hot-path callsites.
//   - Switch-heavy classifiers (definesReg, usesReg, classifyOperand,
//     hasSideEffects, getDefinedReg, updateKnownConsts) are declared here and
//     defined in PeepholeCommon.cpp so sub-pass translation units don't pay
//     their compile cost on every include.
//   - Register classification must stay in sync with MachineIR opcode additions.
//
// Ownership/Lifetime:
//   - Free functions, no dynamic state.
//
// Links: codegen/aarch64/Peephole.hpp, docs/internals/architecture.md
//
//===----------------------------------------------------------------------===//

#pragma once

#include "../MachineIR.hpp"
#include "codegen/common/PeepholeUtil.hpp"

#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace viper::codegen::aarch64::peephole {

// Bring the shared compaction helper into this namespace scope.
using viper::codegen::common::removeMarkedInstructions;

// ---- Register query helpers ------------------------------------------------

/// @brief Check if an operand is a physical register.
[[nodiscard]] inline bool isPhysReg(const MOperand &op) noexcept {
    return op.kind == MOperand::Kind::Reg && op.reg.isPhys;
}

/// @brief Check if two register operands refer to the same physical register.
[[nodiscard]] inline bool samePhysReg(const MOperand &a, const MOperand &b) noexcept {
    if (!isPhysReg(a) || !isPhysReg(b))
        return false;
    return a.reg.cls == b.reg.cls && a.reg.idOrPhys == b.reg.idOrPhys;
}

/// @brief Check if a register is an argument-passing register (x0-x7).
[[nodiscard]] inline bool isArgReg(const MOperand &reg) noexcept {
    if (!isPhysReg(reg) || reg.reg.cls != RegClass::GPR)
        return false;
    const auto pr = static_cast<PhysReg>(reg.reg.idOrPhys);
    return pr <= PhysReg::X7;
}

/// @brief Check if a register is an ABI register (GPR x0-x7 or FPR v0-v7).
[[nodiscard]] inline bool isABIReg(const MOperand &reg) noexcept {
    if (!isPhysReg(reg))
        return false;
    const auto pr = static_cast<PhysReg>(reg.reg.idOrPhys);
    if (reg.reg.cls == RegClass::GPR)
        return pr <= PhysReg::X7;
    if (reg.reg.cls == RegClass::FPR)
        return pr >= PhysReg::V0 && pr <= PhysReg::V7;
    return false;
}

/// @brief Check if an operand is an immediate with a given value.
[[nodiscard]] inline bool isImmValue(const MOperand &op, long long value) noexcept {
    return op.kind == MOperand::Kind::Imm && op.imm == value;
}

/// @brief Get a unique key for a physical register (for use in maps).
[[nodiscard]] inline uint32_t regKey(const MOperand &op) noexcept {
    if (op.kind != MOperand::Kind::Reg || !op.reg.isPhys)
        return UINT32_MAX;
    return (static_cast<uint32_t>(op.reg.cls) << 16) | op.reg.idOrPhys;
}

// ---- Instruction query helpers (defined in PeepholeCommon.cpp) -------------

/// @brief Check if an instruction defines (writes to) a given physical register.
///
/// @details Manually maintained opcode switch (kept out-of-line to avoid bloat
///          in every translation unit that includes this header). Each opcode
///          group has different operand-index semantics (e.g., LDP defines both
///          ops[0] and ops[1]), which is why a generic "dest is ops[0]" table
///          is insufficient.
[[nodiscard]] bool definesReg(const MInstr &instr, const MOperand &reg) noexcept;

/// @brief Check if an instruction uses a given physical register as a source.
[[nodiscard]] bool usesReg(const MInstr &instr, const MOperand &reg) noexcept;

/// @brief Classify an operand as use, def, or both.
[[nodiscard]] std::pair<bool, bool> classifyOperand(const MInstr &instr, std::size_t idx) noexcept;

// ---- Constant tracking -----------------------------------------------------

/// @brief Map of registers to their known constant values from MovRI.
using RegConstMap = std::unordered_map<uint16_t, long long>;

/// @brief Update register constant tracking based on an instruction.
void updateKnownConsts(const MInstr &instr, RegConstMap &knownConsts);

/// @brief Get constant value for a register if known.
[[nodiscard]] inline std::optional<long long> getConstValue(const MOperand &reg,
                                                            const RegConstMap &knownConsts) {
    if (!isPhysReg(reg) || reg.reg.cls != RegClass::GPR)
        return std::nullopt;
    auto it = knownConsts.find(reg.reg.idOrPhys);
    if (it != knownConsts.end())
        return it->second;
    return std::nullopt;
}

// ---- Side-effect / def queries ---------------------------------------------

/// @brief Check if an instruction has side effects and cannot be removed.
[[nodiscard]] bool hasSideEffects(const MInstr &instr) noexcept;

/// @brief Get the physical register defined by an instruction, if any.
[[nodiscard]] std::optional<MOperand> getDefinedReg(const MInstr &instr) noexcept;

} // namespace viper::codegen::aarch64::peephole
