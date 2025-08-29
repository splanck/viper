#pragma once
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>
/// @brief Tracks mapping from file ids to paths and source locations.
/// @invariant File id 0 is invalid.
/// @ownership Owns stored file path strings.
namespace il::support {
struct SourceLoc {
  uint32_t file_id = 0;
  uint32_t line = 0;
  uint32_t column = 0;
  bool isValid() const { return file_id != 0; }
};
class SourceManager {
public:
  uint32_t addFile(std::string path);
  std::string_view getPath(uint32_t file_id) const;

private:
  std::vector<std::string> files_;
};
} // namespace il::support
