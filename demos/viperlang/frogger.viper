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
Integer gameRunning = 1;
Integer homesFilledCount = 0;

// Game objects
Frog frog;
List vehicles;
List platforms;
List homes;

func initGame() {
    score = 0;
    gameRunning = 1;
    homesFilledCount = 0;

    // Create frog at start position
    frog = new Frog();
    frog.init(START_ROW, 35);

    // Create vehicles list
    vehicles = new List[Vehicle]();

    // Lane 1: Cars going right (row 12)
    Vehicle v0 = new Vehicle();
    v0.init(12, 5, 1, 1, 4);
    vehicles.add(v0);
    Vehicle v1 = new Vehicle();
    v1.init(12, 35, 1, 1, 4);
    vehicles.add(v1);

    // Lane 2: Trucks going left (row 13)
    Vehicle v2 = new Vehicle();
    v2.init(13, 15, 1, 0 - 1, 6);
    vehicles.add(v2);
    Vehicle v3 = new Vehicle();
    v3.init(13, 50, 1, 0 - 1, 6);
    vehicles.add(v3);

    // Lane 3: Fast cars going right (row 14)
    Vehicle v4 = new Vehicle();
    v4.init(14, 10, 2, 1, 4);
    vehicles.add(v4);
    Vehicle v5 = new Vehicle();
    v5.init(14, 45, 2, 1, 4);
    vehicles.add(v5);

    // Lane 4: Trucks going left (row 15)
    Vehicle v6 = new Vehicle();
    v6.init(15, 20, 1, 0 - 1, 6);
    vehicles.add(v6);

    // Lane 5: Fast cars going right (row 16)
    Vehicle v7 = new Vehicle();
    v7.init(16, 8, 2, 1, 4);
    vehicles.add(v7);
    Vehicle v8 = new Vehicle();
    v8.init(16, 40, 2, 1, 4);
    vehicles.add(v8);

    // Create platforms list
    platforms = new List[Platform]();

    // Row 4: Logs going right
    Platform p0 = new Platform();
    p0.init(4, 5, 1, 1, 8, 0);
    platforms.add(p0);
    Platform p1 = new Platform();
    p1.init(4, 40, 1, 1, 8, 0);
    platforms.add(p1);

    // Row 5: Turtles going left
    Platform p2 = new Platform();
    p2.init(5, 15, 1, 0 - 1, 5, 1);
    platforms.add(p2);
    Platform p3 = new Platform();
    p3.init(5, 50, 1, 0 - 1, 5, 1);
    platforms.add(p3);

    // Row 6: Long logs going right
    Platform p4 = new Platform();
    p4.init(6, 10, 1, 1, 12, 0);
    platforms.add(p4);
    Platform p5 = new Platform();
    p5.init(6, 50, 1, 1, 10, 0);
    platforms.add(p5);

    // Row 7: Turtles going left
    Platform p6 = new Platform();
    p6.init(7, 20, 1, 0 - 1, 4, 1);
    platforms.add(p6);
    Platform p7 = new Platform();
    p7.init(7, 55, 1, 0 - 1, 4, 1);
    platforms.add(p7);

    // Row 8: Logs going right
    Platform p8 = new Platform();
    p8.init(8, 12, 2, 1, 7, 0);
    platforms.add(p8);
    Platform p9 = new Platform();
    p9.init(8, 48, 2, 1, 7, 0);
    platforms.add(p9);

    // Create homes list
    homes = new List[Home]();
    Home h0 = new Home();
    h0.init(8);
    homes.add(h0);
    Home h1 = new Home();
    h1.init(20);
    homes.add(h1);
    Home h2 = new Home();
    h2.init(32);
    homes.add(h2);
    Home h3 = new Home();
    h3.init(44);
    homes.add(h3);
    Home h4 = new Home();
    h4.init(56);
    homes.add(h4);
}

func drawBoard() {
    Viper.Terminal.BeginBatch();

    // Draw title
    Viper.Terminal.SetPosition(1, 25);
    Viper.Terminal.SetColor(6);
    Viper.Terminal.Print("*** FROGGER ***");

    // Draw lives
    Viper.Terminal.SetPosition(1, 2);
    Viper.Terminal.SetColor(7);
    Viper.Terminal.Print("Lives: ");
    Viper.Terminal.SayInt(frog.getLives());

    // Draw score
    Viper.Terminal.SetPosition(1, 55);
    Viper.Terminal.SetColor(3);
    Viper.Terminal.Print("Score: ");
    Viper.Terminal.SayInt(score);

    // Draw homes row (blue water)
    Integer i = 1;
    while i <= GAME_WIDTH {
        Viper.Terminal.SetPosition(HOME_ROW, i);
        Viper.Terminal.SetColor(4);
        Viper.Terminal.Print("~");
        i = i + 1;
    }

    // Draw each home
    Integer hi = 0;
    while hi < 5 {
        Home home = homes.get(hi);
        Integer homeCol = home.getCol();
        Viper.Terminal.SetPosition(HOME_ROW, homeCol - 1);
        if home.isFilled() == 1 {
            Viper.Terminal.SetColor(2);
            Viper.Terminal.Print("[F]");
        } else {
            Viper.Terminal.SetColor(7);
            Viper.Terminal.Print("[ ]");
        }
        hi = hi + 1;
    }

    // Draw river section (blue water)
    Integer ri = RIVER_START;
    while ri <= RIVER_END {
        Integer rj = 1;
        while rj <= GAME_WIDTH {
            Viper.Terminal.SetPosition(ri, rj);
            Viper.Terminal.SetColor(4);
            Viper.Terminal.Print("~");
            rj = rj + 1;
        }
        ri = ri + 1;
    }

    // Draw platforms (logs and turtles)
    Integer pi = 0;
    while pi < 10 {
        Platform plat = platforms.get(pi);
        Integer platRow = plat.getRow();
        Integer platCol = plat.getCol();
        Integer platWidth = plat.getWidth();
        Integer pj = 0;
        while pj < platWidth {
            Integer drawCol = platCol + pj;
            if drawCol >= 1 {
                if drawCol <= GAME_WIDTH {
                    Viper.Terminal.SetPosition(platRow, drawCol);
                    if plat.getIsTurtle() == 1 {
                        Viper.Terminal.SetColor(2);
                        Viper.Terminal.Print("O");
                    } else {
                        Viper.Terminal.SetColor(3);
                        Viper.Terminal.Print("=");
                    }
                }
            }
            pj = pj + 1;
        }
        pi = pi + 1;
    }

    // Draw median (safe zone)
    Integer mi = 1;
    while mi <= GAME_WIDTH {
        Viper.Terminal.SetPosition(MEDIAN_ROW, mi);
        Viper.Terminal.SetColor(2);
        Viper.Terminal.Print("-");
        mi = mi + 1;
    }
    Viper.Terminal.SetPosition(MEDIAN_ROW, 28);
    Viper.Terminal.Print("SAFE ZONE");

    // Draw road section
    Integer roi = ROAD_START;
    while roi <= ROAD_END {
        Integer roj = 1;
        while roj <= GAME_WIDTH {
            Viper.Terminal.SetPosition(roi, roj);
            Viper.Terminal.SetColor(7);
            Viper.Terminal.Print(".");
            roj = roj + 1;
        }
        roi = roi + 1;
    }

    // Draw vehicles
    Integer vi = 0;
    while vi < 9 {
        Vehicle veh = vehicles.get(vi);
        Integer vehRow = veh.getRow();
        Integer vehCol = veh.getCol();
        Integer vehWidth = veh.getWidth();
        Integer vj = 0;
        while vj < vehWidth {
            Integer drawCol = vehCol + vj;
            if drawCol >= 1 {
                if drawCol <= GAME_WIDTH {
                    Viper.Terminal.SetPosition(vehRow, drawCol);
                    Viper.Terminal.SetColor(1);
                    if vehWidth > 4 {
                        Viper.Terminal.Print("#");
                    } else {
                        Viper.Terminal.Print("=");
                    }
                }
            }
            vj = vj + 1;
        }
        vi = vi + 1;
    }

    // Draw start area
    Integer si = 1;
    while si <= GAME_WIDTH {
        Viper.Terminal.SetPosition(START_ROW, si);
        Viper.Terminal.SetColor(2);
        Viper.Terminal.Print("-");
        si = si + 1;
    }
    Viper.Terminal.SetPosition(START_ROW, 30);
    Viper.Terminal.Print("START");

    // Draw frog
    Viper.Terminal.SetPosition(frog.getRow(), frog.getCol());
    Viper.Terminal.SetColor(10);
    Viper.Terminal.Print("@");

    // Draw instructions
    Viper.Terminal.SetPosition(20, 1);
    Viper.Terminal.SetColor(7);
    Viper.Terminal.Print("WASD=Move  Q=Quit  Goal: Fill all 5 homes!");

    Viper.Terminal.EndBatch();
}

func isInRiver(row: Integer) -> Integer {
    if row >= RIVER_START {
        if row <= RIVER_END {
            return 1;
        }
    }
    return 0;
}

func updateGame() {
    Integer frogRow = frog.getRow();
    Integer frogCol = frog.getCol();

    // Move all vehicles
    Integer vi = 0;
    while vi < 9 {
        Vehicle veh = vehicles.get(vi);
        veh.move();
        vi = vi + 1;
    }

    // Move all platforms
    Integer pi = 0;
    while pi < 10 {
        Platform plat = platforms.get(pi);
        plat.move();
        pi = pi + 1;
    }

    // Check if frog is in river
    if isInRiver(frogRow) == 1 {
        Integer onPlatformFlag = 0;

        // Check each platform
        Integer pci = 0;
        while pci < 10 {
            Platform plat = platforms.get(pci);
            if plat.checkOnPlatform(frogRow, frogCol) == 1 {
                onPlatformFlag = 1;
                Integer platSpeed = plat.getSpeed() * plat.getDirection();
                frog.setOnPlatform(platSpeed);
                frog.updateOnPlatform();
            }
            pci = pci + 1;
        }

        // If not on platform, frog drowns
        if onPlatformFlag == 0 {
            frog.die();
            frog.clearPlatform();
            if frog.isAlive() == 1 {
                Viper.Terminal.SetPosition(12, 28);
                Viper.Terminal.SetColor(1);
                Viper.Terminal.Say("SPLASH!");
                Viper.Time.SleepMs(800);
            }
            return;
        }
    } else {
        frog.clearPlatform();
    }

    // Check road collisions
    if frogRow >= ROAD_START {
        if frogRow <= ROAD_END {
            Integer vci = 0;
            while vci < 9 {
                Vehicle veh = vehicles.get(vci);
                if veh.checkCollision(frogRow, frogCol) == 1 {
                    frog.die();
                    if frog.isAlive() == 1 {
                        Viper.Terminal.SetPosition(12, 28);
                        Viper.Terminal.SetColor(1);
                        Viper.Terminal.Say("SPLAT!");
                        Viper.Time.SleepMs(800);
                    }
                    return;
                }
                vci = vci + 1;
            }
        }
    }

    // Check if frog reached a home
    if frogRow == HOME_ROW {
        Integer foundHome = 0;

        Integer hci = 0;
        while hci < 5 {
            Home home = homes.get(hci);
            Integer homeCol = home.getCol();
            if frogCol >= homeCol - 1 {
                if frogCol <= homeCol + 1 {
                    if home.isFilled() == 0 {
                        home.fill();
                        homesFilledCount = homesFilledCount + 1;
                        score = score + 200;
                        foundHome = 1;

                        if homesFilledCount == 5 {
                            gameRunning = 0;
                        } else {
                            frog.reset();
                            Viper.Terminal.SetPosition(12, 25);
                            Viper.Terminal.SetColor(2);
                            Viper.Terminal.Say("HOME SAFE! +200");
                            Viper.Time.SleepMs(500);
                        }
                    } else {
                        frog.die();
                    }
                }
            }
            hci = hci + 1;
        }

        if foundHome == 0 {
            frog.die();
        }
    }

    // Check if frog went off screen
    if frogCol < 1 {
        frog.die();
    }
    if frogCol > GAME_WIDTH {
        frog.die();
    }
}

func handleInput() {
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
    if key == "q" {
        gameRunning = 0;
    }
    if key == "Q" {
        gameRunning = 0;
    }
}

func gameLoop() {
    Viper.Terminal.Clear();
    Viper.Terminal.SetCursorVisible(0);

    while gameRunning == 1 {
        if frog.isAlive() == 0 {
            gameRunning = 0;
        } else {
            drawBoard();
            handleInput();
            updateGame();
            Viper.Time.SleepMs(50);
        }
    }

    // Game over
    Viper.Terminal.Clear();
    Viper.Terminal.SetCursorVisible(1);
    Viper.Terminal.SetColor(7);

    if homesFilledCount == 5 {
        Viper.Terminal.Say("CONGRATULATIONS! YOU WIN!");
        Viper.Terminal.Say("You filled all 5 homes!");
    } else {
        if frog.isAlive() == 0 {
            Viper.Terminal.Say("GAME OVER");
        } else {
            Viper.Terminal.Say("Thanks for playing!");
        }
    }

    Viper.Terminal.Print("Final Score: ");
    Viper.Terminal.SayInt(score);
    Viper.Terminal.Print("Homes Filled: ");
    Viper.Terminal.PrintInt(homesFilledCount);
    Viper.Terminal.Say(" / 5");
}

func start() {
    Viper.Terminal.Say("FROGGER - ViperLang Demo");
    Viper.Terminal.Say("Press any key to start...");
    Viper.Terminal.GetKey();

    initGame();
    gameLoop();

    Viper.Terminal.Say("Press any key to exit...");
    Viper.Terminal.GetKey();
}
