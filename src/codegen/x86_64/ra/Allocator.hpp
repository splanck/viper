//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
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

#include <bitset>
#include <deque>
#include <unordered_map>
#include <unordered_set>
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
    /// @brief Construct a linear-scan allocator for the supplied Machine IR function.
    /// @details Captures references to the function, target description, and precomputed live
    ///          intervals so that the allocator can walk instructions in block order without
    ///          recomputing analysis.  Ownership of the referenced data remains with the caller.
    LinearScanAllocator(MFunction &func, const TargetInfo &target, const LiveIntervals &intervals);

    /// @brief Execute the allocation algorithm over the bound Machine IR function.
    /// @details Initialises register pools, walks each basic block in dominance order, assigns
    ///          physical registers or spill slots as required, and records the resulting mapping
    ///          inside @ref AllocationResult for downstream passes.
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
    std::deque<PhysReg> freeGPR_{}; ///< O(1) pop_front for register allocation.
    std::deque<PhysReg> freeXMM_{}; ///< O(1) pop_front for register allocation.
    /// @brief Active virtual registers in GPR class. Uses unordered_set for O(1) insert/erase.
    std::unordered_set<uint16_t> activeGPR_{};
    /// @brief Active virtual registers in XMM class. Uses unordered_set for O(1) insert/erase.
    std::unordered_set<uint16_t> activeXMM_{};
    std::size_t currentInstrIdx_{0};         ///< Current instruction index for liveness checks.
    std::vector<PhysReg> reservedForCall_{}; ///< Arg registers reserved during call setup.

    /// @brief Precomputed bitset of caller-saved GPR registers for O(1) lookup.
    /// @details Indexed by static_cast<int>(PhysReg). Avoids linear search in CALL handling.
    std::bitset<32> callerSavedGPRBits_{};

    /// @brief Precomputed bitset of caller-saved FPR registers for O(1) lookup.
    std::bitset<32> callerSavedFPRBits_{};

    /// @brief Populate the free-register pools from the target description.
    /// @details Queries the @ref TargetInfo to enumerate allocatable registers for each class
    ///          and seeds the internal free lists that drive linear-scan allocation.
    void buildPools();

    /// @brief Retrieve the free list associated with a register class.
    /// @details Returns either the general-purpose or floating-point pool so helpers can push
    ///          and pop physical registers while allocating or releasing values.
    [[nodiscard]] std::deque<PhysReg> &poolFor(RegClass cls);

    /// @brief Retrieve the currently active interval set for a register class.
    /// @details Provides access to the set that tracks active virtual register identifiers so
    ///          expiration checks can scan the appropriate list.
    [[nodiscard]] std::unordered_set<uint16_t> &activeFor(RegClass cls);

    /// @brief Lookup or initialise allocation state for a virtual register.
    /// @details Fetches the @ref VirtualAllocation record keyed by @p id, initialising defaults
    ///          when the register is observed for the first time so state mutations remain
    ///          centralised.
    [[nodiscard]] VirtualAllocation &stateFor(RegClass cls, uint16_t id);

    /// @brief Mark a virtual register as active within its class list.
    /// @details Inserts the identifier into the appropriate active set so future spill decisions
    ///          can consider its live interval ordering.
    void addActive(RegClass cls, uint16_t id);

    /// @brief Remove a virtual register from the active set once it expires.
    /// @details Updates bookkeeping so the allocator knows the associated physical register can
    ///          be released back to the free pool.
    void removeActive(RegClass cls, uint16_t id);

    /// @brief Acquire a physical register for the next live virtual value.
    /// @details Removes a register from the free pool when available; otherwise triggers a spill
    ///          via @ref spillOne to free space before returning the assigned physical register.
    [[nodiscard]] PhysReg takeRegister(RegClass cls, std::vector<MInstr> &prefix);

    /// @brief Return a physical register to the free pool once it becomes idle.
    /// @details Pushes @p phys back into the free list for @p cls so subsequent allocations can
    ///          reuse it.
    void releaseRegister(PhysReg phys, RegClass cls);

    /// @brief Release registers for vregs whose live intervals have ended.
    /// @details At each instruction, checks all active vregs and releases those whose
    ///          interval ends at or before the current instruction.
    void expireIntervals();

    /// @brief Spill the active interval with the furthest end point.
    /// @details Selects a victim live range in @p cls, emits the necessary spill code into
    ///          @p prefix, updates state bookkeeping, and frees the associated physical register
    ///          for reuse.
    void spillOne(RegClass cls, std::vector<MInstr> &prefix);

    /// @brief Allocate registers for every instruction in the provided block.
    /// @details Walks the block's instructions in order, handles live range transitions, and
    ///          cooperates with the coalescer to apply PX_COPY eliminations.
    void processBlock(MBasicBlock &block, Coalescer &coalescer);

    /// @brief Release or spill registers at block boundaries.
    /// @details Clears the active sets after a block finishes. Values that are live across
    ///          block boundaries are spilled so successor blocks can reload them.
    /// @param block The block that was just processed, to which spills are appended.
    void releaseActiveForBlock(MBasicBlock &block);

    /// @brief Classify each operand of an instruction as a use, def, or both.
    /// @details Produces a parallel vector describing operand roles so subsequent handling can
    ///          decide whether to allocate, retain, or release registers around the instruction.
    [[nodiscard]] std::vector<OperandRole> classifyOperands(const MInstr &instr) const;

    /// @brief Handle an instruction operand, inserting spills or reloads as required.
    /// @details Depending on @p role, materialises register operands, schedules spill code into
    ///          @p prefix or @p suffix, and records scratch registers that must be released after
    ///          the instruction executes.
    void handleOperand(Operand &operand,
                       const OperandRole &role,
                       std::vector<MInstr> &prefix,
                       std::vector<MInstr> &suffix,
                       std::vector<ScratchRelease> &scratch);

    /// @brief Handle a register operand specifically, mapping it to a physical register.
    /// @details Applies the same logic as @ref handleOperand but operates directly on the
    ///          strongly typed @ref OpReg, updating allocation state and scheduling required
    ///          moves.
    void processRegOperand(OpReg &reg,
                           const OperandRole &role,
                           std::vector<MInstr> &prefix,
                           std::vector<MInstr> &suffix,
                           std::vector<ScratchRelease> &scratch);

    /// @brief Construct a MOV instruction that copies between physical registers.
    /// @details Used when spilling or resolving PX_COPY nodes; the helper populates opcode and
    ///          operand fields according to the register class being moved.
    [[nodiscard]] MInstr makeMove(RegClass cls, PhysReg dst, PhysReg src) const;

    /// @brief Check if a physical register is an argument register for the current ABI.
    [[nodiscard]] bool isArgumentRegister(PhysReg reg) const;

    /// @brief Reserve an argument register during call setup to prevent spill reloads from using
    /// it.
    void reserveForCall(PhysReg reg);

    /// @brief Release all reserved argument registers back to the pool after a CALL.
    void releaseCallReserved();
};

} // namespace viper::codegen::x64::ra
