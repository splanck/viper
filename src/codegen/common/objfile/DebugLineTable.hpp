//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: codegen/common/objfile/DebugLineTable.hpp
// Purpose: Collects address→(file,line) mappings and encodes them as a
//          DWARF v5 .debug_line section.
// Key invariants:
//   - Entries must be added in ascending address order.
//   - File indices are 1-based (matching SourceLoc::file_id).
//   - encodeDwarf5() produces a self-contained .debug_line section.
// Links: support/source_location.hpp, codegen/common/linker/ExeWriterUtil.hpp
//
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace viper::codegen
{

/// A single address-to-line mapping entry.
struct AddressLineEntry
{
    uint64_t address;  ///< Code offset (relative to .text start).
    uint32_t fileIndex; ///< 1-based file index.
    uint32_t line;      ///< 1-based line number.
    uint32_t column;    ///< 1-based column number (0 = unknown).
};

/// Collects address→line mappings and encodes a DWARF v5 .debug_line section.
class DebugLineTable
{
  public:
    /// Register a file path and return its 1-based index.
    /// If the path was already registered, returns the existing index.
    uint32_t addFile(const std::string &path);

    /// Add an address-to-line mapping (entries must be in address order).
    void addEntry(uint64_t address, uint32_t fileIndex, uint32_t line, uint32_t column = 0);

    /// Return true if no entries have been recorded.
    [[nodiscard]] bool empty() const { return entries_.empty(); }

    /// Encode the collected data as a DWARF v5 .debug_line section.
    /// @param addressSize 4 for 32-bit, 8 for 64-bit.
    /// @return The complete .debug_line section bytes.
    [[nodiscard]] std::vector<uint8_t> encodeDwarf5(uint8_t addressSize = 8) const;

  private:
    std::vector<std::string> files_;        ///< Registered file paths.
    std::vector<AddressLineEntry> entries_; ///< Address→line mappings.
};

} // namespace viper::codegen
