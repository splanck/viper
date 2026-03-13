//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: codegen/aarch64/passes/BinaryEmitPass.cpp
// Purpose: Encode AArch64 MIR into machine code bytes via A64BinaryEncoder,
//          producing CodeSection output for direct .o emission.
// Key invariants:
//   - Requires register allocation to have completed (operates on physical regs)
//   - Populates AArch64Module::binaryText and AArch64Module::binaryRodata
//   - RodataPool entries emitted as raw bytes with NUL terminators (.asciz semantics)
// Ownership/Lifetime:
//   - Stateless pass; mutates AArch64Module binary fields
// Links: codegen/aarch64/binenc/A64BinaryEncoder.hpp
//        codegen/aarch64/RodataPool.hpp
//
//===----------------------------------------------------------------------===//

#include "codegen/aarch64/passes/BinaryEmitPass.hpp"

#include "codegen/aarch64/binenc/A64BinaryEncoder.hpp"

#include <utility>

namespace viper::codegen::aarch64::passes
{

bool BinaryEmitPass::run(AArch64Module &module, Diagnostics &diags)
{
    if (module.mir.empty())
    {
        // Not an error — modules with no functions produce empty output.
        module.binaryText.emplace();
        module.binaryRodata.emplace();
        return true;
    }

    // Determine ABI format from host platform.
#if defined(__APPLE__)
    constexpr auto abi = ABIFormat::Darwin;
#elif defined(_WIN32)
    constexpr auto abi = ABIFormat::Windows;
#else
    constexpr auto abi = ABIFormat::Linux;
#endif

    objfile::CodeSection text;
    objfile::CodeSection rodata;
    binenc::A64BinaryEncoder encoder;

    for (const auto &fn : module.mir)
    {
        // Emit each function into its own CodeSection for per-function dead stripping.
        module.binaryTextSections.emplace_back();
        binenc::A64BinaryEncoder funcEncoder;
        funcEncoder.encodeFunction(fn, module.binaryTextSections.back(), rodata, abi);

        // Also emit into merged text for backward compatibility (symbol extraction).
        encoder.encodeFunction(fn, text, rodata, abi);
    }

    // Emit rodata pool entries as raw bytes into the rodata CodeSection.
    // Each entry is a NUL-terminated string (matching .asciz assembly semantics).
    for (const auto &[label, content] : module.rodataPool.entries())
    {
        rodata.defineSymbol(label, objfile::SymbolBinding::Local, objfile::SymbolSection::Rodata);
        rodata.emitBytes(content.data(), content.size());
        rodata.emit8(0); // NUL terminator
    }

    module.binaryText = std::move(text);
    module.binaryRodata = std::move(rodata);
    return true;
}

} // namespace viper::codegen::aarch64::passes
