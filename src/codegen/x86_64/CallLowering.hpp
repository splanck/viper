//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: codegen/x86_64/CallLowering.hpp
// Purpose: Declare utilities that translate high-level call descriptions into
//          Machine IR sequences following SysV AMD64 ABI conventions.
// Key invariants:
//   - Integer arguments use RDI,RSI,RDX,RCX,R8,R9 in order; FP args use
//     XMM0–XMM7; excess arguments spill to the stack.
//   - %rax holds integer returns; %xmm0 holds FP returns.
// Ownership/Lifetime:
//   - Callers retain ownership of Machine IR structures and frame info;
//     lowerCall mutates block in-place.
// Links: codegen/x86_64/CallLowering.cpp,
//        codegen/x86_64/MachineIR.hpp,
//        codegen/x86_64/TargetX64.hpp
//
//===----------------------------------------------------------------------===//

#pragma once

#include "MachineIR.hpp"
#include "TargetX64.hpp"
#include "codegen/common/CallArgLayout.hpp"
#include "codegen/common/CallLoweringPlan.hpp"

#include <cstddef>

namespace zanna::codegen::x64 {

struct FrameInfo;

using CallArg = zanna::codegen::common::CallArg;
using CallArgClass = zanna::codegen::common::CallArgClass;
using CallArgKind = zanna::codegen::common::CallArgKind;
using CallArgLayout = zanna::codegen::common::CallArgLayout;
using CallArgLocation = zanna::codegen::common::CallArgLocation;
using CallArgLayoutConfig = zanna::codegen::common::CallArgLayoutConfig;
using CallLoweringPlan = zanna::codegen::common::CallLoweringPlan;
using CallSlotModel = zanna::codegen::common::CallSlotModel;
using AggregatePassKind = zanna::codegen::common::AggregatePassKind;

/// \brief Emit Machine IR that prepares arguments and issues a call instruction.
///
/// Translates a high-level call description into a sequence of register moves,
/// stack spills, and a CALL pseudo-instruction conforming to the SysV AMD64 ABI.
///
/// @param block     The basic block into which instructions are inserted.
/// @param insertIdx Position within @p block at which the first instruction is placed.
/// @param plan      Describes the callee, arguments, and return convention.
/// @param target    ABI metadata (argument registers, callee-saved sets, etc.).
/// @param frame     Stack frame information updated with any new spill slots.
void lowerCall(MBasicBlock &block,
               std::size_t insertIdx,
               const CallLoweringPlan &plan,
               const TargetInfo &target,
               FrameInfo &frame);

} // namespace zanna::codegen::x64
