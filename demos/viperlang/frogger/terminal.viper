// ============================================================================
// Frogger - Terminal Utilities
// ============================================================================
// ANSI terminal control for rendering the game.
// ============================================================================

module Terminal;

import "config";

// Print text at a specific row/column position
func printAt(row: Integer, col: Integer, text: String) {
    Viper.Terminal.SetPosition(row, col);
    Viper.Terminal.Print(text);
}

// Print colored text at a specific position
func printColorAt(row: Integer, col: Integer, color: String, text: String) {
    Viper.Terminal.SetPosition(row, col);
    Viper.Terminal.Print(color);
    Viper.Terminal.Print(text);
    Viper.Terminal.Print(COLOR_RESET);
}

// Clear entire screen
func clearScreen() {
    Viper.Terminal.Clear();
}

// Hide cursor for game rendering
func hideCursor() {
    Viper.Terminal.SetCursorVisible(0);
}

// Show cursor for menus/input
func showCursor() {
    Viper.Terminal.SetCursorVisible(1);
}

// Begin buffered frame (reduces flicker)
func beginFrame() {
    Viper.Terminal.BeginBatch();
    Viper.Terminal.SetPosition(1, 1);
}

// End buffered frame (flush to screen)
func endFrame() {
    Viper.Terminal.EndBatch();
}

// Get key input (non-blocking)
func getKey() -> String {
    return Viper.Terminal.GetKeyTimeout(0);
}

// Wait for any key press
func waitForKey() {
    while Viper.Terminal.GetKeyTimeout(50) == "" {
        // Keep waiting
    }
}

// Sleep for milliseconds
func sleep(ms: Integer) {
    Viper.Time.SleepMs(ms);
}

// Draw a horizontal line of repeated characters
func drawHLine(row: Integer, startCol: Integer, endCol: Integer, color: String, ch: String) {
    for col in startCol..=endCol {
        printColorAt(row, col, color, ch);
    }
}

// Print centered text
func printCentered(row: Integer, text: String) {
    var textLen = Viper.String.Length(text);
    var col = (GAME_WIDTH - textLen) / 2;
    if col < 1 {
        col = 1;
    }
    Viper.Terminal.SetPosition(row, col);
    Viper.Terminal.Print(text);
}

// Print centered colored text
func printColorCentered(row: Integer, color: String, text: String) {
    var textLen = Viper.String.Length(text);
    var col = (GAME_WIDTH - textLen) / 2;
    if col < 1 {
        col = 1;
    }
    Viper.Terminal.SetPosition(row, col);
    Viper.Terminal.Print(color);
    Viper.Terminal.Print(text);
    Viper.Terminal.Print(COLOR_RESET);
}
