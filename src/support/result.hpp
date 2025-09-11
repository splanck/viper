// File: src/support/result.hpp
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
namespace il::support
{

template <typename T> class Result
{
  public:
    /// @brief Creates a successful result containing a value.
    /// @param value Value to store; ownership is transferred to the Result.
    /// @details Sets @c has_value_ to true and leaves @c error_ empty. After
    /// this constructor, @c value() may be called and @c error() must not be
    /// used.
    Result(T value) : has_value_(true), value_(std::move(value)) {}

    /// @brief Creates an error result with a message.
    /// @param error Error description to store; ownership is transferred to the
    /// Result.
    /// @details Sets @c has_value_ to false and initializes @c error_. After
    /// this constructor, @c error() may be called and @c value() must not be
    /// used.
    Result(std::string error) : has_value_(false), error_(std::move(error)) {}

    /// @brief Indicates whether the Result currently holds a value.
    /// @return True if a value is present; false if an error was stored.
    /// @invariant When true, @c value() is valid; when false, @c error() is
    /// valid.
    bool isOk() const
    {
        return has_value_;
    }

    /// @brief Provides mutable access to the contained value.
    /// @return Reference to the stored value.
    /// @pre @c isOk() must return true.
    T &value()
    {
        return value_;
    }

    /// @brief Provides read-only access to the contained value.
    /// @return Const reference to the stored value.
    /// @pre @c isOk() must return true.
    const T &value() const
    {
        return value_;
    }

    /// @brief Retrieves the stored error message.
    /// @return Reference to the error string.
    /// @pre @c isOk() must return false.
    const std::string &error() const
    {
        return error_;
    }

  private:
    /// Tracks whether the Result currently holds a value.
    bool has_value_;
    /// Storage for the value when @c has_value_ is true; otherwise ignored.
    T value_{};
    /// Storage for the error message when @c has_value_ is false; otherwise
    /// empty.
    std::string error_;
};
} // namespace il::support
