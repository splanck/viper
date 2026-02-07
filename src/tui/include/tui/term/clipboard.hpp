//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// This file declares the Clipboard abstract interface and its concrete
// implementations (Osc52Clipboard and MockClipboard) for clipboard
// operations in Viper's TUI framework.
//
// Osc52Clipboard uses the OSC 52 terminal escape sequence to copy text
// to the system clipboard via the terminal emulator. This works over
// SSH sessions and in modern terminal emulators that support the protocol.
// Paste is not yet supported via OSC 52 (returns empty string).
//
// MockClipboard stores copied text in memory for unit testing without
// requiring a real terminal or system clipboard.
//
// Key invariants:
//   - Clipboard is an abstract interface with virtual copy/paste methods.
//   - Osc52Clipboard writes base64-encoded text via the TermIO interface.
//   - MockClipboard stores exactly the last copied text for verification.
//
// Ownership: Osc52Clipboard borrows a TermIO reference (must outlive it).
// MockClipboard owns its internal string buffer.
//
//===----------------------------------------------------------------------===//

#pragma once

#include <string>
#include <string_view>

namespace viper::tui::term
{
class TermIO;

/// @brief Abstract interface for system clipboard operations in the TUI.
/// @details Provides copy (write to clipboard) and paste (read from clipboard)
///          operations. Concrete implementations handle platform-specific
///          clipboard access mechanisms.
class Clipboard
{
  public:
    virtual ~Clipboard() = default;
    /// @brief Copy text to the system clipboard.
    /// @param text The text to place on the clipboard.
    /// @return True if the copy operation succeeded.
    virtual bool copy(std::string_view text) = 0;
    /// @brief Paste text from the system clipboard.
    /// @return The current clipboard content, or an empty string if unavailable.
    virtual std::string paste() = 0;
};

/// @brief Clipboard implementation using OSC 52 terminal escape sequences.
/// @details Encodes text as base64 and sends it via the OSC 52 protocol to the
///          terminal emulator, which forwards it to the system clipboard. Works
///          over SSH and in terminals supporting the OSC 52 specification.
///          Paste is not currently supported (returns empty string).
class Osc52Clipboard : public Clipboard
{
  public:
    /// @brief Construct an OSC 52 clipboard bound to a terminal I/O sink.
    /// @param io Terminal I/O used to emit the OSC 52 escape sequence. Must outlive this object.
    explicit Osc52Clipboard(TermIO &io);
    bool copy(std::string_view text) override;
    std::string paste() override;

  private:
    TermIO &io_;
};

/// @brief In-memory clipboard implementation for testing.
/// @details Stores the last copied text in memory for inspection by test code.
///          Does not interact with the system clipboard. Useful for verifying
///          clipboard operations in headless tests.
class MockClipboard : public Clipboard
{
  public:
    bool copy(std::string_view text) override;
    std::string paste() override;

    /// @brief Access the last text that was copied to this mock clipboard.
    /// @return Const reference to the stored text.
    const std::string &last() const;

    /// @brief Clear the stored clipboard text.
    void clear();

  private:
    std::string last_{};
};

} // namespace viper::tui::term
