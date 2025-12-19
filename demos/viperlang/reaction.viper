// Reaction Time Game - ViperLang Demo
// Test your reaction speed!
module Reaction;

func start() {
    Viper.Terminal.SetAltScreen(1);
    Viper.Terminal.SetCursorVisible(0);
    Viper.Terminal.Clear();

    var playing = 1;
    var round = 0;

    Viper.Terminal.SetColor(3, 0);
    Viper.Terminal.SetPosition(10, 25);
    Viper.Terminal.Say("=== REACTION TIME TEST ===");
    Viper.Terminal.SetColor(7, 0);
    Viper.Terminal.SetPosition(12, 20);
    Viper.Terminal.Say("Press SPACE as fast as you can when");
    Viper.Terminal.SetPosition(13, 20);
    Viper.Terminal.Say("the screen turns GREEN!");
    Viper.Terminal.SetPosition(15, 25);
    Viper.Terminal.Say("Press any key to start...");
    Viper.Terminal.GetKey();

    while (playing == 1) {
        round = round + 1;

        // Wait phase - red screen
        Viper.Terminal.BeginBatch();
        Viper.Terminal.SetColor(1, 0);  // Red
        Viper.Terminal.Clear();
        Viper.Terminal.SetPosition(10, 30);
        Viper.Terminal.Say("WAIT...");
        Viper.Terminal.SetPosition(12, 25);
        Viper.Terminal.Print("Round: ");
        Viper.Terminal.SayInt(round);
        Viper.Terminal.EndBatch();

        // Variable delay (simple pattern instead of random)
        var delay = 1000 + (round * 200);
        if (delay > 3000) {
            delay = 3000;
        }
        Viper.Time.SleepMs(delay);

        // Check if they pressed too early
        var earlyKey = Viper.Terminal.InKey();
        if (earlyKey != "") {
            Viper.Terminal.BeginBatch();
            Viper.Terminal.SetColor(1, 0);
            Viper.Terminal.Clear();
            Viper.Terminal.SetPosition(10, 25);
            Viper.Terminal.Say("TOO EARLY! You pressed too soon.");
            Viper.Terminal.SetPosition(12, 25);
            Viper.Terminal.Say("Press any key to try again...");
            Viper.Terminal.EndBatch();
            Viper.Terminal.GetKey();
        } else {
            // Go phase - green screen
            Viper.Terminal.BeginBatch();
            Viper.Terminal.SetColor(2, 0);  // Green
            Viper.Terminal.Clear();
            Viper.Terminal.SetPosition(10, 30);
            Viper.Terminal.Say("GO!");
            Viper.Terminal.EndBatch();

            var startTime = Viper.Time.Clock.Millis();
            Viper.Terminal.GetKey();
            var endTime = Viper.Time.Clock.Millis();
            var reaction = endTime - startTime;

            // Show result
            Viper.Terminal.BeginBatch();
            Viper.Terminal.SetColor(7, 0);
            Viper.Terminal.Clear();
            Viper.Terminal.SetPosition(10, 25);
            Viper.Terminal.Print("Reaction time: ");
            Viper.Terminal.PrintInt(reaction);
            Viper.Terminal.Say(" ms");

            if (reaction < 200) {
                Viper.Terminal.SetColor(2, 0);
                Viper.Terminal.SetPosition(12, 30);
                Viper.Terminal.Say("EXCELLENT!");
            } else {
                if (reaction < 300) {
                    Viper.Terminal.SetColor(3, 0);
                    Viper.Terminal.SetPosition(12, 32);
                    Viper.Terminal.Say("GOOD!");
                } else {
                    Viper.Terminal.SetColor(1, 0);
                    Viper.Terminal.SetPosition(12, 28);
                    Viper.Terminal.Say("KEEP TRYING!");
                }
            }

            Viper.Terminal.SetColor(7, 0);
            Viper.Terminal.SetPosition(15, 20);
            Viper.Terminal.Say("Press SPACE to continue, Q to quit");
            Viper.Terminal.EndBatch();

            var choice = Viper.Terminal.GetKey();
            if (choice == "q") {
                playing = 0;
            }
        }
    }

    Viper.Terminal.SetCursorVisible(1);
    Viper.Terminal.SetAltScreen(0);
    Viper.Terminal.Say("Thanks for playing!");
}
