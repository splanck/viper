module colors;

// ============================================================================
// COLORS.VIPER - Color Utility Functions
// ============================================================================
// Provides color creation and manipulation utilities for graphics demos.
// Uses Viper.Graphics.Color.* namespace for RGB/RGBA operations.
// ============================================================================

// Predefined color constants
var BLACK: Integer;
var WHITE: Integer;
var RED: Integer;
var GREEN: Integer;
var BLUE: Integer;
var YELLOW: Integer;
var CYAN: Integer;
var MAGENTA: Integer;
var ORANGE: Integer;
var PURPLE: Integer;
var PINK: Integer;
var GRAY: Integer;
var DARK_GRAY: Integer;
var LIGHT_GRAY: Integer;

// Rainbow colors array for cycling
var RAINBOW: List[Integer];

// Initialize all color constants
func initColors() {
    BLACK = Viper.Graphics.Color.RGB(0, 0, 0);
    WHITE = Viper.Graphics.Color.RGB(255, 255, 255);
    RED = Viper.Graphics.Color.RGB(255, 0, 0);
    GREEN = Viper.Graphics.Color.RGB(0, 255, 0);
    BLUE = Viper.Graphics.Color.RGB(0, 0, 255);
    YELLOW = Viper.Graphics.Color.RGB(255, 255, 0);
    CYAN = Viper.Graphics.Color.RGB(0, 255, 255);
    MAGENTA = Viper.Graphics.Color.RGB(255, 0, 255);
    ORANGE = Viper.Graphics.Color.RGB(255, 165, 0);
    PURPLE = Viper.Graphics.Color.RGB(128, 0, 128);
    PINK = Viper.Graphics.Color.RGB(255, 192, 203);
    GRAY = Viper.Graphics.Color.RGB(128, 128, 128);
    DARK_GRAY = Viper.Graphics.Color.RGB(64, 64, 64);
    LIGHT_GRAY = Viper.Graphics.Color.RGB(192, 192, 192);

    // Rainbow colors for cycling effects
    RAINBOW = [];
    RAINBOW.add(RED);
    RAINBOW.add(ORANGE);
    RAINBOW.add(YELLOW);
    RAINBOW.add(GREEN);
    RAINBOW.add(CYAN);
    RAINBOW.add(BLUE);
    RAINBOW.add(PURPLE);
    RAINBOW.add(MAGENTA);
}

// Create RGB color from components
func rgb(r: Integer, g: Integer, b: Integer) -> Integer {
    return Viper.Graphics.Color.RGB(r, g, b);
}

// Create RGBA color with alpha
func rgba(r: Integer, g: Integer, b: Integer, a: Integer) -> Integer {
    return Viper.Graphics.Color.RGBA(r, g, b, a);
}

// Extract red component from color
func getRed(color: Integer) -> Integer {
    return (color / 65536) - ((color / 16777216) * 256);
}

// Extract green component from color
func getGreen(color: Integer) -> Integer {
    return (color / 256) - ((color / 65536) * 256);
}

// Extract blue component from color
func getBlue(color: Integer) -> Integer {
    return color - ((color / 256) * 256);
}

// Fade a color by a factor (0.0 = black, 1.0 = original)
func fadeColor(color: Integer, factor: Number) -> Integer {
    var r = getRed(color);
    var g = getGreen(color);
    var b = getBlue(color);

    r = Viper.Math.Floor(r * factor);
    g = Viper.Math.Floor(g * factor);
    b = Viper.Math.Floor(b * factor);

    if r < 0 { r = 0; }
    if g < 0 { g = 0; }
    if b < 0 { b = 0; }
    if r > 255 { r = 255; }
    if g > 255 { g = 255; }
    if b > 255 { b = 255; }

    return Viper.Graphics.Color.RGB(r, g, b);
}

// Blend two colors together (factor: 0.0 = color1, 1.0 = color2)
func blendColors(color1: Integer, color2: Integer, factor: Number) -> Integer {
    var r1 = getRed(color1);
    var g1 = getGreen(color1);
    var b1 = getBlue(color1);

    var r2 = getRed(color2);
    var g2 = getGreen(color2);
    var b2 = getBlue(color2);

    var r = Viper.Math.Floor(r1 + (r2 - r1) * factor);
    var g = Viper.Math.Floor(g1 + (g2 - g1) * factor);
    var b = Viper.Math.Floor(b1 + (b2 - b1) * factor);

    return Viper.Graphics.Color.RGB(r, g, b);
}

// Get rainbow color from phase (0-359 degrees)
func rainbowPhase(phase: Integer) -> Integer {
    var p = phase;
    while p < 0 {
        p = p + 360;
    }
    while p >= 360 {
        p = p - 360;
    }

    var r: Integer;
    var g: Integer;
    var b: Integer;

    if p < 60 {
        r = 255;
        g = (p * 255) / 60;
        b = 0;
    } else if p < 120 {
        r = ((120 - p) * 255) / 60;
        g = 255;
        b = 0;
    } else if p < 180 {
        r = 0;
        g = 255;
        b = ((p - 120) * 255) / 60;
    } else if p < 240 {
        r = 0;
        g = ((240 - p) * 255) / 60;
        b = 255;
    } else if p < 300 {
        r = ((p - 240) * 255) / 60;
        g = 0;
        b = 255;
    } else {
        r = 255;
        g = 0;
        b = ((360 - p) * 255) / 60;
    }

    return Viper.Graphics.Color.RGB(r, g, b);
}

// Create a grayscale color (0-255)
func grayscale(value: Integer) -> Integer {
    var v = value;
    if v < 0 { v = 0; }
    if v > 255 { v = 255; }
    return Viper.Graphics.Color.RGB(v, v, v);
}

// Create heat map color (0.0 = cold/blue, 1.0 = hot/red)
func heatColor(intensity: Number) -> Integer {
    var t = intensity;
    if t < 0.0 { t = 0.0; }
    if t > 1.0 { t = 1.0; }

    var r: Integer;
    var g: Integer;
    var b: Integer;

    if t < 0.25 {
        b = Viper.Math.Floor(t * 4.0 * 255.0);
        r = 0;
        g = 0;
    } else if t < 0.5 {
        b = 255;
        g = Viper.Math.Floor((t - 0.25) * 4.0 * 255.0);
        r = 0;
    } else if t < 0.75 {
        g = 255;
        b = Viper.Math.Floor((0.75 - t) * 4.0 * 255.0);
        r = Viper.Math.Floor((t - 0.5) * 4.0 * 255.0);
    } else {
        r = 255;
        g = Viper.Math.Floor((1.0 - t) * 4.0 * 255.0);
        b = 0;
        if t > 0.9 {
            var whiteness = (t - 0.9) * 10.0;
            g = Viper.Math.Floor(g + (255 - g) * whiteness);
            b = Viper.Math.Floor(255.0 * whiteness);
        }
    }

    return Viper.Graphics.Color.RGB(r, g, b);
}

// Brighten a color
func brighten(color: Integer, amount: Integer) -> Integer {
    var r = getRed(color) + amount;
    var g = getGreen(color) + amount;
    var b = getBlue(color) + amount;

    if r > 255 { r = 255; }
    if g > 255 { g = 255; }
    if b > 255 { b = 255; }

    return Viper.Graphics.Color.RGB(r, g, b);
}

// Darken a color
func darken(color: Integer, amount: Integer) -> Integer {
    var r = getRed(color) - amount;
    var g = getGreen(color) - amount;
    var b = getBlue(color) - amount;

    if r < 0 { r = 0; }
    if g < 0 { g = 0; }
    if b < 0 { b = 0; }

    return Viper.Graphics.Color.RGB(r, g, b);
}
