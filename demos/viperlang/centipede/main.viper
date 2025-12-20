module Centipede;

import "./entities";

// Field dimensions
Integer FIELD_WIDTH = 30;
Integer FIELD_HEIGHT = 20;
Integer FIELD_TOP = 3;
Integer FIELD_LEFT = 25;
Integer PLAYER_ZONE = 4;
Integer MAX_SEGMENTS = 12;
Integer SEGMENT_POINTS = 10;
Integer SPIDER_POINTS = 50;

// High scores (in-memory)
Integer score1 = 10000;
Integer score2 = 8000;
Integer score3 = 6000;
Integer score4 = 4000;
Integer score5 = 2000;
String name1 = "ACE";
String name2 = "PRO";
String name3 = "VET";
String name4 = "HOT";
String name5 = "NEW";

// Mushroom grid - 20 rows, each integer holds 30 bits for columns
// Using simplified 1-hit mushrooms (bit = mushroom present)
Integer mushRow0 = 0;
Integer mushRow1 = 0;
Integer mushRow2 = 0;
Integer mushRow3 = 0;
Integer mushRow4 = 0;
Integer mushRow5 = 0;
Integer mushRow6 = 0;
Integer mushRow7 = 0;
Integer mushRow8 = 0;
Integer mushRow9 = 0;
Integer mushRow10 = 0;
Integer mushRow11 = 0;
Integer mushRow12 = 0;
Integer mushRow13 = 0;
Integer mushRow14 = 0;
Integer mushRow15 = 0;
Integer mushRow16 = 0;
Integer mushRow17 = 0;
Integer mushRow18 = 0;
Integer mushRow19 = 0;

func getMushRow(r: Integer) -> Integer {
    if r == 0 { return mushRow0; }
    if r == 1 { return mushRow1; }
    if r == 2 { return mushRow2; }
    if r == 3 { return mushRow3; }
    if r == 4 { return mushRow4; }
    if r == 5 { return mushRow5; }
    if r == 6 { return mushRow6; }
    if r == 7 { return mushRow7; }
    if r == 8 { return mushRow8; }
    if r == 9 { return mushRow9; }
    if r == 10 { return mushRow10; }
    if r == 11 { return mushRow11; }
    if r == 12 { return mushRow12; }
    if r == 13 { return mushRow13; }
    if r == 14 { return mushRow14; }
    if r == 15 { return mushRow15; }
    if r == 16 { return mushRow16; }
    if r == 17 { return mushRow17; }
    if r == 18 { return mushRow18; }
    if r == 19 { return mushRow19; }
    return 0;
}

func setMushRow(r: Integer, v: Integer) {
    if r == 0 { mushRow0 = v; }
    if r == 1 { mushRow1 = v; }
    if r == 2 { mushRow2 = v; }
    if r == 3 { mushRow3 = v; }
    if r == 4 { mushRow4 = v; }
    if r == 5 { mushRow5 = v; }
    if r == 6 { mushRow6 = v; }
    if r == 7 { mushRow7 = v; }
    if r == 8 { mushRow8 = v; }
    if r == 9 { mushRow9 = v; }
    if r == 10 { mushRow10 = v; }
    if r == 11 { mushRow11 = v; }
    if r == 12 { mushRow12 = v; }
    if r == 13 { mushRow13 = v; }
    if r == 14 { mushRow14 = v; }
    if r == 15 { mushRow15 = v; }
    if r == 16 { mushRow16 = v; }
    if r == 17 { mushRow17 = v; }
    if r == 18 { mushRow18 = v; }
    if r == 19 { mushRow19 = v; }
}

func pow2(n: Integer) -> Integer {
    Integer result = 1;
    Integer i = 0;
    while i < n {
        result = result * 2;
        i = i + 1;
    }
    return result;
}

func hasMushroom(r: Integer, c: Integer) -> Integer {
    if r < 0 { return 0; }
    if r >= FIELD_HEIGHT { return 0; }
    if c < 0 { return 0; }
    if c >= FIELD_WIDTH { return 0; }
    Integer rowVal = getMushRow(r);
    Integer mask = pow2(c);
    Integer check = rowVal / mask;
    Integer bit = check - (check / 2) * 2;
    return bit;
}

func setMushroom(r: Integer, c: Integer, val: Integer) {
    if r < 0 { return; }
    if r >= FIELD_HEIGHT { return; }
    if c < 0 { return; }
    if c >= FIELD_WIDTH { return; }
    Integer rowVal = getMushRow(r);
    Integer mask = pow2(c);
    Integer check = rowVal / mask;
    Integer bit = check - (check / 2) * 2;
    if val == 1 {
        if bit == 0 {
            rowVal = rowVal + mask;
        }
    }
    if val == 0 {
        if bit == 1 {
            rowVal = rowVal - mask;
        }
    }
    setMushRow(r, rowVal);
}

func clearAllMushrooms() {
    Integer i = 0;
    while i < FIELD_HEIGHT {
        setMushRow(i, 0);
        i = i + 1;
    }
}

func generateMushrooms(level: Integer) {
    clearAllMushrooms();
    Integer count = 15 + level * 3;
    if count > 40 {
        count = 40;
    }
    Integer placed = 0;
    Integer attempts = 0;
    Integer maxY = FIELD_HEIGHT - PLAYER_ZONE - 1;
    while placed < count {
        if attempts > 300 {
            return;
        }
        Integer x = Viper.Random.NextInt(FIELD_WIDTH);
        Integer y = Viper.Random.NextInt(maxY);
        if hasMushroom(y, x) == 0 {
            setMushroom(y, x, 1);
            placed = placed + 1;
        }
        attempts = attempts + 1;
    }
}

func drawMushroom(r: Integer, c: Integer) {
    Integer screenX = FIELD_LEFT + c;
    Integer screenY = FIELD_TOP + r;
    Viper.Terminal.SetPosition(screenY, screenX);
    if hasMushroom(r, c) == 1 {
        Viper.Terminal.SetColor(10, 0);
        Viper.Terminal.Print("@");
    } else {
        Viper.Terminal.SetColor(0, 0);
        Viper.Terminal.Print(" ");
    }
}

func drawField() {
    // Draw border
    Viper.Terminal.SetColor(8, 0);
    Integer y = FIELD_TOP - 1;
    while y <= FIELD_TOP + FIELD_HEIGHT {
        Viper.Terminal.SetPosition(y, FIELD_LEFT - 1);
        Viper.Terminal.Print("|");
        Viper.Terminal.SetPosition(y, FIELD_LEFT + FIELD_WIDTH);
        Viper.Terminal.Print("|");
        y = y + 1;
    }
    // Draw mushrooms
    Integer row = 0;
    while row < FIELD_HEIGHT {
        Integer col = 0;
        while col < FIELD_WIDTH {
            drawMushroom(row, col);
            col = col + 1;
        }
        row = row + 1;
    }
}

func clearPosition(x: Integer, y: Integer) {
    Integer screenX = FIELD_LEFT + x;
    Integer screenY = FIELD_TOP + y;
    if hasMushroom(y, x) == 1 {
        drawMushroom(y, x);
    } else {
        Viper.Terminal.SetPosition(screenY, screenX);
        Viper.Terminal.SetColor(0, 0);
        Viper.Terminal.Print(" ");
    }
}

// Show main menu
func showMainMenu() {
    Viper.Terminal.Clear();

    // Title box
    Viper.Terminal.SetColor(10, 0);
    Viper.Terminal.SetPosition(3, 20);
    Viper.Terminal.Print("+=================================+");
    Viper.Terminal.SetPosition(4, 20);
    Viper.Terminal.Print("|                                 |");
    Viper.Terminal.SetPosition(5, 20);
    Viper.Terminal.Print("|");
    Viper.Terminal.SetColor(11, 0);
    Viper.Terminal.Print("       C E N T I P E D E       ");
    Viper.Terminal.SetColor(10, 0);
    Viper.Terminal.Print("|");
    Viper.Terminal.SetPosition(6, 20);
    Viper.Terminal.Print("|                                 |");
    Viper.Terminal.SetPosition(7, 20);
    Viper.Terminal.Print("+=================================+");

    // Centipede ASCII art
    Viper.Terminal.SetColor(14, 0);
    Viper.Terminal.SetPosition(9, 27);
    Viper.Terminal.Print("<@@@@@@@@@@>");
    Viper.Terminal.SetColor(6, 0);
    Viper.Terminal.SetPosition(10, 27);
    Viper.Terminal.Print("/||||||||||/");

    // Menu options
    Viper.Terminal.SetPosition(13, 27);
    Viper.Terminal.SetColor(15, 0);
    Viper.Terminal.Print("[1] ");
    Viper.Terminal.SetColor(10, 0);
    Viper.Terminal.Print("NEW GAME");

    Viper.Terminal.SetPosition(15, 27);
    Viper.Terminal.SetColor(15, 0);
    Viper.Terminal.Print("[2] ");
    Viper.Terminal.SetColor(11, 0);
    Viper.Terminal.Print("INSTRUCTIONS");

    Viper.Terminal.SetPosition(17, 27);
    Viper.Terminal.SetColor(15, 0);
    Viper.Terminal.Print("[3] ");
    Viper.Terminal.SetColor(14, 0);
    Viper.Terminal.Print("HIGH SCORES");

    Viper.Terminal.SetPosition(19, 27);
    Viper.Terminal.SetColor(15, 0);
    Viper.Terminal.Print("[Q] ");
    Viper.Terminal.SetColor(12, 0);
    Viper.Terminal.Print("QUIT");

    // Footer
    Viper.Terminal.SetColor(8, 0);
    Viper.Terminal.SetPosition(22, 24);
    Viper.Terminal.Print("ViperLang Demo 2025");
    Viper.Terminal.SetColor(7, 0);
}

func showInstructions() {
    Viper.Terminal.Clear();

    // Title
    Viper.Terminal.SetColor(11, 0);
    Viper.Terminal.SetPosition(2, 25);
    Viper.Terminal.Print("=== INSTRUCTIONS ===");

    // Controls
    Viper.Terminal.SetColor(14, 0);
    Viper.Terminal.SetPosition(5, 15);
    Viper.Terminal.Print("CONTROLS:");
    Viper.Terminal.SetColor(7, 0);
    Viper.Terminal.SetPosition(7, 17);
    Viper.Terminal.Print("W / Up Arrow    - Move up");
    Viper.Terminal.SetPosition(8, 17);
    Viper.Terminal.Print("S / Down Arrow  - Move down");
    Viper.Terminal.SetPosition(9, 17);
    Viper.Terminal.Print("A / Left Arrow  - Move left");
    Viper.Terminal.SetPosition(10, 17);
    Viper.Terminal.Print("D / Right Arrow - Move right");
    Viper.Terminal.SetPosition(11, 17);
    Viper.Terminal.Print("SPACE           - Fire bullet");
    Viper.Terminal.SetPosition(12, 17);
    Viper.Terminal.Print("Q               - Quit to menu");

    // Objectives
    Viper.Terminal.SetColor(14, 0);
    Viper.Terminal.SetPosition(15, 15);
    Viper.Terminal.Print("OBJECTIVE:");
    Viper.Terminal.SetColor(7, 0);
    Viper.Terminal.SetPosition(17, 17);
    Viper.Terminal.Print("Destroy the centipede before it reaches you!");
    Viper.Terminal.SetPosition(18, 17);
    Viper.Terminal.Print("Shoot mushrooms to clear a path.");
    Viper.Terminal.SetPosition(19, 17);
    Viper.Terminal.Print("Watch out for the spider!");

    // Scoring
    Viper.Terminal.SetColor(14, 0);
    Viper.Terminal.SetPosition(22, 15);
    Viper.Terminal.Print("SCORING:");
    Viper.Terminal.SetColor(10, 0);
    Viper.Terminal.SetPosition(24, 17);
    Viper.Terminal.Print("Centipede Segment: 10 pts");
    Viper.Terminal.SetPosition(25, 17);
    Viper.Terminal.Print("Mushroom:           1 pt");
    Viper.Terminal.SetPosition(26, 17);
    Viper.Terminal.Print("Spider:            50 pts");

    // Return prompt
    Viper.Terminal.SetColor(12, 0);
    Viper.Terminal.SetPosition(29, 20);
    Viper.Terminal.Print("Press any key to return...");
    Viper.Terminal.SetColor(7, 0);

    Viper.Terminal.GetKey();
}

func showHighScores() {
    Viper.Terminal.Clear();

    // Header
    Viper.Terminal.SetColor(14, 0);
    Viper.Terminal.SetPosition(3, 25);
    Viper.Terminal.Print("=== HIGH SCORES ===");

    Viper.Terminal.SetColor(11, 0);
    Viper.Terminal.SetPosition(6, 20);
    Viper.Terminal.Print("RANK    NAME        SCORE");
    Viper.Terminal.SetPosition(7, 20);
    Viper.Terminal.Print("-------------------------");

    // Score 1
    Viper.Terminal.SetColor(10, 0);
    Viper.Terminal.SetPosition(9, 20);
    Viper.Terminal.Print(" 1.     ");
    Viper.Terminal.Print(name1);
    Viper.Terminal.Print("         ");
    Viper.Terminal.PrintInt(score1);

    // Score 2
    Viper.Terminal.SetPosition(10, 20);
    Viper.Terminal.Print(" 2.     ");
    Viper.Terminal.Print(name2);
    Viper.Terminal.Print("         ");
    Viper.Terminal.PrintInt(score2);

    // Score 3
    Viper.Terminal.SetPosition(11, 20);
    Viper.Terminal.Print(" 3.     ");
    Viper.Terminal.Print(name3);
    Viper.Terminal.Print("         ");
    Viper.Terminal.PrintInt(score3);

    // Score 4
    Viper.Terminal.SetColor(7, 0);
    Viper.Terminal.SetPosition(12, 20);
    Viper.Terminal.Print(" 4.     ");
    Viper.Terminal.Print(name4);
    Viper.Terminal.Print("         ");
    Viper.Terminal.PrintInt(score4);

    // Score 5
    Viper.Terminal.SetPosition(13, 20);
    Viper.Terminal.Print(" 5.     ");
    Viper.Terminal.Print(name5);
    Viper.Terminal.Print("         ");
    Viper.Terminal.PrintInt(score5);

    // Return prompt
    Viper.Terminal.SetColor(12, 0);
    Viper.Terminal.SetPosition(17, 20);
    Viper.Terminal.Print("Press any key to return...");
    Viper.Terminal.SetColor(7, 0);

    Viper.Terminal.GetKey();
}

func checkHighScore(playerScore: Integer) {
    if playerScore > score5 {
        // Shift scores down
        if playerScore > score1 {
            score5 = score4;
            name5 = name4;
            score4 = score3;
            name4 = name3;
            score3 = score2;
            name3 = name2;
            score2 = score1;
            name2 = name1;
            score1 = playerScore;
            name1 = "YOU";
        } else {
            if playerScore > score2 {
                score5 = score4;
                name5 = name4;
                score4 = score3;
                name4 = name3;
                score3 = score2;
                name3 = name2;
                score2 = playerScore;
                name2 = "YOU";
            } else {
                if playerScore > score3 {
                    score5 = score4;
                    name5 = name4;
                    score4 = score3;
                    name4 = name3;
                    score3 = playerScore;
                    name3 = "YOU";
                } else {
                    if playerScore > score4 {
                        score5 = score4;
                        name5 = name4;
                        score4 = playerScore;
                        name4 = "YOU";
                    } else {
                        score5 = playerScore;
                        name5 = "YOU";
                    }
                }
            }
        }
    }
}

func drawHUD(player: Player, level: Integer) {
    // Score
    Viper.Terminal.SetPosition(1, FIELD_LEFT);
    Viper.Terminal.SetColor(11, 0);
    Viper.Terminal.Print("SCORE: ");
    Viper.Terminal.SetColor(15, 0);
    Viper.Terminal.PrintInt(player.getScore());
    Viper.Terminal.Print("      ");

    // Lives
    Viper.Terminal.SetPosition(1, FIELD_LEFT + 20);
    Viper.Terminal.SetColor(12, 0);
    Viper.Terminal.Print("LIVES: ");
    Viper.Terminal.SetColor(15, 0);
    Integer i = 0;
    while i < player.getLives() {
        Viper.Terminal.Print("A ");
        i = i + 1;
    }
    Viper.Terminal.Print("   ");

    // Level
    Viper.Terminal.SetPosition(1, FIELD_LEFT + FIELD_WIDTH - 8);
    Viper.Terminal.SetColor(14, 0);
    Viper.Terminal.Print("LVL:");
    Viper.Terminal.SetColor(15, 0);
    Viper.Terminal.PrintInt(level);
}

func drawPlayer(player: Player) {
    Integer screenX = FIELD_LEFT + player.getX();
    Integer screenY = FIELD_TOP + player.getY();
    Viper.Terminal.SetPosition(screenY, screenX);
    Viper.Terminal.SetColor(15, 0);
    Viper.Terminal.Print("A");
}

func drawBullet(player: Player) {
    if player.hasBullet() == 1 {
        Integer screenX = FIELD_LEFT + player.getBulletX();
        Integer screenY = FIELD_TOP + player.getBulletY();
        Viper.Terminal.SetPosition(screenY, screenX);
        Viper.Terminal.SetColor(14, 0);
        Viper.Terminal.Print("|");
    }
}

func drawSegment(seg: Segment) {
    if seg.isActive() == 1 {
        Integer screenX = FIELD_LEFT + seg.getX();
        Integer screenY = FIELD_TOP + seg.getY();
        Viper.Terminal.SetPosition(screenY, screenX);
        if seg.isHeadSeg() == 1 {
            Viper.Terminal.SetColor(12, 0);
            Viper.Terminal.Print("O");
        } else {
            Viper.Terminal.SetColor(10, 0);
            Viper.Terminal.Print("o");
        }
    }
}

func drawSpider(spider: Spider) {
    if spider.isActive() == 1 {
        Integer screenX = FIELD_LEFT + spider.getX();
        Integer screenY = FIELD_TOP + spider.getY();
        Viper.Terminal.SetPosition(screenY, screenX);
        Viper.Terminal.SetColor(13, 0);
        Viper.Terminal.Print("X");
    }
}

func showGameOver(player: Player) {
    Viper.Terminal.SetPosition(10, FIELD_LEFT + 8);
    Viper.Terminal.SetColor(12, 0);
    Viper.Terminal.Print("*** GAME OVER ***");

    Viper.Terminal.SetPosition(12, FIELD_LEFT + 6);
    Viper.Terminal.SetColor(11, 0);
    Viper.Terminal.Print("Final Score: ");
    Viper.Terminal.SetColor(15, 0);
    Viper.Terminal.PrintInt(player.getScore());

    Integer sc = player.getScore();
    if sc > score5 {
        checkHighScore(sc);
        Viper.Terminal.SetPosition(14, FIELD_LEFT + 6);
        Viper.Terminal.SetColor(14, 0);
        Viper.Terminal.Print("NEW HIGH SCORE!");
    }

    Viper.Terminal.SetPosition(16, FIELD_LEFT + 5);
    Viper.Terminal.SetColor(8, 0);
    Viper.Terminal.Print("Press any key...");
    Viper.Terminal.GetKey();
}

func showLevelComplete(player: Player) {
    Viper.Terminal.SetPosition(10, FIELD_LEFT + 6);
    Viper.Terminal.SetColor(10, 0);
    Viper.Terminal.Print("*** LEVEL COMPLETE ***");

    Viper.Terminal.SetPosition(12, FIELD_LEFT + 10);
    Viper.Terminal.SetColor(11, 0);
    Viper.Terminal.Print("Score: ");
    Viper.Terminal.SetColor(15, 0);
    Viper.Terminal.PrintInt(player.getScore());

    Viper.Time.SleepMs(1500);
}

func runGame() {
    // Create player
    Player player = new Player();
    Integer startX = FIELD_WIDTH / 2;
    Integer startY = FIELD_HEIGHT - 2;
    player.init(startX, startY);

    // Create spider
    Spider spider = new Spider();
    spider.init();

    // Create 12 centipede segments
    Segment seg0 = new Segment();
    Segment seg1 = new Segment();
    Segment seg2 = new Segment();
    Segment seg3 = new Segment();
    Segment seg4 = new Segment();
    Segment seg5 = new Segment();
    Segment seg6 = new Segment();
    Segment seg7 = new Segment();
    Segment seg8 = new Segment();
    Segment seg9 = new Segment();
    Segment seg10 = new Segment();
    Segment seg11 = new Segment();

    Integer currentLevel = 1;
    Integer gameRunning = 1;
    Integer spiderSpawnTimer = 100;
    Integer centMoveTimer = 0;
    Integer centSpeed = 3;

    // Initialize first level
    generateMushrooms(currentLevel);

    // Initialize segments
    Integer segLength = 8;
    Integer si = 0;
    while si < 12 {
        Integer isHead = 0;
        if si == 0 { isHead = 1; }
        Integer active = 0;
        if si < segLength { active = 1; }
        Integer sx = startX - si;
        if si == 0 { seg0.init(sx, 0, 1, isHead); if active == 0 { seg0.setActive(0); } }
        if si == 1 { seg1.init(sx, 0, 1, isHead); if active == 0 { seg1.setActive(0); } }
        if si == 2 { seg2.init(sx, 0, 1, isHead); if active == 0 { seg2.setActive(0); } }
        if si == 3 { seg3.init(sx, 0, 1, isHead); if active == 0 { seg3.setActive(0); } }
        if si == 4 { seg4.init(sx, 0, 1, isHead); if active == 0 { seg4.setActive(0); } }
        if si == 5 { seg5.init(sx, 0, 1, isHead); if active == 0 { seg5.setActive(0); } }
        if si == 6 { seg6.init(sx, 0, 1, isHead); if active == 0 { seg6.setActive(0); } }
        if si == 7 { seg7.init(sx, 0, 1, isHead); if active == 0 { seg7.setActive(0); } }
        if si == 8 { seg8.init(sx, 0, 1, isHead); if active == 0 { seg8.setActive(0); } }
        if si == 9 { seg9.init(sx, 0, 1, isHead); if active == 0 { seg9.setActive(0); } }
        if si == 10 { seg10.init(sx, 0, 1, isHead); if active == 0 { seg10.setActive(0); } }
        if si == 11 { seg11.init(sx, 0, 1, isHead); if active == 0 { seg11.setActive(0); } }
        si = si + 1;
    }

    // Draw initial screen
    Viper.Terminal.Clear();
    drawHUD(player, currentLevel);
    drawField();
    drawPlayer(player);

    // Main game loop
    while gameRunning == 1 {
        // Get input
        String key = Viper.Terminal.GetKeyTimeout(30);
        Integer oldX = player.getX();
        Integer oldY = player.getY();

        // Handle movement
        if key == "w" {
            if player.getY() > FIELD_HEIGHT - PLAYER_ZONE {
                player.moveUp();
            }
        }
        if key == "W" {
            if player.getY() > FIELD_HEIGHT - PLAYER_ZONE {
                player.moveUp();
            }
        }
        if key == "s" {
            if player.getY() < FIELD_HEIGHT - 1 {
                player.moveDown();
            }
        }
        if key == "S" {
            if player.getY() < FIELD_HEIGHT - 1 {
                player.moveDown();
            }
        }
        if key == "a" {
            if player.getX() > 0 {
                player.moveLeft();
            }
        }
        if key == "A" {
            if player.getX() > 0 {
                player.moveLeft();
            }
        }
        if key == "d" {
            if player.getX() < FIELD_WIDTH - 1 {
                player.moveRight();
            }
        }
        if key == "D" {
            if player.getX() < FIELD_WIDTH - 1 {
                player.moveRight();
            }
        }
        if key == " " {
            player.fire();
        }
        if key == "q" {
            gameRunning = 0;
        }
        if key == "Q" {
            gameRunning = 0;
        }

        // Redraw player if moved
        if oldX != player.getX() {
            clearPosition(oldX, oldY);
            drawPlayer(player);
        }
        if oldY != player.getY() {
            clearPosition(oldX, oldY);
            drawPlayer(player);
        }

        // Update bullet
        if player.hasBullet() == 1 {
            Integer oldBY = player.getBulletY();
            Integer oldBX = player.getBulletX();
            clearPosition(oldBX, oldBY);
            player.updateBullet();

            if player.hasBullet() == 1 {
                Integer bx = player.getBulletX();
                Integer by = player.getBulletY();

                // Check mushroom hit
                if hasMushroom(by, bx) == 1 {
                    setMushroom(by, bx, 0);
                    drawMushroom(by, bx);
                    player.addScore(1);
                    player.deactivateBullet();
                    drawHUD(player, currentLevel);
                }

                // Check centipede hit
                if player.hasBullet() == 1 {
                    Integer hitSeg = 0 - 1;
                    if seg0.isActive() == 1 {
                        if seg0.getX() == bx {
                            if seg0.getY() == by { hitSeg = 0; }
                        }
                    }
                    if seg1.isActive() == 1 {
                        if seg1.getX() == bx {
                            if seg1.getY() == by { hitSeg = 1; }
                        }
                    }
                    if seg2.isActive() == 1 {
                        if seg2.getX() == bx {
                            if seg2.getY() == by { hitSeg = 2; }
                        }
                    }
                    if seg3.isActive() == 1 {
                        if seg3.getX() == bx {
                            if seg3.getY() == by { hitSeg = 3; }
                        }
                    }
                    if seg4.isActive() == 1 {
                        if seg4.getX() == bx {
                            if seg4.getY() == by { hitSeg = 4; }
                        }
                    }
                    if seg5.isActive() == 1 {
                        if seg5.getX() == bx {
                            if seg5.getY() == by { hitSeg = 5; }
                        }
                    }
                    if seg6.isActive() == 1 {
                        if seg6.getX() == bx {
                            if seg6.getY() == by { hitSeg = 6; }
                        }
                    }
                    if seg7.isActive() == 1 {
                        if seg7.getX() == bx {
                            if seg7.getY() == by { hitSeg = 7; }
                        }
                    }
                    if seg8.isActive() == 1 {
                        if seg8.getX() == bx {
                            if seg8.getY() == by { hitSeg = 8; }
                        }
                    }
                    if seg9.isActive() == 1 {
                        if seg9.getX() == bx {
                            if seg9.getY() == by { hitSeg = 9; }
                        }
                    }
                    if seg10.isActive() == 1 {
                        if seg10.getX() == bx {
                            if seg10.getY() == by { hitSeg = 10; }
                        }
                    }
                    if seg11.isActive() == 1 {
                        if seg11.getX() == bx {
                            if seg11.getY() == by { hitSeg = 11; }
                        }
                    }

                    if hitSeg >= 0 {
                        // Kill segment, leave mushroom
                        if hitSeg == 0 { seg0.setActive(0); setMushroom(seg0.getY(), seg0.getX(), 1); if seg1.isActive() == 1 { seg1.setHead(1); } }
                        if hitSeg == 1 { seg1.setActive(0); setMushroom(seg1.getY(), seg1.getX(), 1); if seg2.isActive() == 1 { seg2.setHead(1); } }
                        if hitSeg == 2 { seg2.setActive(0); setMushroom(seg2.getY(), seg2.getX(), 1); if seg3.isActive() == 1 { seg3.setHead(1); } }
                        if hitSeg == 3 { seg3.setActive(0); setMushroom(seg3.getY(), seg3.getX(), 1); if seg4.isActive() == 1 { seg4.setHead(1); } }
                        if hitSeg == 4 { seg4.setActive(0); setMushroom(seg4.getY(), seg4.getX(), 1); if seg5.isActive() == 1 { seg5.setHead(1); } }
                        if hitSeg == 5 { seg5.setActive(0); setMushroom(seg5.getY(), seg5.getX(), 1); if seg6.isActive() == 1 { seg6.setHead(1); } }
                        if hitSeg == 6 { seg6.setActive(0); setMushroom(seg6.getY(), seg6.getX(), 1); if seg7.isActive() == 1 { seg7.setHead(1); } }
                        if hitSeg == 7 { seg7.setActive(0); setMushroom(seg7.getY(), seg7.getX(), 1); if seg8.isActive() == 1 { seg8.setHead(1); } }
                        if hitSeg == 8 { seg8.setActive(0); setMushroom(seg8.getY(), seg8.getX(), 1); if seg9.isActive() == 1 { seg9.setHead(1); } }
                        if hitSeg == 9 { seg9.setActive(0); setMushroom(seg9.getY(), seg9.getX(), 1); if seg10.isActive() == 1 { seg10.setHead(1); } }
                        if hitSeg == 10 { seg10.setActive(0); setMushroom(seg10.getY(), seg10.getX(), 1); if seg11.isActive() == 1 { seg11.setHead(1); } }
                        if hitSeg == 11 { seg11.setActive(0); setMushroom(seg11.getY(), seg11.getX(), 1); }

                        drawMushroom(by, bx);
                        player.addScore(SEGMENT_POINTS);
                        player.deactivateBullet();
                        drawHUD(player, currentLevel);
                    }
                }

                // Check spider hit
                if player.hasBullet() == 1 {
                    if spider.isActive() == 1 {
                        if spider.getX() == bx {
                            if spider.getY() == by {
                                spider.kill();
                                player.addScore(SPIDER_POINTS);
                                player.deactivateBullet();
                                drawHUD(player, currentLevel);
                            }
                        }
                    }
                }

                // Draw bullet if still active
                if player.hasBullet() == 1 {
                    drawBullet(player);
                }
            }
        }

        // Move centipede
        centMoveTimer = centMoveTimer + 1;
        if centMoveTimer >= centSpeed {
            centMoveTimer = 0;
            Integer reachedBottom = 0;

            // Clear old positions
            if seg0.isActive() == 1 { clearPosition(seg0.getX(), seg0.getY()); }
            if seg1.isActive() == 1 { clearPosition(seg1.getX(), seg1.getY()); }
            if seg2.isActive() == 1 { clearPosition(seg2.getX(), seg2.getY()); }
            if seg3.isActive() == 1 { clearPosition(seg3.getX(), seg3.getY()); }
            if seg4.isActive() == 1 { clearPosition(seg4.getX(), seg4.getY()); }
            if seg5.isActive() == 1 { clearPosition(seg5.getX(), seg5.getY()); }
            if seg6.isActive() == 1 { clearPosition(seg6.getX(), seg6.getY()); }
            if seg7.isActive() == 1 { clearPosition(seg7.getX(), seg7.getY()); }
            if seg8.isActive() == 1 { clearPosition(seg8.getX(), seg8.getY()); }
            if seg9.isActive() == 1 { clearPosition(seg9.getX(), seg9.getY()); }
            if seg10.isActive() == 1 { clearPosition(seg10.getX(), seg10.getY()); }
            if seg11.isActive() == 1 { clearPosition(seg11.getX(), seg11.getY()); }

            // Move each segment
            Integer mi = 0;
            while mi < 12 {
                Segment curSeg = seg0;
                if mi == 0 { curSeg = seg0; }
                if mi == 1 { curSeg = seg1; }
                if mi == 2 { curSeg = seg2; }
                if mi == 3 { curSeg = seg3; }
                if mi == 4 { curSeg = seg4; }
                if mi == 5 { curSeg = seg5; }
                if mi == 6 { curSeg = seg6; }
                if mi == 7 { curSeg = seg7; }
                if mi == 8 { curSeg = seg8; }
                if mi == 9 { curSeg = seg9; }
                if mi == 10 { curSeg = seg10; }
                if mi == 11 { curSeg = seg11; }

                if curSeg.isActive() == 1 {
                    Integer newX = curSeg.getX() + curSeg.getDir();
                    Integer blocked = 0;

                    if newX < 0 { blocked = 1; }
                    if newX >= FIELD_WIDTH { blocked = 1; }
                    if blocked == 0 {
                        if hasMushroom(curSeg.getY(), newX) == 1 {
                            blocked = 1;
                        }
                    }

                    if blocked == 1 {
                        curSeg.moveDown();
                        curSeg.reverseDir();
                        if curSeg.getY() >= FIELD_HEIGHT {
                            reachedBottom = 1;
                        }
                    } else {
                        curSeg.setX(newX);
                    }
                }
                mi = mi + 1;
            }

            // Check if centipede reached bottom
            if reachedBottom == 1 {
                Integer gameOver = player.loseLife();
                if gameOver == 1 {
                    showGameOver(player);
                    gameRunning = 0;
                } else {
                    // Reset centipede
                    Integer ri = 0;
                    while ri < 12 {
                        Integer isHead = 0;
                        if ri == 0 { isHead = 1; }
                        Integer active = 0;
                        if ri < segLength { active = 1; }
                        Integer rx = startX - ri;
                        if ri == 0 { seg0.init(rx, 0, 1, isHead); if active == 0 { seg0.setActive(0); } }
                        if ri == 1 { seg1.init(rx, 0, 1, isHead); if active == 0 { seg1.setActive(0); } }
                        if ri == 2 { seg2.init(rx, 0, 1, isHead); if active == 0 { seg2.setActive(0); } }
                        if ri == 3 { seg3.init(rx, 0, 1, isHead); if active == 0 { seg3.setActive(0); } }
                        if ri == 4 { seg4.init(rx, 0, 1, isHead); if active == 0 { seg4.setActive(0); } }
                        if ri == 5 { seg5.init(rx, 0, 1, isHead); if active == 0 { seg5.setActive(0); } }
                        if ri == 6 { seg6.init(rx, 0, 1, isHead); if active == 0 { seg6.setActive(0); } }
                        if ri == 7 { seg7.init(rx, 0, 1, isHead); if active == 0 { seg7.setActive(0); } }
                        if ri == 8 { seg8.init(rx, 0, 1, isHead); if active == 0 { seg8.setActive(0); } }
                        if ri == 9 { seg9.init(rx, 0, 1, isHead); if active == 0 { seg9.setActive(0); } }
                        if ri == 10 { seg10.init(rx, 0, 1, isHead); if active == 0 { seg10.setActive(0); } }
                        if ri == 11 { seg11.init(rx, 0, 1, isHead); if active == 0 { seg11.setActive(0); } }
                        ri = ri + 1;
                    }
                    player.reset(startX, startY);
                    drawHUD(player, currentLevel);
                    drawField();
                }
            }

            // Draw segments
            if gameRunning == 1 {
                drawSegment(seg0);
                drawSegment(seg1);
                drawSegment(seg2);
                drawSegment(seg3);
                drawSegment(seg4);
                drawSegment(seg5);
                drawSegment(seg6);
                drawSegment(seg7);
                drawSegment(seg8);
                drawSegment(seg9);
                drawSegment(seg10);
                drawSegment(seg11);
            }
        }

        // Check player collision with centipede
        if gameRunning == 1 {
            Integer px = player.getX();
            Integer py = player.getY();
            Integer hitPlayer = 0;

            if seg0.isActive() == 1 {
                if seg0.getX() == px {
                    if seg0.getY() == py { hitPlayer = 1; }
                }
            }
            if seg1.isActive() == 1 {
                if seg1.getX() == px {
                    if seg1.getY() == py { hitPlayer = 1; }
                }
            }
            if seg2.isActive() == 1 {
                if seg2.getX() == px {
                    if seg2.getY() == py { hitPlayer = 1; }
                }
            }
            if seg3.isActive() == 1 {
                if seg3.getX() == px {
                    if seg3.getY() == py { hitPlayer = 1; }
                }
            }
            if seg4.isActive() == 1 {
                if seg4.getX() == px {
                    if seg4.getY() == py { hitPlayer = 1; }
                }
            }
            if seg5.isActive() == 1 {
                if seg5.getX() == px {
                    if seg5.getY() == py { hitPlayer = 1; }
                }
            }
            if seg6.isActive() == 1 {
                if seg6.getX() == px {
                    if seg6.getY() == py { hitPlayer = 1; }
                }
            }
            if seg7.isActive() == 1 {
                if seg7.getX() == px {
                    if seg7.getY() == py { hitPlayer = 1; }
                }
            }
            if seg8.isActive() == 1 {
                if seg8.getX() == px {
                    if seg8.getY() == py { hitPlayer = 1; }
                }
            }
            if seg9.isActive() == 1 {
                if seg9.getX() == px {
                    if seg9.getY() == py { hitPlayer = 1; }
                }
            }
            if seg10.isActive() == 1 {
                if seg10.getX() == px {
                    if seg10.getY() == py { hitPlayer = 1; }
                }
            }
            if seg11.isActive() == 1 {
                if seg11.getX() == px {
                    if seg11.getY() == py { hitPlayer = 1; }
                }
            }

            if hitPlayer == 1 {
                Integer gameOver = player.loseLife();
                if gameOver == 1 {
                    showGameOver(player);
                    gameRunning = 0;
                } else {
                    // Reset centipede
                    Integer ri = 0;
                    while ri < 12 {
                        Integer isHead = 0;
                        if ri == 0 { isHead = 1; }
                        Integer active = 0;
                        if ri < segLength { active = 1; }
                        Integer rx = startX - ri;
                        if ri == 0 { seg0.init(rx, 0, 1, isHead); if active == 0 { seg0.setActive(0); } }
                        if ri == 1 { seg1.init(rx, 0, 1, isHead); if active == 0 { seg1.setActive(0); } }
                        if ri == 2 { seg2.init(rx, 0, 1, isHead); if active == 0 { seg2.setActive(0); } }
                        if ri == 3 { seg3.init(rx, 0, 1, isHead); if active == 0 { seg3.setActive(0); } }
                        if ri == 4 { seg4.init(rx, 0, 1, isHead); if active == 0 { seg4.setActive(0); } }
                        if ri == 5 { seg5.init(rx, 0, 1, isHead); if active == 0 { seg5.setActive(0); } }
                        if ri == 6 { seg6.init(rx, 0, 1, isHead); if active == 0 { seg6.setActive(0); } }
                        if ri == 7 { seg7.init(rx, 0, 1, isHead); if active == 0 { seg7.setActive(0); } }
                        if ri == 8 { seg8.init(rx, 0, 1, isHead); if active == 0 { seg8.setActive(0); } }
                        if ri == 9 { seg9.init(rx, 0, 1, isHead); if active == 0 { seg9.setActive(0); } }
                        if ri == 10 { seg10.init(rx, 0, 1, isHead); if active == 0 { seg10.setActive(0); } }
                        if ri == 11 { seg11.init(rx, 0, 1, isHead); if active == 0 { seg11.setActive(0); } }
                        ri = ri + 1;
                    }
                    player.reset(startX, startY);
                    drawHUD(player, currentLevel);
                    drawField();
                }
            }
        }

        // Spider logic
        if gameRunning == 1 {
            spiderSpawnTimer = spiderSpawnTimer - 1;
            if spider.isActive() == 0 {
                if spiderSpawnTimer <= 0 {
                    Integer spawnY = FIELD_HEIGHT - 1 - Viper.Random.NextInt(PLAYER_ZONE);
                    Integer spawnX = 0;
                    Integer spawnDirX = 1;
                    if Viper.Random.NextInt(2) == 1 {
                        spawnX = FIELD_WIDTH - 1;
                        spawnDirX = 0 - 1;
                    }
                    Integer spawnDirY = 1;
                    if Viper.Random.NextInt(2) == 1 {
                        spawnDirY = 0 - 1;
                    }
                    spider.spawn(spawnX, spawnY, spawnDirX, spawnDirY);
                    spiderSpawnTimer = 150 + Viper.Random.NextInt(100);
                }
            }

            if spider.isActive() == 1 {
                spider.incTimer();
                if spider.getTimer() >= 2 {
                    spider.resetTimer();

                    // Clear old position
                    clearPosition(spider.getX(), spider.getY());

                    // Move spider erratically
                    Integer newSpX = spider.getX() + spider.getDirX();
                    spider.setX(newSpX);

                    if Viper.Random.NextInt(3) == 0 {
                        Integer newSpY = spider.getY() + spider.getDirY();
                        spider.setY(newSpY);
                    }

                    // Randomly change vertical direction
                    if Viper.Random.NextInt(5) == 0 {
                        spider.setDirY(0 - spider.getDirY());
                    }

                    // Keep in bounds vertically
                    if spider.getY() < FIELD_HEIGHT - PLAYER_ZONE {
                        spider.setY(FIELD_HEIGHT - PLAYER_ZONE);
                        spider.setDirY(1);
                    }
                    if spider.getY() >= FIELD_HEIGHT {
                        spider.setY(FIELD_HEIGHT - 1);
                        spider.setDirY(0 - 1);
                    }

                    // Kill if left screen
                    if spider.getX() < 0 { spider.kill(); }
                    if spider.getX() >= FIELD_WIDTH { spider.kill(); }

                    // Eat mushrooms
                    if spider.isActive() == 1 {
                        if hasMushroom(spider.getY(), spider.getX()) == 1 {
                            setMushroom(spider.getY(), spider.getX(), 0);
                            drawMushroom(spider.getY(), spider.getX());
                        }
                    }

                    // Check spider collision with player
                    if spider.isActive() == 1 {
                        if spider.getX() == player.getX() {
                            if spider.getY() == player.getY() {
                                Integer gameOver = player.loseLife();
                                if gameOver == 1 {
                                    showGameOver(player);
                                    gameRunning = 0;
                                } else {
                                    spider.kill();
                                    drawHUD(player, currentLevel);
                                }
                            }
                        }
                    }

                    if spider.isActive() == 1 {
                        drawSpider(spider);
                    }
                }
            }
        }

        // Check level complete
        if gameRunning == 1 {
            Integer activeCount = 0;
            if seg0.isActive() == 1 { activeCount = activeCount + 1; }
            if seg1.isActive() == 1 { activeCount = activeCount + 1; }
            if seg2.isActive() == 1 { activeCount = activeCount + 1; }
            if seg3.isActive() == 1 { activeCount = activeCount + 1; }
            if seg4.isActive() == 1 { activeCount = activeCount + 1; }
            if seg5.isActive() == 1 { activeCount = activeCount + 1; }
            if seg6.isActive() == 1 { activeCount = activeCount + 1; }
            if seg7.isActive() == 1 { activeCount = activeCount + 1; }
            if seg8.isActive() == 1 { activeCount = activeCount + 1; }
            if seg9.isActive() == 1 { activeCount = activeCount + 1; }
            if seg10.isActive() == 1 { activeCount = activeCount + 1; }
            if seg11.isActive() == 1 { activeCount = activeCount + 1; }

            if activeCount == 0 {
                showLevelComplete(player);
                currentLevel = currentLevel + 1;
                segLength = 8 + currentLevel;
                if segLength > 12 { segLength = 12; }
                centSpeed = 4 - (currentLevel / 3);
                if centSpeed < 1 { centSpeed = 1; }

                generateMushrooms(currentLevel);

                // Reset segments for new level
                Integer ri = 0;
                while ri < 12 {
                    Integer isHead = 0;
                    if ri == 0 { isHead = 1; }
                    Integer active = 0;
                    if ri < segLength { active = 1; }
                    Integer rx = startX - ri;
                    if ri == 0 { seg0.init(rx, 0, 1, isHead); if active == 0 { seg0.setActive(0); } }
                    if ri == 1 { seg1.init(rx, 0, 1, isHead); if active == 0 { seg1.setActive(0); } }
                    if ri == 2 { seg2.init(rx, 0, 1, isHead); if active == 0 { seg2.setActive(0); } }
                    if ri == 3 { seg3.init(rx, 0, 1, isHead); if active == 0 { seg3.setActive(0); } }
                    if ri == 4 { seg4.init(rx, 0, 1, isHead); if active == 0 { seg4.setActive(0); } }
                    if ri == 5 { seg5.init(rx, 0, 1, isHead); if active == 0 { seg5.setActive(0); } }
                    if ri == 6 { seg6.init(rx, 0, 1, isHead); if active == 0 { seg6.setActive(0); } }
                    if ri == 7 { seg7.init(rx, 0, 1, isHead); if active == 0 { seg7.setActive(0); } }
                    if ri == 8 { seg8.init(rx, 0, 1, isHead); if active == 0 { seg8.setActive(0); } }
                    if ri == 9 { seg9.init(rx, 0, 1, isHead); if active == 0 { seg9.setActive(0); } }
                    if ri == 10 { seg10.init(rx, 0, 1, isHead); if active == 0 { seg10.setActive(0); } }
                    if ri == 11 { seg11.init(rx, 0, 1, isHead); if active == 0 { seg11.setActive(0); } }
                    ri = ri + 1;
                }

                spider.kill();
                spiderSpawnTimer = 100;

                Viper.Terminal.Clear();
                drawHUD(player, currentLevel);
                drawField();
            }
        }

        // Always draw player
        if gameRunning == 1 {
            drawPlayer(player);
        }
    }
}

func start() {
    Integer running = 1;

    while running == 1 {
        showMainMenu();
        String choice = Viper.Terminal.GetKey();

        if choice == "1" {
            runGame();
        }
        if choice == "2" {
            showInstructions();
        }
        if choice == "3" {
            showHighScores();
        }
        if choice == "q" {
            running = 0;
        }
        if choice == "Q" {
            running = 0;
        }
    }

    Viper.Terminal.Clear();
    Viper.Terminal.SetPosition(1, 1);
    Viper.Terminal.Print("Thanks for playing CENTIPEDE!");
}

