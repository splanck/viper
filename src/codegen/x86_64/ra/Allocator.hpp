//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/codegen/x86_64/ra/Allocator.hpp
// Purpose: Declare the linear-scan allocator phase that orchestrates register
//          assignment, spill insertion, and PX_COPY lowering across Machine IR
//          blocks.
// Key invariants: Allocation proceeds in block order using deterministic
//                 register pools sourced from the target description.
// Ownership/Lifetime: The allocator mutates the provided Machine IR function in
//                     place and records the resulting allocation summary.
// Links: src/codegen/x86_64/RegAllocLinear.hpp, src/codegen/x86_64/ra/Spiller.hpp
//
//===----------------------------------------------------------------------===//

#pragma once

#include "../MachineIR.hpp"
#include "../RegAllocLinear.hpp"
#include "../TargetX64.hpp"
#include "LiveIntervals.hpp"
#include "Spiller.hpp"

#include <unordered_map>
#include <vector>

namespace viper::codegen::x64::ra
{

class Coalescer;

/// @brief Allocation state for a single virtual register.
struct VirtualAllocation
{
    bool seen{false};
    RegClass cls{RegClass::GPR};
    bool hasPhys{false};
    PhysReg phys{PhysReg::RAX};
    SpillPlan spill{};
};

/// @brief Core linear-scan allocator working over Machine IR.
class LinearScanAllocator
{
  public:
    LinearScanAllocator(MFunction &func, const TargetInfo &target, const LiveIntervals &intervals);

    [[nodiscard]] AllocationResult run();

  private:
    friend class Coalescer;

    struct OperandRole
    {
        bool isUse{false};
        bool isDef{false};
    };

    struct ScratchRelease
    {
        PhysReg phys{PhysReg::RAX};
        RegClass cls{RegClass::GPR};
    };

    MFunction &func_;
    const TargetInfo &target_;
    const LiveIntervals &intervals_;
    AllocationResult result_{};
    Spiller spiller_{};

    std::unordered_map<uint16_t, VirtualAllocation> states_{};
    std::vector<PhysReg> freeGPR_{};
    std::vector<PhysReg> freeXMM_{};
    std::vector<uint16_t> activeGPR_{};
    std::vector<uint16_t> activeXMM_{};

    void buildPools();
    [[nodiscard]] std::vector<PhysReg> &poolFor(RegClass cls);
    [[nodiscard]] std::vector<uint16_t> &activeFor(RegClass cls);
    [[nodiscard]] VirtualAllocation &stateFor(RegClass cls, uint16_t id);
    void addActive(RegClass cls, uint16_t id);
    void removeActive(RegClass cls, uint16_t id);

    [[nodiscard]] PhysReg takeRegister(RegClass cls, std::vector<MInstr> &prefix);
    void releaseRegister(PhysReg phys, RegClass cls);
    void spillOne(RegClass cls, std::vector<MInstr> &prefix);

    void processBlock(MBasicBlock &block, Coalescer &coalescer);
    void releaseActiveForBlock();
    [[nodiscard]] std::vector<OperandRole> classifyOperands(const MInstr &instr) const;
    void handleOperand(Operand &operand,
                       const OperandRole &role,
                       std::vector<MInstr> &prefix,
                       std::vector<MInstr> &suffix,
                       std::vector<ScratchRelease> &scratch);
    void processRegOperand(OpReg &reg,
                           const OperandRole &role,
                           std::vector<MInstr> &prefix,
                           std::vector<MInstr> &suffix,
                           std::vector<ScratchRelease> &scratch);

    [[nodiscard]] MInstr makeMove(RegClass cls, PhysReg dst, PhysReg src) const;
};

} // namespace viper::codegen::x64::ra
