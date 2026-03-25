# Feature 3: TrueType / Vector Font Rendering

## Current Reality

The current 2D text stack already has:
- built-in 8x8 text on `Canvas`
- `Viper.Graphics.BitmapFont` for BDF/PSF fonts
- `Canvas.TextFont*` helpers for `BitmapFont`

There is no scalable anti-aliased 2D font class yet. `Viper.GUI.Font` exists, but that
is a separate GUI namespace and should not be treated as a drop-in graphics font.

## Problem

Games need clean text at multiple sizes for:
- HUDs
- menus
- dialogue
- subtitles
- titles and splash screens

Bitmap-only fonts force either aliasing or asset duplication.

## Corrected Scope

### New Class

`Viper.Graphics.Font`

```text
Font.Load(path, sizePx) -> Font
Font.LoadMem(bytes, sizePx) -> Font

Font.MeasureWidth(text) -> Integer
Font.LineHeight -> Integer
Font.Ascent -> Integer
Font.Descent -> Integer
Font.LineGap -> Integer
```

### New Canvas Methods

Keep these explicit in v1 to avoid ambiguity with existing `BitmapFont` methods:

```text
Canvas.TextTTF(x, y, text, font, color)
Canvas.TextTTFCentered(y, text, font, color)
Canvas.TextTTFRight(margin, y, text, font, color)
Canvas.TextTTFScaled(x, y, text, font, scale, color)
```

## Implementation

### Phase 1: Font loading and glyph cache (3-4 days)

- Add `src/runtime/graphics/rt_truetype.c` + `rt_truetype.h`
- Use a vendored single-file rasterizer such as `stb_truetype` rather than writing a full TTF parser from scratch
- Build a per-font glyph cache keyed by Unicode codepoint
- Support UTF-8 decoding and on-demand glyph rasterization
- v1 scope:
  - no complex text shaping
  - no HarfBuzz-style layout
  - Latin / general codepoint rendering works, but advanced script shaping is explicitly out of scope

### Phase 2: Canvas integration (1-2 days)

- Add `Canvas.TextTTF*` entry points in `rt_canvas.c`
- Render glyph alpha masks through the existing software blend path
- Support kerning when the rasterizer exposes it
- Support `\n` line breaks

### Phase 3: validation and docs (1-2 days)

- Keep the existing `BitmapFont` API unchanged
- Add tests for measurement and rasterization
- Add visual regression coverage where practical

## Viper-Specific Notes

- This is a new graphics font class, not an alias for `Viper.GUI.Font`
- `docs/viperlib/graphics/fonts.md` already exists and should be extended rather than replaced
- Build integration belongs in `src/runtime/CMakeLists.txt`
- If the vendored rasterizer is used, document the upstream version and license in the file header and third-party manifest

## Runtime Registration

Add a new `Viper.Graphics.Font` class in `runtime.def`, plus explicit `Canvas.TextTTF*`
functions. Do not overload the existing `BitmapFont` runtime entries in v1.

## Files

| File | Action |
|------|--------|
| `src/runtime/graphics/rt_truetype.c` | New |
| `src/runtime/graphics/rt_truetype.h` | New |
| `src/runtime/graphics/rt_canvas.c` | Modify |
| `src/il/runtime/runtime.def` | Add `Font` and `Canvas.TextTTF*` |
| `src/tests/runtime/RTTrueTypeTests.cpp` | New |

## Documentation Updates

- Update `docs/viperlib/graphics/fonts.md`
- Update `docs/viperlib/graphics/README.md`
- Update `docs/viperlib/README.md`
- Update `docs/codemap/graphics.md`

## Cross-Platform Requirements

- CPU-rendered feature; no backend-specific GPU code required
- Use binary file I/O (`rb`) for font data
- Vendor the rasterizer so there is no external system dependency
- Guard with `#ifdef VIPER_ENABLE_GRAPHICS` and provide stubs in `rt_graphics_stubs.c`
