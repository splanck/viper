//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: codegen/aarch64/A64ImmediateUtils.hpp
// Purpose: Shared AArch64 immediate-classification and legalization helpers.
//
//===----------------------------------------------------------------------===//

#pragma once

#include "codegen/aarch64/MachineIR.hpp"

#include <cstdint>
#include <optional>

namespace viper::codegen::aarch64 {

struct AddSubImmEncoding {
    uint32_t imm12;
    bool shift12;
};

[[nodiscard]] inline uint64_t absImmUnsigned(long long imm) noexcept {
    return imm < 0 ? static_cast<uint64_t>(-(imm + 1)) + 1ULL : static_cast<uint64_t>(imm);
}

[[nodiscard]] inline std::optional<AddSubImmEncoding>
classifyAddSubImmEncoding(uint64_t imm) noexcept {
    if (imm <= 0xFFFULL)
        return AddSubImmEncoding{static_cast<uint32_t>(imm), false};
    if ((imm & 0xFFFULL) == 0 && (imm >> 12) <= 0xFFFULL)
        return AddSubImmEncoding{static_cast<uint32_t>(imm >> 12), true};
    return std::nullopt;
}

enum class SignedImmArithKind {
    Add,
    Sub,
};

template <typename MakeTempFn>
inline void emitLegalizedSignedImmArith(MBasicBlock &out,
                                        const MOperand &dst,
                                        const MOperand &lhs,
                                        long long imm,
                                        SignedImmArithKind kind,
                                        MOpcode addImmOpc,
                                        MOpcode subImmOpc,
                                        MOpcode addRegOpc,
                                        MOpcode subRegOpc,
                                        MakeTempFn &&makeTemp) {
    const uint64_t magnitude = absImmUnsigned(imm);
    if (classifyAddSubImmEncoding(magnitude).has_value()) {
        const bool useAddImm =
            (kind == SignedImmArithKind::Add) ? (imm >= 0) : (imm < 0);
        out.instrs.push_back(MInstr{useAddImm ? addImmOpc : subImmOpc,
                                    {dst, lhs, MOperand::immOp(static_cast<long long>(magnitude))}});
        return;
    }

    const MOperand tmp = makeTemp(imm);
    out.instrs.push_back(MInstr{kind == SignedImmArithKind::Add ? addRegOpc : subRegOpc,
                                {dst, lhs, tmp}});
}

} // namespace viper::codegen::aarch64
