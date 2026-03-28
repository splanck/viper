//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: codegen/common/linker/DynStubGen.hpp
// Purpose: Generate synthetic object files containing AArch64 stub trampolines
//          for dynamic symbols and ObjC selector stubs.
// Key invariants:
//   - ObjC selector stubs move symbols out of dynamicSyms (resolved locally)
//   - Dynamic stubs generate GOT entries filled by dyld at load time
//   - Output ObjFiles use ELF relocation format for the reloc applier
// Ownership/Lifetime:
//   - Returned ObjFiles are value types owned by the caller
// Links: codegen/common/linker/NativeLinker.cpp
//
//===----------------------------------------------------------------------===//

#pragma once

#include "codegen/common/linker/ObjFileReader.hpp"

#include <string>
#include <unordered_set>

namespace viper::codegen::linker {

/// @brief Generate ObjC selector stub trampolines for AArch64 macOS.
///
/// @details Creates a synthetic ObjFile containing stubs for
///          `objc_msgSend$selector` symbols. Each stub loads the selector
///          reference pointer into x1 and branches to objc_msgSend.
///          Matching symbols are removed from @p dynamicSyms (resolved
///          locally) and `objc_msgSend` is added as a dynamic dependency.
///
/// @param dynamicSyms Mutable set of dynamic symbols; matched entries are removed.
/// @return Synthetic ObjFile with stub text, selector references, and relocations.
ObjFile generateObjcSelectorStubsAArch64(std::unordered_set<std::string> &dynamicSyms);

/// @brief Generate dynamic symbol stub trampolines and GOT entries for AArch64.
///
/// @details Creates a synthetic ObjFile containing 12-byte stubs
///          (adrp/ldr/br x16) and 8-byte GOT slots for each dynamic symbol.
///          The GOT entries are filled by dyld at load time via non-lazy binding.
///
/// @param dynamicSyms Set of dynamic symbols requiring stubs.
/// @return Synthetic ObjFile with stub text, GOT data, and relocations.
ObjFile generateDynStubsAArch64(const std::unordered_set<std::string> &dynamicSyms);

} // namespace viper::codegen::linker
