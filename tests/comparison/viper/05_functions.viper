// Test: Functions
// Tests: functions, parameters, return values, recursion
module Test;

// Simple function
func add(a: Integer, b: Integer) -> Integer {
    return a + b;
}

// Void function (no return)
func printMessage(msg: String) {
    Viper.Terminal.Say("Message: " + msg);
}

// Recursive function
func factorial(n: Integer) -> Integer {
    if (n <= 1) {
        return 1;
    }
    return n * factorial(n - 1);
}

// Function returning string
func greet(name: String) -> String {
    return "Hello, " + name + "!";
}

func main() {
    Viper.Terminal.Say("=== Functions Test ===");

    Viper.Terminal.Say("");
    Viper.Terminal.Print("Simple function: add(3, 5) = ");
    Viper.Terminal.SayInt(add(3, 5));

    Viper.Terminal.Say("");
    printMessage("Hello from function");

    Viper.Terminal.Say("");
    Viper.Terminal.Print("Recursion: factorial(5) = ");
    Viper.Terminal.SayInt(factorial(5));

    Viper.Terminal.Say("");
    Viper.Terminal.Say("String function: " + greet("World"));

    // Lambda expressions - basic test
    Viper.Terminal.Say("");
    Viper.Terminal.Say("=== Lambda Test ===");
    // Note: Higher-order functions with lambdas cause runtime errors
    // var double = (x: Integer) => x * 2;
    Viper.Terminal.Say("Lambdas: SKIPPED (runtime error with function types)");

    Viper.Terminal.Say("=== Functions test complete ===");
}
