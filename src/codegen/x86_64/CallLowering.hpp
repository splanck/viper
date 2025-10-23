// src/codegen/x86_64/CallLowering.hpp
// SPDX-License-Identifier: MIT
//
// Purpose: Declare utilities that translate high-level call descriptions into
//          Machine IR conforming to the SysV AMD64 calling convention.
// Invariants: Lowering operates on Machine IR basic blocks in-place and emits
//             physical register assignments following the ABI ordering.
// Ownership: Callers retain ownership of Machine IR structures and frame
//            metadata. The lowering routines borrow references only.
// Notes: Interface depends on MachineIR.hpp and TargetX64.hpp. FrameInfo is
//        forward declared to avoid circular dependencies.

#pragma once

#include "MachineIR.hpp"
#include "TargetX64.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace viper::codegen::x64 {

struct FrameInfo;

/// \brief Describes a single call argument prior to lowering.
struct CallArg {
  /// \brief Distinguishes between general-purpose and floating-point argument classes.
  enum Kind { GPR, XMM } kind{GPR};
  uint16_t vreg{0U}; ///< Virtual register containing the argument value when not immediate.
  bool isImm{false}; ///< True when the argument should materialise an immediate value.
  int64_t imm{0};    ///< Immediate payload for constant arguments.
};

/// \brief Aggregate plan for lowering a call to a concrete CALL instruction.
struct CallLoweringPlan {
  std::string calleeLabel{};          ///< Symbolic name of the callee.
  std::vector<CallArg> args{};        ///< Ordered list of call arguments.
  bool returnsF64{false};             ///< True when the call returns a double in XMM0.
};

/// \brief Emit Machine IR that prepares arguments and issues a call instruction.
void lowerCall(MBasicBlock& block, std::size_t insertIdx,
               const CallLoweringPlan& plan, const TargetInfo& target,
               FrameInfo& frame);

} // namespace viper::codegen::x64

