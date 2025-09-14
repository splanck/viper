// File: src/support/result.hpp
// Purpose: Provides a simple Result type for error handling.
// Key invariants: None.
// Ownership/Lifetime: Result owns contained value or error.
// Links: docs/class-catalog.md
#pragma once

#include <optional>
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
    /// @details Engages @c value_ with @p value and leaves @c error_ empty.
    /// After this constructor, @c value() may be called and @c error() must not
    /// be used.
    Result(T value) : value_(std::move(value)) {}

    /// @brief Creates an error result with a message.
    /// @param error Error description to store; ownership is transferred to the
    /// Result.
    /// @details Disengages @c value_ and initializes @c error_. After this
    /// constructor, @c error() may be called and @c value() must not be used.
    Result(std::string error) : error_(std::move(error)) {}

    /// @brief Indicates whether the Result currently holds a value.
    /// @return True if a value is present; false if an error was stored.
    /// @invariant When true, @c value() is valid; when false, @c error() is
    /// valid.
    bool isOk() const
    {
        return value_.has_value();
    }

    /// @brief Provides mutable access to the contained value.
    /// @return Reference to the stored value.
    /// @pre @c isOk() must return true.
    T &value()
    {
        return *value_;
    }

    /// @brief Provides read-only access to the contained value.
    /// @return Const reference to the stored value.
    /// @pre @c isOk() must return true.
    const T &value() const
    {
        return *value_;
    }

    /// @brief Retrieves the stored error message.
    /// @return Reference to the error string.
    /// @pre @c isOk() must return false.
    const std::string &error() const
    {
        return error_;
    }

  private:
    /// Storage for the value when present; otherwise empty.
    std::optional<T> value_;
    /// Storage for the error message when no value is present; otherwise empty.
    std::string error_;
};
} // namespace il::support
