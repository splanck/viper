// tui/src/widgets/tree_view.cpp
// @brief Implementation of TreeView widget with expand/collapse.
// @invariant Visible nodes refreshed on state change.
// @ownership TreeView owns nodes and borrows Theme.

#include "tui/widgets/tree_view.hpp"
#include "tui/render/screen.hpp"

#include <algorithm>

namespace viper::tui::widgets
{

/// @brief Construct a tree node with the provided label string.
TreeNode::TreeNode(std::string lbl) : label(std::move(lbl)) {}

/// @brief Append a child node and wire its parent pointer.
TreeNode *TreeNode::add(std::unique_ptr<TreeNode> child)
{
    child->parent = this;
    children.push_back(std::move(child));
    return children.back().get();
}

TreeView::TreeView(std::vector<std::unique_ptr<TreeNode>> roots, const style::Theme &theme)
    : roots_(std::move(roots)), theme_(theme)
{
    rebuild();
}

/// @brief Tree views require focus to drive expansion and navigation via keyboard.
bool TreeView::wantsFocus() const
{
    return true;
}

void TreeView::paint(render::ScreenBuffer &sb)
{
    const auto &sel = theme_.style(style::Role::Accent);
    const auto &nor = theme_.style(style::Role::Normal);
    for (std::size_t i = 0; i < visible_.size() && static_cast<int>(i) < rect_.h; ++i)
    {
        auto *node = visible_[i];
        int y = rect_.y + static_cast<int>(i);
        int x = rect_.x;
        auto &csel = sb.at(y, x);
        csel.ch = (cursor_ == static_cast<int>(i)) ? U'>' : U' ';
        csel.style = sel;
        x += 1;
        int d = depth(node);
        for (int k = 0; k < d * 2 && k + x < rect_.x + rect_.w; ++k)
        {
            auto &cell = sb.at(y, x + k);
            cell.ch = U' ';
            cell.style = nor;
        }
        x += d * 2;
        if (x < rect_.x + rect_.w)
        {
            auto &mark = sb.at(y, x);
            mark.ch = node->children.empty() ? U' ' : (node->expanded ? U'-' : U'+');
            mark.style = sel;
            ++x;
        }
        int maxw = rect_.x + rect_.w - x;
        for (int j = 0; j < maxw; ++j)
        {
            auto &cell = sb.at(y, x + j);
            if (j < static_cast<int>(node->label.size()))
            {
                cell.ch = static_cast<char32_t>(node->label[j]);
            }
            else
            {
                cell.ch = U' ';
            }
            cell.style = nor;
        }
    }
}

bool TreeView::onEvent(const ui::Event &ev)
{
    if (visible_.empty())
    {
        return false;
    }
    const auto &k = ev.key;
    if (k.code == term::KeyEvent::Code::Up)
    {
        if (cursor_ > 0)
        {
            --cursor_;
        }
        return true;
    }
    if (k.code == term::KeyEvent::Code::Down)
    {
        if (cursor_ + 1 < static_cast<int>(visible_.size()))
        {
            ++cursor_;
        }
        return true;
    }
    auto *node = visible_[cursor_];
    if (k.code == term::KeyEvent::Code::Right)
    {
        if (!node->children.empty() && !node->expanded)
        {
            node->expanded = true;
            rebuild();
        }
        return true;
    }
    if (k.code == term::KeyEvent::Code::Left)
    {
        if (node->expanded)
        {
            node->expanded = false;
            rebuild();
        }
        else if (node->parent)
        {
            auto *p = node->parent;
            auto it = std::find(visible_.begin(), visible_.end(), p);
            if (it != visible_.end())
            {
                cursor_ = static_cast<int>(it - visible_.begin());
            }
        }
        return true;
    }
    if (k.code == term::KeyEvent::Code::Enter)
    {
        if (!node->children.empty())
        {
            node->expanded = !node->expanded;
            rebuild();
        }
        return true;
    }
    return false;
}

TreeNode *TreeView::current() const
{
    if (visible_.empty())
    {
        return nullptr;
    }
    return visible_[cursor_];
}

void TreeView::rebuild()
{
    visible_.clear();
    std::vector<TreeNode *> stack;
    for (auto &r : roots_)
    {
        stack.push_back(r.get());
        while (!stack.empty())
        {
            TreeNode *n = stack.back();
            stack.pop_back();
            visible_.push_back(n);
            if (n->expanded)
            {
                for (auto it = n->children.rbegin(); it != n->children.rend(); ++it)
                {
                    stack.push_back(it->get());
                }
            }
        }
    }
    if (cursor_ >= static_cast<int>(visible_.size()))
    {
        cursor_ = static_cast<int>(visible_.size()) - 1;
    }
    if (cursor_ < 0)
    {
        cursor_ = 0;
    }
}

int TreeView::depth(TreeNode *n)
{
    int d = 0;
    while (n->parent)
    {
        n = n->parent;
        ++d;
    }
    return d;
}

} // namespace viper::tui::widgets
