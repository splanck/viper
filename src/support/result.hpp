// File: src/support/result.hpp
// Purpose: Provides a simple Result type for error handling.
// Key invariants: None.
// Ownership/Lifetime: Result owns contained value or error.
// Links: docs/codemap.md
#pragma once

#include <optional>
#include <string>
#include <utility>

/// @brief Minimal expected-like container.
/// @invariant Either holds a value or an error string.
/// @ownership Owns stored value/error.
namespace il::support
{

/// @brief Tag type used to construct successful Result values explicitly.
struct SuccessTag
{
    /// @brief Constructs the tag. The type carries no state.
    constexpr SuccessTag() = default;
};

/// @brief Sentinel instance for success construction convenience.
inline constexpr SuccessTag kSuccessTag{};

template <typename T> class Result
{
  public:
    /// @brief Creates a successful result containing a value.
    /// @param tag Success disambiguation tag shared across specializations.
    /// @param value Value to store; ownership is transferred to the Result.
    /// @details Engages @c value_ with @p value and leaves @c error_ empty.
    /// After this constructor, @c value() may be called and @c error() must not
    /// be used.
    template <typename U = T> Result(SuccessTag /*tag*/, U &&value) : value_(std::forward<U>(value))
    {
    }

    /// @brief Creates a successful result from a value without an explicit tag.
    /// @param value Value to store; ownership is transferred to the Result.
    /// @details Delegates to the tagged constructor so the same disambiguation
    /// path is used for all specializations, including @c std::string.
    template <typename U = T> Result(U &&value) : Result(kSuccessTag, std::forward<U>(value)) {}

    /// @brief Factory that constructs a successful result.
    /// @param value Value to store in the Result.
    /// @return Result containing the provided @p value.
    template <typename U = T> static Result success(U &&value)
    {
        return Result(kSuccessTag, std::forward<U>(value));
    }

    /// @brief Factory that constructs an error result with a message.
    /// @param error Error description to store; ownership is transferred to the
    /// Result.
    /// @return Result containing the provided error message.
    static Result error(std::string error)
    {
        return Result(ErrorTag{}, std::move(error));
    }

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
    /// @brief Tag type for constructing error Results.
    struct ErrorTag
    {
        /// @brief Constructs the tag. The type carries no state.
        constexpr ErrorTag() = default;
    };

    /// @brief Creates an error result with a message.
    /// @param error Error description to store; ownership is transferred to the
    /// Result.
    /// @details Disengages @c value_ and initializes @c error_. After this
    /// constructor, @c error() may be called and @c value() must not be used.
    Result(ErrorTag /*tag*/, std::string error) : error_(std::move(error)) {}

    /// Storage for the value when present; otherwise empty.
    std::optional<T> value_;
    /// Storage for the error message when no value is present; otherwise empty.
    std::string error_;
};
} // namespace il::support
