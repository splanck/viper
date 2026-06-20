//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: codegen/x86_64/Backend.cpp
// Purpose: Top-level x86-64 backend facade that sequences the Phase A pipeline.
//          Orchestrates IL→MIR lowering, register allocation, frame layout,
//          peephole optimisations, and final assembly/binary emission.
// Key invariants:
//   - Each IL function is lowered independently to keep pass interactions simple.
//   - Module function order is preserved throughout the pipeline.
//   - Unsupported configuration options are surfaced as diagnostic warnings.
// Ownership/Lifetime:
//   - Borrows caller-provided IL modules and TargetInfo; all MIR state is
//     stack-local and discarded after emission.
// Links: codegen/x86_64/Backend.hpp,
//        codegen/x86_64/LowerILToMIR.hpp,
//        codegen/x86_64/AsmEmitter.hpp,
//        codegen/x86_64/passes/PassManager.hpp
//
//===----------------------------------------------------------------------===//

#include "Backend.hpp"

#include "AsmEmitter.hpp"
#include "CallLowering.hpp"
#include "FrameLowering.hpp"
#include "ISel.hpp"
#include "Peephole.hpp"
#include "RegAllocLinear.hpp"
#include "Scheduler.hpp"
#include "TargetX64.hpp"
#include "binenc/X64BinaryEncoder.hpp"
#include "codegen/common/Parallelism.hpp"
#include "codegen/common/ScalarBits.hpp"
#include "codegen/common/objfile/DebugLineTable.hpp"
#include "peephole/PeepholeCommon.hpp"

#include <algorithm>
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstddef>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <iostream>
#include <mutex>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <thread>
#include <unordered_set>
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

/// @brief Map a target-platform enum to its native object-file format.
/// @details Darwin → Mach-O, Linux → ELF, Windows → COFF; `Host` falls back to
///          the compile-time host-format detection.
[[nodiscard]] objfile::ObjFormat targetObjectFormat(CodegenOptions::TargetPlatform platform) {
    switch (platform) {
        case CodegenOptions::TargetPlatform::Darwin:
            return objfile::ObjFormat::MachO;
        case CodegenOptions::TargetPlatform::Linux:
            return objfile::ObjFormat::ELF;
        case CodegenOptions::TargetPlatform::Windows:
            return objfile::ObjFormat::COFF;
        case CodegenOptions::TargetPlatform::Host:
            return objfile::detectHostFormat();
    }
    return objfile::detectHostFormat();
}

/// @brief Return true when `VIPER_X64_BINARY_TRACE` is set in the environment.
/// @details Cached once at first call; controls verbose stderr tracing during
///          binary emission for debug builds.
[[nodiscard]] bool traceX64BinaryEmit() {
    static const bool enabled = std::getenv("VIPER_X64_BINARY_TRACE") != nullptr;
    return enabled;
}

/// @brief Count the total number of MIR instructions across all blocks in @p fn.
std::size_t mirInstructionCount(const MFunction &fn) {
    std::size_t count = 0;
    for (const auto &block : fn.blocks)
        count += block.instructions.size();
    return count;
}

/// @brief Return the label name if @p instr is a LABEL pseudo, else nullopt.
[[nodiscard]] std::optional<std::string> labelDefinedBy(const MInstr &instr) {
    if (instr.opcode != MOpcode::LABEL || instr.operands.empty()) {
        return std::nullopt;
    }
    if (const auto *label = std::get_if<OpLabel>(&instr.operands.front())) {
        return label->name;
    }
    return std::nullopt;
}

/// @brief Return true if @p opcode ends sequential control flow within a block.
/// @details Used by `splitInternalLabelBlocks` to start a new MBasicBlock after
///          any in-block JMP/JCC/RET/UD2.
[[nodiscard]] bool isControlTerminatorForSplit(MOpcode opcode) noexcept {
    return opcode == MOpcode::JMP || opcode == MOpcode::JCC || opcode == MOpcode::RET ||
           opcode == MOpcode::UD2;
}

/// @brief Return true if @p label starts with the synthetic ".Lsplit" prefix
///        used by `splitInternalLabelBlocks` for fresh fall-through blocks.
[[nodiscard]] bool isSyntheticSplitLabel(std::string_view label) noexcept {
    return label.rfind(".Lsplit", 0) == 0;
}

/// @brief Promote in-block labels to real MachineIR basic blocks.
///
/// @details Several lowering rules emit local labels for cold trap paths before
///          register allocation.  The allocator and liveness analysis are
///          block-CFG based; leaving those labels inside one block lets
///          trap-only moves and calls pollute the normal path's register cache.
///          Splitting here preserves layout while making those edges visible.
///          It also starts a fresh block after in-block control transfers so
///          branch-arm instructions are not hidden behind an earlier JCC.
void splitInternalLabelBlocks(MFunction &fn) {
    bool needsSplit = false;
    std::size_t labelCount = 0;
    for (const auto &block : fn.blocks) {
        for (std::size_t idx = 0; idx < block.instructions.size(); ++idx) {
            const auto &instr = block.instructions[idx];
            if (labelDefinedBy(instr)) {
                needsSplit = true;
                ++labelCount;
            }
            if (isControlTerminatorForSplit(instr.opcode) && idx + 1 < block.instructions.size()) {
                needsSplit = true;
            }
        }
    }
    if (!needsSplit) {
        return;
    }

    std::vector<MBasicBlock> splitBlocks;
    splitBlocks.reserve(fn.blocks.size() + labelCount);

    for (auto &block : fn.blocks) {
        MBasicBlock current{};
        current.label = std::move(block.label);

        for (std::size_t idx = 0; idx < block.instructions.size(); ++idx) {
            auto &instr = block.instructions[idx];
            if (auto label = labelDefinedBy(instr)) {
                if (current.instructions.empty()) {
                    if (current.label.empty() || current.label == *label ||
                        isSyntheticSplitLabel(current.label)) {
                        current.label = std::move(*label);
                        continue;
                    }
                    current.instructions.push_back(std::move(instr));
                    continue;
                }
                splitBlocks.push_back(std::move(current));
                current = MBasicBlock{};
                current.label = std::move(*label);
                continue;
            }
            current.instructions.push_back(std::move(instr));

            if (isControlTerminatorForSplit(current.instructions.back().opcode) &&
                idx + 1 < block.instructions.size()) {
                const MOpcode terminator = current.instructions.back().opcode;
                std::string nextLabel;
                if (auto label = labelDefinedBy(block.instructions[idx + 1])) {
                    nextLabel = *label;
                } else {
                    nextLabel = fn.makeLocalLabel(".Lsplit");
                }
                if (terminator == MOpcode::JCC) {
                    current.instructions.push_back(
                        MInstr::make(MOpcode::JMP, {makeLabelOperand(nextLabel)}));
                }
                splitBlocks.push_back(std::move(current));
                current = MBasicBlock{};
                current.label = std::move(nextLabel);
            }
        }

        splitBlocks.push_back(std::move(current));
    }

    fn.blocks = std::move(splitBlocks);
}

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
    std::vector<bool> consumed(plans.size(), false);
    for (auto &block : func.blocks) {
        std::size_t instrIndex = 0;
        while (instrIndex < block.instructions.size()) {
            const auto &instr = block.instructions[instrIndex];
            if (instr.opcode != MOpcode::CALL) {
                ++instrIndex;
                continue;
            }

            if (instr.callPlanId == MInstr::kNoCallPlanId) {
                ++instrIndex;
                continue;
            }

            if (instr.callPlanId >= plans.size()) {
                throw std::runtime_error("x86-64 backend: call plan id " +
                                         std::to_string(instr.callPlanId) +
                                         " is out of range for function '" + func.name + "'");
            }

            // Save callPlanId before lowerCall — insertion may reallocate
            // block.instructions and invalidate the instr reference.
            const std::size_t planId = instr.callPlanId;
            const std::size_t beforeSize = block.instructions.size();
            lowerCall(block, instrIndex, plans[planId], target, frame);
            const std::size_t afterSize = block.instructions.size();
            const std::size_t inserted = afterSize - beforeSize;
            consumed[planId] = true;

            // The CALL instruction we just processed is now at position (instrIndex + inserted).
            // Skip past it to continue searching for the next CALL.
            instrIndex += inserted + 1;
        }
    }

    auto missing = std::find(consumed.begin(), consumed.end(), false);
    if (missing != consumed.end()) {
        const auto planId = static_cast<std::size_t>(std::distance(consumed.begin(), missing));
        throw std::runtime_error("x86-64 backend: call plan " + std::to_string(planId) +
                                 " was not consumed in function '" + func.name + "'");
    }
}

/// @brief Lower one function to MIR and run pre-RA legalization.
static void legalizeFunctionPipeline(const ILFunction &ilFunc,
                                     LowerILToMIR &lowering,
                                     const TargetInfo &target,
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
    splitInternalLabelBlocks(machineFunc);
}

/// @brief Run register allocation and frame lowering on legalized MIR.
static void allocateFunctionPipeline(MFunction &machineFunc,
                                     const TargetInfo &target,
                                     const CodegenOptions &options,
                                     FrameInfo &frame) {
    (void)options;
    const AllocationResult allocResult = allocate(machineFunc, target);

    assignSpillSlots(machineFunc, target, frame);
    frame.spillAreaGPR = std::max(frame.spillAreaGPR, allocResult.spillSlotsGPR * kSlotSizeBytes);
    frame.spillAreaXMM = std::max(frame.spillAreaXMM, allocResult.spillSlotsXMM * kSlotSizeBytes);
    insertPrologueEpilogue(machineFunc, target, frame);
}

/// @brief Seed a debug line table with enough file entries for MIR source locations.
static void seedDebugFiles(DebugLineTable &table,
                           const std::vector<MFunction> &mir,
                           std::string_view debugSourcePath) {
    uint32_t maxFileId = 1;
    for (const auto &fn : mir) {
        for (const auto &block : fn.blocks) {
            for (const auto &instr : block.instructions) {
                if (instr.loc.file_id > maxFileId)
                    maxFileId = instr.loc.file_id;
            }
        }
    }

    std::string filePath = std::string(debugSourcePath);
    if (filePath.empty())
        filePath = "<source>";
    else
        filePath = std::filesystem::path(filePath).lexically_normal().string();

    for (uint32_t fileId = 1; fileId <= maxFileId; ++fileId)
        table.addFileSlot(filePath);
}

/// @brief Single-function overload of seedDebugFiles for per-function emit paths.
/// @details Same semantics as the multi-function version above but scans only
///          @p fn's instructions for the maximum DWARF file id.
static void seedDebugFiles(DebugLineTable &table,
                           const MFunction &fn,
                           std::string_view debugSourcePath) {
    uint32_t maxFileId = 1;
    for (const auto &block : fn.blocks) {
        for (const auto &instr : block.instructions) {
            if (instr.loc.file_id > maxFileId)
                maxFileId = instr.loc.file_id;
        }
    }

    std::string filePath = std::string(debugSourcePath);
    if (filePath.empty())
        filePath = "<source>";
    else
        filePath = std::filesystem::path(filePath).lexically_normal().string();

    for (uint32_t fileId = 1; fileId <= maxFileId; ++fileId)
        table.addFileSlot(filePath);
}

/// @brief Throw a legalization diagnostic through legacy emit facades.
///
/// @details The pass pipeline calls @ref legalizeModuleToMIR directly and consumes
///          its boolean result plus diagnostic string. The public `emit*` helpers
///          historically let invalid-IL legalization exceptions escape as
///          `std::runtime_error`; this helper preserves that API behavior while
///          keeping the pass-facing legalization contract explicit.
///
/// @param errors Diagnostic text produced by @ref legalizeModuleToMIR.
/// @throws std::runtime_error Always, with @p errors or a fallback message.
[[noreturn]] void throwLegalizationDiagnostic(const std::string &errors) {
    throw std::runtime_error(errors.empty() ? "x86-64 legalization failed" : errors);
}

} // namespace

const TargetInfo &selectTarget(CodegenOptions::TargetABI abi) noexcept {
    switch (abi) {
        case CodegenOptions::TargetABI::SysV:
            return sysvTarget();
        case CodegenOptions::TargetABI::Win64:
            return win64Target();
        case CodegenOptions::TargetABI::Host:
            return hostTarget();
    }
    return hostTarget();
}

bool legalizeModuleToMIR(const ILModule &mod,
                         const TargetInfo &target,
                         const CodegenOptions &options,
                         AsmEmitter::RoDataPool &roData,
                         std::vector<MFunction> &mir,
                         std::vector<FrameInfo> &frames,
                         std::string &errors) {
    (void)options;
    mir.clear();
    frames.clear();
    errors.clear();

    try {
        LowerILToMIR lowering{target, roData};
        std::unordered_set<std::string> knownVarArgCallees;
        knownVarArgCallees.reserve(mod.funcs.size());
        for (const auto &fn : mod.funcs) {
            if (fn.isVarArg) {
                knownVarArgCallees.insert(fn.name);
            }
        }
        lowering.setKnownVarArgCallees(std::move(knownVarArgCallees));
        mir.reserve(mod.funcs.size());
        frames.reserve(mod.funcs.size());

        for (std::size_t fi = 0; fi < mod.funcs.size(); ++fi) {
            const auto &func = mod.funcs[fi];
            FrameInfo frame{};
            MFunction machineFunc{};
            legalizeFunctionPipeline(func, lowering, target, frame, machineFunc);
            mir.push_back(std::move(machineFunc));
            frames.push_back(frame);
        }
    } catch (const std::exception &ex) {
        mir.clear();
        frames.clear();
        errors = std::string("x86-64 legalization failed: ") + ex.what();
        return false;
    } catch (...) {
        mir.clear();
        frames.clear();
        errors = "x86-64 legalization failed: unknown exception";
        return false;
    }
    return true;
}

bool allocateModuleMIR(std::vector<MFunction> &mir,
                       std::vector<FrameInfo> &frames,
                       const TargetInfo &target,
                       const CodegenOptions &options,
                       std::string &errors) {
    errors.clear();
    if (mir.size() != frames.size()) {
        errors = "frame/MIR count mismatch prior to register allocation";
        return false;
    }

    std::string firstError;
    std::mutex errorMutex;
    auto allocateOne = [&](std::size_t index) {
        try {
            allocateFunctionPipeline(mir[index], target, options, frames[index]);
        } catch (const std::exception &ex) {
            std::lock_guard<std::mutex> lock(errorMutex);
            if (firstError.empty())
                firstError = ex.what();
        }
    };

    const std::size_t workerCount = common::codegenWorkerCount(mir.size());
    if (workerCount <= 1) {
        for (std::size_t i = 0; i < mir.size(); ++i)
            allocateOne(i);
    } else {
        std::atomic_size_t nextIndex{0};
        std::vector<std::thread> workers;
        workers.reserve(workerCount);
        for (std::size_t worker = 0; worker < workerCount; ++worker) {
            workers.emplace_back([&]() {
                for (;;) {
                    const std::size_t index = nextIndex.fetch_add(1, std::memory_order_relaxed);
                    if (index >= mir.size())
                        break;
                    allocateOne(index);
                }
            });
        }
        for (auto &worker : workers)
            worker.join();
    }

    if (!firstError.empty()) {
        errors = firstError;
        return false;
    }

    // Strip identity moves (mov r, r) that the register allocator may insert
    // when a virtual register happens to be assigned the same physical register
    // as its source.  These are always no-ops and safe to remove at any
    // optimization level.
    for (auto &fn : mir) {
        for (auto &block : fn.blocks) {
            auto &instrs = block.instructions;
            instrs.erase(std::remove_if(instrs.begin(),
                                        instrs.end(),
                                        [](const MInstr &instr) {
                                            return peephole::isIdentityMovRR(instr) ||
                                                   peephole::isIdentityMovSDRR(instr);
                                        }),
                         instrs.end());
        }
    }

    return true;
}

bool scheduleModuleMIR(std::vector<MFunction> &mir,
                       const CodegenOptions &options,
                       std::string &errors) {
    errors.clear();
    if (options.optimizeLevel < 1)
        return true;

    try {
        (void)scheduleModule(mir);
    } catch (const std::exception &ex) {
        errors = ex.what();
        return false;
    }
    return true;
}

bool optimizeModuleMIR(std::vector<MFunction> &mir,
                       const CodegenOptions &options,
                       std::string &errors) {
    errors.clear();
    if (options.optimizeLevel < 1)
        return true;

    const TargetInfo &target = selectTarget(options.targetABI);
    std::string firstError;
    std::mutex errorMutex;
    auto optimizeOne = [&](std::size_t index) {
        try {
            runPeepholes(mir[index], target);
        } catch (const std::exception &ex) {
            std::lock_guard<std::mutex> lock(errorMutex);
            if (firstError.empty())
                firstError = ex.what();
        }
    };

    const std::size_t workerCount = common::codegenWorkerCount(mir.size());
    if (workerCount <= 1) {
        for (std::size_t index = 0; index < mir.size(); ++index)
            optimizeOne(index);
    } else {
        std::atomic_size_t nextIndex{0};
        std::vector<std::thread> workers;
        workers.reserve(workerCount);
        for (std::size_t worker = 0; worker < workerCount; ++worker) {
            workers.emplace_back([&]() {
                for (;;) {
                    const std::size_t index = nextIndex.fetch_add(1, std::memory_order_relaxed);
                    if (index >= mir.size())
                        break;
                    optimizeOne(index);
                }
            });
        }
        for (auto &worker : workers)
            worker.join();
    }

    if (!firstError.empty()) {
        errors = firstError;
        return false;
    }
    return true;
}

CodegenResult emitMIRToAssembly(const std::vector<MFunction> &mir,
                                const AsmEmitter::RoDataPool &roData,
                                const TargetInfo &target,
                                const CodegenOptions &options) {
    CodegenResult result{};

    std::ostringstream asmStream{};
    std::ostringstream errorStream{};
    bool emissionFailed = false;

    if (const auto warning = syntaxWarning(options); !warning.empty()) {
        errorStream << warning;
    }

    const objfile::ObjFormat format = targetObjectFormat(options.targetPlatform);

    try {
        AsmEmitter::RoDataPool roDataCopy = roData;
        AsmEmitter emitter{roDataCopy, format};

        for (std::size_t index = 0; index < mir.size(); ++index) {
            emitter.emitFunction(asmStream, mir[index], target);
            if (index + 1U < mir.size()) {
                asmStream << '\n';
            }
        }

        emitter.emitRoData(asmStream);
    } catch (const std::exception &ex) {
        emissionFailed = true;
        asmStream.str("");
        asmStream.clear();
        errorStream << ex.what() << '\n';
    }

    // Mark the stack as non-executable on ELF targets.  Without this directive
    // the GNU linker defaults to an executable stack, triggering a warning and
    // creating a security issue.
    if (!emissionFailed && format == objfile::ObjFormat::ELF) {
        asmStream << "\n.section .note.GNU-stack,\"\",@progbits\n";
    }

    result.asmText = asmStream.str();
    result.errors = errorStream.str();

    return result;
}

/// @brief Convenience wrapper that emits assembly for a single IL function.
///
/// @details Forwards to @ref emitModuleImpl after wrapping the function in a
///          single-element vector so the main implementation can be reused.
///
/// @param func IL function to translate.
/// @param opt Backend configuration to honour.
/// @return Assembly text and diagnostics for the provided function.
CodegenResult emitFunctionToAssembly(const ILFunction &func, const CodegenOptions &opt) {
    const TargetInfo &target = selectTarget(opt.targetABI);
    AsmEmitter::RoDataPool roData{};
    std::vector<MFunction> mir;
    std::vector<FrameInfo> frames;
    std::string errors;
    ILModule module{};
    module.funcs.push_back(func);
    if (!legalizeModuleToMIR(module, target, opt, roData, mir, frames, errors))
        throwLegalizationDiagnostic(errors);
    if (!allocateModuleMIR(mir, frames, target, opt, errors))
        return CodegenResult{{}, errors};
    if (!scheduleModuleMIR(mir, opt, errors))
        return CodegenResult{{}, errors};
    if (!optimizeModuleMIR(mir, opt, errors))
        return CodegenResult{{}, errors};
    return emitMIRToAssembly(mir, roData, target, opt);
}

/// @brief Emit assembly for every function in an IL module.
///
/// @details Delegates to @ref emitModuleImpl after copying the module's
///          functions so they can be processed as a contiguous list.
///
/// @param mod IL module containing the functions to translate.
/// @param opt Backend configuration supplied by the caller.
/// @return Assembly text and diagnostics for the entire module.
CodegenResult emitModuleToAssembly(const ILModule &mod, const CodegenOptions &opt) {
    const TargetInfo &target = selectTarget(opt.targetABI);
    AsmEmitter::RoDataPool roData{};
    std::vector<MFunction> mir;
    std::vector<FrameInfo> frames;
    std::string errors;
    if (!legalizeModuleToMIR(mod, target, opt, roData, mir, frames, errors))
        throwLegalizationDiagnostic(errors);
    if (!allocateModuleMIR(mir, frames, target, opt, errors))
        return CodegenResult{{}, errors};
    if (!scheduleModuleMIR(mir, opt, errors))
        return CodegenResult{{}, errors};
    if (!optimizeModuleMIR(mir, opt, errors))
        return CodegenResult{{}, errors};
    return emitMIRToAssembly(mir, roData, target, opt);
}

BinaryEmitResult emitMIRToBinary(const std::vector<MFunction> &mir,
                                 const std::vector<FrameInfo> &frames,
                                 const AsmEmitter::RoDataPool &roData,
                                 const TargetInfo &target,
                                 const CodegenOptions &options) {
    BinaryEmitResult result{};
    std::ostringstream errorStream{};
    const objfile::ObjFormat format = targetObjectFormat(options.targetPlatform);
    const bool emitWin64Unwind = format == objfile::ObjFormat::COFF;
    const bool emitDebugLines = options.emitDebugLines && format != objfile::ObjFormat::COFF;

    if (const auto warning = syntaxWarning(options); !warning.empty()) {
        // Syntax warnings don't apply to binary emission, but keep parity.
    }

    DebugLineTable debugLines;
    if (emitDebugLines)
        seedDebugFiles(debugLines, mir, options.debugSourcePath);

    if (mir.size() != frames.size()) {
        result.errors = "frame/MIR count mismatch prior to binary emission";
        return result;
    }

    result.textSections.reserve(mir.size());

    for (int i = 0; i < static_cast<int>(roData.stringCount()); ++i) {
        std::string label = roData.stringLabel(i);
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
        result.rodata.defineSymbol(
            label, objfile::SymbolBinding::Local, objfile::SymbolSection::Rodata);
        result.rodata.emit64LE(viper::codegen::common::f64Bits(roData.f64Value(i)));
    }

    auto encodeOne = [&](std::size_t i,
                         objfile::CodeSection &funcText,
                         DebugLineTable *funcDebugLines) -> std::string {
        binenc::X64BinaryEncoder funcEncoder;
        if (funcDebugLines)
            funcEncoder.setDebugLineTable(funcDebugLines);
        const auto traceStart = std::chrono::steady_clock::now();
        if (traceX64BinaryEmit()) {
            std::cerr << "[x64-binary] encode " << (i + 1) << "/" << mir.size() << " "
                      << mir[i].name << " blocks=" << mir[i].blocks.size()
                      << " instrs=" << mirInstructionCount(mir[i]) << "\n";
        }
        try {
            funcEncoder.encodeFunction(mir[i],
                                       funcText,
                                       result.rodata,
                                       format == objfile::ObjFormat::MachO,
                                       &frames[i],
                                       emitWin64Unwind);
        } catch (const std::exception &ex) {
            return "x86-64 binary emission failed for function '" + mir[i].name + "': " + ex.what();
        }
        if (traceX64BinaryEmit()) {
            const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - traceStart);
            std::cerr << "[x64-binary] done " << mir[i].name << " ms=" << elapsed.count()
                      << " bytes=" << funcText.bytes().size()
                      << " relocs=" << funcText.relocations().size() << "\n";
        }
        return {};
    };

    if (emitDebugLines) {
        for (std::size_t i = 0; i < mir.size(); ++i) {
            objfile::CodeSection funcText;
            DebugLineTable funcDebugLines;
            seedDebugFiles(funcDebugLines, mir[i], options.debugSourcePath);

            if (std::string error = encodeOne(i, funcText, &funcDebugLines); !error.empty()) {
                BinaryEmitResult failure{};
                failure.errors = std::move(error);
                return failure;
            }

            const uint64_t debugBias = static_cast<uint64_t>(result.text.currentOffset());
            debugLines.append(funcDebugLines, debugBias);
            result.text.appendSection(funcText);
            result.textSections.push_back(std::move(funcText));
        }
    } else {
        for (std::size_t i = 0; i < mir.size(); ++i) {
            objfile::CodeSection funcText;
            if (std::string error = encodeOne(i, funcText, nullptr); !error.empty()) {
                BinaryEmitResult failure{};
                failure.errors = std::move(error);
                return failure;
            }
            result.textSections.push_back(std::move(funcText));
        }
    }

    if (emitDebugLines && !debugLines.empty())
        result.debugLineData = debugLines.encodeDwarf5(8);

    result.errors = errorStream.str();
    return result;
}

BinaryEmitResult emitModuleToBinary(const ILModule &mod, const CodegenOptions &opt) {
    const TargetInfo &target = selectTarget(opt.targetABI);
    AsmEmitter::RoDataPool roData{};
    std::vector<MFunction> mir;
    std::vector<FrameInfo> frames;
    std::string errors;
    if (!legalizeModuleToMIR(mod, target, opt, roData, mir, frames, errors))
        throwLegalizationDiagnostic(errors);
    if (!allocateModuleMIR(mir, frames, target, opt, errors))
        return BinaryEmitResult{{}, {}, errors};
    if (!scheduleModuleMIR(mir, opt, errors))
        return BinaryEmitResult{{}, {}, errors};
    if (!optimizeModuleMIR(mir, opt, errors))
        return BinaryEmitResult{{}, {}, errors};
    return emitMIRToBinary(mir, frames, roData, target, opt);
}

} // namespace viper::codegen::x64
