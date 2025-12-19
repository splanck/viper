module Frogger;

import "./entities";

// Game constants
var GAME_WIDTH = 70;
var GAME_HEIGHT = 24;
var HOME_ROW = 2;
var RIVER_START = 4;
var RIVER_END = 8;
var MEDIAN_ROW = 10;
var ROAD_START = 12;
var ROAD_END = 16;
var START_ROW = 18;

// Game state
var score = 0;
var gameRunning = 1;
var homesFilledCount = 0;

// Game objects
var frog: Frog;
var vehicles: List[Vehicle];
var platforms: List[Platform];
var homes: List[Home];

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
    var v0 = new Vehicle();
    v0.init(12, 5, 1, 1, 4);
    vehicles.add(v0);
    var v1 = new Vehicle();
    v1.init(12, 35, 1, 1, 4);
    vehicles.add(v1);

    // Lane 2: Trucks going left (row 13)
    var v2 = new Vehicle();
    v2.init(13, 15, 1, 0 - 1, 6);
    vehicles.add(v2);
    var v3 = new Vehicle();
    v3.init(13, 50, 1, 0 - 1, 6);
    vehicles.add(v3);

    // Lane 3: Fast cars going right (row 14)
    var v4 = new Vehicle();
    v4.init(14, 10, 2, 1, 4);
    vehicles.add(v4);
    var v5 = new Vehicle();
    v5.init(14, 45, 2, 1, 4);
    vehicles.add(v5);

    // Lane 4: Trucks going left (row 15)
    var v6 = new Vehicle();
    v6.init(15, 20, 1, 0 - 1, 6);
    vehicles.add(v6);

    // Lane 5: Fast cars going right (row 16)
    var v7 = new Vehicle();
    v7.init(16, 8, 2, 1, 4);
    vehicles.add(v7);
    var v8 = new Vehicle();
    v8.init(16, 40, 2, 1, 4);
    vehicles.add(v8);

    // Create platforms list
    platforms = new List[Platform]();

    // Row 4: Logs going right
    var p0 = new Platform();
    p0.init(4, 5, 1, 1, 8, 0);
    platforms.add(p0);
    var p1 = new Platform();
    p1.init(4, 40, 1, 1, 8, 0);
    platforms.add(p1);

    // Row 5: Turtles going left
    var p2 = new Platform();
    p2.init(5, 15, 1, 0 - 1, 5, 1);
    platforms.add(p2);
    var p3 = new Platform();
    p3.init(5, 50, 1, 0 - 1, 5, 1);
    platforms.add(p3);

    // Row 6: Long logs going right
    var p4 = new Platform();
    p4.init(6, 10, 1, 1, 12, 0);
    platforms.add(p4);
    var p5 = new Platform();
    p5.init(6, 50, 1, 1, 10, 0);
    platforms.add(p5);

    // Row 7: Turtles going left
    var p6 = new Platform();
    p6.init(7, 20, 1, 0 - 1, 4, 1);
    platforms.add(p6);
    var p7 = new Platform();
    p7.init(7, 55, 1, 0 - 1, 4, 1);
    platforms.add(p7);

    // Row 8: Logs going right
    var p8 = new Platform();
    p8.init(8, 12, 2, 1, 7, 0);
    platforms.add(p8);
    var p9 = new Platform();
    p9.init(8, 48, 2, 1, 7, 0);
    platforms.add(p9);

    // Create homes list
    homes = new List[Home]();
    var h0 = new Home();
    h0.init(8);
    homes.add(h0);
    var h1 = new Home();
    h1.init(20);
    homes.add(h1);
    var h2 = new Home();
    h2.init(32);
    homes.add(h2);
    var h3 = new Home();
    h3.init(44);
    homes.add(h3);
    var h4 = new Home();
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
    var i = 1;
    while i <= GAME_WIDTH {
        Viper.Terminal.SetPosition(HOME_ROW, i);
        Viper.Terminal.SetColor(4);
        Viper.Terminal.Print("~");
        i = i + 1;
    }

    // Draw each home
    var hi = 0;
    while hi < 5 {
        var home = homes.get(hi);
        var homeCol = home.getCol();
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
    var ri = RIVER_START;
    while ri <= RIVER_END {
        var rj = 1;
        while rj <= GAME_WIDTH {
            Viper.Terminal.SetPosition(ri, rj);
            Viper.Terminal.SetColor(4);
            Viper.Terminal.Print("~");
            rj = rj + 1;
        }
        ri = ri + 1;
    }

    // Draw platforms (logs and turtles)
    var pi = 0;
    while pi < 10 {
        var plat = platforms.get(pi);
        var platRow = plat.getRow();
        var platCol = plat.getCol();
        var platWidth = plat.getWidth();
        var pj = 0;
        while pj < platWidth {
            var drawCol = platCol + pj;
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
    var mi = 1;
    while mi <= GAME_WIDTH {
        Viper.Terminal.SetPosition(MEDIAN_ROW, mi);
        Viper.Terminal.SetColor(2);
        Viper.Terminal.Print("-");
        mi = mi + 1;
    }
    Viper.Terminal.SetPosition(MEDIAN_ROW, 28);
    Viper.Terminal.Print("SAFE ZONE");

    // Draw road section
    var roi = ROAD_START;
    while roi <= ROAD_END {
        var roj = 1;
        while roj <= GAME_WIDTH {
            Viper.Terminal.SetPosition(roi, roj);
            Viper.Terminal.SetColor(7);
            Viper.Terminal.Print(".");
            roj = roj + 1;
        }
        roi = roi + 1;
    }

    // Draw vehicles
    var vi = 0;
    while vi < 9 {
        var veh = vehicles.get(vi);
        var vehRow = veh.getRow();
        var vehCol = veh.getCol();
        var vehWidth = veh.getWidth();
        var vj = 0;
        while vj < vehWidth {
            var drawCol = vehCol + vj;
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
    var si = 1;
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
    var frogRow = frog.getRow();
    var frogCol = frog.getCol();

    // Move all vehicles
    var vi = 0;
    while vi < 9 {
        var veh = vehicles.get(vi);
        veh.move();
        vi = vi + 1;
    }

    // Move all platforms
    var pi = 0;
    while pi < 10 {
        var plat = platforms.get(pi);
        plat.move();
        pi = pi + 1;
    }

    // Check if frog is in river
    if isInRiver(frogRow) == 1 {
        var onPlatformFlag = 0;

        // Check each platform
        var pci = 0;
        while pci < 10 {
            var plat = platforms.get(pci);
            if plat.checkOnPlatform(frogRow, frogCol) == 1 {
                onPlatformFlag = 1;
                var platSpeed = plat.getSpeed() * plat.getDirection();
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
            var vci = 0;
            while vci < 9 {
                var veh = vehicles.get(vci);
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
        var foundHome = 0;

        var hci = 0;
        while hci < 5 {
            var home = homes.get(hci);
            var homeCol = home.getCol();
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
    var key = Viper.Terminal.GetKeyTimeout(50);
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
