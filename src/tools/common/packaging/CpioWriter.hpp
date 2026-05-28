//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tools/common/packaging/CpioWriter.hpp
// Purpose: Write portable ASCII CPIO archives for native macOS package payloads.
//
//===----------------------------------------------------------------------===//
#pragma once

#include <cstddef>
#include <cstdint>
#include <set>
#include <string>
#include <vector>

namespace viper::pkg {

class CpioWriter {
  public:
    void addDirectory(const std::string &path, uint32_t mode = 0755, uint32_t mtime = 0);
    void addFile(const std::string &path,
                 const uint8_t *data,
                 size_t size,
                 uint32_t mode = 0644,
                 uint32_t mtime = 0);
    void addFileVec(const std::string &path,
                    const std::vector<uint8_t> &data,
                    uint32_t mode = 0644,
                    uint32_t mtime = 0);
    void addFileString(const std::string &path,
                       const std::string &content,
                       uint32_t mode = 0644,
                       uint32_t mtime = 0);
    void addSymlink(const std::string &path, const std::string &target, uint32_t mtime = 0);

    std::vector<uint8_t> finish() const;

  private:
    enum class EntryKind { Directory, File, Symlink };

    struct Entry {
        EntryKind kind{EntryKind::File};
        std::string path;
        std::string symlinkTarget;
        std::vector<uint8_t> data;
        uint32_t mode{0644};
        uint32_t mtime{0};
    };

    std::vector<Entry> entries_;
    std::set<std::string> seenPaths_;
};

} // namespace viper::pkg
