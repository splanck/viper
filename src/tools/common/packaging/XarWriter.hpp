//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tools/common/packaging/XarWriter.hpp
// Purpose: Minimal XAR writer for native macOS flat package generation.
//
//===----------------------------------------------------------------------===//
#pragma once

#include <cstddef>
#include <cstdint>
#include <set>
#include <string>
#include <vector>

namespace viper::pkg {

class XarWriter {
  public:
    void addDirectory(const std::string &path, uint32_t mode = 0755);
    void addFile(const std::string &name,
                 const uint8_t *data,
                 size_t size,
                 bool compress = false,
                 uint32_t mode = 0644);
    void addFileVec(const std::string &name,
                    const std::vector<uint8_t> &data,
                    bool compress = false,
                    uint32_t mode = 0644);
    void addFileString(const std::string &name,
                       const std::string &content,
                       bool compress = false,
                       uint32_t mode = 0644);

    std::vector<uint8_t> finish() const;
    void finishToFile(const std::string &path) const;

  private:
    enum class EntryKind { Directory, File };

    struct Entry {
        EntryKind kind{EntryKind::File};
        std::string path;
        std::vector<uint8_t> data;
        bool compress{false};
        uint32_t mode{0644};
    };

    std::vector<Entry> entries_;
    std::set<std::string> seenNames_;
};

} // namespace viper::pkg
