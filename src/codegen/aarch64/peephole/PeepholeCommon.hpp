//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/codegen/aarch64/peephole/PeepholeCommon.hpp
// Purpose: Shared utility functions and types for AArch64 peephole sub-passes.
//
// Key invariants:
//   - All helpers are pure queries or in-place rewrites; they never allocate
//     persistent state.
//   - Register classification must stay in sync with MachineIR opcode additions.
//
// Ownership/Lifetime:
//   - Header-only; no dynamic state.
//
// Links: src/codegen/aarch64/Peephole.hpp, docs/architecture.md
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

namespace viper::codegen::aarch64::peephole
{

// Bring the shared compaction helper into this namespace scope.
using viper::codegen::common::removeMarkedInstructions;

// ---- Register query helpers ------------------------------------------------

/// @brief Check if an operand is a physical register.
[[nodiscard]] inline bool isPhysReg(const MOperand &op) noexcept
{
    return op.kind == MOperand::Kind::Reg && op.reg.isPhys;
}

/// @brief Check if two register operands refer to the same physical register.
[[nodiscard]] inline bool samePhysReg(const MOperand &a, const MOperand &b) noexcept
{
    if (!isPhysReg(a) || !isPhysReg(b))
        return false;
    return a.reg.cls == b.reg.cls && a.reg.idOrPhys == b.reg.idOrPhys;
}

/// @brief Check if a register is an argument-passing register (x0-x7).
[[nodiscard]] inline bool isArgReg(const MOperand &reg) noexcept
{
    if (!isPhysReg(reg) || reg.reg.cls != RegClass::GPR)
        return false;
    const auto pr = static_cast<PhysReg>(reg.reg.idOrPhys);
    return pr >= PhysReg::X0 && pr <= PhysReg::X7;
}

/// @brief Check if a register is an ABI register (GPR x0-x7 or FPR v0-v7).
[[nodiscard]] inline bool isABIReg(const MOperand &reg) noexcept
{
    if (!isPhysReg(reg))
        return false;
    const auto pr = static_cast<PhysReg>(reg.reg.idOrPhys);
    if (reg.reg.cls == RegClass::GPR)
        return pr >= PhysReg::X0 && pr <= PhysReg::X7;
    if (reg.reg.cls == RegClass::FPR)
        return pr >= PhysReg::V0 && pr <= PhysReg::V7;
    return false;
}

/// @brief Check if an operand is an immediate with a given value.
[[nodiscard]] inline bool isImmValue(const MOperand &op, long long value) noexcept
{
    return op.kind == MOperand::Kind::Imm && op.imm == value;
}

/// @brief Get a unique key for a physical register (for use in maps).
[[nodiscard]] inline uint32_t regKey(const MOperand &op) noexcept
{
    if (op.kind != MOperand::Kind::Reg || !op.reg.isPhys)
        return UINT32_MAX;
    return (static_cast<uint32_t>(op.reg.cls) << 16) | op.reg.idOrPhys;
}

// ---- Instruction query helpers ---------------------------------------------

/// @brief Check if an instruction defines (writes to) a given physical register.
///
/// @details Uses a manually maintained opcode switch to classify destination
///          operands. A table-driven approach was considered, but each opcode
///          group has different operand-index semantics (e.g., LDP defines
///          ops[0] AND ops[1]) making a simple "dest is ops[0]" table insufficient.
[[nodiscard]] inline bool definesReg(const MInstr &instr, const MOperand &reg) noexcept
{
    if (!isPhysReg(reg))
        return false;

    switch (instr.opc)
    {
        // --- Moves and conversions (dest = ops[0]) ---
        case MOpcode::MovRR:
        case MOpcode::MovRI:
        case MOpcode::FMovRR:
        case MOpcode::FMovRI:
        case MOpcode::FMovGR:
        // --- Integer arithmetic (dest = ops[0]) ---
        case MOpcode::AddRRR:
        case MOpcode::SubRRR:
        case MOpcode::MulRRR:
        case MOpcode::SmulhRRR:
        case MOpcode::SDivRRR:
        case MOpcode::UDivRRR:
        case MOpcode::AddRI:
        case MOpcode::SubRI:
        case MOpcode::MSubRRRR:
        case MOpcode::MAddRRRR:
        // --- Bitwise (dest = ops[0]) ---
        case MOpcode::AndRRR:
        case MOpcode::OrrRRR:
        case MOpcode::EorRRR:
        case MOpcode::AndRI:
        case MOpcode::OrrRI:
        case MOpcode::EorRI:
        // --- Shifts (dest = ops[0]) ---
        case MOpcode::LslRI:
        case MOpcode::LsrRI:
        case MOpcode::AsrRI:
        case MOpcode::LslvRRR:
        case MOpcode::LsrvRRR:
        case MOpcode::AsrvRRR:
        // --- Loads (dest = ops[0]) ---
        case MOpcode::LdrRegFpImm:
        case MOpcode::LdrFprFpImm:
        case MOpcode::LdrRegBaseImm:
        case MOpcode::LdrFprBaseImm:
        // --- Address computation (dest = ops[0]) ---
        case MOpcode::AddFpImm:
        case MOpcode::AdrPage:
        case MOpcode::AddPageOff:
        // --- Floating-point arithmetic (dest = ops[0]) ---
        case MOpcode::FAddRRR:
        case MOpcode::FSubRRR:
        case MOpcode::FMulRRR:
        case MOpcode::FDivRRR:
        // --- FP/int conversions (dest = ops[0]) ---
        case MOpcode::SCvtF:
        case MOpcode::FCvtZS:
        case MOpcode::UCvtF:
        case MOpcode::FCvtZU:
        case MOpcode::FRintN:
        // --- Conditional select/set (dest = ops[0]) ---
        case MOpcode::Cset:
        case MOpcode::Csel:
        // --- Flag-setting arithmetic (dest = ops[0]) ---
        case MOpcode::AddsRRR:
        case MOpcode::SubsRRR:
        case MOpcode::AddsRI:
        case MOpcode::SubsRI:
        case MOpcode::AddOvfRRR:
        case MOpcode::SubOvfRRR:
        case MOpcode::AddOvfRI:
        case MOpcode::SubOvfRI:
        case MOpcode::MulOvfRRR:
            if (!instr.ops.empty() && samePhysReg(instr.ops[0], reg))
                return true;
            break;

        // LDP defines two registers (ops[0] and ops[1])
        case MOpcode::LdpRegFpImm:
        case MOpcode::LdpFprFpImm:
            if (instr.ops.size() >= 1 && samePhysReg(instr.ops[0], reg))
                return true;
            if (instr.ops.size() >= 2 && samePhysReg(instr.ops[1], reg))
                return true;
            break;

        // --- Instructions that don't define registers ---
        case MOpcode::PhiStoreGPR:
        case MOpcode::PhiStoreFPR:
        case MOpcode::CmpRR:
        case MOpcode::CmpRI:
        case MOpcode::TstRR:
        case MOpcode::FCmpRR:
        case MOpcode::Br:
        case MOpcode::BCond:
        case MOpcode::Bl:
        case MOpcode::Blr:
        case MOpcode::Ret:
        case MOpcode::Cbz:
        case MOpcode::Cbnz:
        case MOpcode::StrRegFpImm:
        case MOpcode::StrFprFpImm:
        case MOpcode::StrRegBaseImm:
        case MOpcode::StrFprBaseImm:
        case MOpcode::StrRegSpImm:
        case MOpcode::StrFprSpImm:
        case MOpcode::StpRegFpImm:
        case MOpcode::StpFprFpImm:
        case MOpcode::SubSpImm:
        case MOpcode::AddSpImm:
            break;
    }
    return false;
}

/// @brief Check if an instruction uses a given physical register as a source.
[[nodiscard]] inline bool usesReg(const MInstr &instr, const MOperand &reg) noexcept
{
    if (!isPhysReg(reg))
        return false;

    switch (instr.opc)
    {
        case MOpcode::MovRR:
        case MOpcode::FMovRR:
        case MOpcode::FMovGR:
            if (instr.ops.size() >= 2 && samePhysReg(instr.ops[1], reg))
                return true;
            break;

        case MOpcode::AddRRR:
        case MOpcode::SubRRR:
        case MOpcode::MulRRR:
        case MOpcode::SmulhRRR:
        case MOpcode::SDivRRR:
        case MOpcode::UDivRRR:
        case MOpcode::AndRRR:
        case MOpcode::OrrRRR:
        case MOpcode::EorRRR:
        case MOpcode::FAddRRR:
        case MOpcode::FSubRRR:
        case MOpcode::FMulRRR:
        case MOpcode::FDivRRR:
        case MOpcode::LslvRRR:
        case MOpcode::LsrvRRR:
        case MOpcode::AsrvRRR:
        case MOpcode::AddsRRR:
        case MOpcode::SubsRRR:
        case MOpcode::AddOvfRRR:
        case MOpcode::SubOvfRRR:
        case MOpcode::MulOvfRRR:
            if (instr.ops.size() >= 2 && samePhysReg(instr.ops[1], reg))
                return true;
            if (instr.ops.size() >= 3 && samePhysReg(instr.ops[2], reg))
                return true;
            break;

        case MOpcode::AndRI:
        case MOpcode::OrrRI:
        case MOpcode::EorRI:
        case MOpcode::AddRI:
        case MOpcode::SubRI:
        case MOpcode::LslRI:
        case MOpcode::LsrRI:
        case MOpcode::AsrRI:
        case MOpcode::AddsRI:
        case MOpcode::SubsRI:
        case MOpcode::AddOvfRI:
        case MOpcode::SubOvfRI:
            if (instr.ops.size() >= 2 && samePhysReg(instr.ops[1], reg))
                return true;
            break;

        case MOpcode::CmpRR:
        case MOpcode::TstRR:
        case MOpcode::FCmpRR:
            if (instr.ops.size() >= 1 && samePhysReg(instr.ops[0], reg))
                return true;
            if (instr.ops.size() >= 2 && samePhysReg(instr.ops[1], reg))
                return true;
            break;

        case MOpcode::CmpRI:
            if (instr.ops.size() >= 1 && samePhysReg(instr.ops[0], reg))
                return true;
            break;

        case MOpcode::StrRegFpImm:
        case MOpcode::StrFprFpImm:
            if (instr.ops.size() >= 1 && samePhysReg(instr.ops[0], reg))
                return true;
            break;

        case MOpcode::StrRegBaseImm:
        case MOpcode::StrFprBaseImm:
            if (instr.ops.size() >= 1 && samePhysReg(instr.ops[0], reg))
                return true;
            if (instr.ops.size() >= 2 && samePhysReg(instr.ops[1], reg))
                return true;
            break;

        case MOpcode::LdrRegBaseImm:
        case MOpcode::LdrFprBaseImm:
            if (instr.ops.size() >= 2 && samePhysReg(instr.ops[1], reg))
                return true;
            break;

        case MOpcode::Cbz:
            if (instr.ops.size() >= 1 && samePhysReg(instr.ops[0], reg))
                return true;
            break;

        case MOpcode::Blr:
            if (instr.ops.size() >= 1 && samePhysReg(instr.ops[0], reg))
                return true;
            break;

        case MOpcode::SCvtF:
        case MOpcode::FCvtZS:
        case MOpcode::UCvtF:
        case MOpcode::FCvtZU:
            if (instr.ops.size() >= 2 && samePhysReg(instr.ops[1], reg))
                return true;
            break;

        case MOpcode::MSubRRRR:
        case MOpcode::MAddRRRR:
            for (std::size_t i = 1; i < instr.ops.size() && i <= 3; ++i)
            {
                if (samePhysReg(instr.ops[i], reg))
                    return true;
            }
            break;

        case MOpcode::AddPageOff:
            if (instr.ops.size() >= 2 && samePhysReg(instr.ops[1], reg))
                return true;
            break;

        case MOpcode::Cbnz:
            if (instr.ops.size() >= 1 && samePhysReg(instr.ops[0], reg))
                return true;
            break;

        case MOpcode::Csel:
            if (instr.ops.size() >= 2 && samePhysReg(instr.ops[1], reg))
                return true;
            if (instr.ops.size() >= 3 && samePhysReg(instr.ops[2], reg))
                return true;
            break;

        case MOpcode::StpRegFpImm:
        case MOpcode::StpFprFpImm:
            if (instr.ops.size() >= 1 && samePhysReg(instr.ops[0], reg))
                return true;
            if (instr.ops.size() >= 2 && samePhysReg(instr.ops[1], reg))
                return true;
            break;

        case MOpcode::LdpRegFpImm:
        case MOpcode::LdpFprFpImm:
            break;

        default:
            break;
    }
    return false;
}

/// @brief Classify an operand as use, def, or both.
[[nodiscard]] inline std::pair<bool, bool> classifyOperand(const MInstr &instr,
                                                           std::size_t idx) noexcept
{
    switch (instr.opc)
    {
        case MOpcode::MovRR:
        case MOpcode::MovRI:
        case MOpcode::FMovRR:
        case MOpcode::FMovRI:
        case MOpcode::FMovGR:
            return idx == 0 ? std::make_pair(false, true) : std::make_pair(true, false);

        case MOpcode::AddRRR:
        case MOpcode::SubRRR:
        case MOpcode::MulRRR:
        case MOpcode::SmulhRRR:
        case MOpcode::SDivRRR:
        case MOpcode::UDivRRR:
        case MOpcode::AndRRR:
        case MOpcode::OrrRRR:
        case MOpcode::EorRRR:
        case MOpcode::FAddRRR:
        case MOpcode::FSubRRR:
        case MOpcode::FMulRRR:
        case MOpcode::FDivRRR:
        case MOpcode::LslvRRR:
        case MOpcode::LsrvRRR:
        case MOpcode::AsrvRRR:
            return idx == 0 ? std::make_pair(false, true) : std::make_pair(true, false);

        case MOpcode::AndRI:
        case MOpcode::OrrRI:
        case MOpcode::EorRI:
        case MOpcode::AddRI:
        case MOpcode::SubRI:
        case MOpcode::LslRI:
        case MOpcode::LsrRI:
        case MOpcode::AsrRI:
            if (idx == 0)
                return {false, true};
            if (idx == 1)
                return {true, false};
            return {false, false};

        case MOpcode::CmpRR:
        case MOpcode::TstRR:
        case MOpcode::FCmpRR:
            return {true, false};

        case MOpcode::CmpRI:
            return idx == 0 ? std::make_pair(true, false) : std::make_pair(false, false);

        case MOpcode::Cset:
            return idx == 0 ? std::make_pair(false, true) : std::make_pair(false, false);

        case MOpcode::LdrRegFpImm:
        case MOpcode::LdrFprFpImm:
            return idx == 0 ? std::make_pair(false, true) : std::make_pair(false, false);

        case MOpcode::LdrRegBaseImm:
        case MOpcode::LdrFprBaseImm:
            if (idx == 0)
                return {false, true};
            if (idx == 1)
                return {true, false};
            return {false, false};

        case MOpcode::StrRegFpImm:
        case MOpcode::StrFprFpImm:
        case MOpcode::StrRegSpImm:
        case MOpcode::StrFprSpImm:
            return idx == 0 ? std::make_pair(true, false) : std::make_pair(false, false);

        case MOpcode::StrRegBaseImm:
        case MOpcode::StrFprBaseImm:
            if (idx == 0)
                return {true, false};
            if (idx == 1)
                return {true, false};
            return {false, false};

        case MOpcode::SCvtF:
        case MOpcode::FCvtZS:
        case MOpcode::UCvtF:
        case MOpcode::FCvtZU:
        case MOpcode::FRintN:
            return idx == 0 ? std::make_pair(false, true) : std::make_pair(true, false);

        case MOpcode::MSubRRRR:
        case MOpcode::MAddRRRR:
            return idx == 0 ? std::make_pair(false, true) : std::make_pair(true, false);

        case MOpcode::Csel:
            if (idx == 0)
                return {false, true};
            if (idx == 1 || idx == 2)
                return {true, false};
            return {false, false};

        case MOpcode::LdpRegFpImm:
        case MOpcode::LdpFprFpImm:
            if (idx == 0 || idx == 1)
                return {false, true};
            return {false, false};

        case MOpcode::StpRegFpImm:
        case MOpcode::StpFprFpImm:
            if (idx == 0 || idx == 1)
                return {true, false};
            return {false, false};

        case MOpcode::Cbnz:
            return idx == 0 ? std::make_pair(true, false) : std::make_pair(false, false);

        case MOpcode::AdrPage:
            return idx == 0 ? std::make_pair(false, true) : std::make_pair(false, false);

        case MOpcode::AddPageOff:
            if (idx == 0)
                return {false, true};
            if (idx == 1)
                return {true, false};
            return {false, false};

        case MOpcode::Cbz:
            return idx == 0 ? std::make_pair(true, false) : std::make_pair(false, false);

        case MOpcode::Blr:
            return idx == 0 ? std::make_pair(true, false) : std::make_pair(false, false);

        case MOpcode::AddFpImm:
            return idx == 0 ? std::make_pair(false, true) : std::make_pair(false, false);

        case MOpcode::SubSpImm:
        case MOpcode::AddSpImm:
            return {false, false};

        default:
            return idx == 0 ? std::make_pair(false, true) : std::make_pair(true, false);
    }
}

// ---- Constant tracking -----------------------------------------------------

/// @brief Map of registers to their known constant values from MovRI.
using RegConstMap = std::unordered_map<uint16_t, long long>;

/// @brief Update register constant tracking based on an instruction.
inline void updateKnownConsts(const MInstr &instr, RegConstMap &knownConsts)
{
    if (instr.opc == MOpcode::MovRI && instr.ops.size() == 2 && isPhysReg(instr.ops[0]) &&
        instr.ops[1].kind == MOperand::Kind::Imm)
    {
        knownConsts[instr.ops[0].reg.idOrPhys] = instr.ops[1].imm;
        return;
    }

    switch (instr.opc)
    {
        case MOpcode::MovRR:
        case MOpcode::AddRRR:
        case MOpcode::SubRRR:
        case MOpcode::MulRRR:
        case MOpcode::SmulhRRR:
        case MOpcode::SDivRRR:
        case MOpcode::UDivRRR:
        case MOpcode::AndRRR:
        case MOpcode::OrrRRR:
        case MOpcode::EorRRR:
        case MOpcode::AndRI:
        case MOpcode::OrrRI:
        case MOpcode::EorRI:
        case MOpcode::AddRI:
        case MOpcode::SubRI:
        case MOpcode::LslRI:
        case MOpcode::LsrRI:
        case MOpcode::AsrRI:
        case MOpcode::LslvRRR:
        case MOpcode::LsrvRRR:
        case MOpcode::AsrvRRR:
        case MOpcode::Cset:
        case MOpcode::LdrRegFpImm:
        case MOpcode::LdrRegBaseImm:
        case MOpcode::AddFpImm:
        case MOpcode::AdrPage:
        case MOpcode::AddPageOff:
        case MOpcode::MSubRRRR:
        case MOpcode::MAddRRRR:
        case MOpcode::Csel:
            if (!instr.ops.empty() && isPhysReg(instr.ops[0]))
                knownConsts.erase(instr.ops[0].reg.idOrPhys);
            break;
        case MOpcode::LdpRegFpImm:
        case MOpcode::LdpFprFpImm:
            if (instr.ops.size() >= 1 && isPhysReg(instr.ops[0]))
                knownConsts.erase(instr.ops[0].reg.idOrPhys);
            if (instr.ops.size() >= 2 && isPhysReg(instr.ops[1]))
                knownConsts.erase(instr.ops[1].reg.idOrPhys);
            break;
        default:
            break;
    }

    if (instr.opc == MOpcode::Bl || instr.opc == MOpcode::Blr)
    {
        for (uint16_t i = 0; i <= 18; ++i)
            knownConsts.erase(i);
    }
}

/// @brief Get constant value for a register if known.
[[nodiscard]] inline std::optional<long long> getConstValue(const MOperand &reg,
                                                            const RegConstMap &knownConsts)
{
    if (!isPhysReg(reg) || reg.reg.cls != RegClass::GPR)
        return std::nullopt;
    auto it = knownConsts.find(reg.reg.idOrPhys);
    if (it != knownConsts.end())
        return it->second;
    return std::nullopt;
}

// ---- Side-effect / def queries ---------------------------------------------

/// @brief Check if an instruction has side effects and cannot be removed.
[[nodiscard]] inline bool hasSideEffects(const MInstr &instr) noexcept
{
    switch (instr.opc)
    {
        case MOpcode::StrRegFpImm:
        case MOpcode::StrFprFpImm:
        case MOpcode::StrRegBaseImm:
        case MOpcode::StrFprBaseImm:
        case MOpcode::StrRegSpImm:
        case MOpcode::StrFprSpImm:
        case MOpcode::StpRegFpImm:
        case MOpcode::StpFprFpImm:
        case MOpcode::Bl:
        case MOpcode::Blr:
        case MOpcode::Br:
        case MOpcode::BCond:
        case MOpcode::Ret:
        case MOpcode::Cbz:
        case MOpcode::Cbnz:
        case MOpcode::SubSpImm:
        case MOpcode::AddSpImm:
        case MOpcode::CmpRR:
        case MOpcode::CmpRI:
        case MOpcode::TstRR:
        case MOpcode::FCmpRR:
        case MOpcode::LdrRegFpImm:
        case MOpcode::LdrFprFpImm:
        case MOpcode::LdrRegBaseImm:
        case MOpcode::LdrFprBaseImm:
        case MOpcode::LdpRegFpImm:
        case MOpcode::LdpFprFpImm:
        case MOpcode::AdrPage:
        case MOpcode::AddPageOff:
        case MOpcode::AddFpImm:
            return true;

        case MOpcode::MovRR:
        case MOpcode::MovRI:
        case MOpcode::FMovRR:
        case MOpcode::FMovRI:
        case MOpcode::FMovGR:
        {
            if (instr.ops.empty())
                return false;
            const auto &dst = instr.ops[0];
            if (dst.kind != MOperand::Kind::Reg || !dst.reg.isPhys)
                return false;
            auto pr = static_cast<PhysReg>(dst.reg.idOrPhys);
            if (pr >= PhysReg::X0 && pr <= PhysReg::X7)
                return true;
            if (pr >= PhysReg::V0 && pr <= PhysReg::V7)
                return true;
            return false;
        }

        default:
            return false;
    }
}

/// @brief Get the physical register defined by an instruction, if any.
[[nodiscard]] inline std::optional<MOperand> getDefinedReg(const MInstr &instr) noexcept
{
    switch (instr.opc)
    {
        case MOpcode::MovRR:
        case MOpcode::MovRI:
        case MOpcode::FMovRR:
        case MOpcode::FMovRI:
        case MOpcode::FMovGR:
        case MOpcode::AddRRR:
        case MOpcode::SubRRR:
        case MOpcode::MulRRR:
        case MOpcode::SmulhRRR:
        case MOpcode::SDivRRR:
        case MOpcode::UDivRRR:
        case MOpcode::AndRRR:
        case MOpcode::OrrRRR:
        case MOpcode::EorRRR:
        case MOpcode::AndRI:
        case MOpcode::OrrRI:
        case MOpcode::EorRI:
        case MOpcode::AddRI:
        case MOpcode::SubRI:
        case MOpcode::LslRI:
        case MOpcode::LsrRI:
        case MOpcode::AsrRI:
        case MOpcode::Cset:
        case MOpcode::LdrRegFpImm:
        case MOpcode::LdrFprFpImm:
        case MOpcode::LdrRegBaseImm:
        case MOpcode::LdrFprBaseImm:
        case MOpcode::AddFpImm:
        case MOpcode::AdrPage:
        case MOpcode::AddPageOff:
        case MOpcode::FAddRRR:
        case MOpcode::FSubRRR:
        case MOpcode::FMulRRR:
        case MOpcode::FDivRRR:
        case MOpcode::SCvtF:
        case MOpcode::FCvtZS:
        case MOpcode::UCvtF:
        case MOpcode::FCvtZU:
        case MOpcode::FRintN:
        case MOpcode::MSubRRRR:
        case MOpcode::MAddRRRR:
        case MOpcode::Csel:
        case MOpcode::LdpRegFpImm:
        case MOpcode::LdpFprFpImm:
            if (!instr.ops.empty() && isPhysReg(instr.ops[0]))
                return instr.ops[0];
            break;

        default:
            break;
    }
    return std::nullopt;
}

} // namespace viper::codegen::aarch64::peephole
