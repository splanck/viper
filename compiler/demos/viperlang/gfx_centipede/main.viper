// main.viper - Entry point for Centipede game
module main;

import "./config";
import "./game";

func main() {
    // Create game window
    var canvas = new Viper.Graphics.Canvas(config.TITLE, config.SCREEN_WIDTH, config.SCREEN_HEIGHT);

    // Initialize game
    game.initGame();

    // Main game loop
    var running = true;
    while (running) {
        // Poll for events
        canvas.Poll();

        // Check if window should close
        if (canvas.ShouldClose != 0) {
            running = false;
        }

        // Check for quit (ESC = 27)
        if (canvas.KeyHeld(27) != 0) {
            running = false;
        }

        // Check for pause (P = 112)
        if (canvas.KeyHeld(112) != 0) {
            game.togglePause();
            Viper.Time.SleepMs(200);
        }

        // Update game state
        game.update(canvas);

        // Render
        game.draw(canvas);

        // Frame timing - minimal delay
        Viper.Time.SleepMs(2);
    }
}
