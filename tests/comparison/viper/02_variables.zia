// Test: Variables and Constants
// Tests: var, final, type inference
module Test;

func main() {
    // Mutable variable
    var x: Integer = 10;
    Viper.Terminal.Say("Variable x: " + Viper.Fmt.Int(x));

    // Constants (final)
    final PI = 3.14159;
    final MAX_SIZE = 100;
    final GREETING = "Hello";
    Viper.Terminal.Say("Constant PI: " + Viper.Fmt.Num(PI));
    Viper.Terminal.Say("Constant MAX_SIZE: " + Viper.Fmt.Int(MAX_SIZE));
    Viper.Terminal.Say("Constant GREETING: " + GREETING);

    // Multiple variable declarations
    var a = 1;
    var b = 2;
    var c = 3;
    Viper.Terminal.Say("a, b, c: " + Viper.Fmt.Int(a) + ", " + Viper.Fmt.Int(b) + ", " + Viper.Fmt.Int(c));

    // No STATIC equivalent in ViperLang - would need entity field
    // Testing via closure instead
    var counter = 0;
    testClosure(counter);

    // Variable reassignment
    x = 20;
    Viper.Terminal.Say("Reassigned x: " + Viper.Fmt.Int(x));

    // Type inference works automatically
    var inferInt = 42;
    var inferFloat = 3.14;
    var inferStr = "test";
    Viper.Terminal.Say("Inferred int: " + Viper.Fmt.Int(inferInt));
    Viper.Terminal.Say("Inferred float: " + Viper.Fmt.Num(inferFloat));
    Viper.Terminal.Say("Inferred string: " + inferStr);

    Viper.Terminal.Say("=== Variables test complete ===");
}

func testClosure(counter: Integer) {
    // Note: ViperLang doesn't have STATIC variables
    // Static behavior would need to be achieved via entity fields
    Viper.Terminal.Say("ViperLang has no STATIC - use entity fields instead");
}
