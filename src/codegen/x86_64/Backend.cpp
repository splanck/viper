//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
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
#include "CallLowering.hpp"
#include "FrameLowering.hpp"
#include "ISel.hpp"
#include "Peephole.hpp"
#include "RegAllocLinear.hpp"
#include "TargetX64.hpp"
#include "binenc/X64BinaryEncoder.hpp"
#include "codegen/common/objfile/DebugLineTable.hpp"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstring>
#include <sstream>
#include <string_view>
#include <vector>

namespace viper::codegen::x64 {

/// @brief Lower signed division pseudos into guarded IDIV sequences.
/// @details Declared here and implemented in @ref LowerDiv.cpp so the backend
///          facade can invoke the pass without introducing additional headers.
void lowerSignedDivRem(MFunction &fn);

/// @brief Lower overflow-checked arithmetic pseudos into guarded sequences.
/// @details Declared here and implemented in @ref LowerOvf.cpp.
void lowerOverflowOps(MFunction &fn);

namespace {

/// @brief Emit a warning message when unsupported syntax options are requested.
///
/// @details Phase A only supports AT&T syntax emission.  When callers request
///          Intel syntax this helper returns a diagnostic string so the backend
///          can surface the limitation without aborting code generation.
///
/// @param options Code-generation options supplied by the caller.
/// @return Warning string when unsupported options were set; empty view otherwise.
[[nodiscard]] std::string_view syntaxWarning(const CodegenOptions &options) noexcept {
    if (!options.atandtSyntax) {
        return "Phase A: only AT&T syntax emission is implemented.\n";
    }
    return std::string_view{};
}

/// @brief Lower pending call plans onto their corresponding CALL instructions.
///
/// @details Iterates over the machine function's basic blocks, matching each
///          placeholder CALL emitted during IL lowering with its associated
///          @ref CallLoweringPlan. For every match the helper invokes
///          @ref lowerCall to materialise argument moves and update the frame
///          summary with any required outgoing stack space. The traversal skips
///          over the CALL that triggered lowering to avoid reprocessing it once
///          the preparation sequence has been inserted.
///
/// @param func Machine function containing placeholder CALL instructions.
/// @param plans Ordered sequence of call plans captured during IL lowering.
/// @param target Target description providing ABI register order.
/// @param frame Mutable frame summary updated with outgoing argument usage.
void lowerPendingCalls(MFunction &func,
                       const std::vector<CallLoweringPlan> &plans,
                       const TargetInfo &target,
                       FrameInfo &frame) {
    std::size_t planIndex = 0;
    for (auto &block : func.blocks) {
        std::size_t instrIndex = 0;
        while (instrIndex < block.instructions.size()) {
            if (block.instructions[instrIndex].opcode != MOpcode::CALL) {
                ++instrIndex;
                continue;
            }

            if (planIndex >= plans.size()) {
                break;
            }
            const std::size_t beforeSize = block.instructions.size();
            lowerCall(block, instrIndex, plans[planIndex], target, frame);
            const std::size_t afterSize = block.instructions.size();
            const std::size_t inserted = afterSize - beforeSize;
            ++planIndex;

            // The CALL instruction we just processed is now at position (instrIndex + inserted).
            // Skip past it to continue searching for the next CALL.
            instrIndex += inserted + 1;
        }
    }

    assert(planIndex == plans.size() && "call plan count mismatch");
}

} // namespace

/// @brief Execute the per-function Phase A code-generation pipeline.
///
/// @details Converts an IL function into Machine IR, lowers complex operations,
///          performs register allocation, assigns spill slots, and inserts
///          prologue/epilogue code before optionally running peephole
///          optimisations.  The resulting Machine IR is ready for assembly
///          emission and the frame summary reflects the required stack layout.
static void runFunctionPipeline(const ILFunction &ilFunc,
                                LowerILToMIR &lowering,
                                const TargetInfo &target,
                                const CodegenOptions &options,
                                FrameInfo &frame,
                                MFunction &machineFunc) {
    machineFunc = lowering.lower(ilFunc);

    frame = FrameInfo{};
    lowerPendingCalls(machineFunc, lowering.callPlans(), target, frame);

    ISel isel{target};
    isel.lowerArithmetic(machineFunc);
    isel.lowerCompareAndBranch(machineFunc);
    isel.lowerSelect(machineFunc);
    isel.validateSelectLowering(machineFunc);

    lowerSignedDivRem(machineFunc);
    lowerOverflowOps(machineFunc);

    const AllocationResult allocResult = allocate(machineFunc, target);

    assignSpillSlots(machineFunc, target, frame);
    frame.spillAreaGPR = std::max(frame.spillAreaGPR, allocResult.spillSlotsGPR * kSlotSizeBytes);
    frame.spillAreaXMM = std::max(frame.spillAreaXMM, allocResult.spillSlotsXMM * kSlotSizeBytes);
    // Phase A: outgoing argument area and dynamic allocations are not tracked yet.

    insertPrologueEpilogue(machineFunc, target, frame);

    // Peephole optimizations run at optimize level 1 or higher
    if (options.optimizeLevel >= 1) {
        runPeepholes(machineFunc);
    }
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
static CodegenResult emitModuleImpl(const std::vector<ILFunction> &functions,
                                    const CodegenOptions &options) {
    CodegenResult result{};

    std::ostringstream asmStream{};
    std::ostringstream errorStream{};

    if (const auto warning = syntaxWarning(options); !warning.empty()) {
        errorStream << warning;
    }

    const TargetInfo &target = hostTarget();
    AsmEmitter::RoDataPool roData{};
    LowerILToMIR lowering{target, roData};
    AsmEmitter emitter{roData};

    for (std::size_t index = 0; index < functions.size(); ++index) {
        const auto &func = functions[index];

        FrameInfo frame{};
        MFunction machineFunc{};
        runFunctionPipeline(func, lowering, target, options, frame, machineFunc);

        emitter.emitFunction(asmStream, machineFunc, target);
        if (index + 1U < functions.size()) {
            asmStream << '\n';
        }
    }

    emitter.emitRoData(asmStream);

    // Mark the stack as non-executable on ELF targets.  Without this directive
    // the GNU linker defaults to an executable stack, triggering a warning and
    // creating a security issue.
#if !defined(__APPLE__) && !defined(_WIN32)
    asmStream << "\n.section .note.GNU-stack,\"\",@progbits\n";
#endif

    result.asmText = asmStream.str();
    result.errors = errorStream.str();

    // Phase A: diagnostics only capture unsupported options; individual pass failures
    // are not surfaced yet.

    return result;
}

/// @brief Convenience wrapper that emits assembly for a single IL function.
///
/// @details Forwards to @ref emitModuleImpl after wrapping the function in a
///          single-element vector so the main implementation can be reused.
///
/// @param func IL function to translate.
/// @param options Backend configuration to honour.
/// @return Assembly text and diagnostics for the provided function.
CodegenResult emitFunctionToAssembly(const ILFunction &func, const CodegenOptions &options) {
    CodegenResult result{};

    std::ostringstream asmStream{};
    std::ostringstream errorStream{};

    if (const auto warning = syntaxWarning(options); !warning.empty()) {
        errorStream << warning;
    }

    const TargetInfo &target = hostTarget();
    AsmEmitter::RoDataPool roData{};
    LowerILToMIR lowering{target, roData};
    AsmEmitter emitter{roData};

    FrameInfo frame{};
    MFunction machineFunc{};
    runFunctionPipeline(func, lowering, target, options, frame, machineFunc);

    emitter.emitFunction(asmStream, machineFunc, target);
    emitter.emitRoData(asmStream);

#if !defined(__APPLE__) && !defined(_WIN32)
    asmStream << "\n.section .note.GNU-stack,\"\",@progbits\n";
#endif

    result.asmText = asmStream.str();
    result.errors = errorStream.str();

    return result;
}

/// @brief Emit assembly for every function in an IL module.
///
/// @details Delegates to @ref emitModuleImpl after copying the module's
///          functions so they can be processed as a contiguous list.
///
/// @param mod IL module containing the functions to translate.
/// @param options Backend configuration supplied by the caller.
/// @return Assembly text and diagnostics for the entire module.
CodegenResult emitModuleToAssembly(const ILModule &mod, const CodegenOptions &options) {
    return emitModuleImpl(mod.funcs, options);
}

BinaryEmitResult emitModuleToBinary(const ILModule &mod, const CodegenOptions &opt, bool isDarwin) {
    BinaryEmitResult result{};
    std::ostringstream errorStream{};

    if (const auto warning = syntaxWarning(opt); !warning.empty()) {
        // Syntax warnings don't apply to binary emission, but keep parity.
    }

    const TargetInfo &target = hostTarget();
    AsmEmitter::RoDataPool roData{};
    LowerILToMIR lowering{target, roData};
    binenc::X64BinaryEncoder encoder{};

    // Set up debug line table for address→line mapping.
    DebugLineTable debugLines;
    debugLines.addFile("<source>");
    encoder.setDebugLineTable(&debugLines);

    for (const auto &func : mod.funcs) {
        FrameInfo frame{};
        MFunction machineFunc{};
        runFunctionPipeline(func, lowering, target, opt, frame, machineFunc);

        // Emit each function into its own CodeSection for per-function dead stripping.
        result.textSections.emplace_back();
        binenc::X64BinaryEncoder funcEncoder;
        funcEncoder.encodeFunction(
            machineFunc, result.textSections.back(), result.rodata, isDarwin);

        // Also emit into merged text for backward compatibility (symbol extraction).
        encoder.encodeFunction(machineFunc, result.text, result.rodata, isDarwin);
    }

    // Emit rodata: string literals and f64 constants from RoDataPool into the
    // rodata CodeSection so they can be referenced via RIP-relative LEA.
    for (int i = 0; i < static_cast<int>(roData.stringCount()); ++i) {
        std::string label = roData.stringLabel(i);
        if (isDarwin)
            label = "_" + label;
        result.rodata.defineSymbol(
            label, objfile::SymbolBinding::Local, objfile::SymbolSection::Rodata);
        const auto &bytes = roData.stringBytes(i);
        result.rodata.emitBytes(bytes.data(), bytes.size());
    }
    // Align to 8 before f64 constants.
    if (roData.f64Count() > 0)
        result.rodata.alignTo(8);
    for (int i = 0; i < static_cast<int>(roData.f64Count()); ++i) {
        std::string label = roData.f64Label(i);
        if (isDarwin)
            label = "_" + label;
        result.rodata.defineSymbol(
            label, objfile::SymbolBinding::Local, objfile::SymbolSection::Rodata);
        double val = roData.f64Value(i);
        uint64_t bits;
        std::memcpy(&bits, &val, sizeof(bits));
        result.rodata.emit64LE(bits);
    }

    // Encode DWARF .debug_line if any entries were recorded.
    if (!debugLines.empty())
        result.debugLineData = debugLines.encodeDwarf5(8);

    result.errors = errorStream.str();
    return result;
}

} // namespace viper::codegen::x64
