# Chapter 18: Design Patterns

As you build more programs, you'll notice recurring problems. How do you ensure only one instance of something exists? How do you undo actions? How do you notify multiple objects when something changes?

Experienced programmers have solved these problems before. Their solutions are called *design patterns* — proven approaches to common situations. Learning patterns is like learning the vocabulary of software design.

This chapter introduces the most useful patterns. We won't cover every pattern ever invented — just the ones you'll use regularly.

---

## Why Patterns?

Patterns give you:

**Proven solutions**: These work. Thousands of programmers have used and refined them.

**Common vocabulary**: When someone says "use the Observer pattern," everyone knows what they mean.

**Faster design**: Recognize a problem, apply the pattern, move on.

**Better code**: Patterns encourage good OOP principles.

A word of caution: don't force patterns where they don't fit. Use them when they solve a real problem you have, not just to seem sophisticated.

---

## Singleton: One and Only

Sometimes you need exactly one instance of something — a configuration manager, a game engine, a logging service.

```rust
entity GameEngine {
    hide static instance: GameEngine? = null;

    hide func init() {
        // Private: can't create from outside
    }

    static func getInstance() -> GameEngine {
        if GameEngine.instance == null {
            GameEngine.instance = GameEngine();
        }
        return GameEngine.instance;
    }

    func run() {
        Viper.Terminal.Say("Engine running");
    }
}

// Usage
var engine = GameEngine.getInstance();
engine.run();

// Later, same instance
var sameEngine = GameEngine.getInstance();
// engine and sameEngine are the same object
```

The hidden initializer prevents creating instances directly. `getInstance()` returns the single instance, creating it if needed.

**Use when**: You need exactly one instance globally accessible.

**Caution**: Singletons are essentially global state. They can make testing harder and hide dependencies. Use sparingly.

---

## Factory: Creating Objects

When object creation is complex or you want to hide which concrete entity gets created:

```rust
interface Enemy {
    func attack();
    func getHealth() -> i64;
}

entity Goblin implements Enemy {
    func attack() { Viper.Terminal.Say("Goblin scratches!"); }
    func getHealth() -> i64 { return 30; }
}

entity Orc implements Enemy {
    func attack() { Viper.Terminal.Say("Orc smashes!"); }
    func getHealth() -> i64 { return 50; }
}

entity Dragon implements Enemy {
    func attack() { Viper.Terminal.Say("Dragon breathes fire!"); }
    func getHealth() -> i64 { return 200; }
}

entity EnemyFactory {
    static func create(type: string) -> Enemy {
        if type == "goblin" {
            return Goblin();
        } else if type == "orc" {
            return Orc();
        } else if type == "dragon" {
            return Dragon();
        } else {
            throw Error("Unknown enemy type: " + type);
        }
    }

    static func createRandom(level: i64) -> Enemy {
        if level < 3 {
            return Goblin();
        } else if level < 7 {
            return Orc();
        } else {
            return Dragon();
        }
    }
}

// Usage
var enemy = EnemyFactory.create("orc");
var randomEnemy = EnemyFactory.createRandom(5);
```

The caller doesn't need to know about Goblin, Orc, Dragon. Just ask the factory.

**Use when**: Object creation is complex, or you want to centralize creation logic.

---

## Observer: Watching for Changes

When multiple objects need to react when something changes:

```rust
interface Observer {
    func onUpdate(event: string);
}

entity Subject {
    hide observers: [Observer];

    expose func init() {
        self.observers = [];
    }

    func subscribe(observer: Observer) {
        self.observers.push(observer);
    }

    func unsubscribe(observer: Observer) {
        // Remove from list...
    }

    func notify(event: string) {
        for obs in self.observers {
            obs.onUpdate(event);
        }
    }
}

// Concrete observers
entity Logger implements Observer {
    func onUpdate(event: string) {
        Viper.Terminal.Say("[LOG] " + event);
    }
}

entity SoundPlayer implements Observer {
    func onUpdate(event: string) {
        if event == "player_died" {
            Viper.Terminal.Say("Playing death sound");
        }
    }
}

entity AchievementTracker implements Observer {
    func onUpdate(event: string) {
        if event == "enemy_killed" {
            Viper.Terminal.Say("Checking achievements...");
        }
    }
}

// Usage
var gameEvents = Subject();
gameEvents.subscribe(Logger());
gameEvents.subscribe(SoundPlayer());
gameEvents.subscribe(AchievementTracker());

gameEvents.notify("enemy_killed");
// All three observers react
```

The subject doesn't know what the observers do. Observers don't know about each other. Clean separation.

**Use when**: Multiple components need to react to changes without tight coupling.

---

## Strategy: Swappable Algorithms

When you want to vary behavior independently from the entity that uses it:

```rust
interface MovementStrategy {
    func move(entity: Entity);
}

entity WalkStrategy implements MovementStrategy {
    func move(entity: Entity) {
        entity.x += 1;
    }
}

entity RunStrategy implements MovementStrategy {
    func move(entity: Entity) {
        entity.x += 3;
    }
}

entity FlyStrategy implements MovementStrategy {
    func move(entity: Entity) {
        entity.y -= 1;  // Upward
        entity.x += 2;
    }
}

entity Entity {
    x: f64;
    y: f64;
    hide movementStrategy: MovementStrategy;

    expose func init(strategy: MovementStrategy) {
        self.movementStrategy = strategy;
    }

    func setMovement(strategy: MovementStrategy) {
        self.movementStrategy = strategy;
    }

    func move() {
        self.movementStrategy.move(self);
    }
}

// Usage
var player = Entity(WalkStrategy());
player.move();  // Walks

player.setMovement(RunStrategy());
player.move();  // Runs

player.setMovement(FlyStrategy());
player.move();  // Flies
```

Change behavior at runtime by swapping strategies.

**Use when**: You have multiple ways to do something and want to switch between them.

---

## Command: Encapsulating Actions

When you want to treat actions as objects — for undo, redo, queuing, logging:

```rust
interface Command {
    func execute();
    func undo();
}

entity AddTextCommand implements Command {
    hide editor: TextEditor;
    hide text: string;
    hide position: i64;

    expose func init(editor: TextEditor, position: i64, text: string) {
        self.editor = editor;
        self.position = position;
        self.text = text;
    }

    func execute() {
        self.editor.insertAt(self.position, self.text);
    }

    func undo() {
        self.editor.deleteAt(self.position, self.text.length);
    }
}

entity CommandHistory {
    hide commands: [Command];

    expose func init() {
        self.commands = [];
    }

    func execute(command: Command) {
        command.execute();
        self.commands.push(command);
    }

    func undo() {
        if self.commands.length > 0 {
            var last = self.commands.pop();
            last.undo();
        }
    }
}

// Usage
var history = CommandHistory();
var editor = TextEditor();

history.execute(AddTextCommand(editor, 0, "Hello "));
history.execute(AddTextCommand(editor, 6, "World"));
// Editor contains: "Hello World"

history.undo();
// Editor contains: "Hello "

history.undo();
// Editor contains: ""
```

Each action becomes an object that can be stored, reversed, or replayed.

**Use when**: You need undo/redo, command queuing, or transaction logging.

---

## State: Behavior Changes with State

When an object's behavior changes based on its internal state:

```rust
interface PlayerState {
    func handleInput(player: Player, input: string);
    func update(player: Player);
}

entity IdleState implements PlayerState {
    func handleInput(player: Player, input: string) {
        if input == "move" {
            player.setState(WalkingState());
        } else if input == "jump" {
            player.setState(JumpingState());
        }
    }

    func update(player: Player) {
        // Standing still
    }
}

entity WalkingState implements PlayerState {
    func handleInput(player: Player, input: string) {
        if input == "stop" {
            player.setState(IdleState());
        } else if input == "jump" {
            player.setState(JumpingState());
        }
    }

    func update(player: Player) {
        player.x += 1;
    }
}

entity JumpingState implements PlayerState {
    hide jumpTime: f64;

    func handleInput(player: Player, input: string) {
        // Can't change state while jumping
    }

    func update(player: Player) {
        self.jumpTime += 0.1;
        player.y = Viper.Math.sin(self.jumpTime * Viper.Math.PI) * 10;
        if self.jumpTime >= 1.0 {
            player.setState(IdleState());
        }
    }
}

entity Player {
    x: f64;
    y: f64;
    hide state: PlayerState;

    expose func init() {
        self.state = IdleState();
    }

    func setState(newState: PlayerState) {
        self.state = newState;
    }

    func handleInput(input: string) {
        self.state.handleInput(self, input);
    }

    func update() {
        self.state.update(self);
    }
}
```

The player behaves differently based on state, without tangled if-else chains.

**Use when**: An object has distinct behavioral modes that change at runtime.

---

## Decorator: Adding Behavior

When you want to add features to objects without modifying their entities:

```rust
interface Coffee {
    func cost() -> f64;
    func description() -> string;
}

entity SimpleCoffee implements Coffee {
    func cost() -> f64 { return 2.0; }
    func description() -> string { return "Coffee"; }
}

entity MilkDecorator implements Coffee {
    hide coffee: Coffee;

    expose func init(coffee: Coffee) {
        self.coffee = coffee;
    }

    func cost() -> f64 {
        return self.coffee.cost() + 0.5;
    }

    func description() -> string {
        return self.coffee.description() + ", milk";
    }
}

entity SugarDecorator implements Coffee {
    hide coffee: Coffee;

    expose func init(coffee: Coffee) {
        self.coffee = coffee;
    }

    func cost() -> f64 {
        return self.coffee.cost() + 0.2;
    }

    func description() -> string {
        return self.coffee.description() + ", sugar";
    }
}

// Usage
var order: Coffee = SimpleCoffee();
order = MilkDecorator(order);
order = SugarDecorator(order);
order = SugarDecorator(order);  // Extra sugar

Viper.Terminal.Say(order.description());  // Coffee, milk, sugar, sugar
Viper.Terminal.Say(order.cost());         // 2.9
```

Stack decorators to combine features.

**Use when**: You want flexible combinations of features without subentity explosion.

---

## Which Pattern When?

| Problem | Pattern |
|---------|---------|
| Need exactly one instance | Singleton |
| Complex object creation | Factory |
| React to changes | Observer |
| Multiple algorithms, swap at runtime | Strategy |
| Undo/redo, command queuing | Command |
| Behavior varies with state | State |
| Combine features flexibly | Decorator |

---

## Summary

Design patterns are proven solutions to common problems:

- **Singleton**: One instance only
- **Factory**: Centralize object creation
- **Observer**: Notify multiple listeners of changes
- **Strategy**: Swap algorithms at runtime
- **Command**: Actions as objects (for undo, queuing)
- **State**: Behavior changes with internal state
- **Decorator**: Add features by wrapping

Learn to recognize when patterns apply, but don't force them. A simple solution is often better than an over-engineered one.

---

## Exercises

**Exercise 18.1**: Implement a Logger singleton that writes messages to a file.

**Exercise 18.2**: Create a ShapeFactory that creates circles, rectangles, or triangles based on input.

**Exercise 18.3**: Build a simple stock price observer: `StockPrice` subject, `Investor` observers who react to price changes.

**Exercise 18.4**: Create a sorting strategy system where you can switch between BubbleSort, QuickSort, and MergeSort.

**Exercise 18.5**: Implement an undo/redo system for a simple drawing program with commands for Draw, Erase, and Move.

**Exercise 18.6** (Challenge): Build a simple game with State pattern: character states for Idle, Walking, Attacking, Hurt, Dead, with proper transitions between them.

---

*We've completed Part III! You now understand object-oriented programming: entities, inheritance, interfaces, polymorphism, and common design patterns.*

*Part IV puts everything together to build real applications: graphics, games, networking, and more.*

*[Continue to Part IV: Real Applications →](../part4-applications/19-graphics.md)*
