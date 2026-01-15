// Test: OOP - Generics
// Tests: generic entities, generic functions
module Test;

// Generic entity
entity Box[T] {
    expose T value;

    expose func init(v: T) {
        value = v;
    }

    expose func get() -> T {
        return value;
    }

    expose func set(v: T) {
        value = v;
    }
}

// Generic function
func identity[T](x: T) -> T {
    return x;
}

func main() {
    Viper.Terminal.Say("=== OOP Generics Test ===");

    // Test generic entity with Integer
    Viper.Terminal.Say("");
    Viper.Terminal.Say("--- Generic Box[Integer] ---");
    var intBox = new Box[Integer](42);
    Viper.Terminal.Print("Initial value: ");
    Viper.Terminal.SayInt(intBox.get());
    intBox.set(100);
    Viper.Terminal.Print("After set(100): ");
    Viper.Terminal.SayInt(intBox.get());

    // Test generic entity with String
    Viper.Terminal.Say("");
    Viper.Terminal.Say("--- Generic Box[String] ---");
    var strBox = new Box[String]("Hello");
    Viper.Terminal.Say("Initial value: " + strBox.get());
    strBox.set("World");
    Viper.Terminal.Say("After set: " + strBox.get());

    // Test generic function
    Viper.Terminal.Say("");
    Viper.Terminal.Say("--- Generic Function ---");
    var i = identity[Integer](42);
    Viper.Terminal.Print("identity[Integer](42) = ");
    Viper.Terminal.SayInt(i);

    Viper.Terminal.Say("");
    Viper.Terminal.Say("=== OOP Generics test complete ===");
}
