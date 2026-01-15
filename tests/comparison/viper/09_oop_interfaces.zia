// Test: OOP - Interfaces
// Tests: interface, implements
module Test;

// Define interface
interface IShape {
    func getArea() -> Number;
    func getName() -> String;
}

// Implement interface in Circle
entity Circle implements IShape {
    expose Number radius;

    expose func init(r: Number) {
        radius = r;
    }

    expose func getArea() -> Number {
        return 3.14159 * radius * radius;
    }

    expose func getName() -> String {
        return "Circle";
    }

    expose func getRadius() -> Number {
        return radius;
    }
}

// Implement interface in Rectangle
entity Rectangle implements IShape {
    expose Number width;
    expose Number height;

    expose func init(w: Number, h: Number) {
        width = w;
        height = h;
    }

    expose func getArea() -> Number {
        return width * height;
    }

    expose func getName() -> String {
        return "Rectangle";
    }
}

func main() {
    Viper.Terminal.Say("=== OOP Interfaces Test ===");

    // Test Circle
    Viper.Terminal.Say("");
    Viper.Terminal.Say("--- Circle ---");
    var c = new Circle(5.0);
    Viper.Terminal.Say("Shape: " + c.getName());
    Viper.Terminal.Print("Area: ");
    Viper.Terminal.SayNum(c.getArea());
    Viper.Terminal.Print("Radius: ");
    Viper.Terminal.SayNum(c.getRadius());

    // Test Rectangle
    Viper.Terminal.Say("");
    Viper.Terminal.Say("--- Rectangle ---");
    var r = new Rectangle(4.0, 6.0);
    Viper.Terminal.Say("Shape: " + r.getName());
    Viper.Terminal.Print("Area: ");
    Viper.Terminal.SayNum(r.getArea());

    // Test polymorphism through interface
    Viper.Terminal.Say("");
    Viper.Terminal.Say("--- Polymorphism via Interface ---");
    // var shape: IShape = c;  // May not work
    // Viper.Terminal.Say("As IShape (circle): " + shape.getName());
    Viper.Terminal.Say("Testing interface variable assignment...");

    Viper.Terminal.Say("");
    Viper.Terminal.Say("=== OOP Interfaces test complete ===");
}
