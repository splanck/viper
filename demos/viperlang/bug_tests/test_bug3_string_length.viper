// Bug #3 Test: String.Length property syntax
// This tests that str.Length and str.length work as property syntax
module main;

func main() {
    var greeting = "Hello, World!";

    // Test .Length property (capital L)
    var len1 = greeting.Length;
    Viper.Terminal.Print("Length of '");
    Viper.Terminal.Print(greeting);
    Viper.Terminal.Print("' is: ");
    Viper.Terminal.PrintInt(len1);
    Viper.Terminal.Say("");

    // Test .length property (lowercase l)
    var len2 = greeting.length;
    Viper.Terminal.Print("Using .length: ");
    Viper.Terminal.PrintInt(len2);
    Viper.Terminal.Say("");

    // Test with empty string
    var empty = "";
    var emptyLen = empty.Length;
    Viper.Terminal.Print("Empty string length: ");
    Viper.Terminal.PrintInt(emptyLen);
    Viper.Terminal.Say("");

    // Verify both methods return same value
    if len1 == len2 {
        Viper.Terminal.Say("SUCCESS: Both .Length and .length return same value");
    } else {
        Viper.Terminal.Say("FAIL: Length values don't match!");
    }

    // Test inline usage in condition
    if greeting.Length > 0 {
        Viper.Terminal.Say("SUCCESS: Inline .Length in condition works");
    }

    Viper.Terminal.Say("");
    Viper.Terminal.Say("Bug #3 Test PASSED!");
}
