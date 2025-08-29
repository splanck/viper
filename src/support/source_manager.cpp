#include "source_manager.h"
namespace il::support {
uint32_t SourceManager::addFile(std::string path) {
  files_.push_back(std::move(path));
  return static_cast<uint32_t>(files_.size());
}
std::string_view SourceManager::getPath(uint32_t file_id) const {
  if (file_id == 0 || file_id > files_.size())
    return {};
  return files_[file_id - 1];
}
} // namespace il::support
