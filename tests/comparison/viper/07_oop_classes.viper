// Test: OOP - Entities (Classes)
// Tests: entity, fields, methods
// NOTE: ViperLang uses init() method instead of explicit constructors
module Test;

// Simple entity with fields and methods
entity Point {
    expose Number x;
    expose Number y;

    expose func init(px: Number, py: Number) {
        x = px;
        y = py;
    }

    expose func distance() -> Number {
        return Viper.Math.Sqrt(x * x + y * y);
    }

    expose func move(dx: Number, dy: Number) {
        x = x + dx;
        y = y + dy;
    }

    expose func toString() -> String {
        return "(" + Viper.Fmt.Num(x) + ", " + Viper.Fmt.Num(y) + ")";
    }
}

// Entity with private fields
entity Counter {
    hide Integer count;

    expose func init() {
        count = 0;
    }

    expose func increment() {
        count = count + 1;
    }

    expose func decrement() {
        count = count - 1;
    }

    expose func getCount() -> Integer {
        return count;
    }

    expose func reset() {
        count = 0;
    }
}

// Entity demonstrating field access
entity Circle {
    expose Number radius;

    expose func init(r: Number) {
        radius = r;
    }

    expose func area() -> Number {
        return 3.14159 * radius * radius;
    }

    expose func circumference() -> Number {
        return 2.0 * 3.14159 * radius;
    }
}

func main() {
    Viper.Terminal.Say("=== OOP Entities Test ===");

    // Test Point entity
    Viper.Terminal.Say("");
    Viper.Terminal.Say("--- Point entity ---");
    var p = new Point(3.0, 4.0);
    Viper.Terminal.Say("Point: " + p.toString());
    Viper.Terminal.Print("Distance from origin: ");
    Viper.Terminal.SayNum(p.distance());
    p.move(1.0, 1.0);
    Viper.Terminal.Say("After move(1,1): " + p.toString());

    // Test Counter entity
    Viper.Terminal.Say("");
    Viper.Terminal.Say("--- Counter entity ---");
    var c = new Counter();
    Viper.Terminal.Print("Initial count: ");
    Viper.Terminal.SayInt(c.getCount());
    c.increment();
    c.increment();
    c.increment();
    Viper.Terminal.Print("After 3 increments: ");
    Viper.Terminal.SayInt(c.getCount());
    c.decrement();
    Viper.Terminal.Print("After decrement: ");
    Viper.Terminal.SayInt(c.getCount());

    // Test Circle entity
    Viper.Terminal.Say("");
    Viper.Terminal.Say("--- Circle entity ---");
    var circle = new Circle(5.0);
    Viper.Terminal.Print("Circle radius: ");
    Viper.Terminal.SayNum(circle.radius);
    Viper.Terminal.Print("Area: ");
    Viper.Terminal.SayNum(circle.area());
    Viper.Terminal.Print("Circumference: ");
    Viper.Terminal.SayNum(circle.circumference());

    // No destructor test - ViperLang uses GC
    Viper.Terminal.Say("");
    Viper.Terminal.Say("--- Destructor ---");
    Viper.Terminal.Say("ViperLang: No destructors (uses garbage collection)");

    Viper.Terminal.Say("");
    Viper.Terminal.Say("=== OOP Entities test complete ===");
}
