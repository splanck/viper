//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tui/include/tui/widgets/tree_view.hpp
// Purpose: Implements functionality for this subsystem.
// Key invariants: To be documented.
// Ownership/Lifetime: To be documented.
// Links: docs/architecture.md
//
//===----------------------------------------------------------------------===//

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

    explicit TreeNode(std::string lbl);

    TreeNode *add(std::unique_ptr<TreeNode> child);
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
    [[nodiscard]] bool wantsFocus() const override;

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
