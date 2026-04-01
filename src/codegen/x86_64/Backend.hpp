//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: codegen/x86_64/Backend.hpp
// Purpose: Declare the high-level orchestration entry points for x86-64 codegen.
// Key invariants: emitModuleToAssembly processes all functions in module order;
//                 errors field is empty on success; Phase A only supports AT&T
//                 syntax (atandtSyntax must be true).
// Ownership/Lifetime: Callers retain ownership of IL modules; generated assembly is
//                     returned by value in CodegenResult.
// Links: docs/architecture.md
//
//===----------------------------------------------------------------------===//

#pragma once

#include "AsmEmitter.hpp"
#include "FrameLowering.hpp"
#include "LowerILToMIR.hpp"
#include "codegen/common/objfile/CodeSection.hpp"

#include <string>
#include <vector>

namespace viper::codegen::x64 {

/// \brief Options controlling backend emission behaviour.
struct CodegenOptions {
    enum class TargetABI {
        Host,
        SysV,
        Win64,
    };

    bool atandtSyntax{true}; ///< Emit AT&T syntax when true; Phase A only supports this form.
    int optimizeLevel{1};    ///< Optimization level: 0 = none, 1 = O1 (default), 2 = O2.
    TargetABI targetABI{TargetABI::Host}; ///< Target ABI used for lowering/allocation.
    std::string debugSourcePath{}; ///< Source path used for DWARF line table file entries.
};

/// \brief Aggregated result of a backend emission request.
struct CodegenResult {
    std::string asmText{}; ///< Complete assembly text for the requested module/function.
    std::string errors{};  ///< Phase A diagnostics; empty when emission succeeds.
};

/// \brief Lower a single IL function to assembly text.
[[nodiscard]] CodegenResult emitFunctionToAssembly(const ILFunction &func,
                                                   const CodegenOptions &opt);

/// \brief Lower an IL module to assembly text using the Phase A backend pipeline.
[[nodiscard]] CodegenResult emitModuleToAssembly(const ILModule &mod, const CodegenOptions &opt);

/// \brief Result of binary emission: machine code in CodeSections.
struct BinaryEmitResult {
    objfile::CodeSection text{};   ///< Machine code bytes + relocations (merged).
    objfile::CodeSection rodata{}; ///< Read-only data (.rodata / __TEXT,__const).
    std::string errors{};          ///< Diagnostics; empty on success.

    /// Per-function text sections for function-level dead stripping.
    /// Each CodeSection contains exactly one function's machine code.
    std::vector<objfile::CodeSection> textSections{};

    /// Pre-encoded DWARF .debug_line bytes (empty when no debug info is available).
    std::vector<uint8_t> debugLineData{};
};

/// \brief Resolve an explicit target ABI selection to a concrete target descriptor.
[[nodiscard]] const TargetInfo &selectTarget(CodegenOptions::TargetABI abi) noexcept;

/// \brief Lower adapter IL to MIR and run pre-RA legalization passes.
[[nodiscard]] bool legalizeModuleToMIR(const ILModule &mod,
                                       const TargetInfo &target,
                                       const CodegenOptions &options,
                                       AsmEmitter::RoDataPool &roData,
                                       std::vector<MFunction> &mir,
                                       std::vector<FrameInfo> &frames,
                                       std::string &errors);

/// \brief Run register allocation and frame layout on MIR.
[[nodiscard]] bool allocateModuleMIR(std::vector<MFunction> &mir,
                                     std::vector<FrameInfo> &frames,
                                     const TargetInfo &target,
                                     const CodegenOptions &options,
                                     std::string &errors);

/// \brief Run explicit post-RA backend optimizations on MIR.
[[nodiscard]] bool optimizeModuleMIR(std::vector<MFunction> &mir,
                                     const CodegenOptions &options,
                                     std::string &errors);

/// \brief Emit assembly for an already-lowered/register-allocated MIR module.
[[nodiscard]] CodegenResult emitMIRToAssembly(const std::vector<MFunction> &mir,
                                              const AsmEmitter::RoDataPool &roData,
                                              const TargetInfo &target,
                                              const CodegenOptions &options);

/// \brief Emit binary code for an already-lowered/register-allocated MIR module.
[[nodiscard]] BinaryEmitResult emitMIRToBinary(const std::vector<MFunction> &mir,
                                               const std::vector<FrameInfo> &frames,
                                               const AsmEmitter::RoDataPool &roData,
                                               const TargetInfo &target,
                                               const CodegenOptions &options,
                                               bool isDarwin);

/// \brief Lower an IL module to binary machine code via X64BinaryEncoder.
/// @param mod     The lowered IL module.
/// @param opt     Backend options (optimization level, etc.).
/// @param isDarwin If true, symbol names get underscore-prefixed (Mach-O convention).
[[nodiscard]] BinaryEmitResult emitModuleToBinary(const ILModule &mod,
                                                  const CodegenOptions &opt,
                                                  bool isDarwin);

} // namespace viper::codegen::x64
