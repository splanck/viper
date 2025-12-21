module Renderer;

import "./config";
import "./colors";

// Drawing helper functions

func drawChar(Integer row, Integer col, Integer fg, String ch) {
    if (col < 1 || col > GAME_WIDTH) {
        return;
    }
    Viper.Terminal.MoveCursor(row, col);
    Viper.Terminal.SetForeground(fg);
    Viper.Terminal.Write(ch);
    Viper.Terminal.ResetColors();
}

func drawCharBg(Integer row, Integer col, Integer fg, Integer bg, String ch) {
    if (col < 1 || col > GAME_WIDTH) {
        return;
    }
    Viper.Terminal.MoveCursor(row, col);
    Viper.Terminal.SetForeground(fg);
    Viper.Terminal.SetBackground(bg);
    Viper.Terminal.Write(ch);
    Viper.Terminal.ResetColors();
}

func drawString(Integer row, Integer col, Integer fg, String text) {
    Viper.Terminal.MoveCursor(row, col);
    Viper.Terminal.SetForeground(fg);
    Viper.Terminal.Write(text);
    Viper.Terminal.ResetColors();
}

func drawLine(Integer row, Integer startCol, Integer endCol, Integer fg, String ch) {
    var col = startCol;
    while (col <= endCol) {
        drawChar(row, col, fg, ch);
        col = col + 1;
    }
}

func drawBox(Integer row, Integer col, Integer width, Integer height, Integer fg) {
    // Top border
    drawChar(row, col, fg, "+");
    var i = 1;
    while (i < width - 1) {
        drawChar(row, col + i, fg, "-");
        i = i + 1;
    }
    drawChar(row, col + width - 1, fg, "+");

    // Sides
    var r = 1;
    while (r < height - 1) {
        drawChar(row + r, col, fg, "|");
        drawChar(row + r, col + width - 1, fg, "|");
        r = r + 1;
    }

    // Bottom border
    drawChar(row + height - 1, col, fg, "+");
    i = 1;
    while (i < width - 1) {
        drawChar(row + height - 1, col + i, fg, "-");
        i = i + 1;
    }
    drawChar(row + height - 1, col + width - 1, fg, "+");
}

func drawTitleBar(Integer score, Integer lives, Integer level) {
    // Title
    drawString(TITLE_ROW, 30, COLOR_TITLE, "FROGGER");

    // Lives
    Viper.Terminal.MoveCursor(TITLE_ROW, 2);
    Viper.Terminal.Write("Lives: ");

    var i = 0;
    while (i < lives) {
        Viper.Terminal.SetForeground(COLOR_FROG);
        Viper.Terminal.Write("@");
        i = i + 1;
    }
    Viper.Terminal.ResetColors();

    // Level
    Viper.Terminal.MoveCursor(TITLE_ROW, 20);
    Viper.Terminal.Write("Lv:");
    Viper.Terminal.SayInt(level);

    // Score
    Viper.Terminal.MoveCursor(TITLE_ROW, 55);
    Viper.Terminal.Write("Score: ");
    Viper.Terminal.SayInt(score);
}

func drawHomeRow(Home h0, Home h1, Home h2, Home h3, Home h4) {
    // Water background
    drawLine(HOME_ROW, 1, GAME_WIDTH, COLOR_WATER, "~");

    // Draw each home slot
    drawHomeSlot(h0);
    drawHomeSlot(h1);
    drawHomeSlot(h2);
    drawHomeSlot(h3);
    drawHomeSlot(h4);
}

func drawHomeSlot(Home h) {
    Integer col = h.getCol();

    if (h.isFilled()) {
        drawString(HOME_ROW, col - 1, COLOR_HOME_FILLED, "[F]");
    } else if (h.hasFly()) {
        drawString(HOME_ROW, col - 1, COLOR_POWERUP, "[*]");
    } else {
        drawString(HOME_ROW, col - 1, COLOR_HOME_EMPTY, "[ ]");
    }
}

func drawRiver() {
    var row = RIVER_START;
    while (row <= RIVER_END) {
        drawLine(row, 1, GAME_WIDTH, COLOR_WATER, "~");
        row = row + 1;
    }
}

func drawSafeZone() {
    drawLine(SAFE_ZONE_ROW, 1, GAME_WIDTH, COLOR_SAFE_ZONE, "-");
    drawString(SAFE_ZONE_ROW, 28, COLOR_SAFE_ZONE, " SAFE ZONE ");
}

func drawRoad() {
    var row = ROAD_START;
    while (row <= ROAD_END) {
        drawLine(row, 1, GAME_WIDTH, COLOR_ROAD, ".");
        row = row + 1;
    }
}

func drawStartArea() {
    drawLine(START_ROW, 1, GAME_WIDTH, COLOR_SAFE_ZONE, "-");
    drawString(START_ROW, 32, COLOR_SAFE_ZONE, "START");
}

func drawVehicle(Vehicle v) {
    Integer row = v.getRow();
    Integer col = v.getCol();
    Integer width = v.getWidth();

    Integer color = COLOR_CAR;
    String ch = "#";

    if (v.isTruck()) {
        color = COLOR_TRUCK;
        ch = "=";
    } else if (v.isSportsCar()) {
        color = YELLOW;
        ch = ">";
    }

    var i = 0;
    while (i < width) {
        Integer drawCol = col + i;
        if (drawCol >= 1 && drawCol <= GAME_WIDTH) {
            drawChar(row, drawCol, color, ch);
        }
        i = i + 1;
    }
}

func drawPlatform(Platform p) {
    Integer row = p.getRow();
    Integer col = p.getCol();
    Integer width = p.getWidth();

    // Don't draw if submerged
    if (p.isSubmerged()) {
        return;
    }

    Integer color = COLOR_LOG;
    String ch = "=";

    if (p.isTurtle()) {
        color = COLOR_TURTLE;
        ch = "O";

        if (p.isSinking()) {
            // Sinking turtles flash
            ch = "o";
        }
    }

    var i = 0;
    while (i < width) {
        Integer drawCol = col + i;
        if (drawCol >= 1 && drawCol <= GAME_WIDTH) {
            drawChar(row, drawCol, color, ch);
        }
        i = i + 1;
    }
}

func drawFrog(Frog frog) {
    Integer color = COLOR_FROG;
    String ch = "@";

    if (frog.isInvincible()) {
        color = COLOR_INVINCIBLE;
        ch = "*";
    } else if (frog.hasSpeedBoost()) {
        ch = ">";
    }

    drawChar(frog.getRow(), frog.getCol(), color, ch);
}

func drawPowerUp(PowerUp p) {
    if (!p.isActive()) {
        return;
    }

    String symbol = p.getSymbol();
    drawChar(p.getRow(), p.getCol(), COLOR_POWERUP, symbol);
}

func drawInstructions() {
    Viper.Terminal.MoveCursor(INSTRUCTIONS_ROW, 2);
    Viper.Terminal.Write("WASD=Move  P=Pause  Q=Quit  Goal: Fill all 5 homes!");
}

func drawPauseOverlay() {
    drawString(11, 28, YELLOW, "** PAUSED **");
    drawString(12, 23, WHITE, "Press P to continue");
}

func drawGameOver(Boolean won, Integer score, Integer homesFilled) {
    Viper.Terminal.Clear();

    if (won) {
        drawString(6, 25, COLOR_FROG, "CONGRATULATIONS!");
        drawString(8, 20, WHITE, "You filled all 5 homes!");
    } else {
        drawString(6, 28, RED, "GAME OVER");
        drawString(8, 22, WHITE, "Better luck next time!");
    }

    Viper.Terminal.MoveCursor(11, 25);
    Viper.Terminal.Write("Final Score: ");
    Viper.Terminal.SayInt(score);

    Viper.Terminal.MoveCursor(13, 25);
    Viper.Terminal.Write("Homes Filled: ");
    Viper.Terminal.SayInt(homesFilled);
    Viper.Terminal.Write("/5");

    drawString(16, 22, WHITE, "Press any key to exit...");
}

func drawTitleScreen() {
    Viper.Terminal.Clear();

    drawString(3, 25, COLOR_FROG, "=================");
    drawString(4, 25, COLOR_FROG, "    FROGGER     ");
    drawString(5, 25, COLOR_FROG, "=================");

    drawString(8, 15, WHITE, "Guide your frog across the dangerous road");
    drawString(9, 15, WHITE, "and treacherous river to reach home!");

    drawString(12, 20, COLOR_TITLE, "Controls:");
    drawString(13, 22, WHITE, "W - Move Up");
    drawString(14, 22, WHITE, "S - Move Down");
    drawString(15, 22, WHITE, "A - Move Left");
    drawString(16, 22, WHITE, "D - Move Right");
    drawString(17, 22, WHITE, "P - Pause");
    drawString(18, 22, WHITE, "Q - Quit");

    drawString(21, 20, YELLOW, "Press any key to start...");
}
