// ============================================================================
// Frogger - Game Configuration
// ============================================================================
// Game constants, dimensions, and colors.
// ============================================================================

module Config;

// Screen dimensions
final GAME_WIDTH = 70;
final GAME_HEIGHT = 24;

// Row definitions
final HOME_ROW = 2;
final RIVER_START = 4;
final RIVER_END = 8;
final MEDIAN_ROW = 10;
final ROAD_START = 12;
final ROAD_END = 16;
final START_ROW = 18;

// Starting lives
final STARTING_LIVES = 3;

// Scoring
final SCORE_HOME = 200;
final SCORE_FORWARD = 10;

// Timing (milliseconds)
final GAME_TICK_MS = 100;
final DEATH_DELAY_MS = 800;
final HOME_DELAY_MS = 500;
final INPUT_DELAY_MS = 50;
final MENU_DELAY_MS = 200;

// High score file
final SCORE_FILE = "frogger_highscores.txt";
final MAX_HIGH_SCORES = 5;

// ANSI color codes (built at runtime due to escape sequence limitation)
var COLOR_RESET: String;
var COLOR_BLACK: String;
var COLOR_RED: String;
var COLOR_GREEN: String;
var COLOR_YELLOW: String;
var COLOR_BLUE: String;
var COLOR_MAGENTA: String;
var COLOR_CYAN: String;
var COLOR_WHITE: String;

func initColors() {
    var ESC = Viper.String.Chr(27);
    COLOR_RESET = ESC + "[0m";
    COLOR_BLACK = ESC + "[30m";
    COLOR_RED = ESC + "[31m";
    COLOR_GREEN = ESC + "[32m";
    COLOR_YELLOW = ESC + "[33m";
    COLOR_BLUE = ESC + "[34m";
    COLOR_MAGENTA = ESC + "[35m";
    COLOR_CYAN = ESC + "[36m";
    COLOR_WHITE = ESC + "[37m";
}
