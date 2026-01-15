// Test: Operators
// Tests: arithmetic, comparison, logical, string
module Test;

func main() {
    // Arithmetic operators
    var a = 10;
    var b = 3;

    Viper.Terminal.Say("Arithmetic:");
    Viper.Terminal.Say("  10 + 3 = " + Viper.Fmt.Int(a + b));
    Viper.Terminal.Say("  10 - 3 = " + Viper.Fmt.Int(a - b));
    Viper.Terminal.Say("  10 * 3 = " + Viper.Fmt.Int(a * b));
    Viper.Terminal.Say("  10 / 3 = " + Viper.Fmt.Int(a / b));
    Viper.Terminal.Say("  10 % 3 = " + Viper.Fmt.Int(a % b));
    // Note: No integer division operator and no power operator in ViperLang

    // Comparison operators
    Viper.Terminal.Say("");
    Viper.Terminal.Say("Comparison (5 vs 3):");
    Viper.Terminal.Say("  5 == 3: " + Viper.Fmt.Bool(5 == 3));
    Viper.Terminal.Say("  5 != 3: " + Viper.Fmt.Bool(5 != 3));
    Viper.Terminal.Say("  5 < 3: " + Viper.Fmt.Bool(5 < 3));
    Viper.Terminal.Say("  5 > 3: " + Viper.Fmt.Bool(5 > 3));
    Viper.Terminal.Say("  5 <= 3: " + Viper.Fmt.Bool(5 <= 3));
    Viper.Terminal.Say("  5 >= 3: " + Viper.Fmt.Bool(5 >= 3));
    Viper.Terminal.Say("  5 == 5: " + Viper.Fmt.Bool(5 == 5));

    // Logical operators
    Viper.Terminal.Say("");
    Viper.Terminal.Say("Logical:");
    Viper.Terminal.Say("  true && false: " + Viper.Fmt.Bool(true && false));
    Viper.Terminal.Say("  true || false: " + Viper.Fmt.Bool(true || false));
    Viper.Terminal.Say("  !true: " + Viper.Fmt.Bool(!true));
    Viper.Terminal.Say("  !false: " + Viper.Fmt.Bool(!false));

    // Bitwise operators
    Viper.Terminal.Say("");
    Viper.Terminal.Say("Bitwise:");
    Viper.Terminal.Say("  12 & 10 = " + Viper.Fmt.Int(12 & 10));
    Viper.Terminal.Say("  12 | 10 = " + Viper.Fmt.Int(12 | 10));
    Viper.Terminal.Say("  12 ^ 10 = " + Viper.Fmt.Int(12 ^ 10));

    // String concatenation
    var s1 = "Hello";
    var s2 = "World";
    Viper.Terminal.Say("");
    Viper.Terminal.Say("String concat:");
    Viper.Terminal.Say("  Hello + World = " + s1 + " " + s2);

    // String interpolation - test syntax
    Viper.Terminal.Say("");
    Viper.Terminal.Say("String interpolation:");
    var name = "Alice";
    var age = 30;
    // Test if interpolation works
    var msg = "Hello " + name + ", age " + Viper.Fmt.Int(age);
    Viper.Terminal.Say("  Using concat: " + msg);

    // Operator precedence
    Viper.Terminal.Say("");
    Viper.Terminal.Say("Precedence:");
    Viper.Terminal.Say("  2 + 3 * 4 = " + Viper.Fmt.Int(2 + 3 * 4));
    Viper.Terminal.Say("  (2 + 3) * 4 = " + Viper.Fmt.Int((2 + 3) * 4));

    // Unary operators
    Viper.Terminal.Say("");
    Viper.Terminal.Say("Unary:");
    var x = 5;
    Viper.Terminal.Say("  -5 = " + Viper.Fmt.Int(-x));

    // Null coalescing (ViperLang specific)
    Viper.Terminal.Say("");
    Viper.Terminal.Say("Null operators:");
    var maybeNull: String? = null;
    var result = maybeNull ?? "default";
    Viper.Terminal.Say("  null ?? default = " + result);

    Viper.Terminal.Say("=== Operators test complete ===");
}
