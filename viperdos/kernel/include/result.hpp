//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
#pragma once

#include "error.hpp"
#include "types.hpp"

/**
 * @file result.hpp
 * @brief Result<T> type for unified error handling throughout the kernel.
 *
 * @details
 * This header provides a type-safe way to return either a success value or an
 * error code from kernel functions. It replaces the error-prone patterns of:
 * - Returning -1 or nullptr on error
 * - Using out-parameters for success values
 * - Mixing error codes with valid return values
 *
 * ## Usage
 *
 * Functions return `Result<T>` and callers check `.ok()` before accessing the value:
 *
 * ```cpp
 * Result<u32> allocate_handle() {
 *     if (no_handles_available) {
 *         return Result<u32>::err(error::VERR_NO_RESOURCE);
 *     }
 *     return Result<u32>::ok(handle_id);
 * }
 *
 * void caller() {
 *     auto result = allocate_handle();
 *     if (result.ok()) {
 *         use_handle(result.value());
 *     } else {
 *         handle_error(result.error());
 *     }
 * }
 * ```
 *
 * For void-returning functions, use `Result<void>` (VoidResult typedef):
 *
 * ```cpp
 * VoidResult do_operation() {
 *     if (failed) return VoidResult::err(error::VERR_IO);
 *     return VoidResult::success();
 * }
 * ```
 */

namespace kernel {

/**
 * @brief Result type carrying either a success value or an error code.
 *
 * @tparam T The success value type. Use `void` for operations with no return value.
 */
template <typename T> class Result {
  public:
    /**
     * @brief Create a successful result with a value.
     * @param val The success value.
     * @return Result containing the value.
     */
    static Result ok(T val) {
        Result r;
        r.value_ = val;
        r.error_ = error::VOK;
        r.has_value_ = true;
        return r;
    }

    /**
     * @brief Create an error result.
     * @param code The error code (must be negative).
     * @return Result containing the error.
     */
    static Result err(error::Code code) {
        Result r;
        r.error_ = code;
        r.has_value_ = false;
        return r;
    }

    /**
     * @brief Create an error result from a raw error code.
     * @param code The error code (must be negative).
     * @return Result containing the error.
     */
    static Result err(i64 code) {
        Result r;
        r.error_ = static_cast<error::Code>(code);
        r.has_value_ = false;
        return r;
    }

    /**
     * @brief Check if the result is successful.
     * @return true if result contains a value, false if it contains an error.
     */
    bool ok() const {
        return has_value_;
    }

    /**
     * @brief Check if the result is an error.
     * @return true if result contains an error, false if it contains a value.
     */
    bool failed() const {
        return !has_value_;
    }

    /**
     * @brief Get the success value.
     * @warning Only valid if ok() returns true.
     * @return The success value.
     */
    T value() const {
        return value_;
    }

    /**
     * @brief Get the success value, or a default if error.
     * @param default_val Value to return if this is an error result.
     * @return The success value or default_val.
     */
    T value_or(T default_val) const {
        return has_value_ ? value_ : default_val;
    }

    /**
     * @brief Get the error code.
     * @warning Only meaningful if failed() returns true.
     * @return The error code.
     */
    error::Code error() const {
        return error_;
    }

    /**
     * @brief Get the error code as i64 for syscall returns.
     * @return The error code as i64.
     */
    i64 error_code() const {
        return static_cast<i64>(error_);
    }

    /**
     * @brief Implicit conversion to bool for if-statement use.
     * @return true if successful, false if error.
     */
    explicit operator bool() const {
        return has_value_;
    }

  private:
    Result() = default;

    T value_{};
    error::Code error_ = error::VERR_UNKNOWN;
    bool has_value_ = false;
};

/**
 * @brief Specialization of Result for void (operations with no return value).
 */
template <> class Result<void> {
  public:
    /**
     * @brief Create a successful void result.
     * @return Successful result.
     */
    static Result success() {
        Result r;
        r.error_ = error::VOK;
        r.is_ok_ = true;
        return r;
    }

    /**
     * @brief Create an error result.
     * @param code The error code.
     * @return Result containing the error.
     */
    static Result err(error::Code code) {
        Result r;
        r.error_ = code;
        r.is_ok_ = false;
        return r;
    }

    /**
     * @brief Create an error result from a raw error code.
     * @param code The error code (must be negative).
     * @return Result containing the error.
     */
    static Result err(i64 code) {
        Result r;
        r.error_ = static_cast<error::Code>(code);
        r.is_ok_ = false;
        return r;
    }

    /**
     * @brief Check if the result is successful.
     */
    bool ok() const {
        return is_ok_;
    }

    /**
     * @brief Check if the result is an error.
     */
    bool failed() const {
        return !is_ok_;
    }

    /**
     * @brief Get the error code.
     */
    error::Code error() const {
        return error_;
    }

    /**
     * @brief Get the error code as i64 for syscall returns.
     */
    i64 error_code() const {
        return static_cast<i64>(error_);
    }

    /**
     * @brief Implicit conversion to bool.
     */
    explicit operator bool() const {
        return is_ok_;
    }

  private:
    Result() = default;

    error::Code error_ = error::VERR_UNKNOWN;
    bool is_ok_ = false;
};

/**
 * @brief Convenience typedef for void results.
 */
using VoidResult = Result<void>;

/**
 * @brief Helper macro to propagate errors from Result-returning functions.
 *
 * Usage:
 * ```cpp
 * Result<u32> inner_function();
 *
 * Result<u32> outer_function() {
 *     auto val = TRY(inner_function());
 *     return Result<u32>::ok(val + 1);
 * }
 * ```
 */
#define TRY(expr)                                                                                  \
    ({                                                                                             \
        auto _result = (expr);                                                                     \
        if (_result.failed()) {                                                                    \
            return decltype(_result)::err(_result.error());                                        \
        }                                                                                          \
        _result.value();                                                                           \
    })

/**
 * @brief Helper macro to propagate errors in void-returning functions.
 */
#define TRY_VOID(expr)                                                                             \
    do {                                                                                           \
        auto _result = (expr);                                                                     \
        if (_result.failed()) {                                                                    \
            return kernel::VoidResult::err(_result.error());                                       \
        }                                                                                          \
    } while (0)

} // namespace kernel

// Export commonly used types to global namespace for convenience
using kernel::Result;
using kernel::VoidResult;
