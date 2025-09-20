// tui/include/tui/widgets/list_view.hpp
// @brief Selectable list widget with multi-selection and custom renderer.
// @invariant Cursor and selection indices remain within item range.
// @ownership ListView borrows Theme; caller owns item strings.
#pragma once

#include <functional>
#include <string>
#include <vector>

#include "tui/style/theme.hpp"
#include "tui/ui/widget.hpp"

namespace viper::tui::widgets
{

/// @brief Displays a vertical list of items with keyboard navigation.
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
