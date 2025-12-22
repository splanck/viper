module Game;

import "./config";
import "./frog";
import "./car";
import "./truck";
import "./log";
import "./turtle";
import "./home";

// =============================================================================
// Game - Main game controller
// Manages all entities, game state, and logic
// =============================================================================

entity Game {
    // Player
    expose Frog frog;

    // Score and state
    expose Integer score;
    expose Integer level;
    expose Integer homesFilled;
    expose Integer timeLeft;
    expose Integer running;    // 1=running, 0=stopped (workaround for Boolean alignment bug)
    expose Integer paused;     // 1=paused, 0=not paused
    expose Integer won;        // 1=won, 0=not won
    expose Integer frameCount;

    // Cars (4 total)
    expose Car car0;
    expose Car car1;
    expose Car car2;
    expose Car car3;

    // Trucks (3 total)
    expose Truck truck0;
    expose Truck truck1;
    expose Truck truck2;

    // Logs (4 total)
    expose Log log0;
    expose Log log1;
    expose Log log2;
    expose Log log3;

    // Turtles (3 total - 2 normal, 1 sinking)
    expose Turtle turtle0;
    expose Turtle turtle1;
    expose Turtle turtle2;

    // Home slots (5 total)
    expose HomeSlot home0;
    expose HomeSlot home1;
    expose HomeSlot home2;
    expose HomeSlot home3;
    expose HomeSlot home4;

    expose func init() {
        score = 0;
        level = 1;
        homesFilled = 0;
        timeLeft = TIME_LIMIT;
        running = 1;
        paused = 0;
        won = 0;
        frameCount = 0;

        frog = createFrog();
        self.initVehicles();
        self.initPlatforms();
        self.initHomes();
    }

    expose func initVehicles() {
        // Lane 1 (row 13): Cars going right, medium speed
        car0 = createCar(ROAD_START, 10, MEDIUM_SPEED, 1);
        car1 = createCar(ROAD_START, 50, MEDIUM_SPEED, 1);

        // Lane 2 (row 14): Truck going left, slow
        truck0 = createTruck(ROAD_START + 1, 30, SLOW_SPEED, 0 - 1);

        // Lane 3 (row 15): Fast cars going right
        car2 = createFastCar(ROAD_START + 2, 20, 1);
        car3 = createFastCar(ROAD_START + 2, 60, 1);

        // Lane 4 (row 16): Trucks going left, medium speed
        truck1 = createTruck(ROAD_START + 3, 15, MEDIUM_SPEED, 0 - 1);
        truck2 = createTruck(ROAD_START + 3, 55, MEDIUM_SPEED, 0 - 1);
    }

    expose func initPlatforms() {
        // River row 1 (row 5): Logs going right
        log0 = createMediumLog(RIVER_START, 10, SLOW_SPEED, 1);
        log1 = createMediumLog(RIVER_START, 50, SLOW_SPEED, 1);

        // River row 2 (row 6): Turtles going left
        turtle0 = createTurtle(RIVER_START + 1, 20, SLOW_SPEED, 0 - 1, 4);
        turtle1 = createSinkingTurtle(RIVER_START + 1, 55, SLOW_SPEED, 0 - 1, 4);

        // River row 3 (row 7): Long logs going right
        log2 = createLongLog(RIVER_START + 2, 5, MEDIUM_SPEED, 1);
        log3 = createShortLog(RIVER_START + 2, 45, MEDIUM_SPEED, 1);

        // River row 4 (row 8): Sinking turtles going left
        turtle2 = createSinkingTurtle(RIVER_START + 3, 35, MEDIUM_SPEED, 0 - 1, 5);

        // River row 5 (row 9): covered by other logs at faster speed
    }

    expose func initHomes() {
        // 5 home slots evenly spaced
        home0 = createHomeSlot(10);
        home1 = createHomeSlot(24);
        home2 = createHomeSlot(38);
        home3 = createHomeSlot(52);
        home4 = createHomeSlot(66);
    }

    // Main update loop
    expose func update() {
        if (paused == 1 || running == 0) {
            return;
        }

        frameCount = frameCount + 1;

        // Update time
        timeLeft = timeLeft - 1;
        if (timeLeft <= 0) {
            frog.die();
        }

        // Update frog
        frog.update();

        // Check if frog can respawn
        if (frog.canRespawn()) {
            frog.respawn();
            timeLeft = TIME_LIMIT;
        }

        // Check if frog is out of lives
        if (!frog.isAlive() && !frog.canRespawn()) {
            running = 0;
            return;
        }

        // Move all vehicles
        self.updateVehicles();

        // Move all platforms
        self.updatePlatforms();

        // Update homes (fly spawning)
        self.updateHomes();

        // Check frog position and collisions
        if (!frog.isDead()) {
            self.checkFrogPosition();
        }
    }

    expose func updateVehicles() {
        car0.move();
        car1.move();
        car2.move();
        car3.move();
        truck0.move();
        truck1.move();
        truck2.move();
    }

    expose func updatePlatforms() {
        log0.move();
        log1.move();
        log2.move();
        log3.move();
        turtle0.move();
        turtle1.move();
        turtle2.move();
    }

    expose func updateHomes() {
        home0.update();
        home1.update();
        home2.update();
        home3.update();
        home4.update();

        // Maybe spawn a fly in an empty home
        Integer flyChance = frameCount - ((frameCount / 300) * 300);
        if (flyChance == 0) {
            self.spawnRandomFly();
        }
    }

    expose func spawnRandomFly() {
        // Pick based on frame count
        Integer pick = (frameCount / 300) - ((frameCount / 1500) * 5);

        if (pick == 0 && home0.isEmpty()) {
            home0.spawnFly();
        } else if (pick == 1 && home1.isEmpty()) {
            home1.spawnFly();
        } else if (pick == 2 && home2.isEmpty()) {
            home2.spawnFly();
        } else if (pick == 3 && home3.isEmpty()) {
            home3.spawnFly();
        } else if (pick == 4 && home4.isEmpty()) {
            home4.spawnFly();
        }
    }

    expose func checkFrogPosition() {
        Integer frogRow = frog.getRow();
        Integer frogCol = frog.getCol();

        // Check if in river
        if (frogRow >= RIVER_START && frogRow <= RIVER_END) {
            self.checkRiverPosition(frogRow, frogCol);
            return;
        }

        // Clear platform riding when not in river
        frog.clearPlatform();

        // Check road collisions
        if (frogRow >= ROAD_START && frogRow <= ROAD_END) {
            self.checkRoadCollision(frogRow, frogCol);
            return;
        }

        // Check if reached home row
        if (frogRow == HOME_ROW) {
            self.checkHomeReached(frogCol);
            return;
        }
    }

    expose func checkRiverPosition(Integer frogRow, Integer frogCol) {
        Boolean onPlatform = false;
        Integer rideSpeed = 0;

        // Check logs
        if (log0.isOnLog(frogRow, frogCol)) {
            onPlatform = true;
            rideSpeed = log0.getRideSpeed();
        } else if (log1.isOnLog(frogRow, frogCol)) {
            onPlatform = true;
            rideSpeed = log1.getRideSpeed();
        } else if (log2.isOnLog(frogRow, frogCol)) {
            onPlatform = true;
            rideSpeed = log2.getRideSpeed();
        } else if (log3.isOnLog(frogRow, frogCol)) {
            onPlatform = true;
            rideSpeed = log3.getRideSpeed();
        }

        // Check turtles
        if (!onPlatform) {
            if (turtle0.isOnTurtle(frogRow, frogCol)) {
                onPlatform = true;
                rideSpeed = turtle0.getRideSpeed();
            } else if (turtle1.isOnTurtle(frogRow, frogCol)) {
                onPlatform = true;
                rideSpeed = turtle1.getRideSpeed();
            } else if (turtle2.isOnTurtle(frogRow, frogCol)) {
                onPlatform = true;
                rideSpeed = turtle2.getRideSpeed();
            }
        }

        if (onPlatform) {
            frog.setOnPlatform(rideSpeed);
            frog.updatePlatformRide();
        } else {
            // Drowned!
            frog.die();
        }
    }

    expose func checkRoadCollision(Integer frogRow, Integer frogCol) {
        // Check cars
        if (car0.checkCollision(frogRow, frogCol)) {
            frog.die();
            return;
        }
        if (car1.checkCollision(frogRow, frogCol)) {
            frog.die();
            return;
        }
        if (car2.checkCollision(frogRow, frogCol)) {
            frog.die();
            return;
        }
        if (car3.checkCollision(frogRow, frogCol)) {
            frog.die();
            return;
        }

        // Check trucks
        if (truck0.checkCollision(frogRow, frogCol)) {
            frog.die();
            return;
        }
        if (truck1.checkCollision(frogRow, frogCol)) {
            frog.die();
            return;
        }
        if (truck2.checkCollision(frogRow, frogCol)) {
            frog.die();
            return;
        }
    }

    expose func checkHomeReached(Integer frogCol) {
        // Check each home slot
        if (home0.checkLanding(frogCol)) {
            self.landInHome(home0);
            return;
        }
        if (home1.checkLanding(frogCol)) {
            self.landInHome(home1);
            return;
        }
        if (home2.checkLanding(frogCol)) {
            self.landInHome(home2);
            return;
        }
        if (home3.checkLanding(frogCol)) {
            self.landInHome(home3);
            return;
        }
        if (home4.checkLanding(frogCol)) {
            self.landInHome(home4);
            return;
        }

        // Didn't land in a valid home slot - death!
        frog.die();
    }

    expose func landInHome(HomeSlot home) {
        if (home.isFilled()) {
            // Already filled - death!
            frog.die();
            return;
        }

        // Score bonus
        Integer bonus = home.getBonusPoints();
        Integer timeBonus = timeLeft / 10;
        score = score + bonus + timeBonus;

        // Fill the home
        home.fill();
        homesFilled = homesFilled + 1;

        // Check for win
        if (homesFilled >= MAX_HOMES) {
            won = 1;
            running = 0;
        } else {
            // Reset frog for next attempt
            frog.resetForNewLevel();
            timeLeft = TIME_LIMIT;
        }
    }

    // Input handling
    // NOTE: Using inline strings due to Bug #32 - string constants not dereferenced
    expose func handleInput(String key) {
        // Quit
        if (key == "q" || key == "Q") {
            running = 0;
            return;
        }

        // Pause toggle
        if (key == "p" || key == "P") {
            if (paused == 0) {
                paused = 1;
            } else {
                paused = 0;
            }
            return;
        }

        // Don't process movement when paused or dead
        if (paused == 1 || frog.isDead()) {
            return;
        }

        // Movement
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

    // Getters for array-like access
    expose func getCar(Integer i) -> Car {
        if (i == 0) { return car0; }
        if (i == 1) { return car1; }
        if (i == 2) { return car2; }
        return car3;
    }

    expose func getTruck(Integer i) -> Truck {
        if (i == 0) { return truck0; }
        if (i == 1) { return truck1; }
        return truck2;
    }

    expose func getLog(Integer i) -> Log {
        if (i == 0) { return log0; }
        if (i == 1) { return log1; }
        if (i == 2) { return log2; }
        return log3;
    }

    expose func getTurtle(Integer i) -> Turtle {
        if (i == 0) { return turtle0; }
        if (i == 1) { return turtle1; }
        return turtle2;
    }

    expose func getHome(Integer i) -> HomeSlot {
        if (i == 0) { return home0; }
        if (i == 1) { return home1; }
        if (i == 2) { return home2; }
        if (i == 3) { return home3; }
        return home4;
    }

    // State queries
    expose func isRunning() -> Boolean {
        return running == 1;
    }

    expose func isPaused() -> Boolean {
        return paused == 1;
    }

    expose func hasWon() -> Boolean {
        return won == 1;
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

    expose func getTimeLeft() -> Integer {
        return timeLeft;
    }

    expose func getFrog() -> Frog {
        return frog;
    }
}

// Factory function
func createGame() -> Game {
    Game g = new Game();
    g.init();
    return g;
}
