//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: codegen/common/linker/ArchiveReader.hpp
// Purpose: Parse Unix ar archives (.a files) containing object files.
//          Supports GNU, BSD, and COFF archive variants.
// Key invariants:
//   - Archives start with "!<arch>\n" magic (8 bytes)
//   - Member headers are 60 bytes, data padded to 2-byte boundary
//   - Symbol table format varies: GNU ("/"), BSD ("__.SYMDEF")
// Ownership/Lifetime:
//   - ArchiveReader owns parsed member metadata; raw bytes are memory-mapped
//     or read from the file on demand
// Links: codegen/common/linker/ObjFileReader.hpp
//
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <cstdint>
#include <ostream>
#include <string>
#include <unordered_map>
#include <vector>

namespace viper::codegen::linker
{

/// A member within an archive.
struct ArchiveMember
{
    std::string name;           ///< Member name (e.g., "foo.o").
    size_t dataOffset;          ///< Byte offset of member data within the archive file.
    size_t dataSize;            ///< Size of the member data in bytes.
};

/// Parsed archive with symbol index and member list.
struct Archive
{
    std::string path;                               ///< Path to the archive file.
    std::vector<uint8_t> data;                      ///< Raw archive file contents.
    std::vector<ArchiveMember> members;             ///< All object file members.
    std::unordered_map<std::string, size_t> symbolIndex; ///< Symbol name → member index.
};

/// Parse an archive file (.a / .lib).
/// @param path  Path to the archive.
/// @param ar    Output archive structure.
/// @param err   Error output stream.
/// @return true on success.
bool readArchive(const std::string &path, Archive &ar, std::ostream &err);

/// Extract the raw bytes of a specific member.
/// @param ar     The parsed archive.
/// @param member The member to extract.
/// @return A vector of the member's raw bytes.
std::vector<uint8_t> extractMember(const Archive &ar, const ArchiveMember &member);

} // namespace viper::codegen::linker
