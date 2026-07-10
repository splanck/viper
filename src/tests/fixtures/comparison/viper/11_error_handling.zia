// Test: Error Handling
// Tests: guard statement, optional types
module Test;

func divide(a: Number, b: Number) -> Number {
    guard (b != 0.0) else {
        Viper.Terminal.Say("Error: Division by zero!");
        return 0.0;
    }
    return a / b;
}

func main() {
    Viper.Terminal.Say("=== Error Handling Test ===");

    // Test guard statement
    Viper.Terminal.Say("");
    Viper.Terminal.Say("--- Guard Statement ---");
    Viper.Terminal.Print("10 / 2 = ");
    Viper.Terminal.SayNum(divide(10.0, 2.0));
    Viper.Terminal.Print("10 / 0 = ");
    Viper.Terminal.SayNum(divide(10.0, 0.0));

    Viper.Terminal.Say("");
    Viper.Terminal.Say("ViperLang: No try/catch (use guard or Result types)");

    Viper.Terminal.Say("");
    Viper.Terminal.Say("=== Error Handling test complete ===");
}
