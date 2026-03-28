//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: codegen/common/linker/MachOBindRebase.hpp
// Purpose: Mach-O bind/rebase opcode emission and symbol table construction.
//          Used by MachOExeWriter to generate __LINKEDIT content.
// Key invariants:
//   - Bind opcodes use two-level namespace (per-symbol dylib ordinals)
//   - Rebase opcodes sorted by address, run-length encoded
//   - Symbol table: _main as ext-defined, dynamic imports as undefined
//   - nlist desc encodes library ordinal (bits [15:8]) for MH_TWOLEVEL
// Ownership/Lifetime:
//   - Stateless builder functions — output appended to caller-owned vectors
// Links: codegen/common/linker/MachOExeWriter.cpp
//
//===----------------------------------------------------------------------===//

#pragma once

#include "codegen/common/linker/LinkTypes.hpp"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace viper::codegen::linker {

/// Build the bind opcode stream for non-lazy GOT binding + TLV descriptors.
/// Appends bind opcodes to \p bindData, terminated with BIND_OPCODE_DONE.
/// @param symOrdinals  Map from symbol name to 1-based dylib ordinal.
///                     Ordinal 0 means flat lookup. Missing symbols default to ordinal 1.
void buildBindOpcodes(std::vector<uint8_t> &bindData,
                      const std::vector<GotEntry> &gotEntries,
                      const LinkLayout &layout,
                      uint64_t dataSegVmAddr,
                      uint32_t dataSegIndex,
                      const std::unordered_map<std::string, uint32_t> &symOrdinals);

/// Build rebase opcodes for ASLR pointer fixups in writable sections.
/// dyld processes these at load time, adding the ASLR slide to each pointer.
void buildRebaseOpcodes(std::vector<uint8_t> &rebaseData,
                        const LinkLayout &layout,
                        uint64_t dataSegVmAddr,
                        uint32_t dataSegIndex);

/// Build symbol table and string table for __LINKEDIT.
/// @param symtabData   Output nlist entries (16 bytes each).
/// @param strtabData   Output string table (NUL-separated, 4-byte aligned).
/// @param layout       Link layout with global symbol table.
/// @param dynSyms      Set of dynamic symbol names.
/// @param symOrdinals  Map from symbol name to 1-based dylib ordinal (for nlist desc).
///                     Ordinal 0 maps to DYNAMIC_LOOKUP_ORDINAL (0xFE).
/// @param nExtDef      [out] Number of external defined symbols.
/// @param nUndef       [out] Number of undefined (dynamic import) symbols.
void buildSymtab(std::vector<uint8_t> &symtabData,
                 std::vector<uint8_t> &strtabData,
                 const LinkLayout &layout,
                 const std::unordered_set<std::string> &dynSyms,
                 const std::unordered_map<std::string, uint32_t> &symOrdinals,
                 uint32_t &nExtDef,
                 uint32_t &nUndef);

} // namespace viper::codegen::linker
