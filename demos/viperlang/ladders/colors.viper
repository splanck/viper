// colors.viper - Color constants and utility functions for Ladders
module colors;

// Predefined color constants (initialized in initColors)
var BLACK: Integer;
var WHITE: Integer;
var RED: Integer;
var GREEN: Integer;
var BLUE: Integer;
var YELLOW: Integer;
var CYAN: Integer;
var MAGENTA: Integer;
var ORANGE: Integer;
var BROWN: Integer;
var DARK_GRAY: Integer;
var LIGHT_GRAY: Integer;
var DARK_BLUE: Integer;
var DARK_GREEN: Integer;
var DARK_RED: Integer;
var GOLD: Integer;

// Game-specific colors
var PLATFORM_COLOR: Integer;
var LADDER_COLOR: Integer;
var PLAYER_COLOR: Integer;
var ENEMY_COLOR: Integer;
var COLLECTIBLE_COLOR: Integer;
var BACKGROUND_COLOR: Integer;
var UI_TEXT_COLOR: Integer;
var UI_SCORE_COLOR: Integer;
var UI_LIVES_COLOR: Integer;
var MENU_BG_COLOR: Integer;
var MENU_HIGHLIGHT_COLOR: Integer;

// Initialize all color constants
func initColors() {
    // Basic colors
    BLACK = Viper.Graphics.Color.RGB(0, 0, 0);
    WHITE = Viper.Graphics.Color.RGB(255, 255, 255);
    RED = Viper.Graphics.Color.RGB(255, 0, 0);
    GREEN = Viper.Graphics.Color.RGB(0, 255, 0);
    BLUE = Viper.Graphics.Color.RGB(0, 0, 255);
    YELLOW = Viper.Graphics.Color.RGB(255, 255, 0);
    CYAN = Viper.Graphics.Color.RGB(0, 255, 255);
    MAGENTA = Viper.Graphics.Color.RGB(255, 0, 255);
    ORANGE = Viper.Graphics.Color.RGB(255, 165, 0);
    BROWN = Viper.Graphics.Color.RGB(139, 69, 19);
    DARK_GRAY = Viper.Graphics.Color.RGB(64, 64, 64);
    LIGHT_GRAY = Viper.Graphics.Color.RGB(192, 192, 192);
    DARK_BLUE = Viper.Graphics.Color.RGB(0, 0, 139);
    DARK_GREEN = Viper.Graphics.Color.RGB(0, 100, 0);
    DARK_RED = Viper.Graphics.Color.RGB(139, 0, 0);
    GOLD = Viper.Graphics.Color.RGB(255, 215, 0);

    // Game-specific colors (retro-style palette)
    BACKGROUND_COLOR = Viper.Graphics.Color.RGB(20, 20, 40);
    PLATFORM_COLOR = Viper.Graphics.Color.RGB(100, 80, 60);
    LADDER_COLOR = Viper.Graphics.Color.RGB(180, 140, 80);
    PLAYER_COLOR = Viper.Graphics.Color.RGB(100, 200, 255);
    ENEMY_COLOR = Viper.Graphics.Color.RGB(255, 80, 80);
    COLLECTIBLE_COLOR = Viper.Graphics.Color.RGB(255, 215, 0);
    UI_TEXT_COLOR = WHITE;
    UI_SCORE_COLOR = YELLOW;
    UI_LIVES_COLOR = GREEN;
    MENU_BG_COLOR = Viper.Graphics.Color.RGB(30, 30, 60);
    MENU_HIGHLIGHT_COLOR = Viper.Graphics.Color.RGB(100, 100, 200);
}

// Create RGB color from components
func rgb(r: Integer, g: Integer, b: Integer) -> Integer {
    return Viper.Graphics.Color.RGB(r, g, b);
}

// Create RGBA color with alpha
func rgba(r: Integer, g: Integer, b: Integer, a: Integer) -> Integer {
    return Viper.Graphics.Color.RGBA(r, g, b, a);
}

// Darken a color by an amount (0-255)
func darken(color: Integer, amount: Integer) -> Integer {
    return Viper.Graphics.Color.Darken(color, amount);
}

// Brighten a color by an amount (0-255)
func brighten(color: Integer, amount: Integer) -> Integer {
    return Viper.Graphics.Color.Brighten(color, amount);
}

// Lerp between two colors (t: 0-100)
func lerp(c1: Integer, c2: Integer, t: Integer) -> Integer {
    return Viper.Graphics.Color.Lerp(c1, c2, t);
}
