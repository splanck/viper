//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: codegen/common/objfile/StringTable.cpp
// Purpose: Implementation of interned string table for object files.
// Key invariants:
//   - Offset 0 is always the empty string (single NUL byte)
//   - Deduplication via offsets_ map
// Ownership/Lifetime:
//   - See StringTable.hpp
// Links: codegen/common/objfile/StringTable.hpp
//
//===----------------------------------------------------------------------===//

#include "codegen/common/objfile/StringTable.hpp"

namespace viper::codegen::objfile {

StringTable::StringTable() {
    // ELF convention: offset 0 = empty string (single NUL byte).
    data_.push_back('\0');
    offsets_[""] = 0;
}

uint32_t StringTable::add(std::string_view str) {
    std::string key(str);
    auto it = offsets_.find(key);
    if (it != offsets_.end())
        return it->second;

    auto offset = static_cast<uint32_t>(data_.size());
    data_.insert(data_.end(), str.begin(), str.end());
    data_.push_back('\0');
    offsets_[std::move(key)] = offset;
    return offset;
}

uint32_t StringTable::find(std::string_view str) const {
    auto it = offsets_.find(std::string(str));
    if (it != offsets_.end())
        return it->second;
    return UINT32_MAX;
}

} // namespace viper::codegen::objfile
