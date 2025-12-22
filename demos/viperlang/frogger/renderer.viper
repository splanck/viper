module Renderer;

import "./config";
import "./colors";

// =============================================================================
// Renderer - All drawing functions for the game
// Uses Viper.Terminal for output
// =============================================================================

// Color reset constant (white on black)
final DEFAULT_FG = 7;
final DEFAULT_BG = 0;

// Low-level drawing primitives
func drawAt(Integer row, Integer col, String ch) {
    if (col < 1 || col > GAME_WIDTH) { return; }
    Viper.Terminal.SetPosition(row, col);
    Viper.Terminal.Print(ch);
}

func drawAtColor(Integer row, Integer col, Integer fg, String ch) {
    if (col < 1 || col > GAME_WIDTH) { return; }
    Viper.Terminal.SetPosition(row, col);
    Viper.Terminal.SetColor(fg, DEFAULT_BG);
    Viper.Terminal.Print(ch);
    Viper.Terminal.SetColor(DEFAULT_FG, DEFAULT_BG);
}

func drawString(Integer row, Integer col, Integer fg, String text) {
    Viper.Terminal.SetPosition(row, col);
    Viper.Terminal.SetColor(fg, DEFAULT_BG);
    Viper.Terminal.Print(text);
    Viper.Terminal.SetColor(DEFAULT_FG, DEFAULT_BG);
}

func drawLine(Integer row, Integer startCol, Integer endCol, Integer fg, String ch) {
    Viper.Terminal.SetColor(fg, DEFAULT_BG);
    var col = startCol;
    while (col <= endCol) {
        if (col >= 1 && col <= GAME_WIDTH) {
            Viper.Terminal.SetPosition(row, col);
            Viper.Terminal.Print(ch);
        }
        col = col + 1;
    }
    Viper.Terminal.SetColor(DEFAULT_FG, DEFAULT_BG);
}

// Draw repeated character at position with width
func drawSpan(Integer row, Integer col, Integer width, Integer fg, String ch) {
    Viper.Terminal.SetColor(fg, DEFAULT_BG);
    var i = 0;
    while (i < width) {
        Integer drawCol = col + i;
        if (drawCol >= 1 && drawCol <= GAME_WIDTH) {
            Viper.Terminal.SetPosition(row, drawCol);
            Viper.Terminal.Print(ch);
        }
        i = i + 1;
    }
    Viper.Terminal.SetColor(DEFAULT_FG, DEFAULT_BG);
}

// HUD drawing
func drawHUD(Integer score, Integer lives, Integer level, Integer timeLeft) {
    // Title
    drawString(TITLE_ROW, 35, COLOR_TITLE, "FROGGER");

    // Lives
    Viper.Terminal.SetPosition(TITLE_ROW, 2);
    Viper.Terminal.SetColor(COLOR_HUD, DEFAULT_BG);
    Viper.Terminal.Print("Lives: ");
    Viper.Terminal.SetColor(COLOR_FROG, DEFAULT_BG);
    var i = 0;
    while (i < lives) {
        Viper.Terminal.Print("@");
        i = i + 1;
    }
    Viper.Terminal.SetColor(DEFAULT_FG, DEFAULT_BG);

    // Level
    Viper.Terminal.SetPosition(TITLE_ROW, 20);
    Viper.Terminal.SetColor(COLOR_HUD, DEFAULT_BG);
    Viper.Terminal.Print("Lv: ");
    Viper.Terminal.PrintInt(level);
    Viper.Terminal.SetColor(DEFAULT_FG, DEFAULT_BG);

    // Score
    Viper.Terminal.SetPosition(TITLE_ROW, 55);
    Viper.Terminal.SetColor(COLOR_HUD, DEFAULT_BG);
    Viper.Terminal.Print("Score: ");
    Viper.Terminal.PrintInt(score);
    Viper.Terminal.SetColor(DEFAULT_FG, DEFAULT_BG);

    // Time bar
    Viper.Terminal.SetPosition(TITLE_ROW + 1, 2);
    Viper.Terminal.SetColor(COLOR_HUD, DEFAULT_BG);
    Viper.Terminal.Print("Time: ");
    Integer barLength = timeLeft / 10;
    if (barLength > 50) { barLength = 50; }
    i = 0;
    while (i < barLength) {
        if (timeLeft < 100) {
            Viper.Terminal.SetColor(RED, DEFAULT_BG);
        } else if (timeLeft < 200) {
            Viper.Terminal.SetColor(YELLOW, DEFAULT_BG);
        } else {
            Viper.Terminal.SetColor(GREEN, DEFAULT_BG);
        }
        Viper.Terminal.Print("|");
        i = i + 1;
    }
    Viper.Terminal.SetColor(DEFAULT_FG, DEFAULT_BG);
}

// Background drawing
func drawHomeRow() {
    // Water background behind homes
    drawLine(HOME_ROW, 1, GAME_WIDTH, COLOR_WATER, "~");
}

func drawRiver() {
    var row = RIVER_START;
    while (row <= RIVER_END) {
        drawLine(row, 1, GAME_WIDTH, COLOR_WATER, "~");
        row = row + 1;
    }
}

func drawSafeZone() {
    drawLine(SAFE_ZONE_ROW, 1, GAME_WIDTH, COLOR_GRASS, "-");
    drawString(SAFE_ZONE_ROW, 32, COLOR_GRASS, " SAFE ZONE ");
}

func drawRoad() {
    var row = ROAD_START;
    while (row <= ROAD_END) {
        drawLine(row, 1, GAME_WIDTH, COLOR_ROAD, ".");
        row = row + 1;
    }
}

func drawStartArea() {
    drawLine(START_ROW, 1, GAME_WIDTH, COLOR_GRASS, "-");
    drawString(START_ROW, 35, COLOR_GRASS, " START ");
}

func drawInstructions() {
    drawString(INSTRUCTIONS_ROW, 5, COLOR_HUD, "WASD: Move    P: Pause    Q: Quit    Goal: Guide the frog to all 5 homes!");
}

// Entity drawing
func drawFrog(Frog frog) {
    if (!frog.isVisible()) { return; }

    Integer color = COLOR_FROG;
    if (frog.isDead()) {
        color = COLOR_FROG_DEAD;
    } else if (frog.isInvincible()) {
        color = COLOR_INVINCIBLE;
    }

    drawAtColor(frog.getRow(), frog.getCol(), color, frog.getChar());
}

func drawCar(Car car) {
    Integer row = car.getRow();
    Integer col = car.getCol();
    Integer width = car.getWidth();
    Integer color = car.getColor();
    String ch = car.getChar();

    drawSpan(row, col, width, color, ch);
}

func drawTruck(Truck truck) {
    Integer row = truck.getRow();
    Integer col = truck.getCol();
    Integer width = truck.getWidth();
    Integer color = truck.getColor();
    String ch = truck.getChar();

    drawSpan(row, col, width, color, ch);
}

func drawLog(Log log) {
    Integer row = log.getRow();
    Integer col = log.getCol();
    Integer width = log.getWidth();
    Integer color = log.getColor();
    String ch = log.getChar();

    drawSpan(row, col, width, color, ch);
}

func drawTurtle(Turtle turtle) {
    if (turtle.isSubmerged()) { return; }

    Integer row = turtle.getRow();
    Integer col = turtle.getCol();
    Integer width = turtle.getWidth();
    Integer color = turtle.getColor();
    String ch = turtle.getChar();

    drawSpan(row, col, width, color, ch);
}

func drawHomeSlot(HomeSlot home) {
    Integer col = home.getCol();
    Integer color = home.getColor();
    String ch = home.getChar();

    // Draw home slot as [X] where X is the state character
    drawAtColor(HOME_ROW, col - 1, color, "[");
    drawAtColor(HOME_ROW, col, color, ch);
    drawAtColor(HOME_ROW, col + 1, color, "]");
}

// Screen management
func clearScreen() {
    Viper.Terminal.Clear();
}

func hideCursor() {
    Viper.Terminal.SetCursorVisible(0);
}

func showCursor() {
    Viper.Terminal.SetCursorVisible(1);
}

// Game screens
func drawTitleScreen() {
    clearScreen();

    drawString(4, 28, COLOR_TITLE, "====================");
    drawString(5, 28, COLOR_TITLE, "      FROGGER       ");
    drawString(6, 28, COLOR_TITLE, "====================");

    drawString(9, 20, WHITE, "Guide your frog across the dangerous road");
    drawString(10, 20, WHITE, "and treacherous river to reach home safely!");

    drawString(13, 28, COLOR_FROG, "Controls:");
    drawString(14, 30, WHITE, "W - Move Up");
    drawString(15, 30, WHITE, "S - Move Down");
    drawString(16, 30, WHITE, "A - Move Left");
    drawString(17, 30, WHITE, "D - Move Right");
    drawString(18, 30, WHITE, "P - Pause Game");
    drawString(19, 30, WHITE, "Q - Quit");

    drawString(22, 25, YELLOW, "Press any key to start...");
}

func drawPauseOverlay() {
    drawString(10, 32, YELLOW, "*** PAUSED ***");
    drawString(12, 27, WHITE, "Press P to continue");
}

func drawGameOver(Boolean won, Integer score, Integer homesFilled) {
    clearScreen();

    if (won) {
        drawString(7, 28, COLOR_FROG, "CONGRATULATIONS!");
        drawString(9, 22, WHITE, "You filled all 5 homes!");
        drawString(10, 25, YELLOW, "Level Complete!");
    } else {
        drawString(7, 32, RED, "GAME OVER");
        drawString(9, 25, WHITE, "Better luck next time!");
    }

    Viper.Terminal.SetPosition(12, 28);
    Viper.Terminal.SetColor(WHITE, DEFAULT_BG);
    Viper.Terminal.Print("Final Score: ");
    Viper.Terminal.PrintInt(score);
    Viper.Terminal.SetColor(DEFAULT_FG, DEFAULT_BG);

    Viper.Terminal.SetPosition(14, 28);
    Viper.Terminal.SetColor(WHITE, DEFAULT_BG);
    Viper.Terminal.Print("Homes Filled: ");
    Viper.Terminal.PrintInt(homesFilled);
    Viper.Terminal.Print("/5");
    Viper.Terminal.SetColor(DEFAULT_FG, DEFAULT_BG);

    drawString(18, 25, YELLOW, "Press any key to exit...");
}
