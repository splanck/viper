// tui/tests/test_list_tree.cpp
// @brief Test ListView and TreeView keyboard behavior and painting.
// @invariant Selection and expansion reflect simulated key events.
// @ownership Theme and widgets owned within test.

#include "tui/render/renderer.hpp"
#include "tui/render/screen.hpp"
#include "tui/style/theme.hpp"
#include "tui/term/term_io.hpp"
#include "tui/widgets/list_view.hpp"
#include "tui/widgets/tree_view.hpp"

#include <cassert>
#include <cctype>
#include <memory>
#include <string>
#include <vector>

using viper::tui::render::Renderer;
using viper::tui::render::ScreenBuffer;
using viper::tui::style::Role;
using viper::tui::style::Theme;
using viper::tui::term::KeyEvent;
using viper::tui::term::StringTermIO;
using viper::tui::ui::Event;
using viper::tui::widgets::ListView;
using viper::tui::widgets::TreeNode;
using viper::tui::widgets::TreeView;

int main()
{
    Theme theme;
    ScreenBuffer sb;
    StringTermIO tio;
    Renderer r(tio, true);

    // ListView basic paint and selection
    ListView lv({"one", "two", "three"}, theme);
    lv.layout({0, 0, 8, 3});
    sb.resize(3, 8);
    sb.clear(theme.style(Role::Normal));
    lv.paint(sb);
    tio.clear();
    r.draw(sb);
    assert(tio.buffer().find('>') != std::string::npos);
    assert(tio.buffer().find("one") != std::string::npos);

    Event ev{};
    ev.key.code = KeyEvent::Code::Down;
    lv.onEvent(ev);
    ev.key.mods = KeyEvent::Shift;
    ev.key.code = KeyEvent::Code::Down;
    lv.onEvent(ev);
    auto sel = lv.selection();
    assert(sel.size() == 2 && sel[0] == 1 && sel[1] == 2);

    // Custom renderer outputs uppercase without prefix
    lv.setRenderer(
        [](ScreenBuffer &sb, int row, const std::string &it, bool, const Theme &theme)
        {
            for (int i = 0; i < static_cast<int>(it.size()); ++i)
            {
                auto &c = sb.at(row, i);
                c.ch = static_cast<char32_t>(std::toupper(it[i]));
                c.style = theme.style(Role::Normal);
            }
        });
    sb.clear(theme.style(Role::Normal));
    lv.paint(sb);
    tio.clear();
    r.draw(sb);
    assert(tio.buffer().find("ONE") != std::string::npos);

    // TreeView expand/collapse
    auto root = std::make_unique<TreeNode>("root");
    root->add(std::make_unique<TreeNode>("child1"));
    auto c2 = root->add(std::make_unique<TreeNode>("child2"));
    c2->add(std::make_unique<TreeNode>("grand"));
    std::vector<std::unique_ptr<TreeNode>> roots;
    roots.push_back(std::move(root));
    TreeView tv(std::move(roots), theme);
    tv.layout({0, 0, 12, 5});
    sb.resize(5, 12);
    sb.clear(theme.style(Role::Normal));
    tv.paint(sb);
    tio.clear();
    r.draw(sb);
    assert(tio.buffer().find('+') != std::string::npos);
    assert(tio.buffer().find("root") != std::string::npos);

    ev = {};
    ev.key.code = KeyEvent::Code::Enter;
    tv.onEvent(ev);
    sb.clear(theme.style(Role::Normal));
    tv.paint(sb);
    tio.clear();
    r.draw(sb);
    assert(tio.buffer().find('-') != std::string::npos);
    assert(tio.buffer().find("root") != std::string::npos);

    ev.key.code = KeyEvent::Code::Down;
    tv.onEvent(ev); // child1
    ev.key.code = KeyEvent::Code::Down;
    tv.onEvent(ev); // child2
    ev.key.code = KeyEvent::Code::Enter;
    tv.onEvent(ev); // expand child2
    sb.clear(theme.style(Role::Normal));
    tv.paint(sb);
    tio.clear();
    r.draw(sb);
    assert(tio.buffer().find("grand") != std::string::npos);

    ev.key.code = KeyEvent::Code::Left;
    tv.onEvent(ev); // collapse child2
    sb.clear(theme.style(Role::Normal));
    tv.paint(sb);
    tio.clear();
    r.draw(sb);
    assert(tio.buffer().find("grand") == std::string::npos);

    return 0;
}
