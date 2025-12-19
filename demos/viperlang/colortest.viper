// Color Test - ViperLang Demo
// Displays all terminal colors
module ColorTest;

func start() {
    Viper.Terminal.SetAltScreen(1);
    Viper.Terminal.SetCursorVisible(0);
    Viper.Terminal.Clear();

    Viper.Terminal.SetColor(7, 0);
    Viper.Terminal.SetPosition(1, 25);
    Viper.Terminal.Say("=== TERMINAL COLOR TEST ===");
    Viper.Terminal.SetPosition(3, 5);
    Viper.Terminal.Say("Standard colors (0-7):");

    // Draw standard colors
    var col = 0;
    while (col < 8) {
        Viper.Terminal.SetColor(col, 0);
        Viper.Terminal.SetPosition(5, 5 + col * 8);
        Viper.Terminal.Print("  ");
        Viper.Terminal.PrintInt(col);
        Viper.Terminal.Print("   ");
        col = col + 1;
    }

    // Draw color blocks
    col = 0;
    while (col < 8) {
        Viper.Terminal.SetColor(0, col);
        Viper.Terminal.SetPosition(6, 5 + col * 8);
        Viper.Terminal.Print("      ");
        Viper.Terminal.SetPosition(7, 5 + col * 8);
        Viper.Terminal.Print("      ");
        col = col + 1;
    }

    Viper.Terminal.SetColor(7, 0);
    Viper.Terminal.SetPosition(10, 5);
    Viper.Terminal.Say("Bright colors (8-15):");

    col = 8;
    while (col < 16) {
        Viper.Terminal.SetColor(col, 0);
        Viper.Terminal.SetPosition(12, 5 + (col - 8) * 8);
        Viper.Terminal.Print("  ");
        Viper.Terminal.PrintInt(col);
        Viper.Terminal.Print("  ");
        col = col + 1;
    }

    col = 8;
    while (col < 16) {
        Viper.Terminal.SetColor(0, col);
        Viper.Terminal.SetPosition(13, 5 + (col - 8) * 8);
        Viper.Terminal.Print("      ");
        Viper.Terminal.SetPosition(14, 5 + (col - 8) * 8);
        Viper.Terminal.Print("      ");
        col = col + 1;
    }

    // Color name reference
    Viper.Terminal.SetColor(7, 0);
    Viper.Terminal.SetPosition(17, 5);
    Viper.Terminal.Say("Color Reference:");
    Viper.Terminal.SetPosition(18, 5);
    Viper.Terminal.SetColor(0, 0);
    Viper.Terminal.Print("0=Black ");
    Viper.Terminal.SetColor(1, 0);
    Viper.Terminal.Print("1=Red ");
    Viper.Terminal.SetColor(2, 0);
    Viper.Terminal.Print("2=Green ");
    Viper.Terminal.SetColor(3, 0);
    Viper.Terminal.Print("3=Yellow ");
    Viper.Terminal.SetColor(4, 0);
    Viper.Terminal.Print("4=Blue ");
    Viper.Terminal.SetColor(5, 0);
    Viper.Terminal.Print("5=Magenta ");
    Viper.Terminal.SetColor(6, 0);
    Viper.Terminal.Print("6=Cyan ");
    Viper.Terminal.SetColor(7, 0);
    Viper.Terminal.Say("7=White");

    Viper.Terminal.SetPosition(22, 20);
    Viper.Terminal.Say("Press any key to exit...");

    Viper.Terminal.GetKey();

    Viper.Terminal.SetCursorVisible(1);
    Viper.Terminal.SetAltScreen(0);
}
