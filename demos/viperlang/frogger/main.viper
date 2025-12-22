module Frogger;

import "./config";
import "./colors";
import "./frog";
import "./car";
import "./truck";
import "./log";
import "./turtle";
import "./home";
import "./renderer";
import "./game";

// =============================================================================
// Frogger - Main Entry Point
// A classic arcade game implemented in ViperLang
// =============================================================================

// Draw all game entities
func drawGame(Game game) {
    // Draw background layers (back to front)
    drawHomeRow();
    drawRiver();
    drawSafeZone();
    drawRoad();
    drawStartArea();

    // Draw home slots
    var i = 0;
    while (i < MAX_HOMES) {
        HomeSlot h = game.getHome(i);
        drawHomeSlot(h);
        i = i + 1;
    }

    // Draw platforms (logs and turtles)
    i = 0;
    while (i < 4) {
        Log l = game.getLog(i);
        drawLog(l);
        i = i + 1;
    }

    i = 0;
    while (i < 3) {
        Turtle t = game.getTurtle(i);
        drawTurtle(t);
        i = i + 1;
    }

    // Draw vehicles
    i = 0;
    while (i < NUM_CARS) {
        Car c = game.getCar(i);
        drawCar(c);
        i = i + 1;
    }

    i = 0;
    while (i < NUM_TRUCKS) {
        Truck t = game.getTruck(i);
        drawTruck(t);
        i = i + 1;
    }

    // Draw frog
    Frog f = game.getFrog();
    drawFrog(f);

    // Draw HUD
    drawHUD(game.getScore(), f.getLives(), game.getLevel(), game.getTimeLeft());

    // Draw instructions
    drawInstructions();

    // Draw pause overlay if paused
    if (game.isPaused()) {
        drawPauseOverlay();
    }
}

// Wait for any key press (blocking)
func waitForKey() {
    String key = Viper.Terminal.GetKey();
}

// Main entry point
func start() {
    // Initialize terminal
    clearScreen();
    hideCursor();

    // Show title screen
    drawTitleScreen();
    waitForKey();

    // Create and initialize game
    Game game = createGame();

    // Clear for gameplay
    clearScreen();

    // Main game loop
    while (game.isRunning()) {
        // Handle input (using timeout - 1ms)
        String key = Viper.Terminal.GetKeyTimeout(1);
        game.handleInput(key);

        // Update game state
        game.update();

        // Draw everything
        drawGame(game);

        // Frame delay
        Viper.Time.SleepMs(FRAME_DELAY);
    }

    // Show game over screen
    drawGameOver(game.hasWon(), game.getScore(), game.getHomesFilled());
    waitForKey();

    // Cleanup
    showCursor();
    clearScreen();
    Viper.Terminal.Say("Thanks for playing Frogger!");
}
