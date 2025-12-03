//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: codegen/aarch64/InstrLowering.hpp
// Purpose: Opcode-specific lowering handlers for IL->MIR conversion.
//
// This header declares functions that lower individual IL opcodes to MIR.
// Each handler is responsible for a specific opcode or group of related opcodes.
//
//===----------------------------------------------------------------------===//

#pragma once

#include "LoweringContext.hpp"
#include "MachineIR.hpp"
#include "il/core/Instr.hpp"

namespace viper::codegen::aarch64
{

//===----------------------------------------------------------------------===//
// Global temp registry for FPR tracking (thread-local)
//===----------------------------------------------------------------------===//

/// @brief Maps IL temp id to its register class (GPR or FPR).
/// @details This thread-local state is used during lowering to track which
///          temporaries hold floating-point values vs integer values.
///          It is cleared at the start of each lowerFunction() call.
extern thread_local std::unordered_map<unsigned, RegClass> g_tempRegClass;

//===----------------------------------------------------------------------===//
// Value Materialization
//===----------------------------------------------------------------------===//

/// @brief Materialize an IL value into a vreg, appending MIR to the output block.
/// @param v The IL value to materialize
/// @param bb The current IL basic block (for parameter lookups)
/// @param ti Target info for ABI register mappings
/// @param fb Frame builder for stack allocation
/// @param out The output MIR basic block
/// @param tempVReg Map from temp ID to vreg ID
/// @param nextVRegId Counter for vreg ID allocation
/// @param outVReg [out] The vreg ID assigned to this value
/// @param outCls [out] The register class of the vreg
/// @returns true if successful, false if the value couldn't be materialized
bool materializeValueToVReg(const il::core::Value &v,
                            const il::core::BasicBlock &bb,
                            const TargetInfo &ti,
                            FrameBuilder &fb,
                            MBasicBlock &out,
                            std::unordered_map<unsigned, uint16_t> &tempVReg,
                            uint16_t &nextVRegId,
                            uint16_t &outVReg,
                            RegClass &outCls);

// Wrapper that uses LoweringContext
inline bool materializeValueToVReg(const il::core::Value &v,
                                   const il::core::BasicBlock &bb,
                                   LoweringContext &ctx,
                                   MBasicBlock &out,
                                   uint16_t &outVReg,
                                   RegClass &outCls)
{
    return materializeValueToVReg(v, bb, ctx.ti, ctx.fb, out, ctx.tempVReg, ctx.nextVRegId, outVReg,
                                  outCls);
}

//===----------------------------------------------------------------------===//
// Call Lowering
//===----------------------------------------------------------------------===//

/// @brief Lower a Call instruction to MIR.
/// @param callI The IL call instruction
/// @param bb The current IL basic block
/// @param ti Target info
/// @param fb Frame builder
/// @param out The output MIR basic block
/// @param seq [out] The lowered call sequence (prefix, call, postfix)
/// @param tempVReg Map from temp ID to vreg ID
/// @param nextVRegId Counter for vreg ID allocation
/// @returns true if successful
bool lowerCallWithArgs(const il::core::Instr &callI,
                       const il::core::BasicBlock &bb,
                       const TargetInfo &ti,
                       FrameBuilder &fb,
                       MBasicBlock &out,
                       LoweredCall &seq,
                       std::unordered_map<unsigned, uint16_t> &tempVReg,
                       uint16_t &nextVRegId);

//===----------------------------------------------------------------------===//
// Integer Arithmetic
//===----------------------------------------------------------------------===//

/// @brief Lower signed remainder with divide-by-zero check (srem.chk0).
/// @details Generates: cbz rhs, trap; sdiv tmp, lhs, rhs; msub dst, tmp, rhs, lhs
bool lowerSRemChk0(const il::core::Instr &ins,
                   const il::core::BasicBlock &bb,
                   LoweringContext &ctx,
                   MBasicBlock &out);

/// @brief Lower signed division with divide-by-zero check (sdiv.chk0).
bool lowerSDivChk0(const il::core::Instr &ins,
                   const il::core::BasicBlock &bb,
                   LoweringContext &ctx,
                   MBasicBlock &out);

/// @brief Lower unsigned division with divide-by-zero check (udiv.chk0).
bool lowerUDivChk0(const il::core::Instr &ins,
                   const il::core::BasicBlock &bb,
                   LoweringContext &ctx,
                   MBasicBlock &out);

/// @brief Lower unsigned remainder with divide-by-zero check (urem.chk0).
bool lowerURemChk0(const il::core::Instr &ins,
                   const il::core::BasicBlock &bb,
                   LoweringContext &ctx,
                   MBasicBlock &out);

//===----------------------------------------------------------------------===//
// Type Conversions
//===----------------------------------------------------------------------===//

/// @brief Lower zext1/trunc1 (boolean extension/truncation).
bool lowerZext1Trunc1(const il::core::Instr &ins,
                      const il::core::BasicBlock &bb,
                      LoweringContext &ctx,
                      MBasicBlock &out);

/// @brief Lower cast.si_narrow.chk / cast.ui_narrow.chk.
bool lowerNarrowingCast(const il::core::Instr &ins,
                        const il::core::BasicBlock &bb,
                        LoweringContext &ctx,
                        MBasicBlock &out);

/// @brief Lower sitofp (signed int to float).
bool lowerSitofp(const il::core::Instr &ins,
                 const il::core::BasicBlock &bb,
                 LoweringContext &ctx,
                 MBasicBlock &out);

/// @brief Lower fptosi (float to signed int).
bool lowerFptosi(const il::core::Instr &ins,
                 const il::core::BasicBlock &bb,
                 LoweringContext &ctx,
                 MBasicBlock &out);

//===----------------------------------------------------------------------===//
// Floating-Point Operations
//===----------------------------------------------------------------------===//

/// @brief Lower FP arithmetic (fadd, fsub, fmul, fdiv).
bool lowerFpArithmetic(const il::core::Instr &ins,
                       const il::core::BasicBlock &bb,
                       LoweringContext &ctx,
                       MBasicBlock &out);

/// @brief Lower FP comparisons (fcmp.eq, fcmp.ne, etc.).
bool lowerFpCompare(const il::core::Instr &ins,
                    const il::core::BasicBlock &bb,
                    LoweringContext &ctx,
                    MBasicBlock &out);

} // namespace viper::codegen::aarch64
