---
status: active
audience: public
last-verified: 2026-04-05
---

# Fonts

> Custom bitmap font loading and rendering for games and applications.

**Part of [Viper Runtime Library — Graphics](README.md)**

## Contents

- [Viper.Graphics.BitmapFont](#vipergraphicsbitmapfont)
- [Canvas Text Methods](#canvas-text-methods-with-custom-font)
- [Supported Formats](#supported-formats)
- [Usage Example](#usage-example)

---

## Viper.Graphics.BitmapFont

Load variable-size bitmap fonts from BDF (Bitmap Distribution Format) or PSF (PC Screen Font) files. Provides text measurement and integrates with Canvas for rendering.

**Type:** Static (no instance constructor — use `LoadBDF` or `LoadPSF`)

### Static Methods

| Method | Signature | Description |
|--------|-----------|-------------|
| `LoadBDF(path)` | `BitmapFont(String)` | Load a BDF font file. Returns `null` on failure |
| `LoadPSF(path)` | `BitmapFont(String)` | Load a PSF v1/v2 font file. Returns `null` on failure |

### Properties

| Property | Type | Description |
|----------|------|-------------|
| `CharWidth` | Integer | Maximum glyph width (0 for proportional fonts) |
| `CharHeight` | Integer | Line height in pixels |
| `GlyphCount` | Integer | Number of loaded glyphs |
| `IsMonospace` | Boolean | True if all glyphs have the same advance width |
| `TextHeight` | Integer | Same as `CharHeight` (line height) |

### Methods

| Method | Signature | Description |
|--------|-----------|-------------|
| `TextWidth(text)` | `Integer(String)` | Measure pixel width of a string in this font |

---

## Canvas Text Methods (with Custom Font)

These methods extend the Canvas class for rendering text with a loaded BitmapFont.

| Method | Signature | Description |
|--------|-----------|-------------|
| `TextFont(x, y, text, font, color)` | `void(Integer, Integer, String, BitmapFont, Integer)` | Draw text at position |
| `TextFontBg(x, y, text, font, fg, bg)` | `void(Integer, Integer, String, BitmapFont, Integer, Integer)` | Draw with background color |
| `TextFontScaled(x, y, text, font, scale, color)` | `void(Integer, Integer, String, BitmapFont, Integer, Integer)` | Draw with integer scaling |
| `TextFontCentered(y, text, font, color)` | `void(Integer, String, BitmapFont, Integer)` | Draw horizontally centered |
| `TextFontRight(margin, y, text, font, color)` | `void(Integer, Integer, String, BitmapFont, Integer)` | Draw right-aligned |

---

## Supported Formats

### BDF (Bitmap Distribution Format)

A plain-text format defined by Adobe for the X Window System. Each glyph is encoded as hex bitmap rows.

- **File extension:** `.bdf`
- **Character set:** ASCII + Latin-1 (codepoints 0-255)
- **Features:** Per-glyph width (proportional support), baseline positioning
- **Free fonts:** Terminus, Cozette, Tamzen, GNU Unifont, Spleen

### PSF (PC Screen Font)

A simple binary format used by the Linux console. Supports v1 (2-byte header) and v2 (32-byte header).

- **File extension:** `.psf`
- **Character set:** Up to 256 or 512 glyphs (sequential from codepoint 0)
- **Features:** Always monospace, compact binary format
- **Free fonts:** Linux console fonts (`/usr/share/consolefonts/`)

---

## Usage Example

```zia
bind Viper.Graphics;

// Load a 16px font for the title
var titleFont = BitmapFont.LoadBDF("assets/terminus-bold-32.bdf");

// Load a smaller font for body text
var bodyFont = BitmapFont.LoadBDF("assets/cozette-13.bdf");

// In the draw loop:
canvas.TextFontCentered(50, "MY GAME", titleFont, Color.WHITE);
canvas.TextFont(20, 100, "Score: 1234", bodyFont, Color.YELLOW);

// Measure text for layout
var width = bodyFont.TextWidth("Hello World");
var height = bodyFont.CharHeight;
```

---

## See Also

- [Canvas & Color](canvas.md) — Canvas drawing surface (built-in 8x8 font via `Canvas.Text`)
- [Game UI](../../game/ui.md) — UI widgets that support BitmapFont (planned)
