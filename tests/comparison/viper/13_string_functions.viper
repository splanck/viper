// Test: String Functions
// Tests: Viper.String.* functions
module Test;

func main() {
    Viper.Terminal.Say("=== String Functions Test ===");
    var s = "Hello, World!";

    // Length
    Viper.Terminal.Say("");
    Viper.Terminal.Say("--- Length ---");
    Viper.Terminal.Print("Length of '" + s + "': ");
    Viper.Terminal.SayInt(Viper.String.Length(s));

    // Substring
    Viper.Terminal.Say("");
    Viper.Terminal.Say("--- Substring ---");
    Viper.Terminal.Say("Substring(0, 5): " + Viper.String.Substring(s, 0, 5));

    // Case conversion
    Viper.Terminal.Say("");
    Viper.Terminal.Say("--- Case Conversion ---");
    Viper.Terminal.Say("ToUpper: " + Viper.String.ToUpper(s));
    Viper.Terminal.Say("ToLower: " + Viper.String.ToLower(s));

    // Trim
    Viper.Terminal.Say("");
    Viper.Terminal.Say("--- Trim ---");
    var padded = "  spaces  ";
    Viper.Terminal.Say("Trim '  spaces  ': '" + Viper.String.Trim(padded) + "'");

    // IndexOf
    Viper.Terminal.Say("");
    Viper.Terminal.Say("--- IndexOf ---");
    Viper.Terminal.Print("IndexOf 'World': ");
    Viper.Terminal.SayInt(Viper.String.IndexOf(s, "World"));

    // Char/Asc
    Viper.Terminal.Say("");
    Viper.Terminal.Say("--- Char/Asc ---");
    Viper.Terminal.Say("Chr(65): " + Viper.String.Chr(65));
    Viper.Terminal.Print("Asc('A'): ");
    Viper.Terminal.SayInt(Viper.String.Asc("A"));

    Viper.Terminal.Say("");
    Viper.Terminal.Say("=== String Functions test complete ===");
}
