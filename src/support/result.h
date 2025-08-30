// File: src/support/result.h
// Purpose: Provides a simple Result type for error handling.
// Key invariants: None.
// Ownership/Lifetime: Result owns contained value or error.
// Links: docs/class-catalog.md
#pragma once
#include <string>
#include <utility>
/// @brief Minimal expected-like container.
/// @invariant Either holds a value or an error string.
/// @ownership Owns stored value/error.
namespace il::support {
template <typename T> class Result {
public:
  Result(T value) : has_value_(true), value_(std::move(value)) {}
  Result(std::string error) : has_value_(false), error_(std::move(error)) {}
  bool isOk() const { return has_value_; }
  T &value() { return value_; }
  const T &value() const { return value_; }
  const std::string &error() const { return error_; }

private:
  bool has_value_;
  T value_{};
  std::string error_;
};
} // namespace il::support
