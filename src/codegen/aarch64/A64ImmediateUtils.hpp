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

#include <array>
#include <cstdint>
#include <optional>

namespace viper::codegen::aarch64 {

struct AddSubImmEncoding {
    uint32_t imm12;
    bool shift12;
};

enum class MoveWideOpcode {
    MovZ,
    MovN,
    MovK,
};

struct MoveWideInst {
    MoveWideOpcode opcode;
    uint16_t imm16;
    uint8_t shift;
};

[[nodiscard]] inline uint64_t absImmUnsigned(long long imm) noexcept {
    return imm < 0 ? static_cast<uint64_t>(-(imm + 1)) + 1ULL : static_cast<uint64_t>(imm);
}

[[nodiscard]] inline std::optional<AddSubImmEncoding> classifyAddSubImmEncoding(
    uint64_t imm) noexcept {
    if (imm <= 0xFFFULL)
        return AddSubImmEncoding{static_cast<uint32_t>(imm), false};
    if ((imm & 0xFFFULL) == 0 && (imm >> 12) <= 0xFFFULL)
        return AddSubImmEncoding{static_cast<uint32_t>(imm >> 12), true};
    return std::nullopt;
}

template <typename EmitFn> inline void forEachMoveWideInst(uint64_t imm, EmitFn &&emit) {
    const std::array<uint16_t, 4> chunks = {
        static_cast<uint16_t>(imm & 0xFFFFULL),
        static_cast<uint16_t>((imm >> 16) & 0xFFFFULL),
        static_cast<uint16_t>((imm >> 32) & 0xFFFFULL),
        static_cast<uint16_t>((imm >> 48) & 0xFFFFULL),
    };

    const uint64_t invImm = ~imm;
    const std::array<uint16_t, 4> invChunks = {
        static_cast<uint16_t>(invImm & 0xFFFFULL),
        static_cast<uint16_t>((invImm >> 16) & 0xFFFFULL),
        static_cast<uint16_t>((invImm >> 32) & 0xFFFFULL),
        static_cast<uint16_t>((invImm >> 48) & 0xFFFFULL),
    };

    int nzCount = 0;
    int invNzCount = 0;
    for (std::size_t i = 0; i < chunks.size(); ++i) {
        if (chunks[i] != 0)
            ++nzCount;
        if (invChunks[i] != 0)
            ++invNzCount;
    }

    const bool useMovn = invNzCount < nzCount;
    const auto &seedChunks = useMovn ? invChunks : chunks;
    const MoveWideOpcode seedOpcode = useMovn ? MoveWideOpcode::MovN : MoveWideOpcode::MovZ;

    int firstLane = -1;
    for (std::size_t i = 0; i < seedChunks.size(); ++i) {
        if (seedChunks[i] != 0) {
            firstLane = static_cast<int>(i);
            break;
        }
    }

    if (firstLane < 0) {
        emit(MoveWideInst{seedOpcode, 0, 0});
        return;
    }

    emit(MoveWideInst{seedOpcode,
                      seedChunks[static_cast<std::size_t>(firstLane)],
                      static_cast<uint8_t>(firstLane * 16)});

    for (std::size_t i = 0; i < chunks.size(); ++i) {
        if (static_cast<int>(i) == firstLane)
            continue;
        if (useMovn) {
            if (chunks[i] != 0xFFFFu) {
                emit(MoveWideInst{MoveWideOpcode::MovK, chunks[i], static_cast<uint8_t>(i * 16)});
            }
            continue;
        }
        if (chunks[i] != 0) {
            emit(MoveWideInst{MoveWideOpcode::MovK, chunks[i], static_cast<uint8_t>(i * 16)});
        }
    }
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
        const bool useAddImm = (kind == SignedImmArithKind::Add) ? (imm >= 0) : (imm < 0);
        out.instrs.push_back(
            MInstr{useAddImm ? addImmOpc : subImmOpc,
                   {dst, lhs, MOperand::immOp(static_cast<long long>(magnitude))}});
        return;
    }

    const MOperand tmp = makeTemp(imm);
    out.instrs.push_back(
        MInstr{kind == SignedImmArithKind::Add ? addRegOpc : subRegOpc, {dst, lhs, tmp}});
}

} // namespace viper::codegen::aarch64
