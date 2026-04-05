//===----------------------------------------------------------------------===//
// Tests for GameUI MenuList HandleInput + GameButton (Plan 09).
//===----------------------------------------------------------------------===//

#include "tests/TestHarness.hpp"

extern "C" {
#include "rt_string.h"
void *rt_uimenulist_new(int64_t x, int64_t y, int64_t item_height);
void rt_uimenulist_add_item(void *menu, rt_string text);
int64_t rt_uimenulist_handle_input(void *menu, int8_t up, int8_t down, int8_t confirm);
int64_t rt_uimenulist_get_selected(void *menu);
rt_string rt_const_cstr(const char *s);

void *rt_gamebutton_new(int64_t x, int64_t y, int64_t w, int64_t h, void *text);
int64_t rt_gamebutton_get_x(void *btn);
int64_t rt_gamebutton_get_y(void *btn);
void rt_gamebutton_set_x(void *btn, int64_t x);
}

TEST(GameMenu, HandleInputDown) {
    void *menu = rt_uimenulist_new(0, 0, 20);
    rt_uimenulist_add_item(menu, rt_const_cstr("Item A"));
    rt_uimenulist_add_item(menu, rt_const_cstr("Item B"));
    rt_uimenulist_add_item(menu, rt_const_cstr("Item C"));

    rt_uimenulist_handle_input(menu, 0, 1, 0); // down
    EXPECT_EQ(rt_uimenulist_get_selected(menu), 1);
    rt_uimenulist_handle_input(menu, 0, 1, 0); // down
    EXPECT_EQ(rt_uimenulist_get_selected(menu), 2);
}

TEST(GameMenu, HandleInputUp) {
    void *menu = rt_uimenulist_new(0, 0, 20);
    rt_uimenulist_add_item(menu, rt_const_cstr("A"));
    rt_uimenulist_add_item(menu, rt_const_cstr("B"));

    rt_uimenulist_handle_input(menu, 1, 0, 0); // up from 0 → wrap to 1
    EXPECT_EQ(rt_uimenulist_get_selected(menu), 1);
}

TEST(GameMenu, WrapAround) {
    void *menu = rt_uimenulist_new(0, 0, 20);
    rt_uimenulist_add_item(menu, rt_const_cstr("A"));
    rt_uimenulist_add_item(menu, rt_const_cstr("B"));
    rt_uimenulist_add_item(menu, rt_const_cstr("C"));

    rt_uimenulist_handle_input(menu, 0, 1, 0); // → 1
    rt_uimenulist_handle_input(menu, 0, 1, 0); // → 2
    rt_uimenulist_handle_input(menu, 0, 1, 0); // → 0 (wrap)
    EXPECT_EQ(rt_uimenulist_get_selected(menu), 0);
}

TEST(GameMenu, ConfirmReturnsIndex) {
    void *menu = rt_uimenulist_new(0, 0, 20);
    rt_uimenulist_add_item(menu, rt_const_cstr("Start"));
    rt_uimenulist_add_item(menu, rt_const_cstr("Quit"));

    rt_uimenulist_handle_input(menu, 0, 1, 0);                  // → 1
    int64_t result = rt_uimenulist_handle_input(menu, 0, 0, 1); // confirm
    EXPECT_EQ(result, 1);
}

TEST(GameMenu, NoInputReturnsNeg1) {
    void *menu = rt_uimenulist_new(0, 0, 20);
    rt_uimenulist_add_item(menu, rt_const_cstr("A"));
    int64_t result = rt_uimenulist_handle_input(menu, 0, 0, 0);
    EXPECT_EQ(result, -1);
}

TEST(GameButton, CreateAndPosition) {
    void *btn = rt_gamebutton_new(10, 20, 200, 40, (void *)rt_const_cstr("Click Me"));
    ASSERT_TRUE(btn != nullptr);
    EXPECT_EQ(rt_gamebutton_get_x(btn), 10);
    EXPECT_EQ(rt_gamebutton_get_y(btn), 20);
    rt_gamebutton_set_x(btn, 50);
    EXPECT_EQ(rt_gamebutton_get_x(btn), 50);
}

int main() {
    return viper_test::run_all_tests();
}
