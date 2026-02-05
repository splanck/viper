//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tui/tests/test_app_layout.cpp
// Purpose: Test suite for this component.
// Key invariants: To be documented.
// Ownership/Lifetime: To be documented.
// Links: docs/architecture.md
//
//===----------------------------------------------------------------------===//

#include "tui/app.hpp"
#include "tui/render/screen.hpp"
#include "tui/term/term_io.hpp"
#include "tui/ui/container.hpp"

#include "tests/TestHarness.hpp"
#include <memory>
#include <string>

using viper::tui::App;
using viper::tui::render::ScreenBuffer;
using viper::tui::term::StringTermIO;
using viper::tui::ui::VStack;
using viper::tui::ui::Widget;

struct CharWidget : Widget
{
    char ch;

    explicit CharWidget(char c) : ch(c) {}

    void paint(ScreenBuffer &sb) override
    {
        auto r = rect();
        for (int y = r.y; y < r.y + r.h; ++y)
        {
            for (int x = r.x; x < r.x + r.w; ++x)
            {
                sb.at(y, x).ch = ch;
            }
        }
    }
};

TEST(TUI, AppLayout)
{
    auto root = std::make_unique<VStack>();
    auto a = std::make_unique<CharWidget>('A');
    auto b = std::make_unique<CharWidget>('B');
    CharWidget *ap = a.get();
    CharWidget *bp = b.get();
    root->addChild(std::move(a));
    root->addChild(std::move(b));
    StringTermIO tio;
    App app(std::move(root), tio, 2, 2);
    app.tick();
    ASSERT_EQ(ap->rect().h, 1);
    ASSERT_EQ(bp->rect().y, 1);
    const std::string &out = tio.buffer();
    ASSERT_TRUE(out.find('A') != std::string::npos);
    ASSERT_TRUE(out.find('B') != std::string::npos);
}

int main(int argc, char **argv)
{
    viper_test::init(&argc, argv);
    return viper_test::run_all_tests();
}
