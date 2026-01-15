// Test: OOP - Inheritance
// Tests: extends, override, polymorphism
module Test;

// Base entity
entity Animal {
    hide String name;

    expose func init(n: String) {
        name = n;
    }

    expose func getName() -> String {
        return name;
    }

    expose func speak() -> String {
        return "...";
    }
}

// Derived entity with additional field
entity Dog extends Animal {
    hide String breed;

    expose func init(n: String, b: String) {
        name = n;   // Access inherited field
        breed = b;
    }

    expose func getBreed() -> String {
        return breed;
    }

    // Override parent method
    override expose func speak() -> String {
        return "Woof!";
    }
}

// Another derived entity
entity Cat extends Animal {
    expose func init(n: String) {
        name = n;   // Access inherited field
    }

    override expose func speak() -> String {
        return "Meow!";
    }
}

func main() {
    Viper.Terminal.Say("=== OOP Inheritance Test ===");

    // Test basic inheritance
    Viper.Terminal.Say("");
    Viper.Terminal.Say("--- Basic Inheritance ---");
    var dog = new Dog("Buddy", "Golden Retriever");
    Viper.Terminal.Say("Dog name: " + dog.getName());
    Viper.Terminal.Say("Dog breed: " + dog.getBreed());
    Viper.Terminal.Say("Dog speaks: " + dog.speak());

    Viper.Terminal.Say("");
    var cat = new Cat("Whiskers");
    Viper.Terminal.Say("Cat name: " + cat.getName());
    Viper.Terminal.Say("Cat speaks: " + cat.speak());

    // Test polymorphism - assign derived type to base type variable
    Viper.Terminal.Say("");
    Viper.Terminal.Say("--- Polymorphism ---");
    var animal: Animal = dog;
    Viper.Terminal.Say("As Animal (dog): " + animal.speak());
    animal = cat;
    Viper.Terminal.Say("As Animal (cat): " + animal.speak());

    Viper.Terminal.Say("");
    Viper.Terminal.Say("=== OOP Inheritance test complete ===");
}
