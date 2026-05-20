//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: codegen/aarch64/FrameCodegen.hpp
// Purpose: Shared prologue/epilogue iteration utilities for AArch64 callee-saved
//          register save/restore sequences. Both AsmEmitter (text) and
//          A64BinaryEncoder (binary) delegate to these templates to avoid
//          duplicating the pair/single and reverse-order iteration logic.
// Key invariants:
//   - Save iterates forward in pairs: stp r0,r1; str r_last if odd count.
//   - Restore iterates backward: handle odd tail first, then pairs.
//   - GPRs and FPRs are processed separately (different instruction encodings).
// Ownership/Lifetime:
//   - Header-only; all functions are templates with no persistent state.
// Links: codegen/aarch64/AsmEmitter.cpp,
//        codegen/aarch64/binenc/A64BinaryEncoder.cpp
//
//===----------------------------------------------------------------------===//

#pragma once

#include "TargetAArch64.hpp"

#include <cstddef>
#include <vector>

namespace viper::codegen::aarch64 {

/// Iterate over a register list in forward-pair order for callee save (prologue).
///
/// Processes registers two at a time. For each full pair, calls \p onPair(r0, r1).
/// If the list has an odd length, calls \p onSingle(rLast) for the trailing register.
///
/// @tparam OnPair   Callable with signature void(PhysReg, PhysReg)
/// @tparam OnSingle Callable with signature void(PhysReg)
template <typename OnPair, typename OnSingle>
void forEachSaveReg(const std::vector<PhysReg> &regs, OnPair onPair, OnSingle onSingle) {
    for (std::size_t i = 0; i < regs.size();) {
        const PhysReg r0 = regs[i++];
        if (i < regs.size())
            onPair(r0, regs[i++]);
        else
            onSingle(r0);
    }
}

/// Iterate over a register list in reverse-pair order for callee restore (epilogue).
///
/// Processes registers from the end. If the list has an odd length, calls
/// \p onSingle(rLast) first (the last register saved was last on the stack).
/// Then processes remaining registers in descending pairs via \p onPair(r0, r1).
///
/// @tparam OnPair   Callable with signature void(PhysReg, PhysReg)
/// @tparam OnSingle Callable with signature void(PhysReg)
template <typename OnPair, typename OnSingle>
void forEachRestoreReg(const std::vector<PhysReg> &regs, OnPair onPair, OnSingle onSingle) {
    std::size_t n = regs.size();
    if (n % 2 == 1) {
        onSingle(regs[n - 1]);
        --n;
    }
    while (n > 0) {
        onPair(regs[n - 2], regs[n - 1]);
        n -= 2;
    }
}

/// @brief Walk the AArch64 prologue step sequence, dispatching each step to the
///        caller-provided emitter.
/// @details The step ordering is the single source of truth for "what a function
///          prologue looks like." Both AsmEmitter (text) and A64BinaryEncoder
///          (binary) call this with their own Steps struct so a future addition
///          (e.g., a new ABI step) only needs to be added here, not duplicated.
///
///          The Steps callable must expose: paciasp(), stpFpLrPre(), movFpSp(),
///          subSp(int32_t), stpGprPair(PhysReg,PhysReg), strGprSingle(PhysReg),
///          stpFprPair(PhysReg,PhysReg), strFprSingle(PhysReg).
template <typename Steps>
void iteratePrologue(const std::vector<PhysReg> &savedGPRs,
                     const std::vector<PhysReg> &savedFPRs,
                     int localFrameSize,
                     bool needPaciasp,
                     const Steps &s) {
    if (needPaciasp)
        s.paciasp();
    s.stpFpLrPre();
    s.movFpSp();
    if (localFrameSize > 0)
        s.subSp(localFrameSize);
    forEachSaveReg(savedGPRs,
                   [&](PhysReg a, PhysReg b) { s.stpGprPair(a, b); },
                   [&](PhysReg a) { s.strGprSingle(a); });
    forEachSaveReg(savedFPRs,
                   [&](PhysReg a, PhysReg b) { s.stpFprPair(a, b); },
                   [&](PhysReg a) { s.strFprSingle(a); });
}

/// @brief Walk the AArch64 epilogue step sequence (reverse of prologue).
/// @details Required Steps members (mirrors iteratePrologue): ldpFprPair,
///          ldrFprSingle, ldpGprPair, ldrGprSingle, addSp(int32_t), ldpFpLrPost(),
///          autiasp(), ret().
template <typename Steps>
void iterateEpilogue(const std::vector<PhysReg> &savedGPRs,
                     const std::vector<PhysReg> &savedFPRs,
                     int localFrameSize,
                     bool needAutiasp,
                     const Steps &s) {
    forEachRestoreReg(savedFPRs,
                      [&](PhysReg a, PhysReg b) { s.ldpFprPair(a, b); },
                      [&](PhysReg a) { s.ldrFprSingle(a); });
    forEachRestoreReg(savedGPRs,
                      [&](PhysReg a, PhysReg b) { s.ldpGprPair(a, b); },
                      [&](PhysReg a) { s.ldrGprSingle(a); });
    if (localFrameSize > 0)
        s.addSp(localFrameSize);
    s.ldpFpLrPost();
    if (needAutiasp)
        s.autiasp();
    s.ret();
}

} // namespace viper::codegen::aarch64
