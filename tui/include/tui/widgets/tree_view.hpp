// tui/include/tui/widgets/tree_view.hpp
// @brief Hierarchical tree widget with expandable nodes.
// @invariant Visible list rebuilt after expansion changes.
// @ownership TreeView owns nodes; Theme borrowed.
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "tui/style/theme.hpp"
#include "tui/ui/widget.hpp"

namespace viper::tui::widgets
{

/// @brief Node in TreeView hierarchy.
struct TreeNode
{
    std::string label{};
    std::vector<std::unique_ptr<TreeNode>> children{};
    TreeNode *parent{nullptr};
    bool expanded{false};

    explicit TreeNode(std::string lbl) : label(std::move(lbl)) {}

    TreeNode *add(std::unique_ptr<TreeNode> child)
    {
        child->parent = this;
        children.push_back(std::move(child));
        return children.back().get();
    }
};

/// @brief Displays a tree of nodes with expand/collapse controls.
class TreeView : public ui::Widget
{
  public:
    /// @brief Construct with root nodes and theme.
    TreeView(std::vector<std::unique_ptr<TreeNode>> roots, const style::Theme &theme);

    /// @brief Paint visible nodes.
    void paint(render::ScreenBuffer &sb) override;

    /// @brief Handle navigation and expansion keys.
    /// @return True if event consumed.
    bool onEvent(const ui::Event &ev) override;

    /// @brief Tree view wants focus for keyboard handling.
    [[nodiscard]] bool wantsFocus() const override
    {
        return true;
    }

    /// @brief Current node under cursor.
    [[nodiscard]] TreeNode *current() const;

  private:
    std::vector<std::unique_ptr<TreeNode>> roots_{};
    const style::Theme &theme_;
    std::vector<TreeNode *> visible_{};
    int cursor_{0};

    void rebuild();
    static int depth(TreeNode *n);
};

} // namespace viper::tui::widgets
