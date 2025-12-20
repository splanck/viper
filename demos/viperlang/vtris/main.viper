module VTris;

import "./piece";
import "./board";
import "./game";

// Display constants
Integer BOARD_LEFT = 2;
Integer BOARD_TOP = 1;
Integer INFO_LEFT = 25;

// Block characters
String BLOCK = "[]";
String EMPTY = "  ";

func drawBlock(Integer x, Integer y, Integer color) {
    Viper.Terminal.MoveCursor(y, x);
    // Set color based on piece type
    if (color == 1) {
        Viper.Terminal.SetForeground(6);  // Cyan - I piece
    } else if (color == 2) {
        Viper.Terminal.SetForeground(3);  // Yellow - O piece
    } else if (color == 3) {
        Viper.Terminal.SetForeground(5);  // Magenta - T piece
    } else if (color == 4) {
        Viper.Terminal.SetForeground(2);  // Green - S piece
    } else if (color == 5) {
        Viper.Terminal.SetForeground(1);  // Red - Z piece
    } else if (color == 6) {
        Viper.Terminal.SetForeground(4);  // Blue - J piece
    } else if (color == 7) {
        Viper.Terminal.SetForeground(214); // Orange - L piece (216 is close to orange)
    } else {
        Viper.Terminal.SetForeground(7);  // White/default
    }
    Viper.Terminal.Write(BLOCK);
    Viper.Terminal.ResetColors();
}

func drawEmpty(Integer x, Integer y) {
    Viper.Terminal.MoveCursor(y, x);
    Viper.Terminal.SetForeground(8);  // Dark gray
    Viper.Terminal.Write(". ");
    Viper.Terminal.ResetColors();
}

func drawBoard(Board board) {
    var row = 0;
    while (row < board.height) {
        var col = 0;
        while (col < board.width) {
            Integer cell = board.getCell(row, col);
            Integer screenX = BOARD_LEFT + col * 2;
            Integer screenY = BOARD_TOP + row;
            if (cell > 0) {
                drawBlock(screenX, screenY, cell);
            } else {
                drawEmpty(screenX, screenY);
            }
            col = col + 1;
        }
        row = row + 1;
    }
}

func drawPiece(Piece p, Integer offsetX, Integer offsetY) {
    Integer color = p.getColor();
    var i = 0;
    while (i < 4) {
        Integer blockRow = p.y + p.getBlockRow(i);
        Integer blockCol = p.x + p.getBlockCol(i);
        if (blockRow >= 0) {  // Only draw if on visible board
            Integer screenX = offsetX + blockCol * 2;
            Integer screenY = offsetY + blockRow;
            drawBlock(screenX, screenY, color);
        }
        i = i + 1;
    }
}

func drawNextPiece(Piece p) {
    Viper.Terminal.MoveCursor(3, INFO_LEFT);
    Viper.Terminal.Write("Next:");
    
    // Clear next piece area
    var row = 0;
    while (row < 4) {
        var col = 0;
        while (col < 4) {
            Viper.Terminal.MoveCursor(4 + row, INFO_LEFT + col * 2);
            Viper.Terminal.Write("  ");
            col = col + 1;
        }
        row = row + 1;
    }
    
    // Draw the next piece at fixed position
    Integer color = p.getColor();
    var i = 0;
    while (i < 4) {
        Integer blockRow = p.getBlockRow(i);
        Integer blockCol = p.getBlockCol(i);
        Integer screenX = INFO_LEFT + blockCol * 2;
        Integer screenY = 4 + blockRow;
        drawBlock(screenX, screenY, color);
        i = i + 1;
    }
}

func drawInfo(Game game) {
    Viper.Terminal.MoveCursor(9, INFO_LEFT);
    Viper.Terminal.Write("Score: ");
    Viper.Terminal.SayInt(game.score);
    
    Viper.Terminal.MoveCursor(11, INFO_LEFT);
    Viper.Terminal.Write("Level: ");
    Viper.Terminal.SayInt(game.level);
    
    Viper.Terminal.MoveCursor(13, INFO_LEFT);
    Viper.Terminal.Write("Lines: ");
    Viper.Terminal.SayInt(game.linesCleared);
    
    Viper.Terminal.MoveCursor(16, INFO_LEFT);
    Viper.Terminal.Write("Controls:");
    Viper.Terminal.MoveCursor(17, INFO_LEFT);
    Viper.Terminal.Write("A/D - Move");
    Viper.Terminal.MoveCursor(18, INFO_LEFT);
    Viper.Terminal.Write("W   - Rotate");
    Viper.Terminal.MoveCursor(19, INFO_LEFT);
    Viper.Terminal.Write("S   - Soft Drop");
    Viper.Terminal.MoveCursor(20, INFO_LEFT);
    Viper.Terminal.Write("Space - Hard Drop");
    Viper.Terminal.MoveCursor(21, INFO_LEFT);
    Viper.Terminal.Write("Q   - Quit");
}

func drawGameOver() {
    Viper.Terminal.MoveCursor(10, BOARD_LEFT + 2);
    Viper.Terminal.SetForeground(1);  // Red
    Viper.Terminal.Write("GAME OVER!");
    Viper.Terminal.ResetColors();
    Viper.Terminal.MoveCursor(12, BOARD_LEFT);
    Viper.Terminal.Write("Press Q to quit");
}

func drawBorder() {
    // Draw left and right borders
    var row = 0;
    while (row < 20) {
        Viper.Terminal.MoveCursor(BOARD_TOP + row, BOARD_LEFT - 1);
        Viper.Terminal.Write("|");
        Viper.Terminal.MoveCursor(BOARD_TOP + row, BOARD_LEFT + 20);
        Viper.Terminal.Write("|");
        row = row + 1;
    }
    // Draw bottom border
    Viper.Terminal.MoveCursor(BOARD_TOP + 20, BOARD_LEFT - 1);
    Viper.Terminal.Write("+--------------------+");
}

func render(Game game) {
    drawBoard(game.board);
    drawPiece(game.currentPiece, BOARD_LEFT, BOARD_TOP);
    drawNextPiece(game.nextPiece);
    drawInfo(game);
    drawBorder();
    
    if (game.gameOver) {
        drawGameOver();
    }
}

func start() {
    // Initialize terminal
    Viper.Terminal.Clear();
    Viper.Terminal.HideCursor();
    Viper.Terminal.MoveCursor(0, 0);
    Viper.Terminal.Write("=== VTRIS ===");
    
    // Create game
    Game game = createGame();
    
    Boolean running = true;
    
    while (running) {
        // Handle input
        if (Viper.Terminal.HasKey()) {
            String key = Viper.Terminal.ReadKey();
            
            if (key == "q" || key == "Q") {
                running = false;
            } else if (!game.gameOver) {
                if (key == "a" || key == "A") {
                    game.moveLeft();
                } else if (key == "d" || key == "D") {
                    game.moveRight();
                } else if (key == "w" || key == "W") {
                    game.rotate();
                } else if (key == "s" || key == "S") {
                    game.softDrop();
                } else if (key == " ") {
                    game.hardDrop();
                }
            }
        }
        
        // Update game state
        game.update();
        
        // Render
        render(game);
        
        // Frame delay (roughly 30 FPS)
        Viper.Terminal.Sleep(33);
    }
    
    // Cleanup
    Viper.Terminal.ShowCursor();
    Viper.Terminal.Clear();
    Viper.Terminal.Say("Thanks for playing VTris!");
    Viper.Terminal.Write("Final Score: ");
    Viper.Terminal.SayInt(game.score);
}
