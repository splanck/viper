#pragma once

/**
 * @file result.hpp
 * @brief Result<T, E> type for explicit error handling.
 *
 * @details
 * Provides a Rust-style Result type for functions that can fail. Functions
 * return Result<T, E> where T is the success value type and E is the error
 * type. The caller must explicitly handle the error case.
 *
 * Usage:
 * @code
 * Result<int, ErrorCode> divide(int a, int b) {
 *     if (b == 0) {
 *         return Err(ErrorCode::DivideByZero);
 *     }
 *     return Ok(a / b);
 * }
 *
 * auto result = divide(10, 2);
 * if (result.is_ok()) {
 *     int value = result.unwrap();
 * }
 * @endcode
 */

#include "../include/types.hpp"

namespace viper
{

/**
 * @brief Generic error codes for kernel operations.
 */
enum class Error : i32
{
    None = 0,          ///< No error (success)
    InvalidArg = -1,   ///< Invalid argument provided
    NotFound = -2,     ///< Resource not found
    NoMemory = -3,     ///< Out of memory
    IoError = -4,      ///< I/O operation failed
    Busy = -5,         ///< Resource is busy
    Timeout = -6,      ///< Operation timed out
    Denied = -7,       ///< Permission denied
    Exists = -8,       ///< Resource already exists
    NotSupported = -9, ///< Operation not supported
    Overflow = -10,    ///< Buffer/value overflow
    Interrupted = -11, ///< Operation interrupted
};

/**
 * @brief Result type for operations that can fail.
 *
 * @tparam T Success value type
 * @tparam E Error type (defaults to Error enum)
 */
template <typename T, typename E = Error> class Result
{
  public:
    /// Create a success result
    static Result Ok(T value)
    {
        Result r;
        r.value_ = value;
        r.is_ok_ = true;
        return r;
    }

    /// Create an error result
    static Result Err(E error)
    {
        Result r;
        r.error_ = error;
        r.is_ok_ = false;
        return r;
    }

    /// Check if result is success
    [[nodiscard]] bool is_ok() const
    {
        return is_ok_;
    }

    /// Check if result is error
    [[nodiscard]] bool is_err() const
    {
        return !is_ok_;
    }

    /// Get the success value (undefined behavior if is_err())
    [[nodiscard]] T unwrap() const
    {
        return value_;
    }

    /// Get the success value or a default
    [[nodiscard]] T unwrap_or(T default_value) const
    {
        return is_ok_ ? value_ : default_value;
    }

    /// Get the error (undefined behavior if is_ok())
    [[nodiscard]] E error() const
    {
        return error_;
    }

    /// Convert to bool (true = success)
    explicit operator bool() const
    {
        return is_ok_;
    }

  private:
    Result() = default;

    union
    {
        T value_;
        E error_;
    };

    bool is_ok_ = false;
};

/**
 * @brief Result specialization for void success type.
 *
 * @tparam E Error type
 */
template <typename E> class Result<void, E>
{
  public:
    /// Create a success result
    static Result Ok()
    {
        Result r;
        r.is_ok_ = true;
        return r;
    }

    /// Create an error result
    static Result Err(E error)
    {
        Result r;
        r.error_ = error;
        r.is_ok_ = false;
        return r;
    }

    /// Check if result is success
    [[nodiscard]] bool is_ok() const
    {
        return is_ok_;
    }

    /// Check if result is error
    [[nodiscard]] bool is_err() const
    {
        return !is_ok_;
    }

    /// Get the error (undefined behavior if is_ok())
    [[nodiscard]] E error() const
    {
        return error_;
    }

    /// Convert to bool (true = success)
    explicit operator bool() const
    {
        return is_ok_;
    }

  private:
    Result() = default;

    E error_;
    bool is_ok_ = false;
};

/// Helper to create Ok result (for type inference)
template <typename T> Result<T, Error> Ok(T value)
{
    return Result<T, Error>::Ok(value);
}

/// Helper to create Ok result for void
inline Result<void, Error> Ok()
{
    return Result<void, Error>::Ok();
}

/// Helper to create Err result (for type inference)
template <typename E = Error> Result<void, E> Err(E error)
{
    return Result<void, E>::Err(error);
}

} // namespace viper
