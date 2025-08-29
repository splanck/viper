#include "arena.h"
namespace il::support {
Arena::Arena(size_t size) : buffer_(size) {}
void *Arena::allocate(size_t size, size_t align) {
  size_t current = offset_;
  size_t aligned = (current + align - 1) & ~(align - 1);
  if (aligned + size > buffer_.size())
    return nullptr;
  offset_ = aligned + size;
  return buffer_.data() + aligned;
}
void Arena::reset() { offset_ = 0; }
} // namespace il::support
