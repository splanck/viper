//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: codegen/aarch64/passes/LoweringPass.cpp
// Purpose: IL→MIR lowering pass for the AArch64 modular pipeline.
//
// Responsibilities:
//   1. Build the per-module RodataPool from string globals.
//   2. For each IL function, lower it to MIR via LowerILToMIR.
//   3. Sanitize basic-block labels (hyphens → underscores, uniquify across
//      multi-function modules to prevent label collisions in the assembler).
//   4. Remap AdrPage/AddPageOff label operands to pooled rodata labels.
//
//===----------------------------------------------------------------------===//

#include "codegen/aarch64/passes/LoweringPass.hpp"

#include "codegen/aarch64/LowerILToMIR.hpp"
#include "codegen/common/LabelUtil.hpp"

#include <unordered_map>

namespace viper::codegen::aarch64::passes
{

bool LoweringPass::run(AArch64Module &module, Diagnostics &diags)
{
    if (!module.ilMod || !module.ti)
    {
        diags.error("LoweringPass: ilMod and ti must be non-null");
        return false;
    }

    const il::core::Module &ilMod = *module.ilMod;
    const TargetInfo &ti = *module.ti;

    // Build the rodata pool from all string globals in the module.
    module.rodataPool.buildFromModule(ilMod);
    const auto &n2l = module.rodataPool.nameToLabel();

    LowerILToMIR lowerer{ti};
    const bool uniquify = (ilMod.functions.size() > 1);

    for (const auto &fn : ilMod.functions)
    {
        MFunction mir = lowerer.lowerFunction(fn);

        // --- Label sanitization: hyphens → underscores, optional suffix ----
        using viper::codegen::common::sanitizeLabel;
        std::unordered_map<std::string, std::string> bbMap;
        bbMap.reserve(mir.blocks.size());

        const std::string suffix = uniquify ? (std::string("_") + fn.name) : std::string{};

        for (auto &bb : mir.blocks)
        {
            const std::string old = bb.name;
            const std::string neu = sanitizeLabel(old, suffix);
            bbMap.emplace(old, neu);
            bb.name = neu;
        }

        // Remap branch target labels to their sanitized names.
        for (auto &bb : mir.blocks)
        {
            for (auto &mi : bb.instrs)
            {
                auto remapBB = [&](std::string &lbl)
                {
                    auto it = bbMap.find(lbl);
                    if (it != bbMap.end())
                        lbl = it->second;
                };

                switch (mi.opc)
                {
                    case MOpcode::Br:
                        if (!mi.ops.empty() && mi.ops[0].kind == MOperand::Kind::Label)
                            remapBB(mi.ops[0].label);
                        break;
                    case MOpcode::BCond:
                        if (mi.ops.size() >= 2 && mi.ops[1].kind == MOperand::Kind::Label)
                            remapBB(mi.ops[1].label);
                        break;
                    default:
                        break;
                }

                // --- Rodata label remapping: IL global names → pool labels --
                switch (mi.opc)
                {
                    case MOpcode::AdrPage:
                        if (mi.ops.size() >= 2 && mi.ops[1].kind == MOperand::Kind::Label)
                        {
                            auto it = n2l.find(mi.ops[1].label);
                            if (it != n2l.end())
                                mi.ops[1].label = it->second;
                        }
                        break;
                    case MOpcode::AddPageOff:
                        if (mi.ops.size() >= 3 && mi.ops[2].kind == MOperand::Kind::Label)
                        {
                            auto it = n2l.find(mi.ops[2].label);
                            if (it != n2l.end())
                                mi.ops[2].label = it->second;
                        }
                        break;
                    default:
                        break;
                }
            }
        }

        module.mir.push_back(std::move(mir));
    }

    return true;
}

} // namespace viper::codegen::aarch64::passes
