//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tools/common/packaging/ArWriter.hpp
// Purpose: Write ar archives as used by the .deb package format.
//
// Key invariants:
//   - Global header: "!<arch>\n" (8 bytes).
//   - Per member: 60-byte header + data + optional pad byte for odd sizes.
//   - Member name is "/"-terminated, space-padded to 16 chars.
//
// Ownership/Lifetime:
//   - Output returned as std::vector<uint8_t> or written to file.
//
// Links: ArWriter.cpp, LinuxPackageBuilder.hpp
//
//===----------------------------------------------------------------------===//
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace viper::pkg
{

/// @brief Writes ar archives (used as .deb outer container).
class ArWriter
{
  public:
    /// @brief Add a member to the archive.
    /// @param name Member name (max 15 chars, will be "/" terminated).
    /// @param data Member data.
    /// @param size Member data size.
    /// @param mtime Modification time (Unix timestamp), default 0.
    /// @param mode File mode in octal, default 100644.
    void addMember(const std::string &name,
                   const uint8_t *data,
                   size_t size,
                   uint32_t mtime = 0,
                   uint32_t mode = 0100644);

    /// @brief Convenience: add a member from a string.
    void addMemberString(const std::string &name,
                         const std::string &content,
                         uint32_t mtime = 0,
                         uint32_t mode = 0100644);

    /// @brief Convenience: add a member from a vector.
    void addMemberVec(const std::string &name,
                      const std::vector<uint8_t> &data,
                      uint32_t mtime = 0,
                      uint32_t mode = 0100644);

    /// @brief Finalize and return the complete ar archive.
    std::vector<uint8_t> finish() const;

    /// @brief Finalize and write to a file.
    void finishToFile(const std::string &path) const;

  private:
    struct Member
    {
        std::string name;
        std::vector<uint8_t> data;
        uint32_t mtime;
        uint32_t mode;
    };

    std::vector<Member> members_;
};

} // namespace viper::pkg
