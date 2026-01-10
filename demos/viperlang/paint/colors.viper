// colors.viper - Color management for Viper Paint
module colors;

import "./config";

// ColorManager entity - manages foreground, background, palette, and recent colors
entity ColorManager {
    expose Integer foreground;      // Current foreground color (ARGB)
    expose Integer background;      // Current background color (ARGB)
    expose Integer paletteCount;    // Number of palette colors
    expose Integer recentCount;     // Number of recent colors

    // Palette storage (16 colors)
    hide Integer p0;
    hide Integer p1;
    hide Integer p2;
    hide Integer p3;
    hide Integer p4;
    hide Integer p5;
    hide Integer p6;
    hide Integer p7;
    hide Integer p8;
    hide Integer p9;
    hide Integer p10;
    hide Integer p11;
    hide Integer p12;
    hide Integer p13;
    hide Integer p14;
    hide Integer p15;

    // Recent colors (8 colors)
    hide Integer r0;
    hide Integer r1;
    hide Integer r2;
    hide Integer r3;
    hide Integer r4;
    hide Integer r5;
    hide Integer r6;
    hide Integer r7;

    // Initialize with default colors
    expose func init() {
        foreground = config.COLOR_BLACK;
        background = config.COLOR_WHITE;

        // Initialize 16-color palette
        p0 = 0xFF000000;   // Black
        p1 = 0xFFFFFFFF;   // White
        p2 = 0xFFFF0000;   // Red
        p3 = 0xFF00FF00;   // Green
        p4 = 0xFF0000FF;   // Blue
        p5 = 0xFFFFFF00;   // Yellow
        p6 = 0xFFFF00FF;   // Magenta
        p7 = 0xFF00FFFF;   // Cyan
        p8 = 0xFF808080;   // Gray
        p9 = 0xFFC0C0C0;   // Light Gray
        p10 = 0xFF800000;  // Dark Red
        p11 = 0xFF008000;  // Dark Green
        p12 = 0xFF000080;  // Dark Blue
        p13 = 0xFF808000;  // Olive
        p14 = 0xFF800080;  // Purple
        p15 = 0xFF008080;  // Teal
        paletteCount = 16;

        // Clear recent colors
        r0 = 0;
        r1 = 0;
        r2 = 0;
        r3 = 0;
        r4 = 0;
        r5 = 0;
        r6 = 0;
        r7 = 0;
        recentCount = 0;
    }

    // Swap foreground and background colors
    expose func swap() {
        var temp = foreground;
        foreground = background;
        background = temp;
    }

    // Set foreground color and add to recent
    expose func setForeground(color: Integer) {
        foreground = color;
        addRecent(color);
    }

    // Set background color
    expose func setBackground(color: Integer) {
        background = color;
    }

    // Get palette color by index
    expose func getPaletteColor(index: Integer) -> Integer {
        if index == 0 { return p0; }
        if index == 1 { return p1; }
        if index == 2 { return p2; }
        if index == 3 { return p3; }
        if index == 4 { return p4; }
        if index == 5 { return p5; }
        if index == 6 { return p6; }
        if index == 7 { return p7; }
        if index == 8 { return p8; }
        if index == 9 { return p9; }
        if index == 10 { return p10; }
        if index == 11 { return p11; }
        if index == 12 { return p12; }
        if index == 13 { return p13; }
        if index == 14 { return p14; }
        if index == 15 { return p15; }
        return 0;
    }

    // Get recent color by index
    expose func getRecentColor(index: Integer) -> Integer {
        if index == 0 { return r0; }
        if index == 1 { return r1; }
        if index == 2 { return r2; }
        if index == 3 { return r3; }
        if index == 4 { return r4; }
        if index == 5 { return r5; }
        if index == 6 { return r6; }
        if index == 7 { return r7; }
        return 0;
    }

    // Add color to recent list (front)
    expose func addRecent(color: Integer) {
        // Don't add if already the most recent
        if r0 == color {
            return;
        }

        // Shift colors down
        r7 = r6;
        r6 = r5;
        r5 = r4;
        r4 = r3;
        r3 = r2;
        r2 = r1;
        r1 = r0;
        r0 = color;

        if recentCount < 8 {
            recentCount = recentCount + 1;
        }
    }

    // Get red component (0-255)
    expose func getRed() -> Integer {
        return (foreground / 65536) % 256;
    }

    // Get green component (0-255)
    expose func getGreen() -> Integer {
        return (foreground / 256) % 256;
    }

    // Get blue component (0-255)
    expose func getBlue() -> Integer {
        return foreground % 256;
    }

    // Set foreground from RGB components
    expose func setRGB(r: Integer, g: Integer, b: Integer) {
        // Clamp values
        if r < 0 { r = 0; }
        if r > 255 { r = 255; }
        if g < 0 { g = 0; }
        if g > 255 { g = 255; }
        if b < 0 { b = 0; }
        if b > 255 { b = 255; }

        foreground = 0xFF000000 + r * 65536 + g * 256 + b;
        addRecent(foreground);
    }
}

// Utility: Create RGB color
func makeRGB(r: Integer, g: Integer, b: Integer) -> Integer {
    return 0xFF000000 + r * 65536 + g * 256 + b;
}

// Utility: Blend two colors (t = 0 to 100)
func blendColors(c1: Integer, c2: Integer, t: Integer) -> Integer {
    var r1 = (c1 / 65536) % 256;
    var g1 = (c1 / 256) % 256;
    var b1 = c1 % 256;

    var r2 = (c2 / 65536) % 256;
    var g2 = (c2 / 256) % 256;
    var b2 = c2 % 256;

    var r = r1 + (r2 - r1) * t / 100;
    var g = g1 + (g2 - g1) * t / 100;
    var b = b1 + (b2 - b1) * t / 100;

    return 0xFF000000 + r * 65536 + g * 256 + b;
}
