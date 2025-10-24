// src/codegen/x86_64/Backend.cpp
// SPDX-License-Identifier: MIT
//
// Purpose: Implement the x86-64 backend facade that sequences the Phase A
//          pipeline to transform IL modules into assembly text.
// Invariants: Pipeline stages run in a fixed order and operate on fresh Machine
//             IR instances per function. Generated assembly preserves emission
//             order of functions as provided by the IL module.
// Ownership: The facade borrows IL inputs and returns assembly text along with
//            accumulated diagnostics.
// Notes: Phase A intentionally omits advanced error handling and non-AT&T
//        syntax support; these are documented placeholders for future work.

#include "Backend.hpp"

#include "AsmEmitter.hpp"
#include "FrameLowering.hpp"
#include "ISel.hpp"
#include "Peephole.hpp"
#include "RegAllocLinear.hpp"
#include "TargetX64.hpp"

#include <algorithm>
#include <cstddef>
#include <sstream>
#include <string_view>
#include <vector>

namespace viper::codegen::x64
{

void lowerSignedDivRem(MFunction &fn); // Provided by LowerDiv.cpp.

namespace
{

constexpr int kSpillSlotBytes = 8;

[[nodiscard]] std::string_view syntaxWarning(const CodegenOptions &options) noexcept
{
    if (!options.atandtSyntax)
    {
        return "Phase A: only AT&T syntax emission is implemented.\n";
    }
    return std::string_view{};
}

void runFunctionPipeline(const ILFunction &ilFunc,
                         LowerILToMIR &lowering,
                         const TargetInfo &target,
                         FrameInfo &frame,
                         MFunction &machineFunc)
{
    machineFunc = lowering.lower(ilFunc);

    ISel isel{target};
    isel.lowerArithmetic(machineFunc);
    isel.lowerCompareAndBranch(machineFunc);
    isel.lowerSelect(machineFunc);

    lowerSignedDivRem(machineFunc);

    const AllocationResult allocResult = allocate(machineFunc, target);

    frame = FrameInfo{};
    assignSpillSlots(machineFunc, target, frame);
    frame.spillAreaGPR = std::max(frame.spillAreaGPR, allocResult.spillSlotsGPR * kSpillSlotBytes);
    frame.spillAreaXMM = std::max(frame.spillAreaXMM, allocResult.spillSlotsXMM * kSpillSlotBytes);
    // Phase A: outgoing argument area and dynamic allocations are not tracked yet.

    insertPrologueEpilogue(machineFunc, target, frame);

    runPeepholes(machineFunc);
}

CodegenResult emitModuleImpl(const std::vector<ILFunction> &functions, const CodegenOptions &options)
{
    CodegenResult result{};

    std::ostringstream asmStream{};
    std::ostringstream errorStream{};

    if (const auto warning = syntaxWarning(options); !warning.empty())
    {
        errorStream << warning;
    }

    const TargetInfo &target = sysvTarget();
    LowerILToMIR lowering{target};
    AsmEmitter::RoDataPool roData{};
    AsmEmitter emitter{roData};

    for (std::size_t index = 0; index < functions.size(); ++index)
    {
        const auto &func = functions[index];

        FrameInfo frame{};
        MFunction machineFunc{};
        runFunctionPipeline(func, lowering, target, frame, machineFunc);

        emitter.emitFunction(asmStream, machineFunc, target);
        if (index + 1U < functions.size())
        {
            asmStream << '\n';
        }
    }

    emitter.emitRoData(asmStream);

    result.asmText = asmStream.str();
    result.errors = errorStream.str();

    // Phase A: diagnostics only capture unsupported options; individual pass failures
    // are not surfaced yet.

    return result;
}

} // namespace

CodegenResult emitFunctionToAssembly(const ILFunction &func, const CodegenOptions &options)
{
    std::vector<ILFunction> functions{};
    functions.push_back(func);
    return emitModuleImpl(functions, options);
}

CodegenResult emitModuleToAssembly(const ILModule &mod, const CodegenOptions &options)
{
    return emitModuleImpl(mod.funcs, options);
}

} // namespace viper::codegen::x64
