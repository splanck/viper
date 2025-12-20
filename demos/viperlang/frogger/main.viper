module Frogger;

import "./entities";

// Game constants
Integer GAME_WIDTH = 70;
Integer GAME_HEIGHT = 24;
Integer HOME_ROW = 2;
Integer RIVER_START = 4;
Integer RIVER_END = 8;
Integer MEDIAN_ROW = 10;
Integer ROAD_START = 12;
Integer ROAD_END = 16;
Integer START_ROW = 18;

// Game state
Integer score = 0;
Integer gameRunning = 0;
Integer gamePaused = 0;
Integer homesFilledCount = 0;
Integer menuChoice = 1;
Integer menuRunning = 1;

// High scores (in-memory, top 5)
Integer hs0 = 0;
Integer hs1 = 0;
Integer hs2 = 0;
Integer hs3 = 0;
Integer hs4 = 0;

func start() {
    menuRunning = 1;
    while menuRunning == 1 {
        showMainMenu();
    }

    Viper.Terminal.Clear();
    Viper.Terminal.SetCursorVisible(1);
    Viper.Terminal.Say("Thanks for playing CLASSIC FROGGER!");
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
        Viper.Terminal.Print("          CLASSIC FROGGER              ");
        Viper.Terminal.SetPosition(4, 15);
        Viper.Terminal.Print("          ViperLang Edition            ");
        Viper.Terminal.SetPosition(5, 15);
        Viper.Terminal.Print("========================================");

        Viper.Terminal.SetPosition(8, 25);
        Viper.Terminal.SetColor(7, 0);
        Viper.Terminal.Print("Classic Arcade Action!");

        // Draw menu options
        Viper.Terminal.SetPosition(11, 25);
        if menuChoice == 1 {
            Viper.Terminal.SetColor(2, 0);
            Viper.Terminal.Print("> START GAME");
        } else {
            Viper.Terminal.SetColor(7, 0);
            Viper.Terminal.Print("  START GAME");
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

        // Handle input
        String key = Viper.Terminal.GetKeyTimeout(100);

        if key == "w" {
            menuChoice = menuChoice - 1;
            if menuChoice < 1 {
                menuChoice = 4;
            }
        }
        if key == "W" {
            menuChoice = menuChoice - 1;
            if menuChoice < 1 {
                menuChoice = 4;
            }
        }
        if key == "s" {
            menuChoice = menuChoice + 1;
            if menuChoice > 4 {
                menuChoice = 1;
            }
        }
        if key == "S" {
            menuChoice = menuChoice + 1;
            if menuChoice > 4 {
                menuChoice = 1;
            }
        }

        // Check for selection key (space or e)
        if key == " " {
            menuActive = 0;
        }
        if key == "e" {
            menuActive = 0;
        }
        if key == "E" {
            menuActive = 0;
        }

        Viper.Time.SleepMs(50);
    }

    // Handle selection
    if menuChoice == 1 {
        runGame();
    }
    if menuChoice == 2 {
        showHighScores();
    }
    if menuChoice == 3 {
        showInstructions();
    }
    if menuChoice == 4 {
        menuRunning = 0;
    }
}

func runGame() {
    // Create all game objects locally
    Frog frog = new Frog();

    Vehicle v0 = new Vehicle();
    Vehicle v1 = new Vehicle();
    Vehicle v2 = new Vehicle();
    Vehicle v3 = new Vehicle();
    Vehicle v4 = new Vehicle();
    Vehicle v5 = new Vehicle();
    Vehicle v6 = new Vehicle();
    Vehicle v7 = new Vehicle();
    Vehicle v8 = new Vehicle();
    Vehicle v9 = new Vehicle();

    Platform p0 = new Platform();
    Platform p1 = new Platform();
    Platform p2 = new Platform();
    Platform p3 = new Platform();
    Platform p4 = new Platform();
    Platform p5 = new Platform();
    Platform p6 = new Platform();
    Platform p7 = new Platform();
    Platform p8 = new Platform();
    Platform p9 = new Platform();

    Home h0 = new Home();
    Home h1 = new Home();
    Home h2 = new Home();
    Home h3 = new Home();
    Home h4 = new Home();

    // Initialize game state
    score = 0;
    gameRunning = 1;
    gamePaused = 0;
    homesFilledCount = 0;

    // Initialize frog at start position
    frog.init(START_ROW, 35);

    // Initialize homes
    h0.init(8);
    h1.init(20);
    h2.init(32);
    h3.init(44);
    h4.init(56);

    // Initialize vehicles - row, col, speed, direction, width, isTruck
    // Lane 1: Cars going right (row 12)
    v0.init(12, 5, 1, 1, 4, 0);
    v1.init(12, 35, 1, 1, 4, 0);

    // Lane 2: Trucks going left (row 13)
    v2.init(13, 15, 1, 0 - 1, 6, 1);
    v3.init(13, 50, 1, 0 - 1, 6, 1);

    // Lane 3: Cars going right (row 14)
    v4.init(14, 10, 2, 1, 4, 0);
    v5.init(14, 45, 2, 1, 4, 0);

    // Lane 4: Trucks going left (row 15)
    v6.init(15, 20, 1, 0 - 1, 6, 1);

    // Lane 5: Fast cars going right (row 16)
    v7.init(16, 8, 2, 1, 4, 0);
    v8.init(16, 40, 2, 1, 4, 0);
    v9.init(16, 60, 2, 1, 4, 0);

    // Initialize platforms - row, col, speed, direction, width, isTurtle
    // Row 1: Logs going right (row 4)
    p0.init(4, 5, 1, 1, 8, 0);
    p1.init(4, 40, 1, 1, 8, 0);

    // Row 2: Turtles going left (row 5)
    p2.init(5, 15, 1, 0 - 1, 5, 1);
    p3.init(5, 50, 1, 0 - 1, 5, 1);

    // Row 3: Long logs going right (row 6)
    p4.init(6, 10, 1, 1, 12, 0);
    p5.init(6, 50, 1, 1, 10, 0);

    // Row 4: Turtles going left (row 7)
    p6.init(7, 20, 1, 0 - 1, 4, 1);
    p7.init(7, 55, 1, 0 - 1, 4, 1);

    // Row 5: Logs going right (row 8)
    p8.init(8, 12, 2, 1, 7, 0);
    p9.init(8, 48, 2, 1, 7, 0);

    Viper.Terminal.Clear();
    Viper.Terminal.SetCursorVisible(0);

    // Main game loop
    while gameRunning == 1 {
        if frog.isAlive() == 0 {
            gameRunning = 0;
        } else {
            if gamePaused == 0 {
                // Draw board
                Viper.Terminal.BeginBatch();

                // Draw title and status
                Viper.Terminal.SetPosition(1, 25);
                Viper.Terminal.SetColor(6, 0);
                Viper.Terminal.Print("*** CLASSIC FROGGER ***");

                Viper.Terminal.SetPosition(1, 2);
                Viper.Terminal.SetColor(7, 0);
                Viper.Terminal.Print("Lives: ");
                Viper.Terminal.PrintInt(frog.getLives());
                Viper.Terminal.Print("  ");

                Viper.Terminal.SetPosition(1, 55);
                Viper.Terminal.SetColor(3, 0);
                Viper.Terminal.Print("Score: ");
                Viper.Terminal.PrintInt(score);
                Viper.Terminal.Print("   ");

                // Draw homes row (blue water)
                Integer i = 1;
                while i <= GAME_WIDTH {
                    Viper.Terminal.SetPosition(HOME_ROW, i);
                    Viper.Terminal.SetColor(4, 0);
                    Viper.Terminal.Print("~");
                    i = i + 1;
                }

                // Draw homes
                drawHome(h0);
                drawHome(h1);
                drawHome(h2);
                drawHome(h3);
                drawHome(h4);

                // Draw gap row 3
                i = 1;
                while i <= GAME_WIDTH {
                    Viper.Terminal.SetPosition(3, i);
                    Viper.Terminal.SetColor(7, 0);
                    Viper.Terminal.Print(" ");
                    i = i + 1;
                }

                // Draw river section
                Integer ri = RIVER_START;
                while ri <= RIVER_END {
                    Integer rj = 1;
                    while rj <= GAME_WIDTH {
                        Viper.Terminal.SetPosition(ri, rj);
                        Viper.Terminal.SetColor(4, 0);
                        Viper.Terminal.Print("~");
                        rj = rj + 1;
                    }
                    ri = ri + 1;
                }

                // Draw platforms
                drawPlatform(p0);
                drawPlatform(p1);
                drawPlatform(p2);
                drawPlatform(p3);
                drawPlatform(p4);
                drawPlatform(p5);
                drawPlatform(p6);
                drawPlatform(p7);
                drawPlatform(p8);
                drawPlatform(p9);

                // Draw gap row 9
                i = 1;
                while i <= GAME_WIDTH {
                    Viper.Terminal.SetPosition(9, i);
                    Viper.Terminal.SetColor(7, 0);
                    Viper.Terminal.Print(" ");
                    i = i + 1;
                }

                // Draw median (safe zone)
                i = 1;
                while i <= GAME_WIDTH {
                    Viper.Terminal.SetPosition(MEDIAN_ROW, i);
                    Viper.Terminal.SetColor(2, 0);
                    Viper.Terminal.Print("-");
                    i = i + 1;
                }
                Viper.Terminal.SetPosition(MEDIAN_ROW, 28);
                Viper.Terminal.Print("SAFE ZONE");

                // Draw gap row 11
                i = 1;
                while i <= GAME_WIDTH {
                    Viper.Terminal.SetPosition(11, i);
                    Viper.Terminal.SetColor(7, 0);
                    Viper.Terminal.Print(" ");
                    i = i + 1;
                }

                // Draw road section
                Integer roi = ROAD_START;
                while roi <= ROAD_END {
                    Integer roj = 1;
                    while roj <= GAME_WIDTH {
                        Viper.Terminal.SetPosition(roi, roj);
                        Viper.Terminal.SetColor(7, 0);
                        Viper.Terminal.Print(".");
                        roj = roj + 1;
                    }
                    roi = roi + 1;
                }

                // Draw vehicles
                drawVehicle(v0);
                drawVehicle(v1);
                drawVehicle(v2);
                drawVehicle(v3);
                drawVehicle(v4);
                drawVehicle(v5);
                drawVehicle(v6);
                drawVehicle(v7);
                drawVehicle(v8);
                drawVehicle(v9);

                // Draw gap row 17
                i = 1;
                while i <= GAME_WIDTH {
                    Viper.Terminal.SetPosition(17, i);
                    Viper.Terminal.SetColor(7, 0);
                    Viper.Terminal.Print(" ");
                    i = i + 1;
                }

                // Draw start area
                i = 1;
                while i <= GAME_WIDTH {
                    Viper.Terminal.SetPosition(START_ROW, i);
                    Viper.Terminal.SetColor(2, 0);
                    Viper.Terminal.Print("-");
                    i = i + 1;
                }
                Viper.Terminal.SetPosition(START_ROW, 30);
                Viper.Terminal.Print("START");

                // Draw frog
                Viper.Terminal.SetPosition(frog.getRow(), frog.getCol());
                Viper.Terminal.SetColor(10, 0);
                Viper.Terminal.Print("@");

                // Draw instructions
                Viper.Terminal.SetPosition(20, 1);
                Viper.Terminal.SetColor(7, 0);
                Viper.Terminal.Print("WASD=Move  P=Pause  Q=Quit  Goal: Fill all 5 homes!");

                Viper.Terminal.EndBatch();

                // Handle input
                String key = Viper.Terminal.GetKeyTimeout(50);

                if key == "w" {
                    frog.moveUp();
                }
                if key == "W" {
                    frog.moveUp();
                }
                if key == "s" {
                    frog.moveDown();
                }
                if key == "S" {
                    frog.moveDown();
                }
                if key == "a" {
                    frog.moveLeft();
                }
                if key == "A" {
                    frog.moveLeft();
                }
                if key == "d" {
                    frog.moveRight();
                }
                if key == "D" {
                    frog.moveRight();
                }
                if key == "p" {
                    gamePaused = 1;
                }
                if key == "P" {
                    gamePaused = 1;
                }
                if key == "q" {
                    gameRunning = 0;
                }
                if key == "Q" {
                    gameRunning = 0;
                }

                // Update game state
                Integer frogRow = frog.getRow();
                Integer frogCol = frog.getCol();

                // Move all vehicles
                v0.move();
                v1.move();
                v2.move();
                v3.move();
                v4.move();
                v5.move();
                v6.move();
                v7.move();
                v8.move();
                v9.move();

                // Move all platforms
                p0.move();
                p1.move();
                p2.move();
                p3.move();
                p4.move();
                p5.move();
                p6.move();
                p7.move();
                p8.move();
                p9.move();

                // Check river (only if in river area)
                if frogRow >= RIVER_START {
                    if frogRow <= RIVER_END {
                        Integer onPlat = 0;
                        onPlat = checkPlatform(frog, p0, onPlat);
                        onPlat = checkPlatform(frog, p1, onPlat);
                        onPlat = checkPlatform(frog, p2, onPlat);
                        onPlat = checkPlatform(frog, p3, onPlat);
                        onPlat = checkPlatform(frog, p4, onPlat);
                        onPlat = checkPlatform(frog, p5, onPlat);
                        onPlat = checkPlatform(frog, p6, onPlat);
                        onPlat = checkPlatform(frog, p7, onPlat);
                        onPlat = checkPlatform(frog, p8, onPlat);
                        onPlat = checkPlatform(frog, p9, onPlat);

                        if onPlat == 0 {
                            frog.die();
                            frog.clearPlatform();
                            if frog.isAlive() == 1 {
                                Viper.Terminal.SetPosition(12, 28);
                                Viper.Terminal.SetColor(1, 0);
                                Viper.Terminal.Print("SPLASH!");
                                Viper.Time.SleepMs(800);
                            }
                        }
                    } else {
                        frog.clearPlatform();
                    }
                } else {
                    frog.clearPlatform();
                }

                // Refresh frog position after possible platform movement
                frogRow = frog.getRow();
                frogCol = frog.getCol();

                // Check road collisions (only if in road area)
                if frogRow >= ROAD_START {
                    if frogRow <= ROAD_END {
                        Integer hit = 0;
                        if v0.checkCollision(frogRow, frogCol) == 1 {
                            hit = 1;
                        }
                        if v1.checkCollision(frogRow, frogCol) == 1 {
                            hit = 1;
                        }
                        if v2.checkCollision(frogRow, frogCol) == 1 {
                            hit = 1;
                        }
                        if v3.checkCollision(frogRow, frogCol) == 1 {
                            hit = 1;
                        }
                        if v4.checkCollision(frogRow, frogCol) == 1 {
                            hit = 1;
                        }
                        if v5.checkCollision(frogRow, frogCol) == 1 {
                            hit = 1;
                        }
                        if v6.checkCollision(frogRow, frogCol) == 1 {
                            hit = 1;
                        }
                        if v7.checkCollision(frogRow, frogCol) == 1 {
                            hit = 1;
                        }
                        if v8.checkCollision(frogRow, frogCol) == 1 {
                            hit = 1;
                        }
                        if v9.checkCollision(frogRow, frogCol) == 1 {
                            hit = 1;
                        }

                        if hit == 1 {
                            frog.die();
                            if frog.isAlive() == 1 {
                                Viper.Terminal.SetPosition(12, 28);
                                Viper.Terminal.SetColor(1, 0);
                                Viper.Terminal.Print("SPLAT!");
                                Viper.Time.SleepMs(800);
                            }
                        }
                    }
                }

                // Check if frog reached home row
                if frogRow == HOME_ROW {
                    Integer foundHome = 0;
                    foundHome = checkHome(frog, h0, foundHome);
                    foundHome = checkHome(frog, h1, foundHome);
                    foundHome = checkHome(frog, h2, foundHome);
                    foundHome = checkHome(frog, h3, foundHome);
                    foundHome = checkHome(frog, h4, foundHome);

                    if foundHome == 0 {
                        frog.die();
                        if frog.isAlive() == 1 {
                            Viper.Terminal.SetPosition(12, 25);
                            Viper.Terminal.SetColor(1, 0);
                            Viper.Terminal.Print("MISSED HOME!");
                            Viper.Time.SleepMs(800);
                        }
                    }
                }

                // Check bounds
                if frogCol < 1 {
                    frog.die();
                }
                if frogCol > GAME_WIDTH {
                    frog.die();
                }
            } else {
                // Handle pause
                Viper.Terminal.SetPosition(12, 25);
                Viper.Terminal.SetColor(3, 0);
                Viper.Terminal.Print("*** PAUSED ***");
                Viper.Terminal.SetPosition(13, 20);
                Viper.Terminal.SetColor(7, 0);
                Viper.Terminal.Print("Press P to resume");

                String pkey = Viper.Terminal.GetKeyTimeout(100);
                if pkey == "p" {
                    gamePaused = 0;
                }
                if pkey == "P" {
                    gamePaused = 0;
                }
            }
            Viper.Time.SleepMs(80);
        }
    }

    // Game over
    Viper.Terminal.Clear();
    Viper.Terminal.SetCursorVisible(1);

    Viper.Terminal.SetPosition(5, 20);
    Viper.Terminal.SetColor(6, 0);
    Viper.Terminal.Print("========================================");

    if homesFilledCount >= 5 {
        Viper.Terminal.SetPosition(7, 20);
        Viper.Terminal.SetColor(2, 0);
        Viper.Terminal.Print("   CONGRATULATIONS! YOU WIN!");
        Viper.Terminal.SetPosition(9, 20);
        Viper.Terminal.SetColor(7, 0);
        Viper.Terminal.Print("   You successfully filled all 5 homes!");
    } else {
        if frog.isAlive() == 0 {
            Viper.Terminal.SetPosition(7, 20);
            Viper.Terminal.SetColor(1, 0);
            Viper.Terminal.Print("         G A M E   O V E R");
        } else {
            Viper.Terminal.SetPosition(7, 20);
            Viper.Terminal.SetColor(7, 0);
            Viper.Terminal.Print("         Thanks for playing!");
        }
    }

    Viper.Terminal.SetPosition(11, 20);
    Viper.Terminal.SetColor(6, 0);
    Viper.Terminal.Print("========================================");

    Viper.Terminal.SetPosition(13, 25);
    Viper.Terminal.SetColor(7, 0);
    Viper.Terminal.Print("Final Score: ");
    Viper.Terminal.PrintInt(score);

    Viper.Terminal.SetPosition(14, 25);
    Viper.Terminal.Print("Homes Filled: ");
    Viper.Terminal.PrintInt(homesFilledCount);
    Viper.Terminal.Print(" / 5");

    // Add high score
    if score > 0 {
        addHighScore(score);
    }

    Viper.Terminal.SetPosition(18, 20);
    Viper.Terminal.Print("Press any key to return to menu...");
    Viper.Terminal.GetKey();
}

func drawHome(home: Home) {
    Integer homeCol = home.getCol();
    Viper.Terminal.SetPosition(HOME_ROW, homeCol - 1);
    if home.isFilled() == 1 {
        Viper.Terminal.SetColor(2, 0);
        Viper.Terminal.Print("[F]");
    } else {
        Viper.Terminal.SetColor(7, 0);
        Viper.Terminal.Print("[ ]");
    }
}

func drawPlatform(plat: Platform) {
    Integer platRow = plat.getRow();
    Integer platCol = plat.getCol();
    Integer platWidth = plat.getWidth();
    Integer j = 0;
    while j < platWidth {
        Integer drawCol = platCol + j;
        if drawCol >= 1 {
            if drawCol <= GAME_WIDTH {
                Viper.Terminal.SetPosition(platRow, drawCol);
                if plat.getIsTurtle() == 1 {
                    Viper.Terminal.SetColor(2, 0);
                    Viper.Terminal.Print("O");
                } else {
                    Viper.Terminal.SetColor(3, 0);
                    Viper.Terminal.Print("=");
                }
            }
        }
        j = j + 1;
    }
}

func drawVehicle(veh: Vehicle) {
    Integer vehRow = veh.getRow();
    Integer vehCol = veh.getCol();
    Integer vehWidth = veh.getWidth();
    Integer j = 0;
    while j < vehWidth {
        Integer drawCol = vehCol + j;
        if drawCol >= 1 {
            if drawCol <= GAME_WIDTH {
                Viper.Terminal.SetPosition(vehRow, drawCol);
                Viper.Terminal.SetColor(1, 0);
                if veh.getIsTruck() == 1 {
                    Viper.Terminal.Print("#");
                } else {
                    Viper.Terminal.Print("=");
                }
            }
        }
        j = j + 1;
    }
}

func checkPlatform(fr: Frog, plat: Platform, onPlat: Integer) -> Integer {
    if onPlat == 1 {
        return 1;
    }
    Integer frogRow = fr.getRow();
    Integer frogCol = fr.getCol();
    if plat.checkOnPlatform(frogRow, frogCol) == 1 {
        Integer platSpeed = plat.getSpeed() * plat.getDirection();
        fr.setOnPlatform(platSpeed);
        fr.updateOnPlatform();
        return 1;
    }
    return 0;
}

func checkHome(fr: Frog, home: Home, foundHome: Integer) -> Integer {
    if foundHome == 1 {
        return 1;
    }
    Integer frogCol = fr.getCol();
    Integer homeCol = home.getCol();

    if frogCol >= homeCol - 1 {
        if frogCol <= homeCol + 1 {
            if home.isFilled() == 0 {
                home.fill();
                homesFilledCount = homesFilledCount + 1;
                score = score + 200;

                if homesFilledCount >= 5 {
                    gameRunning = 0;
                } else {
                    fr.reset();
                    Viper.Terminal.SetPosition(12, 25);
                    Viper.Terminal.SetColor(2, 0);
                    Viper.Terminal.Print("HOME SAFE! +200");
                    Viper.Time.SleepMs(500);
                }
                return 1;
            } else {
                // Home already filled
                fr.die();
                if fr.isAlive() == 1 {
                    Viper.Terminal.SetPosition(12, 22);
                    Viper.Terminal.SetColor(1, 0);
                    Viper.Terminal.Print("HOME OCCUPIED!");
                    Viper.Time.SleepMs(800);
                }
            }
        }
    }
    return foundHome;
}

func showHighScores() {
    Viper.Terminal.Clear();
    Viper.Terminal.SetCursorVisible(0);

    Viper.Terminal.SetPosition(3, 25);
    Viper.Terminal.SetColor(6, 0);
    Viper.Terminal.Print("========================================");
    Viper.Terminal.SetPosition(4, 25);
    Viper.Terminal.Print("            HIGH SCORES");
    Viper.Terminal.SetPosition(5, 25);
    Viper.Terminal.Print("========================================");

    Viper.Terminal.SetColor(7, 0);

    Viper.Terminal.SetPosition(8, 30);
    Viper.Terminal.Print("1. ");
    if hs0 > 0 {
        Viper.Terminal.PrintInt(hs0);
    } else {
        Viper.Terminal.Print("----");
    }

    Viper.Terminal.SetPosition(10, 30);
    Viper.Terminal.Print("2. ");
    if hs1 > 0 {
        Viper.Terminal.PrintInt(hs1);
    } else {
        Viper.Terminal.Print("----");
    }

    Viper.Terminal.SetPosition(12, 30);
    Viper.Terminal.Print("3. ");
    if hs2 > 0 {
        Viper.Terminal.PrintInt(hs2);
    } else {
        Viper.Terminal.Print("----");
    }

    Viper.Terminal.SetPosition(14, 30);
    Viper.Terminal.Print("4. ");
    if hs3 > 0 {
        Viper.Terminal.PrintInt(hs3);
    } else {
        Viper.Terminal.Print("----");
    }

    Viper.Terminal.SetPosition(16, 30);
    Viper.Terminal.Print("5. ");
    if hs4 > 0 {
        Viper.Terminal.PrintInt(hs4);
    } else {
        Viper.Terminal.Print("----");
    }

    Viper.Terminal.SetPosition(20, 20);
    Viper.Terminal.Print("Press any key to return to menu...");
    Viper.Terminal.GetKey();
}

func addHighScore(newScore: Integer) {
    // Simple insertion sort for top 5 scores
    if newScore > hs0 {
        hs4 = hs3;
        hs3 = hs2;
        hs2 = hs1;
        hs1 = hs0;
        hs0 = newScore;
    } else {
        if newScore > hs1 {
            hs4 = hs3;
            hs3 = hs2;
            hs2 = hs1;
            hs1 = newScore;
        } else {
            if newScore > hs2 {
                hs4 = hs3;
                hs3 = hs2;
                hs2 = newScore;
            } else {
                if newScore > hs3 {
                    hs4 = hs3;
                    hs3 = newScore;
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

    Viper.Terminal.SetPosition(2, 20);
    Viper.Terminal.SetColor(6, 0);
    Viper.Terminal.Print("========================================");
    Viper.Terminal.SetPosition(3, 20);
    Viper.Terminal.Print("            HOW TO PLAY");
    Viper.Terminal.SetPosition(4, 20);
    Viper.Terminal.Print("========================================");

    Viper.Terminal.SetColor(7, 0);

    Viper.Terminal.SetPosition(6, 5);
    Viper.Terminal.Print("OBJECTIVE:");
    Viper.Terminal.SetPosition(7, 5);
    Viper.Terminal.Print("Guide your frog safely across the road and river");
    Viper.Terminal.SetPosition(8, 5);
    Viper.Terminal.Print("to fill all 5 homes at the top of the screen.");

    Viper.Terminal.SetPosition(10, 5);
    Viper.Terminal.SetColor(3, 0);
    Viper.Terminal.Print("CONTROLS:");
    Viper.Terminal.SetColor(7, 0);
    Viper.Terminal.SetPosition(11, 7);
    Viper.Terminal.Print("W - Move Up");
    Viper.Terminal.SetPosition(12, 7);
    Viper.Terminal.Print("S - Move Down");
    Viper.Terminal.SetPosition(13, 7);
    Viper.Terminal.Print("A - Move Left");
    Viper.Terminal.SetPosition(14, 7);
    Viper.Terminal.Print("D - Move Right");
    Viper.Terminal.SetPosition(15, 7);
    Viper.Terminal.Print("P - Pause Game");
    Viper.Terminal.SetPosition(16, 7);
    Viper.Terminal.Print("Q - Quit to Menu");

    Viper.Terminal.SetPosition(18, 5);
    Viper.Terminal.SetColor(2, 0);
    Viper.Terminal.Print("GAMEPLAY:");
    Viper.Terminal.SetColor(7, 0);
    Viper.Terminal.SetPosition(19, 7);
    Viper.Terminal.Print("* Avoid cars (=) and trucks (#) on the road");
    Viper.Terminal.SetPosition(20, 7);
    Viper.Terminal.Print("* Jump on logs (=) and turtles (O) in the river");
    Viper.Terminal.SetPosition(21, 7);
    Viper.Terminal.Print("* You drown if you fall in the water!");
    Viper.Terminal.SetPosition(22, 7);
    Viper.Terminal.Print("* Land in home slots [ ] at the top: +200 points");
    Viper.Terminal.SetPosition(23, 7);
    Viper.Terminal.Print("* You have 3 lives - use them wisely!");

    Viper.Terminal.SetPosition(25, 20);
    Viper.Terminal.Print("Press any key to return to menu...");
    Viper.Terminal.GetKey();
}
