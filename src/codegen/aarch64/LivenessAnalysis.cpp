//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
///
/// @file LivenessAnalysis.cpp
/// @brief Cross-block liveness analysis for IL to MIR lowering.
///
/// This file implements liveness analysis that identifies IL temporaries whose
/// values must survive across basic block boundaries. Such temporaries require
/// special handling during MIR lowering because the register allocator operates
/// on a per-block basis.
///
/// **What is Cross-Block Liveness?**
/// In SSA form, a temporary may be defined in one basic block and used in
/// another. These "cross-block live" temporaries need their values preserved
/// when control flows between blocks.
///
/// **Problem Statement:**
/// ```
/// block entry:
///   %0 = const.i64 42       ; %0 defined here
///   br loop
///
/// block loop:
///   print_i64 %0            ; %0 used here (different block!)
///   cbr condition, loop, exit
/// ```
///
/// Since the register allocator processes blocks independently, it has no
/// knowledge that %0's value must survive the transition from "entry" to "loop".
/// The liveness analysis identifies such temporaries so the lowering pass can:
/// 1. Allocate spill slots for them
/// 2. Store them before block exits
/// 3. Reload them at block entries
///
/// **Analysis Algorithm:**
/// ```
/// 1. Build definition map: tempId → defining block index
///    - Block parameters are "defined" by their block
///    - Instructions with results define their result temp
///
/// 2. Scan all temp uses in each block
///    - For each temp used in block B
///    - If temp was defined in block D where D ≠ B
///    - Mark temp as "cross-block live"
///
/// 3. Allocate spill slots for cross-block temps
///    - Each cross-block temp gets a stack slot
///    - Slot offset is recorded for later use
/// ```
///
/// **Output: LivenessInfo Structure:**
/// | Field          | Description                                 |
/// |----------------|---------------------------------------------|
/// | tempDefBlock   | Map from temp ID to defining block index    |
/// | crossBlockTemps| Set of temp IDs that are live across blocks |
/// | spillOffset    | Map from temp ID to spill slot offset       |
///
/// **Exclusions:**
/// - Alloca temps are excluded because they represent stack addresses,
///   not values. Their address can be recomputed from the frame pointer.
///
/// **Example:**
/// ```
/// Input IL:
///   block entry:
///     %0 = alloca 8         ; excluded - alloca
///     %1 = const.i64 42     ; cross-block if used in loop
///     %2 = const.i64 1      ; local only
///     br loop
///
///   block loop:
///     %3 = add.i64 %1, %2   ; %1 is cross-block, %2 is local
///     br exit
///
/// Output:
///   crossBlockTemps = {1}   ; only %1 needs spilling
///   spillOffset[1] = -24    ; spill slot for %1
/// ```
///
/// **Integration with Lowering:**
/// The lowering pass uses LivenessInfo to:
/// 1. Insert stores after definitions of cross-block temps
/// 2. Insert reloads before uses in different blocks
/// 3. Skip alloca temps (they don't need value preservation)
///
/// @see LowerILToMIR.cpp For usage during IL lowering
/// @see FrameBuilder.cpp For spill slot allocation
/// @see RegAllocLinear.cpp For per-block register allocation
///
//===----------------------------------------------------------------------===//

#include "LivenessAnalysis.hpp"
#include "il/core/Instr.hpp"

namespace viper::codegen::aarch64
{

LivenessInfo analyzeCrossBlockLiveness(const il::core::Function &fn,
                                       const std::unordered_set<unsigned> &allocaTemps,
                                       FrameBuilder &fb)
{
    LivenessInfo info;

    // ===========================================================================
    // Step 1: Build map of tempId -> defining block index
    // ===========================================================================
    for (std::size_t bi = 0; bi < fn.blocks.size(); ++bi)
    {
        const auto &bb = fn.blocks[bi];
        // Block parameters are "defined" by their block
        for (const auto &param : bb.params)
        {
            info.tempDefBlock[param.id] = bi;
        }
        // Instructions that produce a result
        for (const auto &instr : bb.instructions)
        {
            if (instr.result)
            {
                info.tempDefBlock[*instr.result] = bi;
            }
        }
    }

    // ===========================================================================
    // Step 2: Find temps used in different blocks than their definition
    // ===========================================================================
    // Exclude alloca temps since they don't hold values - they represent stack addresses
    for (std::size_t bi = 0; bi < fn.blocks.size(); ++bi)
    {
        const auto &bb = fn.blocks[bi];
        auto checkValue = [&](const il::core::Value &v)
        {
            if (v.kind == il::core::Value::Kind::Temp)
            {
                // Skip alloca temps - they don't need cross-block spilling
                // Their address is computed from the frame pointer when needed
                if (allocaTemps.contains(v.id))
                    return;
                auto it = info.tempDefBlock.find(v.id);
                if (it != info.tempDefBlock.end() && it->second != bi)
                {
                    // This temp is used in block bi but defined in a different block
                    info.crossBlockTemps.insert(v.id);
                }
            }
        };

        for (const auto &instr : bb.instructions)
        {
            for (const auto &op : instr.operands)
            {
                checkValue(op);
            }
        }

        // Check terminator operands (branch conditions and arguments)
        // The terminator is the last instruction in the block
        if (!bb.instructions.empty())
        {
            const auto &term = bb.instructions.back();
            // Check condition operand for CBr
            if (term.op == il::core::Opcode::CBr && !term.operands.empty())
            {
                checkValue(term.operands[0]); // condition
            }
            // Check return value for Ret
            if (term.op == il::core::Opcode::Ret && !term.operands.empty())
            {
                checkValue(term.operands[0]);
            }
            // Check branch arguments (phi values)
            for (const auto &argList : term.brArgs)
            {
                for (const auto &arg : argList)
                {
                    checkValue(arg);
                }
            }
        }
    }

    // ===========================================================================
    // Step 3: Allocate spill slots for cross-block temps
    // ===========================================================================
    for (unsigned tempId : info.crossBlockTemps)
    {
        int offset = fb.ensureSpill(50000 + tempId); // Use high ID range to avoid conflicts
        info.crossBlockSpillOffset[tempId] = offset;
    }

    return info;
}

} // namespace viper::codegen::aarch64
