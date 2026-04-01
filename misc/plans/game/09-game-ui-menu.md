# Plan 09: Game UI Button + Menu Enhancements

## Context

Viper.Game.UI has Label, Bar, Panel, NineSlice, and MenuList. MenuList already provides
selection state + rendering (Draw, MoveUp, MoveDown, SetSelected). ButtonGroup tracks
selection but has no rendering.

Despite MenuList existing, XENOSCAPE's menu.zia (1039 lines) does ALL rendering with
raw canvas calls — suggesting MenuList doesn't provide enough customization, or the
demo predates it. The gap is: no standalone Button class, no input handling integration
(MenuList requires manual MoveUp/MoveDown calls), and limited styling.

## Design

Enhance the existing MenuList with input handling integration (HandleInput method that
accepts up/down/confirm booleans and returns selected index), and add a standalone
Game.UI.Button class with visual states (normal/hover/selected) for custom layouts.

NOTE: UIMenuList already exists with Draw()/MoveUp()/MoveDown()/SetSelected().
This plan EXTENDS it rather than replacing it.

## Changes

### New file: `src/runtime/game/rt_gameui_button.c` (~200 LOC)

**Button struct:**
```c
typedef struct rt_gameui_button_impl {
    int64_t x, y, width, height;
    char text[64];
    int64_t text_scale;
    // Colors per state
    int64_t color_normal;       // Background
    int64_t color_hover;
    int64_t color_selected;
    int64_t text_color;
    int64_t text_color_selected;
    int64_t border_color;
    int64_t border_width;
    int64_t corner_radius;
    int8_t visible;
    int8_t enabled;
} rt_gameui_button_impl;
```

**Functions:**
```c
void *rt_gameui_button_new(int64_t x, int64_t y, int64_t w, int64_t h, rt_string text);
void rt_gameui_button_set_text(void *btn, rt_string text);
void rt_gameui_button_set_colors(void *btn, int64_t normal, int64_t hover, int64_t selected);
void rt_gameui_button_set_text_colors(void *btn, int64_t normal, int64_t selected);
void rt_gameui_button_set_border(void *btn, int64_t width, int64_t color);
void rt_gameui_button_draw(void *btn, void *canvas, int8_t is_selected);
```

Draw method renders: background box (color depends on selected state) + centered text.

### New file: `src/runtime/game/rt_gameui_menu.c` (~250 LOC)

**Menu struct:**
```c
#define MENU_MAX_ITEMS 16

typedef struct rt_gameui_menu_impl {
    void *buttons[MENU_MAX_ITEMS]; // Button objects
    int32_t count;
    int32_t selected;              // Currently selected index
    int64_t x, y;                  // Menu origin
    int64_t spacing;               // Vertical spacing between items
    int64_t item_width, item_height;
    int8_t wrap;                   // Wrap selection at ends
    // Style
    int64_t color_normal;
    int64_t color_hover;
    int64_t color_selected;
    int64_t text_color;
    int64_t text_color_selected;
    int64_t border_color;
    int64_t text_scale;
} rt_gameui_menu_impl;
```

**Functions:**
```c
void *rt_gameui_menu_new(int64_t x, int64_t y, int64_t item_w, int64_t item_h);
void rt_gameui_menu_add_item(void *menu, rt_string text);
void rt_gameui_menu_set_spacing(void *menu, int64_t spacing);
void rt_gameui_menu_set_colors(void *menu, int64_t normal, int64_t hover, int64_t selected);
void rt_gameui_menu_set_text_colors(void *menu, int64_t normal, int64_t selected);
void rt_gameui_menu_set_wrap(void *menu, int8_t wrap);

// Input: pass named action states. Handles up/down navigation.
// Returns selected item index when confirm pressed, -1 otherwise.
int64_t rt_gameui_menu_handle_input(void *menu, int8_t up, int8_t down, int8_t confirm);

void rt_gameui_menu_draw(void *menu, void *canvas);

// State
int64_t rt_gameui_menu_get_selected(void *menu);
void rt_gameui_menu_set_selected(void *menu, int64_t index);
int64_t rt_gameui_menu_get_count(void *menu);
```

**HandleInput logic:**
1. If `up` pressed: selected = (selected - 1 + count) % count (or clamp if !wrap)
2. If `down` pressed: selected = (selected + 1) % count (or clamp)
3. If `confirm` pressed: return selected index
4. Otherwise return -1

**Draw logic:**
For each button, draw at (x, y + i * (item_height + spacing)) with selected state.

### runtime.def
```
RT_CLASS_BEGIN("Viper.Game.UI.GameButton", GameButton, "obj", GameButtonNew)
    RT_METHOD("SetText",       "void(str)",              GameButtonSetText)
    RT_METHOD("SetColors",     "void(i64,i64,i64)",      GameButtonSetColors)
    RT_METHOD("SetTextColors", "void(i64,i64)",           GameButtonSetTextColors)
    RT_METHOD("SetBorder",     "void(i64,i64)",           GameButtonSetBorder)
    RT_METHOD("Draw",          "void(obj,i1)",            GameButtonDraw)
    RT_PROP("X", "i64", GameButtonGetX, GameButtonSetX)
    RT_PROP("Y", "i64", GameButtonGetY, GameButtonSetY)
RT_CLASS_END()

RT_CLASS_BEGIN("Viper.Game.UI.Menu", GameMenu, "obj", GameMenuNew)
    RT_METHOD("AddItem",       "void(str)",               GameMenuAddItem)
    RT_METHOD("SetSpacing",    "void(i64)",               GameMenuSetSpacing)
    RT_METHOD("SetColors",     "void(i64,i64,i64)",       GameMenuSetColors)
    RT_METHOD("SetTextColors", "void(i64,i64)",           GameMenuSetTextColors)
    RT_METHOD("SetWrap",       "void(i1)",                GameMenuSetWrap)
    RT_METHOD("HandleInput",   "i64(i1,i1,i1)",           GameMenuHandleInput)
    RT_METHOD("Draw",          "void(obj)",               GameMenuDraw)
    RT_PROP("Selected",        "i64", GameMenuGetSelected, GameMenuSetSelected)
    RT_PROP("Count",           "i64", GameMenuGetCount, none)
RT_CLASS_END()
```

### Zia usage
```zia
var menu = GameMenu.New(SCREEN_W / 2 - 150, 300, 300, 50)
menu.AddItem("Start Game")
menu.AddItem("Options")
menu.AddItem("Quit")
menu.SetColors(0x202040, 0x303060, 0x4040A0)  // normal, hover, selected
menu.SetTextColors(0xCCCCCC, 0xFFFFFF)
menu.SetSpacing(10)
menu.SetWrap(true)

// In update:
var choice = menu.HandleInput(upPressed, downPressed, confirmPressed)
if choice == 0 { scenes.Switch("playing") }
if choice == 1 { scenes.Switch("options") }
if choice == 2 { quit() }

// In draw:
menu.Draw(canvas)
```

### Files to modify
- New: `src/runtime/game/rt_gameui_button.c` (~200 LOC)
- New: `src/runtime/game/rt_gameui_menu.c` (~250 LOC)
- New: `src/runtime/game/rt_gameui_button.h` (~30 LOC)
- New: `src/runtime/game/rt_gameui_menu.h` (~30 LOC)
- `src/il/runtime/runtime.def` — ~20 entries
- `src/il/runtime/RuntimeSignatures.cpp` — include headers
- `src/il/runtime/classes/RuntimeClasses.hpp` — add RTCLS_GameButton, RTCLS_GameMenu
- `src/runtime/CMakeLists.txt` — add sources

### Tests

**File:** `src/tests/unit/runtime/TestGameMenu.cpp`
```
TEST(GameMenu, AddItemsAndCount)
TEST(GameMenu, HandleInputDown)
  — Down pressed, verify selected advances

TEST(GameMenu, HandleInputUp)
  — Up pressed, verify selected decrements

TEST(GameMenu, WrapAround)
  — At last item, down wraps to 0

TEST(GameMenu, NoWrapClamps)
  — Wrap off, at last item, down stays at last

TEST(GameMenu, ConfirmReturnsIndex)
  — Navigate to item 2, confirm, verify returns 2

TEST(GameMenu, NoInputReturnsNeg1)
  — No buttons pressed, verify returns -1

TEST(GameButton, DrawStates)
  — Draw with selected=false and selected=true, no crash
```

### Doc update
- New: `docs/viperlib/game/ui-menu.md`
