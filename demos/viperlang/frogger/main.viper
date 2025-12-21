module Frogger;

import "./config";
import "./colors";
import "./frog";
import "./vehicle";
import "./platform";
import "./home";
import "./powerup";
import "./renderer";
import "./game";

// Draw the complete game board
func drawBoard(Game game) {
    // Title and status bar
    drawTitleBar(game.score, game.frog.getLives(), game.level);

    // Home row
    drawHomeRow(game.home0, game.home1, game.home2, game.home3, game.home4);

    // River section
    drawRiver();

    // Draw platforms on river
    var i = 0;
    while (i < MAX_PLATFORMS) {
        Platform p = game.getPlatform(i);
        drawPlatform(p);
        i = i + 1;
    }

    // Safe zone
    drawSafeZone();

    // Road section
    drawRoad();

    // Draw vehicles on road
    i = 0;
    while (i < MAX_VEHICLES) {
        Vehicle v = game.getVehicle(i);
        drawVehicle(v);
        i = i + 1;
    }

    // Start area
    drawStartArea();

    // Draw power-ups
    i = 0;
    while (i < MAX_POWERUPS) {
        PowerUp p = game.getPowerUp(i);
        drawPowerUp(p);
        i = i + 1;
    }

    // Draw frog
    drawFrog(game.frog);

    // Instructions
    drawInstructions();

    // Pause overlay if needed
    if (game.isPaused()) {
        drawPauseOverlay();
    }
}

// Wait for any key press
func waitForKey() {
    while (!Viper.Terminal.HasKey()) {
        Viper.Terminal.Sleep(50);
    }
    String key = Viper.Terminal.ReadKey();
}

// Main game entry point
func start() {
    // Initialize terminal
    Viper.Terminal.Clear();
    Viper.Terminal.HideCursor();

    // Show title screen
    drawTitleScreen();
    waitForKey();

    // Create and initialize game
    Game game = createGame();

    // Clear screen for gameplay
    Viper.Terminal.Clear();

    // Main game loop
    while (!game.isGameOver()) {
        // Handle input
        if (Viper.Terminal.HasKey()) {
            String key = Viper.Terminal.ReadKey();
            game.handleInput(key);
        }

        // Update game state
        game.update();

        // Draw everything
        drawBoard(game);

        // Frame delay
        Viper.Terminal.Sleep(FRAME_DELAY);
    }

    // Show game over screen
    drawGameOver(game.hasWon(), game.getScore(), game.getHomesFilled());
    waitForKey();

    // Cleanup
    Viper.Terminal.ShowCursor();
    Viper.Terminal.Clear();
    Viper.Terminal.Say("Thanks for playing Frogger!");
}
