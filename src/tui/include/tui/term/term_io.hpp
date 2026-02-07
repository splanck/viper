//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// This file declares the TermIO abstract interface and its concrete
// implementations (RealTermIO and StringTermIO) for terminal output in
// Viper's TUI framework.
//
// TermIO provides a minimal write/flush interface that the Renderer uses
// to emit ANSI escape sequences. RealTermIO writes directly to stdout
// via POSIX write(). StringTermIO captures output into an in-memory
// string buffer for headless testing.
//
// Key invariants:
//   - write() may be called with partial data; flush() ensures delivery.
//   - StringTermIO accumulates all written data until explicitly cleared.
//   - RealTermIO writes to STDOUT_FILENO with no buffering beyond the OS.
//
// Ownership: RealTermIO holds no resources. StringTermIO owns its internal
// string buffer.
//
//===----------------------------------------------------------------------===//

#pragma once
#include <string>
#include <string_view>

namespace viper::tui::term
{

/// @brief Abstract interface for terminal output used by the TUI renderer.
/// @details Provides a write/flush contract that abstracts the output sink,
///          enabling both real terminal output and in-memory capture for testing.
///          The Renderer writes ANSI escape sequences through this interface.
class TermIO
{
  public:
    virtual ~TermIO() = default;
    /// @brief Write a string of bytes to the terminal output.
    /// @param s Byte sequence to write (typically ANSI escape codes or text).
    virtual void write(std::string_view s) = 0;
    /// @brief Flush any buffered output to ensure it reaches the terminal.
    virtual void flush() = 0;
};

/// @brief Terminal output implementation that writes directly to stdout.
/// @details Uses POSIX write() to emit bytes to file descriptor 1 (stdout).
///          Flush performs an fsync/fdatasync to ensure bytes are delivered.
class RealTermIO : public TermIO
{
  public:
    void write(std::string_view s) override;
    void flush() override;
};

/// @brief In-memory terminal output sink for headless testing.
/// @details Captures all written bytes into an internal string buffer that
///          can be inspected by tests. Useful for verifying renderer output
///          without a real terminal.
class StringTermIO : public TermIO
{
  public:
    void write(std::string_view s) override;

    void flush() override;

    /// @brief Access the accumulated output buffer.
    /// @return Const reference to the string containing all written data.
    const std::string &buffer() const;

    /// @brief Clear the output buffer, discarding all accumulated data.
    void clear();

  private:
    std::string buf_;
};

} // namespace viper::tui::term
