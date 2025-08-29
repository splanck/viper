#pragma once
#include <cstddef>
#include <vector>
/// @brief Simple bump allocator for fast allocations.
/// @invariant Allocations are not individually freed; use reset() to reuse.
/// @ownership Owns its internal buffer.
namespace il::support {
class Arena {
public:
  explicit Arena(size_t size);
  void *allocate(size_t size, size_t align);
  void reset();

private:
  std::vector<std::byte> buffer_;
  size_t offset_ = 0;
};
} // namespace il::support
