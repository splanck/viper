// Test: Primitive Types and Literals
// Tests: integers, floats, strings, booleans
module Test;

func main() {
    // Integer
    var i: Integer = 42;
    Viper.Terminal.Say("Integer: " + Viper.Fmt.Int(i));

    // Large integer
    var lng: Integer = 1000000;
    Viper.Terminal.Say("Long: " + Viper.Fmt.Int(lng));

    // Number (float)
    var sng: Number = 3.14;
    Viper.Terminal.Say("Single: " + Viper.Fmt.Num(sng));

    var dbl: Number = 3.14159265358979;
    Viper.Terminal.Say("Double: " + Viper.Fmt.Num(dbl));

    // String
    var s: String = "Hello, World!";
    Viper.Terminal.Say("String: " + s);

    // Boolean
    var b: Boolean = true;
    Viper.Terminal.Say("Boolean TRUE: " + Viper.Fmt.Bool(b));
    b = false;
    Viper.Terminal.Say("Boolean FALSE: " + Viper.Fmt.Bool(b));

    // Type inference
    var x = 100;
    Viper.Terminal.Say("Inferred int: " + Viper.Fmt.Int(x));

    var y = 2.718;
    Viper.Terminal.Say("Inferred float: " + Viper.Fmt.Num(y));

    var z = "inferred string";
    Viper.Terminal.Say("Inferred string: " + z);

    // Hex and binary literals (ViperLang specific)
    var hex = 0xFF;
    Viper.Terminal.Say("Hex 0xFF: " + Viper.Fmt.Int(hex));

    var bin = 0b1010;
    Viper.Terminal.Say("Binary 0b1010: " + Viper.Fmt.Int(bin));

    // Byte type
    var bt: Byte = 255;
    Viper.Terminal.Say("Byte: " + Viper.Fmt.Int(bt));

    Viper.Terminal.Say("=== Primitives test complete ===");
}
