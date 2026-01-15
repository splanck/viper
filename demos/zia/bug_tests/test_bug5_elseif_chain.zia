// Bug #5 Test: else-if chain code generation with String return
// This tests that else-if chains correctly return String values
// Previously this would fail with "ret value type mismatch: expected str but got i64"
module main;

// Function with else-if chain returning String
func getColorName(code: String) -> String {
    if code == "R" {
        return "Red";
    } else if code == "G" {
        return "Green";
    } else if code == "B" {
        return "Blue";
    } else if code == "Y" {
        return "Yellow";
    } else if code == "O" {
        return "Orange";
    } else if code == "P" {
        return "Purple";
    } else {
        return "Unknown";
    }
}

// Function with deeply nested else-if chain
func getDayName(num: Integer) -> String {
    if num == 1 {
        return "Monday";
    } else if num == 2 {
        return "Tuesday";
    } else if num == 3 {
        return "Wednesday";
    } else if num == 4 {
        return "Thursday";
    } else if num == 5 {
        return "Friday";
    } else if num == 6 {
        return "Saturday";
    } else if num == 7 {
        return "Sunday";
    } else {
        return "Invalid day";
    }
}

// Function that may not hit any return in branches (tests implicit return)
func maybeReturn(value: Integer) -> String {
    if value > 100 {
        return "Large";
    } else if value > 50 {
        return "Medium";
    } else if value > 0 {
        return "Small";
    }
    // Implicit return path for value <= 0
    // Bug #5 fix ensures this returns "" (empty string) not 0 (integer)
}

func main() {
    Viper.Terminal.Say("Testing Bug #5: else-if chain with String return");
    Viper.Terminal.Say("================================================");
    Viper.Terminal.Say("");

    // Test color function
    Viper.Terminal.Print("Color R: ");
    Viper.Terminal.Say(getColorName("R"));

    Viper.Terminal.Print("Color G: ");
    Viper.Terminal.Say(getColorName("G"));

    Viper.Terminal.Print("Color B: ");
    Viper.Terminal.Say(getColorName("B"));

    Viper.Terminal.Print("Color X: ");
    Viper.Terminal.Say(getColorName("X"));

    Viper.Terminal.Say("");

    // Test day function
    Viper.Terminal.Print("Day 1: ");
    Viper.Terminal.Say(getDayName(1));

    Viper.Terminal.Print("Day 5: ");
    Viper.Terminal.Say(getDayName(5));

    Viper.Terminal.Print("Day 7: ");
    Viper.Terminal.Say(getDayName(7));

    Viper.Terminal.Print("Day 99: ");
    Viper.Terminal.Say(getDayName(99));

    Viper.Terminal.Say("");

    // Test implicit return path
    Viper.Terminal.Print("maybeReturn(150): ");
    Viper.Terminal.Say(maybeReturn(150));

    Viper.Terminal.Print("maybeReturn(75): ");
    Viper.Terminal.Say(maybeReturn(75));

    Viper.Terminal.Print("maybeReturn(25): ");
    Viper.Terminal.Say(maybeReturn(25));

    Viper.Terminal.Print("maybeReturn(-5): '");
    Viper.Terminal.Print(maybeReturn(-5));
    Viper.Terminal.Say("' (should be empty)");

    Viper.Terminal.Say("");
    Viper.Terminal.Say("Bug #5 Test PASSED!");
}
