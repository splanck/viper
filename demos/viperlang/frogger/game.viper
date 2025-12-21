module Game;

import "./config";
import "./frog";
import "./vehicle";
import "./platform";
import "./home";
import "./powerup";

// Game entity - main game controller
entity Game {
    expose Frog frog;
    expose Integer score;
    expose Integer level;
    expose Integer homesFilled;
    expose Boolean running;
    expose Boolean paused;
    expose Integer frameCount;

    // Vehicles (10 total across 5 lanes)
    expose Vehicle v0;
    expose Vehicle v1;
    expose Vehicle v2;
    expose Vehicle v3;
    expose Vehicle v4;
    expose Vehicle v5;
    expose Vehicle v6;
    expose Vehicle v7;
    expose Vehicle v8;
    expose Vehicle v9;

    // Platforms (10 total across 5 river rows)
    expose Platform p0;
    expose Platform p1;
    expose Platform p2;
    expose Platform p3;
    expose Platform p4;
    expose Platform p5;
    expose Platform p6;
    expose Platform p7;
    expose Platform p8;
    expose Platform p9;

    // Home slots (5 total)
    expose Home home0;
    expose Home home1;
    expose Home home2;
    expose Home home3;
    expose Home home4;

    // Power-ups (3 slots)
    expose PowerUp power0;
    expose PowerUp power1;
    expose PowerUp power2;

    expose func init() {
        score = 0;
        level = 1;
        homesFilled = 0;
        running = true;
        paused = false;
        frameCount = 0;

        frog = createFrog();

        // Create homes
        home0 = createHome(8);
        home1 = createHome(22);
        home2 = createHome(36);
        home3 = createHome(50);
        home4 = createHome(64);

        // Initialize vehicles and platforms
        self.initVehicles();
        self.initPlatforms();

        // Power-ups start inactive
        power0 = createInactivePowerUp();
        power1 = createInactivePowerUp();
        power2 = createInactivePowerUp();
    }

    expose func initVehicles() {
        // Lane 1 (row 12): Cars going right
        v0 = createCar(12, 5, 1, 1);
        v1 = createCar(12, 40, 1, 1);

        // Lane 2 (row 13): Trucks going left
        v2 = createTruck(13, 20, 1, 0 - 1);
        v3 = createTruck(13, 55, 1, 0 - 1);

        // Lane 3 (row 14): Fast sports cars going right
        v4 = createSportsCar(14, 10, 2, 1);
        v5 = createSportsCar(14, 50, 2, 1);

        // Lane 4 (row 15): Mixed vehicles going left
        v6 = createTruck(15, 15, 1, 0 - 1);
        v7 = createCar(15, 45, 2, 0 - 1);

        // Lane 5 (row 16): Fast cars going right
        v8 = createCar(16, 8, 2, 1);
        v9 = createCar(16, 60, 2, 1);
    }

    expose func initPlatforms() {
        // River row 1 (row 4): Logs going right
        p0 = createLog(4, 5, 1, 1, 8);
        p1 = createLog(4, 40, 1, 1, 8);

        // River row 2 (row 5): Turtles going left
        p2 = createTurtle(5, 15, 1, 0 - 1, 5);
        p3 = createSinkingTurtle(5, 50, 1, 0 - 1, 4);

        // River row 3 (row 6): Long logs going right
        p4 = createLog(6, 10, 1, 1, 12);
        p5 = createLog(6, 50, 1, 1, 10);

        // River row 4 (row 7): Sinking turtles going left
        p6 = createSinkingTurtle(7, 20, 1, 0 - 1, 4);
        p7 = createTurtle(7, 55, 1, 0 - 1, 5);

        // River row 5 (row 8): Fast logs going right
        p8 = createLog(8, 12, 2, 1, 7);
        p9 = createLog(8, 48, 2, 1, 7);
    }

    // Accessors for array-like access
    expose func getVehicle(Integer i) -> Vehicle {
        if (i == 0) { return v0; }
        if (i == 1) { return v1; }
        if (i == 2) { return v2; }
        if (i == 3) { return v3; }
        if (i == 4) { return v4; }
        if (i == 5) { return v5; }
        if (i == 6) { return v6; }
        if (i == 7) { return v7; }
        if (i == 8) { return v8; }
        return v9;
    }

    expose func getPlatform(Integer i) -> Platform {
        if (i == 0) { return p0; }
        if (i == 1) { return p1; }
        if (i == 2) { return p2; }
        if (i == 3) { return p3; }
        if (i == 4) { return p4; }
        if (i == 5) { return p5; }
        if (i == 6) { return p6; }
        if (i == 7) { return p7; }
        if (i == 8) { return p8; }
        return p9;
    }

    expose func getHome(Integer i) -> Home {
        if (i == 0) { return home0; }
        if (i == 1) { return home1; }
        if (i == 2) { return home2; }
        if (i == 3) { return home3; }
        return home4;
    }

    expose func getPowerUp(Integer i) -> PowerUp {
        if (i == 0) { return power0; }
        if (i == 1) { return power1; }
        return power2;
    }

    // Position checks
    expose func isInRiver(Integer row) -> Boolean {
        return row >= RIVER_START && row <= RIVER_END;
    }

    expose func isOnRoad(Integer row) -> Boolean {
        return row >= ROAD_START && row <= ROAD_END;
    }

    expose func isSafeZone(Integer row) -> Boolean {
        return row == SAFE_ZONE_ROW || row == START_ROW;
    }

    // Main update loop
    expose func update() {
        if (paused || !running) {
            return;
        }

        if (!frog.isAlive()) {
            running = false;
            return;
        }

        frameCount = frameCount + 1;

        // Update frog timers
        frog.updateTimers();

        // Move all vehicles
        self.updateVehicles();

        // Move all platforms
        self.updatePlatforms();

        // Update homes (flies)
        self.updateHomes();

        // Update power-ups
        self.updatePowerUps();

        // Check collisions and interactions
        self.checkFrogPosition();

        // Maybe spawn power-up
        self.maybeSpawnPowerUp();
    }

    expose func updateVehicles() {
        var i = 0;
        while (i < MAX_VEHICLES) {
            Vehicle v = self.getVehicle(i);
            v.move();
            i = i + 1;
        }
    }

    expose func updatePlatforms() {
        var i = 0;
        while (i < MAX_PLATFORMS) {
            Platform p = self.getPlatform(i);
            p.move();
            i = i + 1;
        }
    }

    expose func updateHomes() {
        home0.update();
        home1.update();
        home2.update();
        home3.update();
        home4.update();

        // Randomly spawn flies
        if (frameCount > 0) {
            Integer flyChance = frameCount - ((frameCount / 500) * 500);
            if (flyChance == 0) {
                // Pick a random unfilled home
                self.spawnFlyRandomHome();
            }
        }
    }

    expose func spawnFlyRandomHome() {
        // Simple rotation based on frame count
        Integer pick = (frameCount / 500) - ((frameCount / 2500) * 5);

        Home h = self.getHome(pick);
        if (h.isEmpty()) {
            h.spawnFly();
        }
    }

    expose func updatePowerUps() {
        power0.update();
        power1.update();
        power2.update();
    }

    expose func maybeSpawnPowerUp() {
        // Spawn power-up every 300 frames
        Integer spawnChance = frameCount - ((frameCount / 300) * 300);
        if (spawnChance != 0) {
            return;
        }

        // Find an inactive power-up slot
        var i = 0;
        while (i < MAX_POWERUPS) {
            PowerUp p = self.getPowerUp(i);
            if (!p.isActive()) {
                // Spawn in safe zone or median
                Integer row = SAFE_ZONE_ROW;
                Integer col = 20 + (frameCount - ((frameCount / 40) * 40));

                // Cycle through power-up types
                Integer pType = 1 + (frameCount / 300) - ((frameCount / 900) * 3);
                p.spawn(row, col, pType);
                return;
            }
            i = i + 1;
        }
    }

    expose func checkFrogPosition() {
        Integer frogRow = frog.getRow();
        Integer frogCol = frog.getCol();

        // Check power-up collection
        self.checkPowerUpCollection(frogRow, frogCol);

        // Check river
        if (self.isInRiver(frogRow)) {
            self.checkRiverPosition(frogRow, frogCol);
            return;
        }

        frog.clearPlatform();

        // Check road collisions
        if (self.isOnRoad(frogRow)) {
            self.checkRoadCollision(frogRow, frogCol);
            return;
        }

        // Check if reached home row
        if (frogRow == HOME_ROW) {
            self.checkHomeReached(frogCol);
            return;
        }

        // Check bounds
        if (frogCol < 1 || frogCol > GAME_WIDTH) {
            frog.die();
        }
    }

    expose func checkPowerUpCollection(Integer row, Integer col) {
        var i = 0;
        while (i < MAX_POWERUPS) {
            PowerUp p = self.getPowerUp(i);
            if (p.checkCollect(row, col)) {
                self.applyPowerUp(p);
                p.collect();
                score = score + SCORE_POWERUP;
            }
            i = i + 1;
        }
    }

    expose func applyPowerUp(PowerUp p) {
        if (p.isSpeed()) {
            frog.activateSpeedBoost(50);
        } else if (p.isInvincible()) {
            frog.activateInvincibility(INVINCIBLE_FRAMES);
        } else if (p.isExtraLife()) {
            frog.addLife();
        }
    }

    expose func checkRiverPosition(Integer row, Integer col) {
        Boolean onPlatform = false;

        var i = 0;
        while (i < MAX_PLATFORMS) {
            Platform p = self.getPlatform(i);
            if (p.checkOnPlatform(row, col)) {
                onPlatform = true;
                Integer rideSpeed = p.getRideSpeed();
                frog.setOnPlatform(rideSpeed);
                frog.updatePlatformRide();
            }
            i = i + 1;
        }

        if (!onPlatform) {
            frog.die();
        }
    }

    expose func checkRoadCollision(Integer row, Integer col) {
        var i = 0;
        while (i < MAX_VEHICLES) {
            Vehicle v = self.getVehicle(i);
            if (v.checkCollision(row, col)) {
                frog.die();
                return;
            }
            i = i + 1;
        }
    }

    expose func checkHomeReached(Integer frogCol) {
        Boolean foundHome = false;

        var i = 0;
        while (i < MAX_HOMES) {
            Home h = self.getHome(i);
            if (h.checkLanding(frogCol)) {
                if (h.isFilled()) {
                    // Already filled - die
                    frog.die();
                    return;
                }

                foundHome = true;
                Integer bonus = h.getBonusPoints();
                h.fill();
                homesFilled = homesFilled + 1;
                score = score + bonus;

                // Check win
                if (homesFilled >= MAX_HOMES) {
                    running = false;
                } else {
                    frog.reset();
                }
                return;
            }
            i = i + 1;
        }

        // Didn't land in a home slot - death
        if (!foundHome) {
            frog.die();
        }
    }

    // Input handling
    expose func handleInput(String key) {
        if (key == "q" || key == "Q") {
            running = false;
            return;
        }

        if (key == "p" || key == "P") {
            paused = !paused;
            return;
        }

        if (paused) {
            return;
        }

        if (key == "w" || key == "W") {
            frog.moveUp();
        } else if (key == "s" || key == "S") {
            frog.moveDown();
        } else if (key == "a" || key == "A") {
            frog.moveLeft();
        } else if (key == "d" || key == "D") {
            frog.moveRight();
        }
    }

    // State queries
    expose func isGameOver() -> Boolean {
        return !running;
    }

    expose func hasWon() -> Boolean {
        return homesFilled >= MAX_HOMES;
    }

    expose func isPaused() -> Boolean {
        return paused;
    }

    expose func getScore() -> Integer {
        return score;
    }

    expose func getLevel() -> Integer {
        return level;
    }

    expose func getHomesFilled() -> Integer {
        return homesFilled;
    }
}

// Factory function
func createGame() -> Game {
    Game g = new Game();
    g.init();
    return g;
}
