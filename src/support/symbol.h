#pragma once
#include <cstdint>
#include <functional>
/// @brief Opaque identifier for interned strings.
/// @invariant 0 denotes an invalid symbol.
/// @ownership Value type, no ownership semantics.
namespace il::support {
struct Symbol {
  uint32_t id = 0;
  friend bool operator==(Symbol a, Symbol b) noexcept { return a.id == b.id; }
  friend bool operator!=(Symbol a, Symbol b) noexcept { return a.id != b.id; }
  explicit operator bool() const noexcept { return id != 0; }
};
} // namespace il::support
namespace std {
template <> struct hash<il::support::Symbol> {
  size_t operator()(il::support::Symbol s) const noexcept { return s.id; }
};
} // namespace std
