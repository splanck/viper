# Plan: Game UI Overlay System

## 1. Summary & Objective

Add a `Viper.Game.UI` module with lightweight in-game UI widgets: Label, ProgressBar, Panel, NineSlice, and MenuList. These draw directly to a Canvas (not the desktop ViperGUI widget system) and are purpose-built for game HUDs, menus, and overlays.

**Why:** The sidescroller's `hud.zia` is 155 lines of manual heart-drawing, progress bars, and text positioning using raw canvas primitives. Every game reinvents this. A game UI layer reduces HUD code by ~80% and provides consistent visual quality.

## 2. Scope

**In scope:**
- `UILabel` — positioned text with optional BitmapFont
- `UIBar` — progress/health/XP bar with fill, background, and border
- `UIPanel` — semi-transparent rectangular panel with optional border
- `UINineSlice` — scalable bordered UI element from a 9-region Pixels source
- `UIMenuList` — vertical menu with selection highlight and keyboard navigation
- All widgets draw to Canvas (immediate-mode style)
- Color, alpha, and basic styling per widget
- Stubs for non-graphics builds

**Out of scope:**
- Layout system / auto-positioning (widgets use explicit x,y)
- Widget tree / parent-child hierarchy (use Scene graph for that)
- Event system / mouse click routing (handled by Input system)
- Text input / text fields (complex IME handling)
- Animations / tweened transitions (use Tween for that)
- The desktop ViperGUI system (`rt_gui.h`) — completely separate

## 3. Zero-Dependency Implementation Strategy

All widgets are thin wrappers around existing Canvas drawing primitives (Box, BoxAlpha, Frame, Text, TextScaled, Blit, BlitRegion). No new rendering primitives needed. NineSlice uses existing Pixels.Copy and Canvas.BlitRegion. Pure C, ~500 LOC total.

## 4. Technical Requirements

### New Files
- `src/runtime/collections/rt_gameui.h` — public API declarations
- `src/runtime/collections/rt_gameui.c` — implementation (~500 LOC)

### C API (rt_gameui.h)

```c
// === UILabel ===
void *rt_uilabel_new(int64_t x, int64_t y, rt_string text, int64_t color);
void  rt_uilabel_set_text(void *label, rt_string text);
void  rt_uilabel_set_pos(void *label, int64_t x, int64_t y);
void  rt_uilabel_set_color(void *label, int64_t color);
void  rt_uilabel_set_font(void *label, void *font);     // BitmapFont or NULL for default
void  rt_uilabel_set_scale(void *label, int64_t scale);  // Integer scale (1,2,3...)
void  rt_uilabel_set_visible(void *label, int8_t visible);
void  rt_uilabel_draw(void *label, void *canvas);
int64_t rt_uilabel_get_x(void *label);
int64_t rt_uilabel_get_y(void *label);

// === UIBar ===
void *rt_uibar_new(int64_t x, int64_t y, int64_t w, int64_t h,
                    int64_t fg_color, int64_t bg_color);
void  rt_uibar_set_value(void *bar, int64_t value, int64_t max_value);
void  rt_uibar_set_pos(void *bar, int64_t x, int64_t y);
void  rt_uibar_set_size(void *bar, int64_t w, int64_t h);
void  rt_uibar_set_colors(void *bar, int64_t fg, int64_t bg);
void  rt_uibar_set_border(void *bar, int64_t color);     // 0 = no border
void  rt_uibar_set_direction(void *bar, int64_t dir);     // 0=left-to-right, 1=right-to-left, 2=bottom-to-top, 3=top-to-bottom
void  rt_uibar_set_visible(void *bar, int8_t visible);
void  rt_uibar_draw(void *bar, void *canvas);
int64_t rt_uibar_get_value(void *bar);
int64_t rt_uibar_get_max(void *bar);

// === UIPanel ===
void *rt_uipanel_new(int64_t x, int64_t y, int64_t w, int64_t h,
                      int64_t bg_color, int64_t alpha);
void  rt_uipanel_set_pos(void *panel, int64_t x, int64_t y);
void  rt_uipanel_set_size(void *panel, int64_t w, int64_t h);
void  rt_uipanel_set_color(void *panel, int64_t bg_color, int64_t alpha);
void  rt_uipanel_set_border(void *panel, int64_t color, int64_t thickness);
void  rt_uipanel_set_corner_radius(void *panel, int64_t radius);
void  rt_uipanel_set_visible(void *panel, int8_t visible);
void  rt_uipanel_draw(void *panel, void *canvas);

// === UINineSlice ===
void *rt_uinineslice_new(void *pixels, int64_t left, int64_t top,
                          int64_t right, int64_t bottom);
void  rt_uinineslice_draw(void *ns, void *canvas, int64_t x, int64_t y,
                           int64_t w, int64_t h);
void  rt_uinineslice_set_tint(void *ns, int64_t color); // 0 = no tint

// === UIMenuList ===
void   *rt_uimenulist_new(int64_t x, int64_t y, int64_t item_height);
void    rt_uimenulist_add_item(void *menu, rt_string text);
void    rt_uimenulist_clear(void *menu);
void    rt_uimenulist_set_selected(void *menu, int64_t index);
int64_t rt_uimenulist_get_selected(void *menu);
void    rt_uimenulist_move_up(void *menu);
void    rt_uimenulist_move_down(void *menu);
void    rt_uimenulist_set_colors(void *menu, int64_t text_color,
                                  int64_t selected_color, int64_t highlight_bg);
void    rt_uimenulist_set_font(void *menu, void *font);
void    rt_uimenulist_set_visible(void *menu, int8_t visible);
int64_t rt_uimenulist_get_count(void *menu);
void    rt_uimenulist_draw(void *menu, void *canvas);
```

## 5. runtime.def Registration

```c
//=============================================================================
// GAME - UI WIDGETS
//=============================================================================

// UILabel
RT_FUNC(UILabelNew,        rt_uilabel_new,        "Viper.Game.UI.Label.New",        "obj(i64,i64,str,i64)")
RT_FUNC(UILabelSetText,    rt_uilabel_set_text,    "Viper.Game.UI.Label.SetText",    "void(obj,str)")
RT_FUNC(UILabelSetPos,     rt_uilabel_set_pos,     "Viper.Game.UI.Label.SetPos",     "void(obj,i64,i64)")
RT_FUNC(UILabelSetColor,   rt_uilabel_set_color,   "Viper.Game.UI.Label.set_Color",  "void(obj,i64)")
RT_FUNC(UILabelSetFont,    rt_uilabel_set_font,    "Viper.Game.UI.Label.set_Font",   "void(obj,obj)")
RT_FUNC(UILabelSetScale,   rt_uilabel_set_scale,   "Viper.Game.UI.Label.set_Scale",  "void(obj,i64)")
RT_FUNC(UILabelSetVisible, rt_uilabel_set_visible, "Viper.Game.UI.Label.set_Visible","void(obj,i1)")
RT_FUNC(UILabelDraw,       rt_uilabel_draw,        "Viper.Game.UI.Label.Draw",       "void(obj,obj)")
RT_FUNC(UILabelGetX,       rt_uilabel_get_x,       "Viper.Game.UI.Label.get_X",      "i64(obj)")
RT_FUNC(UILabelGetY,       rt_uilabel_get_y,       "Viper.Game.UI.Label.get_Y",      "i64(obj)")

RT_CLASS_BEGIN("Viper.Game.UI.Label", UILabel, "obj", UILabelNew)
    RT_PROP("X", "i64", UILabelGetX, none)
    RT_PROP("Y", "i64", UILabelGetY, none)
    RT_PROP("Color", "i64", none, UILabelSetColor)
    RT_PROP("Font", "obj", none, UILabelSetFont)
    RT_PROP("Scale", "i64", none, UILabelSetScale)
    RT_PROP("Visible", "i1", none, UILabelSetVisible)
    RT_METHOD("SetText", "void(str)", UILabelSetText)
    RT_METHOD("SetPos", "void(i64,i64)", UILabelSetPos)
    RT_METHOD("Draw", "void(obj)", UILabelDraw)
RT_CLASS_END()

// UIBar
RT_FUNC(UIBarNew,          rt_uibar_new,          "Viper.Game.UI.Bar.New",          "obj(i64,i64,i64,i64,i64,i64)")
RT_FUNC(UIBarSetValue,     rt_uibar_set_value,     "Viper.Game.UI.Bar.SetValue",     "void(obj,i64,i64)")
RT_FUNC(UIBarSetPos,       rt_uibar_set_pos,       "Viper.Game.UI.Bar.SetPos",       "void(obj,i64,i64)")
RT_FUNC(UIBarSetSize,      rt_uibar_set_size,      "Viper.Game.UI.Bar.SetSize",      "void(obj,i64,i64)")
RT_FUNC(UIBarSetColors,    rt_uibar_set_colors,    "Viper.Game.UI.Bar.SetColors",    "void(obj,i64,i64)")
RT_FUNC(UIBarSetBorder,    rt_uibar_set_border,    "Viper.Game.UI.Bar.set_Border",   "void(obj,i64)")
RT_FUNC(UIBarSetDir,       rt_uibar_set_direction, "Viper.Game.UI.Bar.set_Direction","void(obj,i64)")
RT_FUNC(UIBarSetVisible,   rt_uibar_set_visible,   "Viper.Game.UI.Bar.set_Visible",  "void(obj,i1)")
RT_FUNC(UIBarDraw,         rt_uibar_draw,          "Viper.Game.UI.Bar.Draw",         "void(obj,obj)")
RT_FUNC(UIBarGetValue,     rt_uibar_get_value,     "Viper.Game.UI.Bar.get_Value",    "i64(obj)")
RT_FUNC(UIBarGetMax,       rt_uibar_get_max,       "Viper.Game.UI.Bar.get_Max",      "i64(obj)")

RT_CLASS_BEGIN("Viper.Game.UI.Bar", UIBar, "obj", UIBarNew)
    RT_PROP("Value", "i64", UIBarGetValue, none)
    RT_PROP("Max", "i64", UIBarGetMax, none)
    RT_PROP("Border", "i64", none, UIBarSetBorder)
    RT_PROP("Direction", "i64", none, UIBarSetDir)
    RT_PROP("Visible", "i1", none, UIBarSetVisible)
    RT_METHOD("SetValue", "void(i64,i64)", UIBarSetValue)
    RT_METHOD("SetPos", "void(i64,i64)", UIBarSetPos)
    RT_METHOD("SetSize", "void(i64,i64)", UIBarSetSize)
    RT_METHOD("SetColors", "void(i64,i64)", UIBarSetColors)
    RT_METHOD("Draw", "void(obj)", UIBarDraw)
RT_CLASS_END()

// UIPanel
RT_FUNC(UIPanelNew,           rt_uipanel_new,              "Viper.Game.UI.Panel.New",           "obj(i64,i64,i64,i64,i64,i64)")
RT_FUNC(UIPanelSetPos,        rt_uipanel_set_pos,          "Viper.Game.UI.Panel.SetPos",        "void(obj,i64,i64)")
RT_FUNC(UIPanelSetSize,       rt_uipanel_set_size,         "Viper.Game.UI.Panel.SetSize",       "void(obj,i64,i64)")
RT_FUNC(UIPanelSetColor,      rt_uipanel_set_color,        "Viper.Game.UI.Panel.SetColor",      "void(obj,i64,i64)")
RT_FUNC(UIPanelSetBorder,     rt_uipanel_set_border,       "Viper.Game.UI.Panel.SetBorder",     "void(obj,i64,i64)")
RT_FUNC(UIPanelSetCorner,     rt_uipanel_set_corner_radius,"Viper.Game.UI.Panel.set_CornerRadius","void(obj,i64)")
RT_FUNC(UIPanelSetVisible,    rt_uipanel_set_visible,      "Viper.Game.UI.Panel.set_Visible",   "void(obj,i1)")
RT_FUNC(UIPanelDraw,          rt_uipanel_draw,             "Viper.Game.UI.Panel.Draw",          "void(obj,obj)")

RT_CLASS_BEGIN("Viper.Game.UI.Panel", UIPanel, "obj", UIPanelNew)
    RT_PROP("CornerRadius", "i64", none, UIPanelSetCorner)
    RT_PROP("Visible", "i1", none, UIPanelSetVisible)
    RT_METHOD("SetPos", "void(i64,i64)", UIPanelSetPos)
    RT_METHOD("SetSize", "void(i64,i64)", UIPanelSetSize)
    RT_METHOD("SetColor", "void(i64,i64)", UIPanelSetColor)
    RT_METHOD("SetBorder", "void(i64,i64)", UIPanelSetBorder)
    RT_METHOD("Draw", "void(obj)", UIPanelDraw)
RT_CLASS_END()

// UINineSlice
RT_FUNC(UINineSliceNew,     rt_uinineslice_new,     "Viper.Game.UI.NineSlice.New",     "obj(obj,i64,i64,i64,i64)")
RT_FUNC(UINineSliceDraw,    rt_uinineslice_draw,    "Viper.Game.UI.NineSlice.Draw",    "void(obj,obj,i64,i64,i64,i64)")
RT_FUNC(UINineSliceTint,    rt_uinineslice_set_tint,"Viper.Game.UI.NineSlice.set_Tint","void(obj,i64)")

RT_CLASS_BEGIN("Viper.Game.UI.NineSlice", UINineSlice, "obj", UINineSliceNew)
    RT_PROP("Tint", "i64", none, UINineSliceTint)
    RT_METHOD("Draw", "void(obj,i64,i64,i64,i64)", UINineSliceDraw)
RT_CLASS_END()

// UIMenuList
RT_FUNC(UIMenuListNew,        rt_uimenulist_new,         "Viper.Game.UI.MenuList.New",        "obj(i64,i64,i64)")
RT_FUNC(UIMenuListAddItem,    rt_uimenulist_add_item,    "Viper.Game.UI.MenuList.AddItem",    "void(obj,str)")
RT_FUNC(UIMenuListClear,      rt_uimenulist_clear,       "Viper.Game.UI.MenuList.Clear",      "void(obj)")
RT_FUNC(UIMenuListGetSel,     rt_uimenulist_get_selected,"Viper.Game.UI.MenuList.get_Selected","i64(obj)")
RT_FUNC(UIMenuListSetSel,     rt_uimenulist_set_selected,"Viper.Game.UI.MenuList.set_Selected","void(obj,i64)")
RT_FUNC(UIMenuListMoveUp,     rt_uimenulist_move_up,     "Viper.Game.UI.MenuList.MoveUp",     "void(obj)")
RT_FUNC(UIMenuListMoveDown,   rt_uimenulist_move_down,   "Viper.Game.UI.MenuList.MoveDown",   "void(obj)")
RT_FUNC(UIMenuListSetColors,  rt_uimenulist_set_colors,  "Viper.Game.UI.MenuList.SetColors",  "void(obj,i64,i64,i64)")
RT_FUNC(UIMenuListSetFont,    rt_uimenulist_set_font,    "Viper.Game.UI.MenuList.set_Font",   "void(obj,obj)")
RT_FUNC(UIMenuListSetVisible, rt_uimenulist_set_visible, "Viper.Game.UI.MenuList.set_Visible","void(obj,i1)")
RT_FUNC(UIMenuListGetCount,   rt_uimenulist_get_count,   "Viper.Game.UI.MenuList.get_Count",  "i64(obj)")
RT_FUNC(UIMenuListDraw,       rt_uimenulist_draw,        "Viper.Game.UI.MenuList.Draw",       "void(obj,obj)")

RT_CLASS_BEGIN("Viper.Game.UI.MenuList", UIMenuList, "obj", UIMenuListNew)
    RT_PROP("Selected", "i64", UIMenuListGetSel, UIMenuListSetSel)
    RT_PROP("Count", "i64", UIMenuListGetCount, none)
    RT_PROP("Font", "obj", none, UIMenuListSetFont)
    RT_PROP("Visible", "i1", none, UIMenuListSetVisible)
    RT_METHOD("AddItem", "void(str)", UIMenuListAddItem)
    RT_METHOD("Clear", "void()", UIMenuListClear)
    RT_METHOD("MoveUp", "void()", UIMenuListMoveUp)
    RT_METHOD("MoveDown", "void()", UIMenuListMoveDown)
    RT_METHOD("SetColors", "void(i64,i64,i64)", UIMenuListSetColors)
    RT_METHOD("Draw", "void(obj)", UIMenuListDraw)
RT_CLASS_END()
```

## 6. CMakeLists.txt Changes

In `src/runtime/CMakeLists.txt`, add to `RT_COLLECTIONS_SOURCES`:
```cmake
collections/rt_gameui.c
```

## 7. Error Handling

| Scenario | Behavior |
|----------|----------|
| NULL canvas passed to Draw | No-op |
| NULL font on Label | Falls back to built-in 8x8 font |
| UIBar value > max | Clamped to max |
| UIBar value < 0 | Clamped to 0 |
| UIMenuList empty + MoveUp/MoveDown | No-op |
| UIMenuList index out of range | Clamped to valid range |
| NineSlice margins exceed source dimensions | Clamped, center region collapsed |
| UIMenuList.AddItem with NULL string | No-op |
| Panel alpha out of 0-255 range | Clamped |

## 8. Tests

### Zia Runtime Tests (`tests/runtime/test_gameui.zia`)

1. **UIBar value clamping**
   - Given: `Bar.New(0,0,100,20, 0xFF0000, 0x333333)`
   - When: `bar.SetValue(75, 100)` then `bar.SetValue(150, 100)`
   - Then: `bar.Value == 100` (clamped), `bar.Max == 100`

2. **UIMenuList navigation**
   - Given: MenuList with 3 items
   - When: `menu.MoveDown()` twice, then `menu.MoveUp()` once
   - Then: `menu.Selected == 1`

3. **UIMenuList wrap-around**
   - Given: MenuList with 3 items, Selected = 0
   - When: `menu.MoveUp()`
   - Then: `menu.Selected == 2` (wraps to bottom)

4. **UILabel draw smoke test**
   - Given: Canvas + Label
   - When: `label.Draw(canvas)`
   - Then: No crash

5. **NineSlice creation**
   - Given: Pixels buffer 32x32
   - When: `NineSlice.New(pixels, 8, 8, 8, 8)`
   - Then: Non-null handle returned

## 9. Documentation Deliverables

| Action | File |
|--------|------|
| CREATE | `docs/viperlib/game/ui.md` — full UILabel/UIBar/UIPanel/UINineSlice/UIMenuList reference |
| UPDATE | `docs/viperlib/game.md` — add `Viper.Game.UI.*` section to contents |
| UPDATE | `docs/viperlib/game/README.md` — add UI widgets entry |

## 10. Code References

| File | Role |
|------|------|
| `src/runtime/collections/rt_screenfx.c` | Pattern: game collection class with GC |
| `src/runtime/collections/rt_buttongroup.c` | Pattern: existing menu-like widget (ButtonGroup) |
| `src/runtime/graphics/rt_drawing.c` | Canvas Box/Frame/BoxAlpha drawing functions |
| `src/runtime/graphics/rt_drawing_advanced.c` | RoundBox/RoundFrame for Panel corners |
| `src/runtime/graphics/rt_canvas.c` | Canvas.Blit/BlitRegion for NineSlice |
| `examples/games/sidescroller/hud.zia` | Evidence of manual UI drawing pain |
| `src/il/runtime/runtime.def` | Registration (add after ScreenFX block ~line 8166) |
