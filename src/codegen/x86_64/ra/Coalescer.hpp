//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/codegen/x86_64/ra/Coalescer.hpp
// Purpose: Declare the PX_COPY lowering helper used by the linear-scan
//          allocator to coalesce parallel move bundles into executable
//          instruction sequences.
// Key invariants: Coalescing preserves the semantics of the parallel copy by
//                 emitting loads/stores/moves in a deterministic order.
// Ownership/Lifetime: The coalescer operates on Machine IR supplied by the
//                     allocator and does not take ownership of any structures.
// Links: src/codegen/x86_64/ra/Allocator.hpp
//
//===----------------------------------------------------------------------===//

#pragma once

#include "../MachineIR.hpp"

#include <optional>
#include <vector>

namespace viper::codegen::x64::ra
{

class LinearScanAllocator;
class Spiller;

/// @brief Describes the source of a parallel copy operand.
struct CopySource
{
    enum class Kind
    {
        Reg,
        Mem
    };

    Kind kind{Kind::Reg};
    PhysReg reg{PhysReg::RAX};
    int slot{-1};
};

/// @brief Represents a single PX_COPY transfer lowered by the coalescer.
struct CopyTask
{
    enum class DestKind
    {
        Reg,
        Mem
    };

    DestKind destKind{DestKind::Reg};
    RegClass cls{RegClass::GPR};
    PhysReg destReg{PhysReg::RAX};
    int destSlot{-1};
    CopySource src{};
    std::optional<uint16_t> destVReg{};
};

/// @brief Handles lowering of PX_COPY instructions using allocator facilities.
class Coalescer
{
  public:
    /// @brief Construct a coalescer using the given allocator and spiller.
    /// @param allocator The linear scan allocator providing register assignment state.
    /// @param spiller The spiller providing stack slot allocation for spilled values.
    Coalescer(LinearScanAllocator &allocator, Spiller &spiller);

    /// @brief Lower a PX_COPY bundle into concrete move instructions.
    /// @param instr The PX_COPY pseudo-instruction containing parallel copy operands.
    /// @param out Output vector to append the generated concrete move instructions to.
    void lower(const MInstr &instr, std::vector<MInstr> &out);

  private:
    LinearScanAllocator &allocator_;
    Spiller &spiller_;

    /// @brief Emit a single copy task as one or more concrete machine instructions.
    /// @param task The copy task describing source and destination (reg or mem).
    /// @param generated Output vector to append the generated instructions to.
    void emitCopyTask(const CopyTask &task, std::vector<MInstr> &generated);
};

} // namespace viper::codegen::x64::ra
