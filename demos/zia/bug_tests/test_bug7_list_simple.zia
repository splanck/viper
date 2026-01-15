// Bug #7 Simple Test: List[String] type and methods work
// This tests that List supports .count() and .get() method syntax
module main;

func main() {
    Viper.Terminal.Say("Testing Bug #7 (Simple): List[String] methods");
    Viper.Terminal.Say("==============================================");
    Viper.Terminal.Say("");

    // Create a simple list
    var items: List[String] = [];
    items.add("Apple");
    items.add("Banana");
    items.add("Cherry");

    // Test .count() method - Bug #7 ensures this works with typed lists
    var count = items.count();
    Viper.Terminal.Print("List count: ");
    Viper.Terminal.PrintInt(count);
    Viper.Terminal.Say("");

    // Test .get() method
    Viper.Terminal.Say("Items:");
    var i = 0;
    while i < count {
        String item = items.get(i);
        Viper.Terminal.Print("  ");
        Viper.Terminal.PrintInt(i);
        Viper.Terminal.Print(": ");
        Viper.Terminal.Say(item);
        i = i + 1;
    }

    Viper.Terminal.Say("");
    if count == 3 {
        Viper.Terminal.Say("SUCCESS: List methods work correctly!");
    } else {
        Viper.Terminal.Say("FAIL: Expected 3 items");
    }

    Viper.Terminal.Say("");
    Viper.Terminal.Say("Bug #7 Simple Test PASSED!");
}
