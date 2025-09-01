// File: src/support/source_manager.cpp
// Purpose: Implements utilities to manage source buffers.
// Key invariants: None.
// Ownership/Lifetime: SourceManager owns loaded buffers.
// Links: docs/class-catalog.md
#include "source_manager.hpp"
namespace il::support
{
uint32_t SourceManager::addFile(std::string path)
{
    files_.push_back(std::move(path));
    return static_cast<uint32_t>(files_.size());
}
std::string_view SourceManager::getPath(uint32_t file_id) const
{
    if (file_id == 0 || file_id > files_.size())
        return {};
    return files_[file_id - 1];
}
} // namespace il::support
