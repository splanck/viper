//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: codegen/aarch64/CodegenPipeline.cpp
// Purpose: Implements the modular AArch64 code-generation pipeline by wiring
//          all AArch64 passes through the PassManager in the correct order.
//
// Key invariants:
//   - Pass order: Lowering → RegAlloc → Scheduler → BlockLayout → Peephole → Emit.
//   - MIR dump hooks fire between passes when requested via PipelineOptions.
//   - All passes implement the Pass<AArch64Module> interface.
//
// Ownership/Lifetime:
//   - The pipeline does not own the AArch64Module; it is borrowed from the caller.
//
// Links: codegen/aarch64/CodegenPipeline.hpp (public API),
//        codegen/aarch64/passes/ (individual pass implementations)
//
//===----------------------------------------------------------------------===//

#include "codegen/aarch64/CodegenPipeline.hpp"

#include "codegen/aarch64/MachineIR.hpp"
#include "codegen/aarch64/passes/EmitPass.hpp"
#include "codegen/aarch64/passes/LoweringPass.hpp"
#include "codegen/aarch64/passes/PeepholePass.hpp"
#include "codegen/aarch64/passes/RegAllocPass.hpp"

#include <iostream>

namespace viper::codegen::aarch64
{

/// @brief Dump all MIR functions to stderr with a header tag.
static void dumpMir(const passes::AArch64Module &module, const char *tag)
{
    for (const auto &fn : module.mir)
    {
        std::cerr << "=== MIR " << tag << ": " << fn.name << " ===\n";
        std::cerr << toString(fn) << "\n";
    }
}

bool runCodegenPipeline(passes::AArch64Module &module, const PipelineOptions &opts)
{
    passes::Diagnostics diags;

    // Phase 1: Lowering (IL → MIR + rodata pool + label sanitization)
    {
        passes::LoweringPass pass;
        if (!pass.run(module, diags))
        {
            diags.flush(std::cerr);
            return false;
        }
    }

    // Detect leaf functions: scan for Bl/Blr instructions, but skip trap blocks
    // (e.g. .Ltrap_ovf_*) because those are cold, noreturn paths that should not
    // force the function to save/restore callee-saved registers on the hot path.
    for (auto &fn : module.mir)
    {
        fn.isLeaf = true;
        for (const auto &bb : fn.blocks)
        {
            // Skip trap blocks — they only contain a noreturn call to rt_trap
            // and should not affect leaf-function classification.
            if (bb.name.find(".Ltrap_") == 0)
                continue;

            for (const auto &mi : bb.instrs)
            {
                if (mi.opc == MOpcode::Bl || mi.opc == MOpcode::Blr)
                {
                    fn.isLeaf = false;
                    break;
                }
            }
            if (!fn.isLeaf)
                break;
        }
    }

    if (opts.dumpMirBeforeRA)
        dumpMir(module, "before RA");

    // Phase 2: Register Allocation
    {
        passes::RegAllocPass pass;
        if (!pass.run(module, diags))
        {
            diags.flush(std::cerr);
            return false;
        }
    }

    if (opts.dumpMirAfterRA)
        dumpMir(module, "after RA");

    // Phase 3: Peephole Optimizations + Prune Unused Callee-Saved Regs
    // NOTE: SchedulerPass and BlockLayoutPass are available but not yet enabled
    // in the default pipeline to maintain output compatibility with existing
    // golden tests.  They can be enabled via a future --schedule flag.
    {
        passes::PeepholePass pass;
        if (!pass.run(module, diags))
        {
            diags.flush(std::cerr);
            return false;
        }
    }

    if (opts.dumpMirAfterRA)
        dumpMir(module, "after peephole");

    // Phase 6: Assembly Emission
    {
        passes::EmitPass pass;
        if (!pass.run(module, diags))
        {
            diags.flush(std::cerr);
            return false;
        }
    }

    diags.flush(std::cerr);
    return true;
}

} // namespace viper::codegen::aarch64
