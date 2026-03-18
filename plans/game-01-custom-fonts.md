# Plan: Custom Bitmap Font Rendering

## 1. Summary & Objective

Add a `Viper.Graphics.BitmapFont` class that loads variable-size bitmap fonts from BDF (Bitmap Distribution Format) and PSF (PC Screen Font) files. Extends Canvas with font-aware text drawing methods. Replaces the hardcoded 8x8 bitmap font as the only text option.

**Why:** Every game demo uses `TextScaled(..., 2, ...)` for chunky 16px text. Title screens, HUDs, and menus need readable, attractive, variable-size fonts. This is the single highest-impact improvement for visual quality.

## 2. Scope

**In scope:**
- BDF file parser (pure C, ~250 LOC)
- PSF v1/v2 file parser (pure C, ~100 LOC)
- `BitmapFont` runtime class with glyph cache
- Canvas integration: draw text with custom font
- Text measurement (width, height) per font
- Color and background color support
- Multi-font: games can load multiple fonts at different sizes simultaneously
- Stubs for non-graphics builds

**Out of scope:**
- TrueType / OpenType / vector fonts (would require rasterizer, violates zero-dependency)
- Unicode beyond Latin-1 (BDF supports it, but initial scope is ASCII + Latin-1)
- Font atlas / GPU texture (software renderer only)
- Kerning (BDF fonts are monospaced or have per-glyph widths, no kern tables)
- Anti-aliased rendering (BDF glyphs are 1-bit bitmaps)

## 3. Zero-Dependency Implementation Strategy

**BDF format** is a plain-text format defined by Adobe (X11). Each glyph is encoded as hex rows. Parser reads line-by-line: `STARTCHAR`, `ENCODING`, `BBX`, `BITMAP` blocks. No compression, no binary structures — just string parsing and hex decoding. ~250 LOC.

**PSF format** is a simple binary format (Linux console fonts). v1: 2-byte header + raw glyph bitmaps. v2: 32-byte header, optional Unicode table. ~100 LOC.

Both are public domain formats with extensive free font libraries.

## 4. Technical Requirements

### New Files
- `src/runtime/graphics/rt_bitmapfont.h` — public API declarations
- `src/runtime/graphics/rt_bitmapfont.c` — implementation (~600 LOC total)

### C API (rt_bitmapfont.h)

```c
// Opaque handle
typedef struct rt_bitmapfont_impl *rt_bitmapfont;

// Construction / destruction
void       *rt_bitmapfont_load_bdf(rt_string path);     // Load BDF file → BitmapFont handle
void       *rt_bitmapfont_load_psf(rt_string path);     // Load PSF file → BitmapFont handle
void        rt_bitmapfont_destroy(void *font);           // GC finalizer

// Properties
int64_t     rt_bitmapfont_char_width(void *font);        // Max glyph width (or 0 for proportional)
int64_t     rt_bitmapfont_char_height(void *font);       // Line height in pixels
int64_t     rt_bitmapfont_glyph_count(void *font);       // Number of loaded glyphs
int8_t      rt_bitmapfont_is_monospace(void *font);      // 1 if all glyphs same width

// Text measurement
int64_t     rt_bitmapfont_text_width(void *font, rt_string text);   // Width of string in pixels
int64_t     rt_bitmapfont_text_height(void *font);                   // Same as char_height

// Canvas drawing (extends rt_canvas)
void        rt_canvas_text_font(void *canvas, int64_t x, int64_t y,
                                 rt_string text, void *font, int64_t color);
void        rt_canvas_text_font_bg(void *canvas, int64_t x, int64_t y,
                                    rt_string text, void *font,
                                    int64_t fg_color, int64_t bg_color);
void        rt_canvas_text_font_scaled(void *canvas, int64_t x, int64_t y,
                                        rt_string text, void *font,
                                        int64_t scale, int64_t color);
void        rt_canvas_text_font_centered(void *canvas, int64_t y,
                                          rt_string text, void *font, int64_t color);
void        rt_canvas_text_font_right(void *canvas, int64_t margin, int64_t y,
                                       rt_string text, void *font, int64_t color);
```

### Internal Data Structure

```c
typedef struct {
    uint8_t *bitmap;      // Packed 1-bit bitmap (row-major, MSB-left)
    int16_t  width;       // Glyph width in pixels
    int16_t  height;      // Glyph height in pixels
    int16_t  x_offset;    // Horizontal bearing
    int16_t  y_offset;    // Vertical bearing (from baseline)
    int16_t  advance;     // Horizontal advance after glyph
} rt_glyph;

struct rt_bitmapfont_impl {
    rt_glyph  glyphs[256]; // ASCII + Latin-1 (expandable later)
    int16_t   line_height; // Max glyph height + leading
    int16_t   max_width;   // Widest glyph
    int8_t    monospace;   // 1 if all advances equal
    int64_t   glyph_count; // Number of valid glyphs
};
```

## 5. runtime.def Registration

```c
//=============================================================================
// GRAPHICS - BITMAP FONT
//=============================================================================

RT_FUNC(BitmapFontLoadBDF,     rt_bitmapfont_load_bdf,     "Viper.Graphics.BitmapFont.LoadBDF",     "obj(str)")
RT_FUNC(BitmapFontLoadPSF,     rt_bitmapfont_load_psf,     "Viper.Graphics.BitmapFont.LoadPSF",     "obj(str)")
RT_FUNC(BitmapFontCharWidth,   rt_bitmapfont_char_width,   "Viper.Graphics.BitmapFont.get_CharWidth",  "i64(obj)")
RT_FUNC(BitmapFontCharHeight,  rt_bitmapfont_char_height,  "Viper.Graphics.BitmapFont.get_CharHeight", "i64(obj)")
RT_FUNC(BitmapFontGlyphCount,  rt_bitmapfont_glyph_count,  "Viper.Graphics.BitmapFont.get_GlyphCount", "i64(obj)")
RT_FUNC(BitmapFontIsMonospace, rt_bitmapfont_is_monospace, "Viper.Graphics.BitmapFont.get_IsMonospace","i1(obj)")
RT_FUNC(BitmapFontTextWidth,   rt_bitmapfont_text_width,   "Viper.Graphics.BitmapFont.TextWidth",      "i64(obj,str)")
RT_FUNC(BitmapFontTextHeight,  rt_bitmapfont_text_height,  "Viper.Graphics.BitmapFont.get_TextHeight", "i64(obj)")
RT_FUNC(CanvasTextFont,        rt_canvas_text_font,        "Viper.Graphics.Canvas.TextFont",           "void(obj,i64,i64,str,obj,i64)")
RT_FUNC(CanvasTextFontBg,      rt_canvas_text_font_bg,     "Viper.Graphics.Canvas.TextFontBg",         "void(obj,i64,i64,str,obj,i64,i64)")
RT_FUNC(CanvasTextFontScaled,  rt_canvas_text_font_scaled, "Viper.Graphics.Canvas.TextFontScaled",     "void(obj,i64,i64,str,obj,i64,i64)")
RT_FUNC(CanvasTextFontCentered,rt_canvas_text_font_centered,"Viper.Graphics.Canvas.TextFontCentered",  "void(obj,i64,str,obj,i64)")
RT_FUNC(CanvasTextFontRight,   rt_canvas_text_font_right,  "Viper.Graphics.Canvas.TextFontRight",      "void(obj,i64,i64,str,obj,i64)")

RT_CLASS_BEGIN("Viper.Graphics.BitmapFont", BitmapFont, "none", none)
    RT_PROP("CharWidth", "i64", BitmapFontCharWidth, none)
    RT_PROP("CharHeight", "i64", BitmapFontCharHeight, none)
    RT_PROP("GlyphCount", "i64", BitmapFontGlyphCount, none)
    RT_PROP("IsMonospace", "i1", BitmapFontIsMonospace, none)
    RT_PROP("TextHeight", "i64", BitmapFontTextHeight, none)
    RT_METHOD("TextWidth", "i64(str)", BitmapFontTextWidth)
RT_CLASS_END()
```

Note: `LoadBDF` and `LoadPSF` are static factory methods, registered as standalone `RT_FUNC` entries. Canvas methods extend the existing Canvas class registration.

## 6. CMakeLists.txt Changes

In `src/runtime/CMakeLists.txt`, add to `RT_GRAPHICS_SOURCES`:
```cmake
graphics/rt_bitmapfont.c
```

## 7. Error Handling

| Scenario | Behavior |
|----------|----------|
| File not found | Return NULL (caller checks) |
| Invalid BDF header | Return NULL |
| Invalid PSF magic bytes | Return NULL |
| Glyph index out of range | Use fallback glyph (filled rectangle) |
| NULL font passed to Canvas.TextFont | No-op (silent, matches Canvas.Text behavior) |
| NULL canvas passed | No-op |
| Empty string | No-op |
| Allocation failure in glyph table | Return NULL from load |

## 8. Tests

### Zia Runtime Tests (`tests/runtime/test_bitmapfont.zia`)

**Given/When/Then:**

1. **Load BDF font**
   - Given: A valid BDF font file exists at a test path
   - When: `BitmapFont.LoadBDF(path)` is called
   - Then: Non-null font handle returned, `CharHeight > 0`, `GlyphCount > 0`

2. **Load PSF font**
   - Given: A valid PSF v2 font file exists
   - When: `BitmapFont.LoadPSF(path)` is called
   - Then: Non-null font handle returned, `IsMonospace == true`

3. **Text measurement**
   - Given: A loaded BDF font with known metrics
   - When: `font.TextWidth("Hello")` is called
   - Then: Returns sum of glyph advances for 'H','e','l','l','o'

4. **Canvas drawing (smoke test)**
   - Given: A canvas and loaded font
   - When: `canvas.TextFont(10, 10, "Test", font, 0xFFFFFF)` is called
   - Then: No crash, pixels drawn at expected coordinates

5. **Invalid file path**
   - Given: A non-existent file path
   - When: `BitmapFont.LoadBDF("nonexistent.bdf")` is called
   - Then: Returns null

6. **Empty string drawing**
   - Given: A canvas and loaded font
   - When: `canvas.TextFont(0, 0, "", font, 0xFFFFFF)` is called
   - Then: No crash, no pixels modified

### C Unit Test (`src/tests/test_rt_bitmapfont.cpp`)
- BDF parser: line parsing, hex bitmap decoding, BBX handling
- PSF parser: v1 header, v2 header, glyph extraction
- Glyph lookup: valid ASCII, out-of-range fallback

## 9. Documentation Deliverables

| Action | File |
|--------|------|
| CREATE | `docs/viperlib/graphics/fonts.md` — full BitmapFont API reference |
| UPDATE | `docs/viperlib/graphics/README.md` — add Fonts entry to contents table |
| UPDATE | `docs/viperlib/graphics/canvas.md` — add TextFont/TextFontBg/etc. to Canvas methods |

## 10. Code References

| File | Role |
|------|------|
| `src/runtime/graphics/rt_font.h` | Existing 8x8 font (pattern for glyph rendering) |
| `src/runtime/graphics/rt_font.c` | Existing font implementation (~glyph lookup pattern) |
| `src/runtime/graphics/rt_drawing.c` | Canvas text rendering functions to extend |
| `src/runtime/graphics/rt_canvas.c` | Canvas handle structure, pixel plotting |
| `src/runtime/graphics/rt_graphics_internal.h` | Internal canvas struct definition |
| `src/runtime/graphics/rt_camera.c` | Example of complete runtime class (pattern reference) |
| `src/il/runtime/runtime.def` | Registration entries (add after existing Canvas text methods) |
