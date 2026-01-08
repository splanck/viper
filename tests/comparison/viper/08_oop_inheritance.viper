// Test: OOP - Inheritance
// Tests: extends (LIMITATIONS FOUND: inherited fields not accessible, polymorphism not working)
module Test;

// Base entity
entity Animal {
    expose String name;

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

// Derived entity - LIMITATION: Cannot access inherited 'name' field
entity Dog extends Animal {
    expose String breed;
    expose String dogName;  // Workaround: duplicate field

    expose func init(n: String, b: String) {
        dogName = n;  // Can't use: name = n
        breed = b;
    }

    expose func getBreed() -> String {
        return breed;
    }

    expose func getDogName() -> String {
        return dogName;
    }

    // Methods override implicitly (override keyword causes parse error)
    expose func speak() -> String {
        return "Woof!";
    }
}

// Another derived entity
entity Cat extends Animal {
    expose String catName;  // Workaround: duplicate field

    expose func init(n: String) {
        catName = n;  // Can't use: name = n
    }

    expose func getCatName() -> String {
        return catName;
    }

    expose func speak() -> String {
        return "Meow!";
    }
}

func main() {
    Viper.Terminal.Say("=== OOP Inheritance Test ===");

    // Test basic inheritance
    Viper.Terminal.Say("");
    Viper.Terminal.Say("--- Basic Inheritance ---");
    var dog = new Dog("Buddy", "Golden Retriever");
    Viper.Terminal.Say("Dog name: " + dog.getDogName());
    Viper.Terminal.Say("Dog breed: " + dog.getBreed());
    Viper.Terminal.Say("Dog speaks: " + dog.speak());

    Viper.Terminal.Say("");
    var cat = new Cat("Whiskers");
    Viper.Terminal.Say("Cat name: " + cat.getCatName());
    Viper.Terminal.Say("Cat speaks: " + cat.speak());

    // LIMITATION: Polymorphism not working
    // var animal: Animal = dog;  // Type mismatch error
    // animal = cat;               // Type mismatch error
    Viper.Terminal.Say("");
    Viper.Terminal.Say("--- Polymorphism ---");
    Viper.Terminal.Say("LIMITATION: Cannot assign Dog/Cat to Animal variable");
    Viper.Terminal.Say("LIMITATION: Inherited fields not accessible in child");

    Viper.Terminal.Say("");
    Viper.Terminal.Say("=== OOP Inheritance test complete ===");
}
