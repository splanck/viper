// Countdown - ViperLang Demo
// Simple countdown animation
module Countdown;

func start() {
    Viper.Terminal.SetAltScreen(1);
    Viper.Terminal.SetCursorVisible(0);
    Viper.Terminal.Clear();

    var count = 10;
    while (count >= 0) {
        Viper.Terminal.BeginBatch();
        Viper.Terminal.Clear();

        // Draw big number
        Viper.Terminal.SetColor(3, 0);  // Cyan
        Viper.Terminal.SetPosition(10, 35);
        Viper.Terminal.SayInt(count);

        Viper.Terminal.SetColor(7, 0);  // White
        Viper.Terminal.SetPosition(15, 30);
        if (count == 0) {
            Viper.Terminal.Say("BLAST OFF!");
        } else {
            Viper.Terminal.Say("Counting down...");
        }

        Viper.Terminal.EndBatch();

        Viper.Time.SleepMs(500);
        count = count - 1;
    }

    Viper.Time.SleepMs(2000);
    Viper.Terminal.SetCursorVisible(1);
    Viper.Terminal.SetAltScreen(0);
}
