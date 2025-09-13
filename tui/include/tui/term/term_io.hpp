// tui/include/tui/term/term_io.hpp
// @brief Terminal I/O abstraction interfaces.
// @invariant No terminal mode changes are performed.
// @ownership RealTermIO writes to std::cout; StringTermIO owns its buffer.
#pragma once

#include <string>
#include <string_view>

namespace viper::tui::term
{
/// @brief Abstract interface for terminal I/O backends.
/// @invariant Implementations must not throw.
/// @ownership Does not own passed data.
class TermIO
{
  public:
    virtual ~TermIO() = default;

    /// @brief Write a sequence of bytes to the terminal.
    /// @param data Bytes to write.
    virtual void write(std::string_view data) = 0;

    /// @brief Flush any buffered output to the terminal.
    virtual void flush() = 0;
};

/// @brief Real terminal backend writing to std::cout.
/// @invariant std::cout's rdbuf must be valid.
/// @ownership Does not own std::cout.
class RealTermIO final : public TermIO
{
  public:
    void write(std::string_view data) override;
    void flush() override;
};

/// @brief In-memory terminal backend capturing output into a string.
/// @invariant Buffer grows to accommodate written data.
/// @ownership Owns its internal buffer.
class StringTermIO final : public TermIO
{
  public:
    void write(std::string_view data) override;
    void flush() override;

    /// @brief Access the captured output buffer.
    /// @return Reference to internal string buffer.
    [[nodiscard]] const std::string &buffer() const noexcept
    {
        return buffer_;
    }

  private:
    std::string buffer_{};
};

} // namespace viper::tui::term
