//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// This file declares the TreeView widget and TreeNode data structure for
// Viper's TUI framework. TreeView displays a hierarchical tree of labeled
// nodes with expand/collapse functionality, similar to a file explorer.
//
// Each TreeNode contains a label, child nodes, and expansion state.
// The TreeView renders visible nodes (expanded subtrees) with indentation
// proportional to depth and supports keyboard navigation (Up/Down arrows,
// Enter to toggle expansion).
//
// Key invariants:
//   - Root nodes are the top-level entries in the tree.
//   - Only expanded nodes' children are visible and navigable.
//   - The cursor index is always within the visible node list bounds.
//   - Parent pointers are maintained by the add() method.
//
// Ownership: TreeView owns root TreeNodes via unique_ptr. TreeNode owns
// its children via unique_ptr and stores a non-owning parent pointer.
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

/// @brief Node in a hierarchical tree structure with label, children, and expansion state.
/// @details Each node contains a display label, a vector of child nodes owned via
///          unique_ptr, a non-owning parent pointer for traversal, and a flag indicating
///          whether the node's children are visible (expanded).
struct TreeNode
{
    std::string label{};
    std::vector<std::unique_ptr<TreeNode>> children{};
    TreeNode *parent{nullptr};
    bool expanded{false};

    explicit TreeNode(std::string lbl);

    TreeNode *add(std::unique_ptr<TreeNode> child);
};

/// @brief Hierarchical tree display widget with keyboard navigation and expand/collapse.
/// @details Renders a tree of labeled nodes with indentation proportional to depth.
///          Supports Up/Down arrow navigation, Enter to toggle node expansion, and
///          Left/Right arrows to collapse/expand. Only expanded subtrees are visible.
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
