# Plan: Dialogue / Typewriter Text System

## 1. Summary & Objective

Add a `Viper.Game.Dialogue` class that provides a typewriter text reveal system with speaker labels, word wrapping, pagination, and input advancement. Draws to Canvas as an immediate-mode overlay suitable for RPGs, visual novels, adventure games, and tutorials.

**Why:** Character-by-character text reveal with word wrapping and pagination is non-trivial to implement correctly (~200+ LOC). Every story-driven game needs it. Currently Viper has no way to progressively reveal text, manage dialogue queues, or handle text pagination.

## 2. Scope

**In scope:**
- Character-by-character typewriter reveal
- Configurable reveal speed (characters per second)
- Word-wrapping within bounds
- Speaker name label (drawn above text box)
- Dialogue queue (Say multiple lines, auto-advance)
- Player input: complete current line or advance to next
- Page breaks (when text exceeds box height)
- "Press to continue" indicator
- Optional BitmapFont support
- Draw to Canvas (immediate-mode)
- Background panel with configurable color/alpha

**Out of scope:**
- Choice/branching dialogue (select from options)
- Rich text markup (bold, italic, color per word)
- Portrait/avatar rendering
- Voice acting integration
- Dialogue scripting language / DSL
- Auto-scrolling text
- Localization framework

## 3. Zero-Dependency Implementation Strategy

The typewriter effect is a simple accumulator: each `Update(dt)` advances a character counter, and `Draw()` renders only the first N characters with word wrapping. Word wrapping uses a greedy algorithm (scan forward to next space, check if it fits in remaining line width). Pagination tracks line count vs available height. Pure C, ~400 LOC.

## 4. Technical Requirements

### New Files
- `src/runtime/collections/rt_dialogue.h` — public API
- `src/runtime/collections/rt_dialogue.c` — implementation (~400 LOC)

### C API (rt_dialogue.h)

```c
// Construction / destruction
void   *rt_dialogue_new(int64_t x, int64_t y, int64_t width, int64_t height);
void    rt_dialogue_destroy(void *dlg);

// Configuration
void    rt_dialogue_set_speed(void *dlg, int64_t chars_per_second);   // Default: 30
void    rt_dialogue_set_font(void *dlg, void *font);                   // BitmapFont or NULL
void    rt_dialogue_set_text_color(void *dlg, int64_t color);          // Default: white
void    rt_dialogue_set_speaker_color(void *dlg, int64_t color);       // Default: yellow
void    rt_dialogue_set_bg_color(void *dlg, int64_t color, int64_t alpha); // Panel bg
void    rt_dialogue_set_border_color(void *dlg, int64_t color);
void    rt_dialogue_set_padding(void *dlg, int64_t padding);           // Inner padding pixels
void    rt_dialogue_set_text_scale(void *dlg, int64_t scale);          // For default font
void    rt_dialogue_set_pos(void *dlg, int64_t x, int64_t y);
void    rt_dialogue_set_size(void *dlg, int64_t w, int64_t h);

// Dialogue queue
void    rt_dialogue_say(void *dlg, rt_string speaker, rt_string text);  // Queue a line
void    rt_dialogue_say_text(void *dlg, rt_string text);                // Queue without speaker
void    rt_dialogue_clear(void *dlg);                                    // Clear all queued lines

// Playback
void    rt_dialogue_update(void *dlg, int64_t dt_ms);                  // Advance typewriter
void    rt_dialogue_advance(void *dlg);                                 // Player input: skip/next
void    rt_dialogue_skip(void *dlg);                                    // Complete current line instantly

// State queries
int8_t  rt_dialogue_is_active(void *dlg);         // Any dialogue queued/showing
int8_t  rt_dialogue_is_line_complete(void *dlg);   // Current line fully revealed
int8_t  rt_dialogue_is_finished(void *dlg);         // All lines shown and acknowledged
int8_t  rt_dialogue_is_waiting(void *dlg);          // Line complete, waiting for input
int64_t rt_dialogue_get_line_count(void *dlg);      // Queued lines remaining
int64_t rt_dialogue_get_current_line(void *dlg);    // Current line index
rt_string rt_dialogue_get_speaker(void *dlg);       // Current speaker name

// Rendering
void    rt_dialogue_draw(void *dlg, void *canvas);
```

### Internal Data Structure

```c
#define DLG_MAX_LINES    64
#define DLG_MAX_TEXT_LEN 512

typedef struct {
    char     speaker[64];
    char     text[DLG_MAX_TEXT_LEN];
    int32_t  text_len;
} dlg_line;

struct rt_dialogue_impl {
    // Geometry
    int32_t  x, y, width, height;
    int32_t  padding;
    int32_t  text_scale;

    // Colors
    int32_t  text_color, speaker_color;
    int32_t  bg_color, bg_alpha, border_color;

    // Font
    void    *font;            // BitmapFont handle or NULL

    // Dialogue queue
    dlg_line lines[DLG_MAX_LINES];
    int32_t  line_count;
    int32_t  current_line;

    // Typewriter state
    int32_t  chars_per_second; // Default 30
    int32_t  revealed_chars;   // How many chars shown
    int32_t  accumulator_us;   // Microsecond accumulator

    // State
    int8_t   active;
    int8_t   line_complete;
    int8_t   waiting_for_input;
};
```

### Typewriter Algorithm

```
Update(dt_ms):
    if not active or waiting: return
    accumulator += dt_ms * 1000
    chars_to_add = accumulator / (1_000_000 / chars_per_second)
    accumulator %= (1_000_000 / chars_per_second)
    revealed_chars += chars_to_add
    if revealed_chars >= current_line.text_len:
        line_complete = true
        waiting_for_input = true

Advance():
    if not line_complete:
        revealed_chars = current_line.text_len  // Skip to end
        line_complete = true
        waiting_for_input = true
    else:
        current_line++
        if current_line >= line_count:
            active = false  // All done
        else:
            revealed_chars = 0
            line_complete = false
            waiting_for_input = false
```

### Word Wrap Algorithm

```
Draw: render text[0..revealed_chars] with word wrap:
    cursor_x = x + padding
    cursor_y = y + padding + speaker_height
    for each char in text[0..revealed_chars]:
        if at word boundary:
            measure word width
            if cursor_x + word_width > x + width - padding:
                cursor_x = x + padding
                cursor_y += line_height
                if cursor_y + line_height > y + height - padding:
                    break  // Page full (draw "..." indicator)
        draw char at cursor_x, cursor_y
        cursor_x += char_width
```

## 5. runtime.def Registration

```c
//=============================================================================
// GAME - DIALOGUE SYSTEM
//=============================================================================

RT_FUNC(DialogueNew,            rt_dialogue_new,             "Viper.Game.Dialogue.New",            "obj(i64,i64,i64,i64)")
RT_FUNC(DialogueSetSpeed,       rt_dialogue_set_speed,       "Viper.Game.Dialogue.set_Speed",      "void(obj,i64)")
RT_FUNC(DialogueSetFont,        rt_dialogue_set_font,        "Viper.Game.Dialogue.set_Font",       "void(obj,obj)")
RT_FUNC(DialogueSetTextColor,   rt_dialogue_set_text_color,  "Viper.Game.Dialogue.set_TextColor",  "void(obj,i64)")
RT_FUNC(DialogueSetSpeakerColor,rt_dialogue_set_speaker_color,"Viper.Game.Dialogue.set_SpeakerColor","void(obj,i64)")
RT_FUNC(DialogueSetBgColor,     rt_dialogue_set_bg_color,    "Viper.Game.Dialogue.SetBgColor",     "void(obj,i64,i64)")
RT_FUNC(DialogueSetBorderColor, rt_dialogue_set_border_color,"Viper.Game.Dialogue.set_BorderColor","void(obj,i64)")
RT_FUNC(DialogueSetPadding,     rt_dialogue_set_padding,     "Viper.Game.Dialogue.set_Padding",    "void(obj,i64)")
RT_FUNC(DialogueSetTextScale,   rt_dialogue_set_text_scale,  "Viper.Game.Dialogue.set_TextScale",  "void(obj,i64)")
RT_FUNC(DialogueSetPos,         rt_dialogue_set_pos,         "Viper.Game.Dialogue.SetPos",         "void(obj,i64,i64)")
RT_FUNC(DialogueSetSize,        rt_dialogue_set_size,        "Viper.Game.Dialogue.SetSize",        "void(obj,i64,i64)")
RT_FUNC(DialogueSay,            rt_dialogue_say,             "Viper.Game.Dialogue.Say",            "void(obj,str,str)")
RT_FUNC(DialogueSayText,        rt_dialogue_say_text,        "Viper.Game.Dialogue.SayText",        "void(obj,str)")
RT_FUNC(DialogueClear,          rt_dialogue_clear,           "Viper.Game.Dialogue.Clear",          "void(obj)")
RT_FUNC(DialogueUpdate,         rt_dialogue_update,          "Viper.Game.Dialogue.Update",         "void(obj,i64)")
RT_FUNC(DialogueAdvance,        rt_dialogue_advance,         "Viper.Game.Dialogue.Advance",        "void(obj)")
RT_FUNC(DialogueSkip,           rt_dialogue_skip,            "Viper.Game.Dialogue.Skip",           "void(obj)")
RT_FUNC(DialogueIsActive,       rt_dialogue_is_active,       "Viper.Game.Dialogue.get_IsActive",   "i1(obj)")
RT_FUNC(DialogueIsLineComplete, rt_dialogue_is_line_complete,"Viper.Game.Dialogue.get_IsLineComplete","i1(obj)")
RT_FUNC(DialogueIsFinished,     rt_dialogue_is_finished,     "Viper.Game.Dialogue.get_IsFinished", "i1(obj)")
RT_FUNC(DialogueIsWaiting,      rt_dialogue_is_waiting,      "Viper.Game.Dialogue.get_IsWaiting",  "i1(obj)")
RT_FUNC(DialogueGetLineCount,   rt_dialogue_get_line_count,  "Viper.Game.Dialogue.get_LineCount",  "i64(obj)")
RT_FUNC(DialogueGetCurrentLine, rt_dialogue_get_current_line,"Viper.Game.Dialogue.get_CurrentLine","i64(obj)")
RT_FUNC(DialogueGetSpeaker,     rt_dialogue_get_speaker,     "Viper.Game.Dialogue.get_Speaker",    "str(obj)")
RT_FUNC(DialogueDraw,           rt_dialogue_draw,            "Viper.Game.Dialogue.Draw",           "void(obj,obj)")

RT_CLASS_BEGIN("Viper.Game.Dialogue", Dialogue, "obj", DialogueNew)
    RT_PROP("Speed", "i64", none, DialogueSetSpeed)
    RT_PROP("Font", "obj", none, DialogueSetFont)
    RT_PROP("TextColor", "i64", none, DialogueSetTextColor)
    RT_PROP("SpeakerColor", "i64", none, DialogueSetSpeakerColor)
    RT_PROP("BorderColor", "i64", none, DialogueSetBorderColor)
    RT_PROP("Padding", "i64", none, DialogueSetPadding)
    RT_PROP("TextScale", "i64", none, DialogueSetTextScale)
    RT_PROP("IsActive", "i1", DialogueIsActive, none)
    RT_PROP("IsLineComplete", "i1", DialogueIsLineComplete, none)
    RT_PROP("IsFinished", "i1", DialogueIsFinished, none)
    RT_PROP("IsWaiting", "i1", DialogueIsWaiting, none)
    RT_PROP("LineCount", "i64", DialogueGetLineCount, none)
    RT_PROP("CurrentLine", "i64", DialogueGetCurrentLine, none)
    RT_PROP("Speaker", "str", DialogueGetSpeaker, none)
    RT_METHOD("Say", "void(str,str)", DialogueSay)
    RT_METHOD("SayText", "void(str)", DialogueSayText)
    RT_METHOD("Clear", "void()", DialogueClear)
    RT_METHOD("Update", "void(i64)", DialogueUpdate)
    RT_METHOD("Advance", "void()", DialogueAdvance)
    RT_METHOD("Skip", "void()", DialogueSkip)
    RT_METHOD("SetBgColor", "void(i64,i64)", DialogueSetBgColor)
    RT_METHOD("SetPos", "void(i64,i64)", DialogueSetPos)
    RT_METHOD("SetSize", "void(i64,i64)", DialogueSetSize)
    RT_METHOD("Draw", "void(obj)", DialogueDraw)
RT_CLASS_END()
```

## 6. CMakeLists.txt Changes

In `src/runtime/CMakeLists.txt`, add to `RT_COLLECTIONS_SOURCES`:
```cmake
collections/rt_dialogue.c
```

## 7. Error Handling

| Scenario | Behavior |
|----------|----------|
| Say() when queue full (64 lines) | Oldest line dropped |
| Text > 512 chars | Truncated |
| Speaker name > 63 chars | Truncated |
| Speed = 0 | All text revealed instantly |
| Speed < 0 | Clamped to 1 |
| Advance() when not active | No-op |
| Draw() with NULL canvas | No-op |
| NULL font | Use built-in 8x8 font (default) |
| Width/height too small for any text | Draw bg panel only |

## 8. Tests

### Zia Runtime Tests (`tests/runtime/test_dialogue.zia`)

1. **Basic typewriter progression**
   - Given: `dlg.Say("NPC", "Hello world")`
   - When: `dlg.Update(100)` with speed=10 chars/sec → 1 char revealed
   - Then: `dlg.IsLineComplete == false`, text partially visible

2. **Line completion**
   - Given: `dlg.Say("NPC", "Hi")` with speed=100
   - When: `dlg.Update(1000)` (enough for all chars)
   - Then: `dlg.IsLineComplete == true`, `dlg.IsWaiting == true`

3. **Advance skips then progresses**
   - Given: `dlg.Say("A", "First")`, `dlg.Say("B", "Second")` — mid-reveal
   - When: `dlg.Advance()` (skips to end of first) then `dlg.Advance()` (moves to second)
   - Then: `dlg.CurrentLine == 1`, `dlg.Speaker == "B"`

4. **Finished state**
   - Given: Single-line dialogue, fully revealed and advanced
   - Then: `dlg.IsFinished == true`, `dlg.IsActive == false`

5. **Empty text handling**
   - When: `dlg.SayText("")`
   - Then: Line immediately complete on first Update

6. **Queue multiple lines**
   - When: 5 lines queued via Say()
   - Then: `dlg.LineCount == 5`, advances through all in order

## 9. Documentation Deliverables

| Action | File |
|--------|------|
| CREATE | `docs/viperlib/game/dialogue.md` — full Dialogue API reference with examples |
| UPDATE | `docs/viperlib/game.md` — add `Viper.Game.Dialogue` to contents |

## 10. Code References

| File | Role |
|------|------|
| `src/runtime/collections/rt_screenfx.c` | Pattern: game collection class with state machine |
| `src/runtime/collections/rt_statemachine.c` | Pattern: state tracking |
| `src/runtime/graphics/rt_drawing.c` | Canvas.Text/TextScaled for rendering |
| `src/runtime/graphics/rt_font.h` | Built-in font metrics |
| `src/runtime/collections/rt_buttongroup.c` | Pattern: selection/navigation widget |
| `src/il/runtime/runtime.def` | Registration (add after ScreenFX block) |
