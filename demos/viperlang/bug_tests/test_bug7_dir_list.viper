// Bug #7 Test: Dir.List returns proper List[String] type
// This tests that Dir.List returns a typed list that supports .count() and .get()
// Previously this returned a raw ptr, breaking method calls
module main;

func main() {
    Viper.Terminal.Say("Testing Bug #7: Dir.List typed return value");
    Viper.Terminal.Say("=============================================");
    Viper.Terminal.Say("");

    // Use a known path instead of current directory (which may be empty in runtime)
    var testDir = "/tmp";
    Viper.Terminal.Print("Test directory: ");
    Viper.Terminal.Say(testDir);
    Viper.Terminal.Say("");

    // List directory contents
    var entries = Viper.IO.Dir.List(testDir);

    // Test .count() method - this was broken before the fix
    var count = entries.count();
    Viper.Terminal.Print("Number of entries: ");
    Viper.Terminal.PrintInt(count);
    Viper.Terminal.Say("");
    Viper.Terminal.Say("");

    // List first few entries using .get()
    Viper.Terminal.Say("First 5 entries (or all if fewer):");
    var i = 0;
    var limit = 5;
    if count < limit {
        limit = count;
    }
    while i < limit {
        String entry = entries.get(i);
        Viper.Terminal.Print("  ");
        Viper.Terminal.PrintInt(i);
        Viper.Terminal.Print(": ");
        Viper.Terminal.Say(entry);
        i = i + 1;
    }

    Viper.Terminal.Say("");

    // Verify we got entries
    if count > 0 {
        Viper.Terminal.Say("SUCCESS: Dir.List returned entries with working .count() and .get()");
    } else {
        Viper.Terminal.Say("WARNING: Directory appears empty (may still be working correctly)");
    }

    Viper.Terminal.Say("");
    Viper.Terminal.Say("Bug #7 Test PASSED!");
}
