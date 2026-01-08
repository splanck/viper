// Test: Control Flow
// Tests: if, for, while, break, continue
// NOTE: String concat with Viper.Fmt.Int in loops causes crash (BUG)
module Test;

func main() {
    // IF statement
    Viper.Terminal.Say("=== IF Statement ===");
    var x = 5;

    if (x > 10) {
        Viper.Terminal.Say("x > 10");
    } else if (x > 3) {
        Viper.Terminal.Say("x > 3 but <= 10");
    } else {
        Viper.Terminal.Say("x <= 3");
    }

    // Simple if
    if (x == 5) {
        Viper.Terminal.Say("x is 5");
    }

    // FOR loop (using SayInt to avoid concat bug)
    Viper.Terminal.Say("");
    Viper.Terminal.Say("=== FOR Loop ===");
    for (var i = 1; i <= 5; i = i + 1) {
        Viper.Terminal.Print("  i = ");
        Viper.Terminal.SayInt(i);
    }

    // FOR with decrement
    Viper.Terminal.Say("FOR with decrement:");
    for (var i = 10; i >= 2; i = i - 2) {
        Viper.Terminal.Print("  i = ");
        Viper.Terminal.SayInt(i);
    }

    // WHILE loop
    Viper.Terminal.Say("");
    Viper.Terminal.Say("=== WHILE Loop ===");
    var n = 1;
    while (n <= 3) {
        Viper.Terminal.Print("  n = ");
        Viper.Terminal.SayInt(n);
        n = n + 1;
    }

    // BREAK
    Viper.Terminal.Say("");
    Viper.Terminal.Say("=== BREAK ===");
    for (var i = 1; i <= 10; i = i + 1) {
        if (i == 5) {
            break;
        }
        Viper.Terminal.Print("  i = ");
        Viper.Terminal.SayInt(i);
    }
    Viper.Terminal.Say("Exited with break");

    // CONTINUE
    Viper.Terminal.Say("");
    Viper.Terminal.Say("=== CONTINUE ===");
    for (var i = 1; i <= 5; i = i + 1) {
        if (i == 3) {
            continue;
        }
        Viper.Terminal.Print("  i = ");
        Viper.Terminal.SayInt(i);
    }
    Viper.Terminal.Say("Skipped i=3");

    // Nested loops
    Viper.Terminal.Say("");
    Viper.Terminal.Say("=== Nested Loops ===");
    for (var i = 1; i <= 2; i = i + 1) {
        for (var j = 1; j <= 2; j = j + 1) {
            Viper.Terminal.Print("  (");
            Viper.Terminal.Print(Viper.Fmt.Int(i));
            Viper.Terminal.Print(",");
            Viper.Terminal.Print(Viper.Fmt.Int(j));
            Viper.Terminal.Say(")");
        }
    }

    Viper.Terminal.Say("=== Control flow test complete ===");
}
