// ============================================================================
// Frogger - Main Game
// ============================================================================
// Classic Frogger arcade game implemented in ViperLang.
// Features:
// - Roads with cars and trucks
// - River with logs and turtles to ride
// - 5 homes to fill at the top
// - Lives system and scoring
// - High score tracking
// ============================================================================

module Main;

import "config";
import "terminal";
import "frog";
import "vehicle";
import "platform";
import "home";
import "scores";

// Game state
var frog: Frog;
var vehicles: List[Vehicle];
var platforms: List[Platform];
var homes: List[Home];
var scoreManager: HighScoreManager;

var score: Integer;
var level: Integer;
var gameRunning: Boolean;
var homesFilledCount: Integer;

// ============================================================================
// Game Initialization
// ============================================================================

func initGame() {
    score = 0;
    level = 1;
    gameRunning = true;
    homesFilledCount = 0;

    // Create frog at start position
    frog = new Frog(START_ROW, 35);

    // Create 5 homes at top
    homes = [];
    var h0 = new Home(8);
    var h1 = new Home(20);
    var h2 = new Home(32);
    var h3 = new Home(44);
    var h4 = new Home(56);
    homes.add(h0);
    homes.add(h1);
    homes.add(h2);
    homes.add(h3);
    homes.add(h4);

    // Create road vehicles
    vehicles = [];
    createVehicles();

    // Create river platforms
    platforms = [];
    createPlatforms();
}

func createVehicles() {
    // Lane 1 (row 12): Cars going right
    var v0 = new Vehicle(12, 5, 1, 1, "=", 4);
    var v1 = new Vehicle(12, 35, 1, 1, "=", 4);
    vehicles.add(v0);
    vehicles.add(v1);

    // Lane 2 (row 13): Trucks going left
    var v2 = new Vehicle(13, 15, 1, 0 - 1, "#", 6);
    var v3 = new Vehicle(13, 50, 1, 0 - 1, "#", 6);
    vehicles.add(v2);
    vehicles.add(v3);

    // Lane 3 (row 14): Fast cars going right
    var v4 = new Vehicle(14, 10, 2, 1, "=", 4);
    var v5 = new Vehicle(14, 45, 2, 1, "=", 4);
    vehicles.add(v4);
    vehicles.add(v5);

    // Lane 4 (row 15): Trucks going left
    var v6 = new Vehicle(15, 20, 1, 0 - 1, "#", 6);
    var v7 = new Vehicle(15, 55, 1, 0 - 1, "#", 6);
    vehicles.add(v6);
    vehicles.add(v7);

    // Lane 5 (row 16): Fast cars going right
    var v8 = new Vehicle(16, 8, 2, 1, "=", 4);
    var v9 = new Vehicle(16, 40, 2, 1, "=", 4);
    var v10 = new Vehicle(16, 60, 2, 1, "=", 4);
    vehicles.add(v8);
    vehicles.add(v9);
    vehicles.add(v10);
}

func createPlatforms() {
    // Row 1 (row 4): Logs going right
    var p0 = new Platform(4, 5, 1, 1, "=", 8);
    var p1 = new Platform(4, 35, 1, 1, "=", 8);
    var p2 = new Platform(4, 60, 1, 1, "=", 6);
    platforms.add(p0);
    platforms.add(p1);
    platforms.add(p2);

    // Row 2 (row 5): Turtles going left
    var p3 = new Platform(5, 15, 1, 0 - 1, "O", 5);
    var p4 = new Platform(5, 40, 1, 0 - 1, "O", 5);
    var p5 = new Platform(5, 65, 1, 0 - 1, "O", 4);
    platforms.add(p3);
    platforms.add(p4);
    platforms.add(p5);

    // Row 3 (row 6): Long logs going right
    var p6 = new Platform(6, 10, 1, 1, "=", 12);
    var p7 = new Platform(6, 45, 1, 1, "=", 10);
    platforms.add(p6);
    platforms.add(p7);

    // Row 4 (row 7): Turtles going left
    var p8 = new Platform(7, 20, 1, 0 - 1, "O", 4);
    var p9 = new Platform(7, 45, 1, 0 - 1, "O", 4);
    var p10 = new Platform(7, 65, 1, 0 - 1, "O", 3);
    platforms.add(p8);
    platforms.add(p9);
    platforms.add(p10);

    // Row 5 (row 8): Fast logs going right
    var p11 = new Platform(8, 12, 2, 1, "=", 7);
    var p12 = new Platform(8, 48, 2, 1, "=", 7);
    platforms.add(p11);
    platforms.add(p12);
}

// ============================================================================
// Game Rendering
// ============================================================================

func drawBoard() {
    beginFrame();

    // Draw title and status bar
    printColorAt(1, 2, COLOR_WHITE, "Lives:");
    printAt(1, 8, Viper.Convert.IntToStr(frog.getLives()));
    printColorCentered(1, COLOR_CYAN, "*** CLASSIC FROGGER ***");
    printColorAt(1, 55, COLOR_YELLOW, "Score:");
    printAt(1, 62, Viper.Convert.IntToStr(score));

    // Draw homes row (water background)
    drawHLine(HOME_ROW, 1, GAME_WIDTH, COLOR_BLUE, "~");

    // Draw each home slot
    for i in 0..homes.size() {
        var home = homes.get(i);
        drawHome(home);
    }

    // Clear gap row between homes and river
    drawHLine(3, 1, GAME_WIDTH, COLOR_WHITE, " ");

    // Draw river section (water)
    for row in RIVER_START..=RIVER_END {
        drawHLine(row, 1, GAME_WIDTH, COLOR_BLUE, "~");
    }

    // Draw river platforms (logs and turtles)
    for i in 0..platforms.size() {
        var plat = platforms.get(i);
        drawPlatform(plat);
    }

    // Clear gap row between river and median
    drawHLine(9, 1, GAME_WIDTH, COLOR_WHITE, " ");

    // Draw median (safe zone)
    drawHLine(MEDIAN_ROW, 1, GAME_WIDTH, COLOR_GREEN, "-");
    printColorAt(MEDIAN_ROW, 28, COLOR_GREEN, "SAFE ZONE");

    // Clear gap row between median and road
    drawHLine(11, 1, GAME_WIDTH, COLOR_WHITE, " ");

    // Draw road section
    for row in ROAD_START..=ROAD_END {
        drawHLine(row, 1, GAME_WIDTH, COLOR_WHITE, ".");
    }

    // Draw vehicles
    for i in 0..vehicles.size() {
        var veh = vehicles.get(i);
        drawVehicle(veh);
    }

    // Clear gap row between road and start
    drawHLine(17, 1, GAME_WIDTH, COLOR_WHITE, " ");

    // Draw start area
    drawHLine(START_ROW, 1, GAME_WIDTH, COLOR_GREEN, "-");
    printColorAt(START_ROW, 30, COLOR_GREEN, "START");

    // Draw frog
    printColorAt(frog.getRow(), frog.getCol(), COLOR_GREEN, frog.getSymbol());

    // Draw instructions
    printAt(20, 1, "WASD=Move  P=Pause  Q=Quit  Goal: Fill all 5 homes!");

    endFrame();
}

func drawHome(home: Home) {
    var c = home.getCol();
    if home.isFilled() {
        printColorAt(HOME_ROW, c - 1, COLOR_GREEN, "[");
        printColorAt(HOME_ROW, c, COLOR_GREEN, "F");
        printColorAt(HOME_ROW, c + 1, COLOR_GREEN, "]");
    } else {
        printColorAt(HOME_ROW, c - 1, COLOR_WHITE, "[");
        printColorAt(HOME_ROW, c, COLOR_WHITE, " ");
        printColorAt(HOME_ROW, c + 1, COLOR_WHITE, "]");
    }
}

func drawVehicle(veh: Vehicle) {
    var r = veh.getRow();
    var c = veh.getCol();
    var w = veh.getWidth();
    var sym = veh.getSymbol();

    for i in 0..w {
        var drawCol = c + i;
        if drawCol >= 1 && drawCol <= GAME_WIDTH {
            printColorAt(r, drawCol, COLOR_RED, sym);
        }
    }
}

func drawPlatform(plat: Platform) {
    var r = plat.getRow();
    var c = plat.getCol();
    var w = plat.getWidth();
    var sym = plat.getSymbol();
    var color = COLOR_YELLOW;
    if plat.getIsTurtle() {
        color = COLOR_GREEN;
    }

    for i in 0..w {
        var drawCol = c + i;
        if drawCol >= 1 && drawCol <= GAME_WIDTH {
            printColorAt(r, drawCol, color, sym);
        }
    }
}

// ============================================================================
// Game Logic
// ============================================================================

func isInRiver(row: Integer) -> Boolean {
    return row >= RIVER_START && row <= RIVER_END;
}

func updateGame() {
    var frogRow = frog.getRow();
    var frogCol = frog.getCol();

    // Move all vehicles
    for i in 0..vehicles.size() {
        var veh = vehicles.get(i);
        veh.move();
    }

    // Move all platforms
    for i in 0..platforms.size() {
        var plat = platforms.get(i);
        plat.move();
    }

    // Check if frog is in river
    if isInRiver(frogRow) {
        var onPlatform = false;

        // Check each platform
        for i in 0..platforms.size() {
            var plat = platforms.get(i);
            if plat.checkOnPlatform(frogRow, frogCol) {
                onPlatform = true;
                frog.setOnPlatform(plat.getMovementSpeed());
                frog.updateOnPlatform();
                break;
            }
        }

        // If not on platform, frog drowns
        if !onPlatform {
            frog.die();
            frog.clearPlatform();
            if frog.isAlive() {
                printColorCentered(12, COLOR_RED, "SPLASH!");
                endFrame();
                sleep(DEATH_DELAY_MS);
            }
            return;
        }
    } else {
        frog.clearPlatform();
    }

    // Check road collisions
    if frogRow >= ROAD_START && frogRow <= ROAD_END {
        for i in 0..vehicles.size() {
            var veh = vehicles.get(i);
            if veh.checkCollision(frogRow, frogCol) {
                frog.die();
                if frog.isAlive() {
                    printColorCentered(12, COLOR_RED, "SPLAT!");
                    endFrame();
                    sleep(DEATH_DELAY_MS);
                }
                return;
            }
        }
    }

    // Check if frog reached home row
    if frogRow == HOME_ROW {
        var foundHome = false;

        for i in 0..homes.size() {
            var home = homes.get(i);
            if home.checkReached(frogCol) {
                if !home.isFilled() {
                    home.fill();
                    homesFilledCount = homesFilledCount + 1;
                    score = score + SCORE_HOME;
                    foundHome = true;

                    // Check win condition
                    if homesFilledCount == 5 {
                        gameRunning = false;
                    } else {
                        frog.reset();
                        printColorCentered(12, COLOR_GREEN, "HOME SAFE! +200");
                        endFrame();
                        sleep(HOME_DELAY_MS);
                    }
                } else {
                    // Home already filled
                    frog.die();
                }
                break;
            }
        }

        // Didn't land in a home slot
        if !foundHome {
            frog.die();
        }
    }

    // Check if frog went off screen
    if frogCol < 1 || frogCol > GAME_WIDTH {
        frog.die();
    }
}

func handleInput() {
    var key = getKey();

    if Viper.String.Length(key) > 0 {
        var ch = key.ToUpper();

        if ch == "W" {
            var bonus = frog.moveUp();
            score = score + bonus;
        } else if ch == "S" {
            frog.moveDown();
        } else if ch == "A" {
            frog.moveLeft();
        } else if ch == "D" {
            frog.moveRight();
        } else if ch == "Q" {
            gameRunning = false;
        } else if ch == "P" {
            handlePause();
        }
    }
}

func handlePause() {
    printColorCentered(12, COLOR_YELLOW, "*** PAUSED ***");
    printColorCentered(13, COLOR_WHITE, "Press P to resume");
    endFrame();

    while true {
        var key = getKey();
        if Viper.String.Length(key) > 0 {
            var ch = key.ToUpper();
            if ch == "P" {
                break;
            }
        }
        sleep(INPUT_DELAY_MS);
    }
}

// ============================================================================
// Game Loop
// ============================================================================

func gameLoop() {
    clearScreen();
    hideCursor();

    while gameRunning && frog.isAlive() {
        drawBoard();
        handleInput();
        updateGame();
        sleep(GAME_TICK_MS);
    }

    // Game over screen
    clearScreen();
    showCursor();

    Viper.Terminal.Say("");

    if homesFilledCount == 5 {
        Viper.Terminal.Say("============================================");
        Viper.Terminal.Say("      * CONGRATULATIONS! YOU WIN! *        ");
        Viper.Terminal.Say("============================================");
        Viper.Terminal.Say("");
        Viper.Terminal.Say("  You successfully filled all 5 homes!");
    } else if !frog.isAlive() {
        Viper.Terminal.Say("============================================");
        Viper.Terminal.Say("           G A M E   O V E R               ");
        Viper.Terminal.Say("============================================");
    } else {
        Viper.Terminal.Say("  Thanks for playing!");
    }

    Viper.Terminal.Say("");
    Viper.Terminal.Print("  Final Score: ");
    Viper.Terminal.SayInt(score);
    Viper.Terminal.Print("  Homes Filled: ");
    Viper.Terminal.PrintInt(homesFilledCount);
    Viper.Terminal.Say(" / 5");
    Viper.Terminal.Say("");

    // Check for high score
    if scoreManager.isHighScore(score) && score > 0 {
        var playerName = getPlayerName();
        scoreManager.addScore(playerName, score);
        scoreManager.save();
        Viper.Terminal.Say("");
        Viper.Terminal.Say("  Your score has been added to the high score list!");
    }

    Viper.Terminal.Say("");
    Viper.Terminal.Say("  Press any key to return to menu...");
    waitForKey();
}

// ============================================================================
// Main Menu
// ============================================================================

func showMainMenu() {
    var choice = 1;

    while true {
        clearScreen();
        Viper.Terminal.Say("");
        Viper.Terminal.Say("============================================");
        Viper.Terminal.Say("");
        Viper.Terminal.Say("   FFFFF  RRRR    OOO    GGGG   GGGG  EEEEE  RRRR ");
        Viper.Terminal.Say("   F      R   R  O   O  G      G      E      R   R");
        Viper.Terminal.Say("   FFF    RRRR   O   O  G  GG  G  GG  EEE    RRRR ");
        Viper.Terminal.Say("   F      R  R   O   O  G   G  G   G  E      R  R ");
        Viper.Terminal.Say("   F      R   R   OOO    GGGG   GGGG  EEEEE  R   R");
        Viper.Terminal.Say("");
        Viper.Terminal.Say("              Classic Arcade Action!             ");
        Viper.Terminal.Say("============================================");
        Viper.Terminal.Say("");

        // Menu options
        if choice == 1 {
            Viper.Terminal.Say("                > START GAME");
        } else {
            Viper.Terminal.Say("                  START GAME");
        }

        if choice == 2 {
            Viper.Terminal.Say("                > HIGH SCORES");
        } else {
            Viper.Terminal.Say("                  HIGH SCORES");
        }

        if choice == 3 {
            Viper.Terminal.Say("                > INSTRUCTIONS");
        } else {
            Viper.Terminal.Say("                  INSTRUCTIONS");
        }

        if choice == 4 {
            Viper.Terminal.Say("                > QUIT");
        } else {
            Viper.Terminal.Say("                  QUIT");
        }

        Viper.Terminal.Say("");
        Viper.Terminal.Say("         Use W/S to navigate, ENTER to select");

        var key = getKey();
        if Viper.String.Length(key) > 0 {
            var ch = key.ToUpper();
            var code = key.CharAt(0);

            if ch == "W" {
                choice = choice - 1;
                if choice < 1 {
                    choice = 4;
                }
            } else if ch == "S" {
                choice = choice + 1;
                if choice > 4 {
                    choice = 1;
                }
            } else if code == 13 || code == 10 {
                // Enter pressed
                sleep(MENU_DELAY_MS);

                if choice == 1 {
                    initGame();
                    gameLoop();
                    sleep(MENU_DELAY_MS);
                } else if choice == 2 {
                    scoreManager.display();
                    waitForKey();
                    sleep(MENU_DELAY_MS);
                } else if choice == 3 {
                    showInstructions();
                    sleep(MENU_DELAY_MS);
                } else if choice == 4 {
                    break;
                }
            }
        }

        sleep(INPUT_DELAY_MS);
    }
}

func showInstructions() {
    clearScreen();
    Viper.Terminal.Say("");
    Viper.Terminal.Say("============================================");
    Viper.Terminal.Say("              HOW TO PLAY                   ");
    Viper.Terminal.Say("============================================");
    Viper.Terminal.Say("");
    Viper.Terminal.Say("  OBJECTIVE:");
    Viper.Terminal.Say("  Guide your frog safely across the road and river");
    Viper.Terminal.Say("  to fill all 5 homes at the top of the screen.");
    Viper.Terminal.Say("");
    Viper.Terminal.Say("  CONTROLS:");
    Viper.Terminal.Say("    W - Move Up");
    Viper.Terminal.Say("    S - Move Down");
    Viper.Terminal.Say("    A - Move Left");
    Viper.Terminal.Say("    D - Move Right");
    Viper.Terminal.Say("    P - Pause Game");
    Viper.Terminal.Say("    Q - Quit to Menu");
    Viper.Terminal.Say("");
    Viper.Terminal.Say("  GAMEPLAY:");
    Viper.Terminal.Say("  - Avoid cars and trucks on the road");
    Viper.Terminal.Say("  - Jump on logs and turtles in the river");
    Viper.Terminal.Say("  - You drown if you fall in the water!");
    Viper.Terminal.Say("  - Land in the home slots [ ] at the top");
    Viper.Terminal.Say("  - Each home filled = +200 points");
    Viper.Terminal.Say("  - You have 3 lives - use them wisely!");
    Viper.Terminal.Say("");
    Viper.Terminal.Say("  Press any key to return to menu...");
    waitForKey();
}

// ============================================================================
// Entry Point
// ============================================================================

func start() {
    // Initialize color constants
    initColors();

    // Initialize high score manager
    scoreManager = new HighScoreManager();
    scoreManager.load();

    // Show main menu
    showMainMenu();

    // Cleanup
    clearScreen();
    Viper.Terminal.Say("");
    Viper.Terminal.Say("Thanks for playing CLASSIC FROGGER!");
    Viper.Terminal.Say("");
}
