//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Implements the top-level x86-64 backend facade responsible for sequencing the
// Phase A pipeline.  The translation unit orchestrates lowering from IL to
// Machine IR, register allocation, frame layout, peephole optimisations, and
// final assembly emission while gathering diagnostics about unsupported
// features.
//
// Each IL function is lowered independently to keep pass interactions simple.
// The backend preserves module function order, reuses shared helpers such as
// @ref LowerILToMIR and @ref AsmEmitter, and surfaces warnings when callers
// request configuration that Phase A does not yet implement.
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Coordinates the Phase A x86-64 code-generation pipeline.
/// @details Provides utility routines that execute the per-function pipeline,
///          emit module-level assembly, and surface configuration diagnostics to
///          the caller.  The implementation focuses on clarity over maximal
///          optimisation, matching Phase A's educational goals.

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

/// @brief Lower signed division pseudos into guarded IDIV sequences.
/// @details Declared here and implemented in @ref LowerDiv.cpp so the backend
///          facade can invoke the pass without introducing additional headers.
void lowerSignedDivRem(MFunction &fn);

namespace
{

constexpr int kSpillSlotBytes = 8;

/// @brief Emit a warning message when unsupported syntax options are requested.
///
/// @details Phase A only supports AT&T syntax emission.  When callers request
///          Intel syntax this helper returns a diagnostic string so the backend
///          can surface the limitation without aborting code generation.
///
/// @param options Code-generation options supplied by the caller.
/// @return Warning string when unsupported options were set; empty view otherwise.
[[nodiscard]] std::string_view syntaxWarning(const CodegenOptions &options) noexcept
{
    if (!options.atandtSyntax)
    {
        return "Phase A: only AT&T syntax emission is implemented.\n";
    }
    return std::string_view{};
}

/// @brief Execute the per-function Phase A code-generation pipeline.
///
/// @details Converts an IL function into Machine IR, lowers complex operations,
///          performs register allocation, assigns spill slots, and inserts
///          prologue/epilogue code before running peephole optimisations.  The
///          resulting Machine IR is ready for assembly emission and the frame
///          summary reflects the required stack layout.
///
/// @param ilFunc IL function being translated.
/// @param lowering Shared lowering context reused across functions.
/// @param target Target description supplying register orderings and ABI facts.
/// @param frame Output parameter that records frame layout requirements.
/// @param machineFunc Output parameter receiving the transformed Machine IR.
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

/// @brief Emit assembly for a collection of IL functions.
///
/// @details Applies the per-function pipeline to each function in order,
///          collects emitted assembly into a single stream, and accumulates
///          diagnostics such as syntax warnings.  The helper underpins both the
///          single-function and whole-module entry points.
///
/// @param functions IL functions to translate.
/// @param options Backend configuration supplied by the caller.
/// @return Result structure containing assembly text and diagnostic messages.
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
    AsmEmitter::RoDataPool roData{};
    LowerILToMIR lowering{target, roData};
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

/// @brief Convenience wrapper that emits assembly for a single IL function.
///
/// @details Forwards to @ref emitModuleImpl after wrapping the function in a
///          single-element vector so the main implementation can be reused.
///
/// @param func IL function to translate.
/// @param options Backend configuration to honour.
/// @return Assembly text and diagnostics for the provided function.
CodegenResult emitFunctionToAssembly(const ILFunction &func, const CodegenOptions &options)
{
    std::vector<ILFunction> functions{};
    functions.push_back(func);
    return emitModuleImpl(functions, options);
}

/// @brief Emit assembly for every function in an IL module.
///
/// @details Delegates to @ref emitModuleImpl after copying the module's
///          functions so they can be processed as a contiguous list.
///
/// @param mod IL module containing the functions to translate.
/// @param options Backend configuration supplied by the caller.
/// @return Assembly text and diagnostics for the entire module.
CodegenResult emitModuleToAssembly(const ILModule &mod, const CodegenOptions &options)
{
    return emitModuleImpl(mod.funcs, options);
}

} // namespace viper::codegen::x64
