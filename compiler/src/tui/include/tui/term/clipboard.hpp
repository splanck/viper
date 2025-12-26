//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tui/include/tui/term/clipboard.hpp
// Purpose: Implements functionality for this subsystem.
// Key invariants: To be documented.
// Ownership/Lifetime: To be documented.
// Links: docs/architecture.md
//
//===----------------------------------------------------------------------===//

#pragma once

#include <string>
#include <string_view>

namespace viper::tui::term
{
class TermIO;

class Clipboard
{
  public:
    virtual ~Clipboard() = default;
    virtual bool copy(std::string_view text) = 0;
    virtual std::string paste() = 0;
};

class Osc52Clipboard : public Clipboard
{
  public:
    explicit Osc52Clipboard(TermIO &io);
    bool copy(std::string_view text) override;
    std::string paste() override;

  private:
    TermIO &io_;
};

class MockClipboard : public Clipboard
{
  public:
    bool copy(std::string_view text) override;
    std::string paste() override;

    const std::string &last() const;

    void clear();

  private:
    std::string last_{};
};

} // namespace viper::tui::term
