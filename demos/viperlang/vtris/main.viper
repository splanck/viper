module VTris;

import "./entities";

// Game constants
Integer BOARD_WIDTH = 10;
Integer BOARD_HEIGHT = 20;

// Game state
Integer gameScore = 0;
Integer gameLines = 0;
Integer gameLevel = 1;
Integer gameOver = 0;
Integer dropCounter = 0;
Integer dropSpeed = 30;
Integer menuChoice = 1;
Integer menuRunning = 1;

// High scores (in-memory)
Integer hs0 = 50000;
Integer hs1 = 35000;
Integer hs2 = 25000;
Integer hs3 = 15000;
Integer hs4 = 10000;

// Board represented as 200 cells (20 rows x 10 cols)
// We'll use row-encoded integers (10 bits per row)
Integer row0 = 0; Integer row1 = 0; Integer row2 = 0; Integer row3 = 0;
Integer row4 = 0; Integer row5 = 0; Integer row6 = 0; Integer row7 = 0;
Integer row8 = 0; Integer row9 = 0; Integer row10 = 0; Integer row11 = 0;
Integer row12 = 0; Integer row13 = 0; Integer row14 = 0; Integer row15 = 0;
Integer row16 = 0; Integer row17 = 0; Integer row18 = 0; Integer row19 = 0;

// Row colors (simplified - one color per row for placed blocks)
Integer rowColor0 = 0; Integer rowColor1 = 0; Integer rowColor2 = 0; Integer rowColor3 = 0;
Integer rowColor4 = 0; Integer rowColor5 = 0; Integer rowColor6 = 0; Integer rowColor7 = 0;
Integer rowColor8 = 0; Integer rowColor9 = 0; Integer rowColor10 = 0; Integer rowColor11 = 0;
Integer rowColor12 = 0; Integer rowColor13 = 0; Integer rowColor14 = 0; Integer rowColor15 = 0;
Integer rowColor16 = 0; Integer rowColor17 = 0; Integer rowColor18 = 0; Integer rowColor19 = 0;

func start() {
    menuRunning = 1;
    while menuRunning == 1 {
        showMainMenu();
    }

    Viper.Terminal.Clear();
    Viper.Terminal.SetCursorVisible(1);
    Viper.Terminal.Say("Thanks for playing vTRIS!");
}

func showMainMenu() {
    Integer menuActive = 1;

    while menuActive == 1 {
        Viper.Terminal.Clear();
        Viper.Terminal.SetCursorVisible(0);

        // Draw title
        Viper.Terminal.SetPosition(2, 15);
        Viper.Terminal.SetColor(6, 0);
        Viper.Terminal.Print("========================================");
        Viper.Terminal.SetPosition(3, 15);
        Viper.Terminal.Print("              vTRIS                    ");
        Viper.Terminal.SetPosition(4, 15);
        Viper.Terminal.Print("          ViperLang Edition            ");
        Viper.Terminal.SetPosition(5, 15);
        Viper.Terminal.Print("========================================");

        Viper.Terminal.SetPosition(8, 25);
        Viper.Terminal.SetColor(7, 0);
        Viper.Terminal.Print("Classic Block-Stacking Fun!");

        // Menu options
        Viper.Terminal.SetPosition(11, 25);
        if menuChoice == 1 {
            Viper.Terminal.SetColor(2, 0);
            Viper.Terminal.Print("> NEW GAME");
        } else {
            Viper.Terminal.SetColor(7, 0);
            Viper.Terminal.Print("  NEW GAME");
        }

        Viper.Terminal.SetPosition(13, 25);
        if menuChoice == 2 {
            Viper.Terminal.SetColor(2, 0);
            Viper.Terminal.Print("> HIGH SCORES");
        } else {
            Viper.Terminal.SetColor(7, 0);
            Viper.Terminal.Print("  HIGH SCORES");
        }

        Viper.Terminal.SetPosition(15, 25);
        if menuChoice == 3 {
            Viper.Terminal.SetColor(2, 0);
            Viper.Terminal.Print("> INSTRUCTIONS");
        } else {
            Viper.Terminal.SetColor(7, 0);
            Viper.Terminal.Print("  INSTRUCTIONS");
        }

        Viper.Terminal.SetPosition(17, 25);
        if menuChoice == 4 {
            Viper.Terminal.SetColor(2, 0);
            Viper.Terminal.Print("> QUIT");
        } else {
            Viper.Terminal.SetColor(7, 0);
            Viper.Terminal.Print("  QUIT");
        }

        Viper.Terminal.SetPosition(20, 20);
        Viper.Terminal.SetColor(7, 0);
        Viper.Terminal.Print("Use W/S to navigate, SPACE or E to select");

        String key = Viper.Terminal.GetKeyTimeout(100);

        if key == "w" { menuChoice = menuChoice - 1; if menuChoice < 1 { menuChoice = 4; } }
        if key == "W" { menuChoice = menuChoice - 1; if menuChoice < 1 { menuChoice = 4; } }
        if key == "s" { menuChoice = menuChoice + 1; if menuChoice > 4 { menuChoice = 1; } }
        if key == "S" { menuChoice = menuChoice + 1; if menuChoice > 4 { menuChoice = 1; } }
        if key == " " { menuActive = 0; }
        if key == "e" { menuActive = 0; }
        if key == "E" { menuActive = 0; }

        Viper.Time.SleepMs(50);
    }

    if menuChoice == 1 { runGame(); }
    if menuChoice == 2 { showHighScores(); }
    if menuChoice == 3 { showInstructions(); }
    if menuChoice == 4 { menuRunning = 0; }
}

func clearBoard() {
    row0 = 0; row1 = 0; row2 = 0; row3 = 0;
    row4 = 0; row5 = 0; row6 = 0; row7 = 0;
    row8 = 0; row9 = 0; row10 = 0; row11 = 0;
    row12 = 0; row13 = 0; row14 = 0; row15 = 0;
    row16 = 0; row17 = 0; row18 = 0; row19 = 0;
    rowColor0 = 7; rowColor1 = 7; rowColor2 = 7; rowColor3 = 7;
    rowColor4 = 7; rowColor5 = 7; rowColor6 = 7; rowColor7 = 7;
    rowColor8 = 7; rowColor9 = 7; rowColor10 = 7; rowColor11 = 7;
    rowColor12 = 7; rowColor13 = 7; rowColor14 = 7; rowColor15 = 7;
    rowColor16 = 7; rowColor17 = 7; rowColor18 = 7; rowColor19 = 7;
}

func getRow(r: Integer) -> Integer {
    if r == 0 { return row0; } if r == 1 { return row1; }
    if r == 2 { return row2; } if r == 3 { return row3; }
    if r == 4 { return row4; } if r == 5 { return row5; }
    if r == 6 { return row6; } if r == 7 { return row7; }
    if r == 8 { return row8; } if r == 9 { return row9; }
    if r == 10 { return row10; } if r == 11 { return row11; }
    if r == 12 { return row12; } if r == 13 { return row13; }
    if r == 14 { return row14; } if r == 15 { return row15; }
    if r == 16 { return row16; } if r == 17 { return row17; }
    if r == 18 { return row18; } if r == 19 { return row19; }
    return 1023;  // Full row (all 10 bits set) for out of bounds
}

func setRow(r: Integer, val: Integer) {
    if r == 0 { row0 = val; } if r == 1 { row1 = val; }
    if r == 2 { row2 = val; } if r == 3 { row3 = val; }
    if r == 4 { row4 = val; } if r == 5 { row5 = val; }
    if r == 6 { row6 = val; } if r == 7 { row7 = val; }
    if r == 8 { row8 = val; } if r == 9 { row9 = val; }
    if r == 10 { row10 = val; } if r == 11 { row11 = val; }
    if r == 12 { row12 = val; } if r == 13 { row13 = val; }
    if r == 14 { row14 = val; } if r == 15 { row15 = val; }
    if r == 16 { row16 = val; } if r == 17 { row17 = val; }
    if r == 18 { row18 = val; } if r == 19 { row19 = val; }
}

func getRowColor(r: Integer) -> Integer {
    if r == 0 { return rowColor0; } if r == 1 { return rowColor1; }
    if r == 2 { return rowColor2; } if r == 3 { return rowColor3; }
    if r == 4 { return rowColor4; } if r == 5 { return rowColor5; }
    if r == 6 { return rowColor6; } if r == 7 { return rowColor7; }
    if r == 8 { return rowColor8; } if r == 9 { return rowColor9; }
    if r == 10 { return rowColor10; } if r == 11 { return rowColor11; }
    if r == 12 { return rowColor12; } if r == 13 { return rowColor13; }
    if r == 14 { return rowColor14; } if r == 15 { return rowColor15; }
    if r == 16 { return rowColor16; } if r == 17 { return rowColor17; }
    if r == 18 { return rowColor18; } if r == 19 { return rowColor19; }
    return 7;
}

func setRowColor(r: Integer, c: Integer) {
    if r == 0 { rowColor0 = c; } if r == 1 { rowColor1 = c; }
    if r == 2 { rowColor2 = c; } if r == 3 { rowColor3 = c; }
    if r == 4 { rowColor4 = c; } if r == 5 { rowColor5 = c; }
    if r == 6 { rowColor6 = c; } if r == 7 { rowColor7 = c; }
    if r == 8 { rowColor8 = c; } if r == 9 { rowColor9 = c; }
    if r == 10 { rowColor10 = c; } if r == 11 { rowColor11 = c; }
    if r == 12 { rowColor12 = c; } if r == 13 { rowColor13 = c; }
    if r == 14 { rowColor14 = c; } if r == 15 { rowColor15 = c; }
    if r == 16 { rowColor16 = c; } if r == 17 { rowColor17 = c; }
    if r == 18 { rowColor18 = c; } if r == 19 { rowColor19 = c; }
}

func getCell(r: Integer, c: Integer) -> Integer {
    Integer rowVal = getRow(r);
    // Check if bit c is set (using division and modulo)
    Integer mask = 1;
    Integer i = 0;
    while i < c {
        mask = mask * 2;
        i = i + 1;
    }
    Integer check = rowVal / mask;
    Integer bit = check - (check / 2) * 2;
    return bit;
}

func setCell(r: Integer, c: Integer, val: Integer) {
    Integer rowVal = getRow(r);
    Integer mask = 1;
    Integer i = 0;
    while i < c {
        mask = mask * 2;
        i = i + 1;
    }
    if val == 1 {
        rowVal = rowVal + mask;
    } else {
        // Clear bit (only if set)
        Integer check = rowVal / mask;
        Integer bit = check - (check / 2) * 2;
        if bit == 1 {
            rowVal = rowVal - mask;
        }
    }
    setRow(r, rowVal);
}

func canPlace(piece: Piece) -> Integer {
    Integer i = 0;
    while i < 4 {
        Integer j = 0;
        while j < 4 {
            if piece.getCell(i, j) == 1 {
                Integer bx = piece.getPosX() + j;
                Integer by = piece.getPosY() + i;
                // Check bounds
                if bx < 0 { return 0; }
                if bx >= BOARD_WIDTH { return 0; }
                if by < 0 { return 0; }
                if by >= BOARD_HEIGHT { return 0; }
                // Check collision
                if getCell(by, bx) == 1 { return 0; }
            }
            j = j + 1;
        }
        i = i + 1;
    }
    return 1;
}

func placePiece(piece: Piece) {
    Integer i = 0;
    while i < 4 {
        Integer j = 0;
        while j < 4 {
            if piece.getCell(i, j) == 1 {
                Integer bx = piece.getPosX() + j;
                Integer by = piece.getPosY() + i;
                if bx >= 0 {
                    if bx < BOARD_WIDTH {
                        if by >= 0 {
                            if by < BOARD_HEIGHT {
                                setCell(by, bx, 1);
                                setRowColor(by, piece.getColor());
                            }
                        }
                    }
                }
            }
            j = j + 1;
        }
        i = i + 1;
    }
}

func checkAndClearLines() -> Integer {
    Integer linesCleared = 0;
    Integer r = 19;
    while r >= 0 {
        Integer rowVal = getRow(r);
        // Full row = 1023 (all 10 bits set)
        if rowVal == 1023 {
            linesCleared = linesCleared + 1;
            // Move all rows above down
            Integer moveR = r;
            while moveR > 0 {
                setRow(moveR, getRow(moveR - 1));
                setRowColor(moveR, getRowColor(moveR - 1));
                moveR = moveR - 1;
            }
            setRow(0, 0);
            setRowColor(0, 7);
            // Don't decrement r, check same row again
        } else {
            r = r - 1;
        }
    }
    return linesCleared;
}

func runGame() {
    clearBoard();
    gameScore = 0;
    gameLines = 0;
    gameLevel = 1;
    gameOver = 0;
    dropSpeed = 30;
    dropCounter = 0;

    // Create current piece
    Piece currentPiece = new Piece();
    Integer pieceType = Viper.Random.NextInt() - (Viper.Random.NextInt() / 7) * 7;
    if pieceType < 0 { pieceType = 0 - pieceType; }
    pieceType = pieceType - (pieceType / 7) * 7;
    currentPiece.init(pieceType);

    // Create next piece
    Piece nextPiece = new Piece();
    Integer nextType = Viper.Random.NextInt() - (Viper.Random.NextInt() / 7) * 7;
    if nextType < 0 { nextType = 0 - nextType; }
    nextType = nextType - (nextType / 7) * 7;
    nextPiece.init(nextType);

    Viper.Terminal.Clear();
    Viper.Terminal.SetCursorVisible(0);

    while gameOver == 0 {
        // Draw board
        drawBoard(currentPiece, nextPiece);

        // Handle input
        String key = Viper.Terminal.GetKeyTimeout(50);

        if key == "a" {
            currentPiece.moveLeft();
            if canPlace(currentPiece) == 0 { currentPiece.moveRight(); }
        }
        if key == "A" {
            currentPiece.moveLeft();
            if canPlace(currentPiece) == 0 { currentPiece.moveRight(); }
        }
        if key == "d" {
            currentPiece.moveRight();
            if canPlace(currentPiece) == 0 { currentPiece.moveLeft(); }
        }
        if key == "D" {
            currentPiece.moveRight();
            if canPlace(currentPiece) == 0 { currentPiece.moveLeft(); }
        }
        if key == "w" {
            currentPiece.rotateClockwise();
            if canPlace(currentPiece) == 0 {
                currentPiece.rotateClockwise();
                currentPiece.rotateClockwise();
                currentPiece.rotateClockwise();
            }
        }
        if key == "W" {
            currentPiece.rotateClockwise();
            if canPlace(currentPiece) == 0 {
                currentPiece.rotateClockwise();
                currentPiece.rotateClockwise();
                currentPiece.rotateClockwise();
            }
        }
        if key == "s" {
            currentPiece.moveDown();
            if canPlace(currentPiece) == 0 {
                Integer py = currentPiece.getPosY();
                currentPiece.setPosY(py - 1);
                placePiece(currentPiece);
                Integer cleared = checkAndClearLines();
                if cleared > 0 {
                    gameScore = gameScore + cleared * cleared * 100;
                    gameLines = gameLines + cleared;
                    updateLevel();
                }
                spawnNextPiece(currentPiece, nextPiece);
                if canPlace(currentPiece) == 0 { gameOver = 1; }
            }
        }
        if key == "S" {
            currentPiece.moveDown();
            if canPlace(currentPiece) == 0 {
                Integer py = currentPiece.getPosY();
                currentPiece.setPosY(py - 1);
                placePiece(currentPiece);
                Integer cleared = checkAndClearLines();
                if cleared > 0 {
                    gameScore = gameScore + cleared * cleared * 100;
                    gameLines = gameLines + cleared;
                    updateLevel();
                }
                spawnNextPiece(currentPiece, nextPiece);
                if canPlace(currentPiece) == 0 { gameOver = 1; }
            }
        }
        if key == " " {
            // Hard drop
            while canPlace(currentPiece) == 1 {
                currentPiece.moveDown();
            }
            Integer py = currentPiece.getPosY();
            currentPiece.setPosY(py - 1);
            placePiece(currentPiece);
            Integer cleared = checkAndClearLines();
            if cleared > 0 {
                gameScore = gameScore + cleared * cleared * 100;
                gameLines = gameLines + cleared;
                updateLevel();
            }
            spawnNextPiece(currentPiece, nextPiece);
            if canPlace(currentPiece) == 0 { gameOver = 1; }
        }
        if key == "q" { gameOver = 1; }
        if key == "Q" { gameOver = 1; }

        // Auto drop
        dropCounter = dropCounter + 1;
        if dropCounter >= dropSpeed {
            dropCounter = 0;
            currentPiece.moveDown();
            if canPlace(currentPiece) == 0 {
                Integer py = currentPiece.getPosY();
                currentPiece.setPosY(py - 1);
                placePiece(currentPiece);
                Integer cleared = checkAndClearLines();
                if cleared > 0 {
                    gameScore = gameScore + cleared * cleared * 100;
                    gameLines = gameLines + cleared;
                    updateLevel();
                }
                spawnNextPiece(currentPiece, nextPiece);
                if canPlace(currentPiece) == 0 { gameOver = 1; }
            }
        }

        Viper.Time.SleepMs(50);
    }

    showGameOver();
}

func spawnNextPiece(current: Piece, next: Piece) {
    // Copy next piece to current
    Integer nextType = next.getType();
    current.init(nextType);
    current.setPosX(3);
    current.setPosY(0);

    // Generate new next piece
    Integer newType = Viper.Random.NextInt();
    if newType < 0 { newType = 0 - newType; }
    newType = newType - (newType / 7) * 7;
    next.init(newType);
}

func updateLevel() {
    Integer newLevel = gameLines / 10 + 1;
    if newLevel > gameLevel {
        gameLevel = newLevel;
        dropSpeed = 30 - gameLevel * 2;
        if dropSpeed < 5 { dropSpeed = 5; }
    }
}

func drawBoard(piece: Piece, nextPiece: Piece) {
    Viper.Terminal.BeginBatch();

    // Top border
    Viper.Terminal.SetPosition(1, 1);
    Viper.Terminal.SetColor(6, 0);
    Viper.Terminal.Print("+===================+");

    // Draw rows
    Integer r = 0;
    while r < 20 {
        Viper.Terminal.SetPosition(r + 2, 1);
        Viper.Terminal.SetColor(6, 0);
        Viper.Terminal.Print("|");

        Integer c = 0;
        while c < 10 {
            Integer cell = getCell(r, c);
            if cell == 1 {
                Viper.Terminal.SetColor(getRowColor(r), 0);
                Viper.Terminal.Print("[]");
            } else {
                Viper.Terminal.SetColor(0, 0);
                Viper.Terminal.Print("  ");
            }
            c = c + 1;
        }

        Viper.Terminal.SetPosition(r + 2, 22);
        Viper.Terminal.SetColor(6, 0);
        Viper.Terminal.Print("|");
        r = r + 1;
    }

    // Bottom border
    Viper.Terminal.SetPosition(22, 1);
    Viper.Terminal.SetColor(6, 0);
    Viper.Terminal.Print("+===================+");

    // Draw current piece
    Integer pi = 0;
    while pi < 4 {
        Integer pj = 0;
        while pj < 4 {
            if piece.getCell(pi, pj) == 1 {
                Integer bx = piece.getPosX() + pj;
                Integer by = piece.getPosY() + pi;
                if by >= 0 {
                    if by < 20 {
                        if bx >= 0 {
                            if bx < 10 {
                                Integer screenRow = by + 2;
                                Integer screenCol = bx * 2 + 2;
                                Viper.Terminal.SetPosition(screenRow, screenCol);
                                Viper.Terminal.SetColor(piece.getColor(), 0);
                                Viper.Terminal.Print("[]");
                            }
                        }
                    }
                }
            }
            pj = pj + 1;
        }
        pi = pi + 1;
    }

    // Draw UI panel
    Viper.Terminal.SetPosition(2, 26);
    Viper.Terminal.SetColor(6, 0);
    Viper.Terminal.Print("+--------------+");
    Viper.Terminal.SetPosition(3, 26);
    Viper.Terminal.Print("| vTRIS        |");
    Viper.Terminal.SetPosition(4, 26);
    Viper.Terminal.Print("+--------------+");

    Viper.Terminal.SetPosition(5, 26);
    Viper.Terminal.SetColor(7, 0);
    Viper.Terminal.Print("| SCORE:       |");
    Viper.Terminal.SetPosition(6, 26);
    Viper.Terminal.Print("| ");
    Viper.Terminal.PrintInt(gameScore);
    Viper.Terminal.Print("          ");
    Viper.Terminal.SetPosition(6, 41);
    Viper.Terminal.Print("|");

    Viper.Terminal.SetPosition(7, 26);
    Viper.Terminal.Print("| LINES:       |");
    Viper.Terminal.SetPosition(8, 26);
    Viper.Terminal.Print("| ");
    Viper.Terminal.PrintInt(gameLines);
    Viper.Terminal.Print("          ");
    Viper.Terminal.SetPosition(8, 41);
    Viper.Terminal.Print("|");

    Viper.Terminal.SetPosition(9, 26);
    Viper.Terminal.Print("| LEVEL:       |");
    Viper.Terminal.SetPosition(10, 26);
    Viper.Terminal.Print("| ");
    Viper.Terminal.PrintInt(gameLevel);
    Viper.Terminal.Print("          ");
    Viper.Terminal.SetPosition(10, 41);
    Viper.Terminal.Print("|");

    Viper.Terminal.SetPosition(11, 26);
    Viper.Terminal.SetColor(6, 0);
    Viper.Terminal.Print("+--------------+");
    Viper.Terminal.SetPosition(12, 26);
    Viper.Terminal.Print("| NEXT:        |");

    // Draw next piece preview
    Integer ni = 0;
    while ni < 4 {
        Viper.Terminal.SetPosition(13 + ni, 26);
        Viper.Terminal.SetColor(6, 0);
        Viper.Terminal.Print("| ");
        Integer nj = 0;
        while nj < 4 {
            if nextPiece.getCell(ni, nj) == 1 {
                Viper.Terminal.SetColor(nextPiece.getColor(), 0);
                Viper.Terminal.Print("[]");
            } else {
                Viper.Terminal.Print("  ");
            }
            nj = nj + 1;
        }
        Viper.Terminal.SetColor(6, 0);
        Viper.Terminal.Print("    |");
        ni = ni + 1;
    }

    Viper.Terminal.SetPosition(17, 26);
    Viper.Terminal.SetColor(6, 0);
    Viper.Terminal.Print("+--------------+");
    Viper.Terminal.SetPosition(18, 26);
    Viper.Terminal.SetColor(7, 0);
    Viper.Terminal.Print("| A/D Move     |");
    Viper.Terminal.SetPosition(19, 26);
    Viper.Terminal.Print("| W Rotate     |");
    Viper.Terminal.SetPosition(20, 26);
    Viper.Terminal.Print("| S Soft drop  |");
    Viper.Terminal.SetPosition(21, 26);
    Viper.Terminal.Print("| SPACE Hard   |");
    Viper.Terminal.SetPosition(22, 26);
    Viper.Terminal.Print("| Q Quit       |");
    Viper.Terminal.SetPosition(23, 26);
    Viper.Terminal.SetColor(6, 0);
    Viper.Terminal.Print("+--------------+");

    Viper.Terminal.EndBatch();
}

func showGameOver() {
    Viper.Terminal.Clear();
    Viper.Terminal.SetCursorVisible(1);

    Viper.Terminal.SetPosition(5, 15);
    Viper.Terminal.SetColor(1, 0);
    Viper.Terminal.Print("========================================");
    Viper.Terminal.SetPosition(7, 15);
    Viper.Terminal.Print("           G A M E   O V E R");
    Viper.Terminal.SetPosition(9, 15);
    Viper.Terminal.Print("========================================");

    Viper.Terminal.SetPosition(12, 20);
    Viper.Terminal.SetColor(7, 0);
    Viper.Terminal.Print("Final Score: ");
    Viper.Terminal.PrintInt(gameScore);

    Viper.Terminal.SetPosition(13, 20);
    Viper.Terminal.Print("Lines Cleared: ");
    Viper.Terminal.PrintInt(gameLines);

    Viper.Terminal.SetPosition(14, 20);
    Viper.Terminal.Print("Level Reached: ");
    Viper.Terminal.PrintInt(gameLevel);

    // Check high score
    if gameScore > hs4 {
        addHighScore(gameScore);
        Viper.Terminal.SetPosition(16, 20);
        Viper.Terminal.SetColor(3, 0);
        Viper.Terminal.Print("NEW HIGH SCORE!");
    }

    Viper.Terminal.SetPosition(20, 15);
    Viper.Terminal.SetColor(7, 0);
    Viper.Terminal.Print("Press any key to return to menu...");
    Viper.Terminal.GetKey();
}

func showHighScores() {
    Viper.Terminal.Clear();
    Viper.Terminal.SetCursorVisible(0);

    Viper.Terminal.SetPosition(3, 20);
    Viper.Terminal.SetColor(6, 0);
    Viper.Terminal.Print("========================================");
    Viper.Terminal.SetPosition(4, 20);
    Viper.Terminal.Print("            HIGH SCORES");
    Viper.Terminal.SetPosition(5, 20);
    Viper.Terminal.Print("========================================");

    Viper.Terminal.SetColor(7, 0);
    Viper.Terminal.SetPosition(8, 25);
    Viper.Terminal.Print("1. ");
    Viper.Terminal.PrintInt(hs0);
    Viper.Terminal.SetPosition(10, 25);
    Viper.Terminal.Print("2. ");
    Viper.Terminal.PrintInt(hs1);
    Viper.Terminal.SetPosition(12, 25);
    Viper.Terminal.Print("3. ");
    Viper.Terminal.PrintInt(hs2);
    Viper.Terminal.SetPosition(14, 25);
    Viper.Terminal.Print("4. ");
    Viper.Terminal.PrintInt(hs3);
    Viper.Terminal.SetPosition(16, 25);
    Viper.Terminal.Print("5. ");
    Viper.Terminal.PrintInt(hs4);

    Viper.Terminal.SetPosition(20, 15);
    Viper.Terminal.Print("Press any key to return to menu...");
    Viper.Terminal.GetKey();
}

func addHighScore(newScore: Integer) {
    if newScore > hs0 {
        hs4 = hs3; hs3 = hs2; hs2 = hs1; hs1 = hs0; hs0 = newScore;
    } else {
        if newScore > hs1 {
            hs4 = hs3; hs3 = hs2; hs2 = hs1; hs1 = newScore;
        } else {
            if newScore > hs2 {
                hs4 = hs3; hs3 = hs2; hs2 = newScore;
            } else {
                if newScore > hs3 {
                    hs4 = hs3; hs3 = newScore;
                } else {
                    if newScore > hs4 {
                        hs4 = newScore;
                    }
                }
            }
        }
    }
}

func showInstructions() {
    Viper.Terminal.Clear();
    Viper.Terminal.SetCursorVisible(0);

    Viper.Terminal.SetPosition(2, 15);
    Viper.Terminal.SetColor(6, 0);
    Viper.Terminal.Print("========================================");
    Viper.Terminal.SetPosition(3, 15);
    Viper.Terminal.Print("            HOW TO PLAY");
    Viper.Terminal.SetPosition(4, 15);
    Viper.Terminal.Print("========================================");

    Viper.Terminal.SetColor(7, 0);
    Viper.Terminal.SetPosition(6, 5);
    Viper.Terminal.Print("OBJECTIVE:");
    Viper.Terminal.SetPosition(7, 5);
    Viper.Terminal.Print("Complete horizontal lines to score points!");

    Viper.Terminal.SetPosition(9, 5);
    Viper.Terminal.SetColor(3, 0);
    Viper.Terminal.Print("CONTROLS:");
    Viper.Terminal.SetColor(7, 0);
    Viper.Terminal.SetPosition(10, 7);
    Viper.Terminal.Print("A/D - Move piece left/right");
    Viper.Terminal.SetPosition(11, 7);
    Viper.Terminal.Print("W   - Rotate piece clockwise");
    Viper.Terminal.SetPosition(12, 7);
    Viper.Terminal.Print("S   - Soft drop (move down faster)");
    Viper.Terminal.SetPosition(13, 7);
    Viper.Terminal.Print("SPACE - Hard drop (instant placement)");
    Viper.Terminal.SetPosition(14, 7);
    Viper.Terminal.Print("Q   - Quit to main menu");

    Viper.Terminal.SetPosition(16, 5);
    Viper.Terminal.SetColor(2, 0);
    Viper.Terminal.Print("SCORING:");
    Viper.Terminal.SetColor(7, 0);
    Viper.Terminal.SetPosition(17, 7);
    Viper.Terminal.Print("1 Line = 100 pts, 2 Lines = 400 pts");
    Viper.Terminal.SetPosition(18, 7);
    Viper.Terminal.Print("3 Lines = 900 pts, 4 Lines = 1600 pts");

    Viper.Terminal.SetPosition(20, 5);
    Viper.Terminal.SetColor(4, 0);
    Viper.Terminal.Print("LEVELS:");
    Viper.Terminal.SetColor(7, 0);
    Viper.Terminal.SetPosition(21, 7);
    Viper.Terminal.Print("Speed increases every 10 lines!");

    Viper.Terminal.SetPosition(24, 15);
    Viper.Terminal.Print("Press any key to return to menu...");
    Viper.Terminal.GetKey();
}
