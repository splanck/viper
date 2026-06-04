//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/repl/ReplOutputCapture.hpp
// Purpose: Scoped capture of runtime stdout produced by REPL evaluation.
// Key invariants:
//   - Capture is restored when the object is destroyed.
//   - Captured bytes are stored verbatim without UTF-8 interpretation.
// Ownership/Lifetime:
//   - Owns only the accumulated std::string buffer.
//   - Temporarily borrows the runtime output hook while alive.
// Links: src/runtime/core/rt_output.h
//
//===----------------------------------------------------------------------===//

#pragma once

#include <string>

namespace viper::repl {

/// @brief RAII guard that captures Viper runtime stdout into memory.
/// @details The VM's terminal output routines flow through the runtime
///          @c rt_output layer. This guard installs a temporary capture hook so
///          REPL evaluation can collect program output without redirecting the
///          process-wide stdout file descriptor. That avoids pipe-buffer
///          deadlocks and ensures hook restoration happens on every exit path.
class ScopedReplOutputCapture {
  public:
    /// @brief Begin capturing runtime stdout.
    /// @details Saves the previously installed runtime output hook and replaces
    ///          it with a hook that appends raw bytes to this object's internal
    ///          buffer. The saved hook is restored by the destructor.
    ScopedReplOutputCapture();

    /// @brief Restore the runtime stdout hook active before construction.
    /// @details Destruction is noexcept so callers can safely use this guard
    ///          across compile/run paths that may return early on errors.
    ~ScopedReplOutputCapture() noexcept;

    ScopedReplOutputCapture(const ScopedReplOutputCapture &) = delete;
    ScopedReplOutputCapture &operator=(const ScopedReplOutputCapture &) = delete;

    /// @brief Return all bytes captured so far.
    /// @details The returned string may contain arbitrary bytes, including
    ///          embedded NUL characters. The REPL currently prints it as text.
    /// @return Captured runtime stdout bytes.
    const std::string &output() const noexcept {
        return output_;
    }

  private:
    struct HookState;

    /// @brief Runtime C callback that forwards captured bytes into @p ctx.
    /// @details The runtime invokes this function through a plain function
    ///          pointer, so it must not throw. Invalid or null context pointers
    ///          are ignored.
    /// @param data Pointer to output bytes.
    /// @param len Number of bytes available at @p data.
    /// @param ctx Opaque pointer expected to reference this guard.
    static void captureCallback(const char *data, size_t len, void *ctx) noexcept;

    /// @brief Append a byte range received from the runtime output layer.
    /// @param data Pointer to output bytes.
    /// @param len Number of bytes in @p data.
    void append(const char *data, size_t len);

    std::string output_;
    HookState *state_{nullptr};
};

} // namespace viper::repl
