// Bouncing Ball - ViperLang Demo
// Demonstrates terminal graphics with animation
module Bounce;

func start() {
    // Setup terminal
    Viper.Terminal.SetAltScreen(1);
    Viper.Terminal.SetCursorVisible(0);
    Viper.Terminal.Clear();

    var x = 10;
    var y = 5;
    var dx = 1;
    var dy = 1;
    var running = 1;

    while (running == 1) {
        Viper.Terminal.BeginBatch();

        // Clear old position
        Viper.Terminal.SetPosition(y, x);
        Viper.Terminal.Print(" ");

        // Update position
        x = x + dx;
        y = y + dy;

        // Bounce off walls (assume 80x24 terminal)
        if (x <= 1) {
            dx = 1;
            x = 2;
        }
        if (x >= 78) {
            dx = 0 - 1;
            x = 77;
        }
        if (y <= 1) {
            dy = 1;
            y = 2;
        }
        if (y >= 22) {
            dy = 0 - 1;
            y = 21;
        }

        // Draw new position
        Viper.Terminal.SetColor(2, 0);
        Viper.Terminal.SetPosition(y, x);
        Viper.Terminal.Print("O");

        // Draw border
        Viper.Terminal.SetColor(7, 0);
        Viper.Terminal.SetPosition(24, 1);
        Viper.Terminal.Print("Press 'q' to quit");

        Viper.Terminal.EndBatch();

        // Check for quit
        var key = Viper.Terminal.GetKeyTimeout(50);
        if (key == "q") {
            running = 0;
        }
    }

    // Cleanup
    Viper.Terminal.SetCursorVisible(1);
    Viper.Terminal.SetAltScreen(0);
    Viper.Terminal.Say("Goodbye!");
}
