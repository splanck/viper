// Test: I/O & Console
// Tests: Terminal output functions
module Test;

func main() {
    Viper.Terminal.Say("=== I/O Console Test ===");

    // Test Say (print with newline)
    Viper.Terminal.Say("");
    Viper.Terminal.Say("--- Say (with newline) ---");
    Viper.Terminal.Say("Hello, World!");

    // Test Print (without newline)
    Viper.Terminal.Say("");
    Viper.Terminal.Say("--- Print (no newline) ---");
    Viper.Terminal.Print("Part 1... ");
    Viper.Terminal.Print("Part 2... ");
    Viper.Terminal.Say("Done!");

    // Test numeric output
    Viper.Terminal.Say("");
    Viper.Terminal.Say("--- Numeric Output ---");
    Viper.Terminal.Print("Integer: ");
    Viper.Terminal.SayInt(42);
    Viper.Terminal.Print("Number: ");
    Viper.Terminal.SayNum(3.14159);

    // Test formatted numbers
    Viper.Terminal.Say("");
    Viper.Terminal.Say("--- Formatted Output ---");
    Viper.Terminal.Say("Formatted int: " + Viper.Fmt.Int(42));
    Viper.Terminal.Say("Formatted num: " + Viper.Fmt.Num(3.14159));
    Viper.Terminal.Say("Formatted bool: " + Viper.Fmt.Bool(true));

    Viper.Terminal.Say("");
    Viper.Terminal.Say("=== I/O Console test complete ===");
}
