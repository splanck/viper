//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: codegen/common/objfile/StringTable.hpp
// Purpose: Interned NUL-terminated string table for object file serialization.
//          Used for ELF .strtab/.shstrtab and COFF string tables.
// Key invariants:
//   - Offset 0 is always the empty string (single NUL byte)
//   - Strings are deduplicated via hash map
//   - PE/COFF 4-byte size prefix is NOT included; COFF writer prepends it
// Ownership/Lifetime:
//   - Value type, typically lives for the duration of object file writing
// Links: codegen/common/objfile/SymbolTable.hpp
//
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace viper::codegen::objfile
{

/// A serializable string table for object file formats.
///
/// Accumulates NUL-terminated strings and returns byte offsets.
/// ELF convention: offset 0 is the empty string (single NUL byte).
class StringTable
{
  public:
    /// Initialize with a single NUL byte at offset 0 (empty string).
    StringTable();

    /// Add a string to the table. Returns offset. Deduplicates.
    uint32_t add(std::string_view str);

    /// Find an existing string. Returns offset or UINT32_MAX if not found.
    uint32_t find(std::string_view str) const;

    /// Raw table bytes (NUL-separated strings).
    const std::vector<char> &data() const
    {
        return data_;
    }

    /// Total byte size of the table.
    uint32_t size() const
    {
        return static_cast<uint32_t>(data_.size());
    }

  private:
    std::vector<char> data_;
    std::unordered_map<std::string, uint32_t> offsets_;
};

} // namespace viper::codegen::objfile
