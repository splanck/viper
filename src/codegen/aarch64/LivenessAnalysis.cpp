//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: codegen/aarch64/LivenessAnalysis.cpp
// Purpose: Cross-block liveness analysis for IL->MIR lowering.
//
// This file contains the implementation of cross-block liveness analysis,
// which identifies temps that need spill/reload handling due to being
// used in different blocks than where they are defined.
//
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
