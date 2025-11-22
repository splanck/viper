//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tui/include/tui/widgets/command_palette.hpp
// Purpose: Implements functionality for this subsystem.
// Key invariants: To be documented.
// Ownership/Lifetime: To be documented.
// Links: docs/architecture.md
//
//===----------------------------------------------------------------------===//

#pragma once

#include <string>
#include <vector>

#include "tui/input/keymap.hpp"
#include "tui/style/theme.hpp"
#include "tui/ui/widget.hpp"

namespace viper::tui::widgets
{

/// @brief Widget showing command palette with incremental search.
class CommandPalette : public ui::Widget
{
  public:
    CommandPalette(input::Keymap &km, const style::Theme &theme);

    /// @brief Paint query and filtered commands.
    void paint(render::ScreenBuffer &sb) override;

    /// @brief Handle typing and Enter to execute command.
    bool onEvent(const ui::Event &ev) override;

    /// @brief Palette requires focus for typing.
    [[nodiscard]] bool wantsFocus() const override;

  private:
    input::Keymap &km_;
    const style::Theme &theme_;
    std::string query_{};
    std::vector<input::CommandId> results_{};

    void update();
};

} // namespace viper::tui::widgets
