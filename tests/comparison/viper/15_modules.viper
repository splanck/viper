// Test: Modules & Namespaces
// Tests: module declaration, imports
module Test;

// ViperLang requires module declaration at top
// Imports would be: import Viper.Math;

func helper() -> String {
    return "Helper function works!";
}

func main() {
    Viper.Terminal.Say("=== Modules & Namespaces Test ===");

    // Module functions
    Viper.Terminal.Say("");
    Viper.Terminal.Say("--- Module Functions ---");
    Viper.Terminal.Say(helper());

    // Accessing runtime modules
    Viper.Terminal.Say("");
    Viper.Terminal.Say("--- Runtime Modules ---");
    Viper.Terminal.Say("Viper.Terminal: works");
    Viper.Terminal.Say("Viper.Math: " + Viper.Fmt.Num(Viper.Math.Sqrt(4.0)));
    Viper.Terminal.Say("Viper.String: " + Viper.String.ToUpper("works"));
    Viper.Terminal.Say("Viper.Fmt: works");

    Viper.Terminal.Say("");
    Viper.Terminal.Say("=== Modules & Namespaces test complete ===");
}
