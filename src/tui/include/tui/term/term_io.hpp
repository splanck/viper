//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tui/include/tui/term/term_io.hpp
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

class TermIO
{
  public:
    virtual ~TermIO() = default;
    virtual void write(std::string_view s) = 0;
    virtual void flush() = 0;
};

class RealTermIO : public TermIO
{
  public:
    void write(std::string_view s) override;
    void flush() override;
};

class StringTermIO : public TermIO
{
  public:
    void write(std::string_view s) override;

    void flush() override;

    const std::string &buffer() const;

    void clear();

  private:
    std::string buf_;
};

} // namespace viper::tui::term
