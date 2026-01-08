// Test: Math Functions
// Tests: Viper.Math.* functions
module Test;

func main() {
    Viper.Terminal.Say("=== Math Functions Test ===");

    // Abs
    Viper.Terminal.Say("");
    Viper.Terminal.Say("--- Abs ---");
    Viper.Terminal.Print("Abs(-5): ");
    Viper.Terminal.SayNum(Viper.Math.Abs(-5.0));

    // Sqrt
    Viper.Terminal.Say("");
    Viper.Terminal.Say("--- Sqrt ---");
    Viper.Terminal.Print("Sqrt(16): ");
    Viper.Terminal.SayNum(Viper.Math.Sqrt(16.0));

    // Pow
    Viper.Terminal.Say("");
    Viper.Terminal.Say("--- Pow ---");
    Viper.Terminal.Print("Pow(2, 8): ");
    Viper.Terminal.SayNum(Viper.Math.Pow(2.0, 8.0));

    // Trig
    Viper.Terminal.Say("");
    Viper.Terminal.Say("--- Trigonometry ---");
    Viper.Terminal.Print("Sin(0): ");
    Viper.Terminal.SayNum(Viper.Math.Sin(0.0));
    Viper.Terminal.Print("Cos(0): ");
    Viper.Terminal.SayNum(Viper.Math.Cos(0.0));

    // Floor/Ceil
    Viper.Terminal.Say("");
    Viper.Terminal.Say("--- Floor/Ceil ---");
    Viper.Terminal.Print("Floor(3.7): ");
    Viper.Terminal.SayNum(Viper.Math.Floor(3.7));
    Viper.Terminal.Print("Ceil(3.2): ");
    Viper.Terminal.SayNum(Viper.Math.Ceil(3.2));

    // Min/Max
    Viper.Terminal.Say("");
    Viper.Terminal.Say("--- Min/Max ---");
    Viper.Terminal.Print("Min(5, 3): ");
    Viper.Terminal.SayNum(Viper.Math.Min(5.0, 3.0));
    Viper.Terminal.Print("Max(5, 3): ");
    Viper.Terminal.SayNum(Viper.Math.Max(5.0, 3.0));

    Viper.Terminal.Say("");
    Viper.Terminal.Say("=== Math Functions test complete ===");
}
