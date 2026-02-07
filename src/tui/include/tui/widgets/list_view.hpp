//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// This file declares the ListView widget for Viper's TUI framework.
// ListView displays a vertical scrollable list of string items with
// keyboard navigation and selection support.
//
// The widget supports single and range selection: clicking or pressing
// Enter selects a single item, while Shift+arrow keys extend the
// selection range. A custom ItemRenderer can be installed to control
// how each item is painted.
//
// Key invariants:
//   - The cursor index is always within [0, items_.size()).
//   - Selection state is maintained per-item via a boolean vector.
//   - The default renderer highlights the cursor and selected items.
//
// Ownership: ListView owns items and selection state by value. It
// borrows the Theme reference (must outlive the widget). The optional
// ItemRenderer is owned via std::function.
//
//===----------------------------------------------------------------------===//

#pragma once

#include <functional>
#include <string>
#include <vector>

#include "tui/style/theme.hpp"
#include "tui/ui/widget.hpp"

namespace viper::tui::widgets
{

/// @brief Vertical scrollable list widget with keyboard navigation and selection.
/// @details Displays a list of string items with cursor-based navigation (Up/Down),
///          single selection (Enter), and range selection (Shift+arrows). Supports
///          custom item rendering via an installable ItemRenderer callback.
class ListView : public ui::Widget
{
  public:
    using ItemRenderer = std::function<void(render::ScreenBuffer &,
                                            int row,
                                            const std::string &item,
                                            bool selected,
                                            const style::Theme &theme)>;

    /// @brief Construct list view with items and theme.
    ListView(std::vector<std::string> items, const style::Theme &theme);

    /// @brief Set custom item renderer.
    void setRenderer(ItemRenderer r);

    /// @brief Paint list items.
    void paint(render::ScreenBuffer &sb) override;

    /// @brief Handle navigation and selection keys.
    /// @return True if event consumed.
    bool onEvent(const ui::Event &ev) override;

    /// @brief List wants focus for keyboard navigation.
    [[nodiscard]] bool wantsFocus() const override;

    /// @brief Retrieve selected item indices.
    [[nodiscard]] std::vector<int> selection() const;

  private:
    std::vector<std::string> items_{};
    std::vector<bool> selected_{};
    ItemRenderer renderer_{};
    const style::Theme &theme_;
    int cursor_{0};
    int anchor_{0};

    void defaultRender(render::ScreenBuffer &sb, int row, const std::string &item, bool selected);
    void clearSelection();
    void selectRange(int a, int b);
};

} // namespace viper::tui::widgets
