# Chapter 18: Design Patterns

Imagine you're an architect designing your hundredth building. You don't start from scratch each time, reinventing how to support a roof or where to put stairs. Instead, you draw on solutions that have worked for countless buildings before — proven approaches that architects have refined over centuries. A "load-bearing wall" isn't just a wall; it's a recognized solution to the problem of supporting upper floors.

Programming has the same phenomenon. After building enough software, you notice the same problems appearing again and again. How do you ensure only one instance of something exists? How do you undo user actions? How do you notify multiple components when something changes? How do you swap algorithms without rewriting code?

Experienced programmers have solved these problems thousands of times. Their best solutions have been documented, named, and refined. We call them *design patterns* — reusable solutions to common software design problems. Learning patterns is like learning the vocabulary of software architecture. When someone says "use the Observer pattern," every experienced programmer knows exactly what they mean.

This chapter introduces the most practical patterns. We won't cover every pattern ever cataloged — just the ones you'll actually use. More importantly, we'll focus on *when* and *why* to use each pattern, not just *how*.

---

## What Are Design Patterns?

A design pattern is not a piece of code you copy and paste. It's a *template* for solving a problem — a description of how to structure your code to address a specific challenge. The actual implementation varies based on your language and situation, but the core idea remains the same.

Think of it like a recipe versus a frozen dinner. A recipe tells you the approach: "brown the meat, add aromatics, simmer in liquid." You adapt it to your ingredients and preferences. A frozen dinner gives you one specific result. Patterns are recipes, not frozen dinners.

### The Origins of Patterns

The concept of design patterns in software came from architecture. Christopher Alexander, an architect, wrote about patterns in building design — recurring solutions to common problems in constructing living spaces. In 1994, four programmers (nicknamed the "Gang of Four") published *Design Patterns: Elements of Reusable Object-Oriented Software*, cataloging 23 patterns they'd observed in successful software. That book launched patterns as a formal discipline.

You don't need to read that book (it's quite academic), but you should know that patterns aren't arbitrary. They emerged from observing what works in real systems built by real programmers solving real problems.

---

## Why Learn Patterns?

Learning patterns gives you several powerful advantages:

### Proven Solutions

These solutions work. They've been battle-tested in thousands of programs across decades. When you use the Observer pattern for event handling, you're not experimenting — you're applying a technique that's succeeded in countless applications from word processors to video games to operating systems.

Compare this to designing your own solution from scratch. You might create something clever, but you'll probably hit problems that pattern designers already solved. Why rediscover what others learned the hard way?

### A Shared Vocabulary

Imagine explaining a solution: "Create an interface for the algorithm, then have different entities implement it. Store one of them in your main entity and delegate to it. You can swap implementations at runtime."

Or you could say: "Use the Strategy pattern."

The second version communicates instantly to anyone who knows patterns. It's like the difference between describing "a sequence of handshakes, key exchanges, and encrypted communication" versus saying "TLS." Vocabulary matters.

When you join a team or read open-source code, pattern names appear everywhere. Knowing patterns lets you understand existing code faster and communicate your designs more clearly.

### Faster Design Decisions

With patterns in your toolkit, design decisions become: "Which pattern fits this problem?" instead of "How do I solve this from nothing?"

You recognize a situation — "I need to notify multiple objects when something changes" — and immediately think "Observer pattern." You don't waste time designing a custom solution. You spend that time on the parts of your problem that are actually unique.

### Better Architecture

Patterns embody good object-oriented principles. They naturally lead to code that's:

- **Loosely coupled**: Components don't depend on each other's internals
- **Highly cohesive**: Each component has a clear, focused purpose
- **Open for extension**: You can add new behavior without modifying existing code
- **Testable**: You can test components in isolation

Using patterns teaches you to think about software structure in healthy ways.

### Standing on Shoulders of Giants

Isaac Newton famously said, "If I have seen further, it is by standing on the shoulders of giants." Patterns let you build on the accumulated wisdom of the programming profession. You don't have to be a genius to write well-structured code — you just have to learn what geniuses figured out before you.

---

## A Word of Caution

Before diving into specific patterns, a warning: **patterns can be overused**.

Some programmers, upon learning patterns, see them everywhere. They add Factory classes when simple construction would work. They create Observer systems for two objects that could just call each other. They introduce layers of abstraction that obscure rather than clarify.

The goal isn't to use as many patterns as possible. The goal is to write clear, maintainable code. Patterns are tools, not trophies. A simple solution is almost always better than a pattern-heavy one.

**Use a pattern when:**
- You have a real problem that the pattern solves
- The pattern makes your code clearer, not more complex
- You expect the flexibility the pattern provides to matter

**Don't use a pattern when:**
- Your problem is simpler than what the pattern addresses
- The pattern adds complexity without clear benefit
- You're using it just because you can

We'll revisit this caution throughout the chapter, noting when each pattern is overkill.

---

## Creational Patterns

Creational patterns deal with object creation — how you instantiate things. Sometimes simple construction (`var x = Something()`) isn't enough. You need control over how, when, and how many objects get created.

---

## Singleton: One and Only One

### The Problem

Imagine you're building a game. You have a `GameEngine` that manages the game loop, renders graphics, and coordinates everything. How many game engines should exist? Exactly one. Having two would be chaos — which one controls the game?

Or consider a logging system. Every part of your application might need to log messages. Should each component create its own logger? That would mean dozens of log files, no coordination, no consistent formatting. You want one logger that everything uses.

Some things should exist as a single instance throughout your program:
- Configuration managers (one set of settings)
- Connection pools (one pool shared by all code)
- Hardware interfaces (one object talking to the printer)
- Game engines, window managers, audio systems

The problem: how do you ensure only one instance exists and provide global access to it?

You might think: "I'll just be careful and only create one." That works until your program grows, other people modify it, or you forget your own convention. We want the *language* to enforce the constraint, not discipline.

### Real-World Analogy

Think of a country's president or prime minister. There's exactly one at any time. You don't create a new president whenever you need to talk to the government — you go to the existing one. And there's a defined way to access them (through official channels), not just wandering into their office.

### The Solution

The Singleton pattern provides a single instance and global access:

```rust
bind Viper.Terminal;

entity GameEngine {
    // The single instance, hidden from outside
    hide static instance: GameEngine? = null;

    // Hidden initializer prevents direct construction
    hide func init() {
        Say("GameEngine initialized");
    }

    // Public access point
    static func getInstance() -> GameEngine {
        if GameEngine.instance == null {
            GameEngine.instance = GameEngine();
        }
        return GameEngine.instance;
    }

    // Instance methods
    func run() {
        Say("Game engine running");
    }

    func getFrameRate() -> Integer {
        return 60;
    }
}
```

The key elements:
1. **Hidden static instance**: The single instance is stored in a static field, hidden from outside code
2. **Hidden initializer**: No one can call `GameEngine()` directly
3. **Static getInstance()**: The only way to get the engine — creates it if needed, returns existing one otherwise

Using it:

```rust
bind Viper.Terminal;

func start() {
    // Get the engine (creates it first time)
    var engine = GameEngine.getInstance();
    engine.run();

    // Later, anywhere in the program
    var sameEngine = GameEngine.getInstance();

    // These are the same object
    Say(engine == sameEngine);  // true
}
```

No matter how many times you call `getInstance()`, you get the same engine. The first call creates it; subsequent calls return the existing one.

### A More Complete Example: Configuration Manager

```rust
bind Viper.Terminal;
bind Viper.File;

entity Config {
    hide static instance: Config? = null;

    // Configuration data
    hide settings: {String: String};

    hide func init() {
        // Load default settings
        self.settings = {};
        self.settings["theme"] = "dark";
        self.settings["language"] = "en";
        self.settings["volume"] = "80";
    }

    static func getInstance() -> Config {
        if Config.instance == null {
            Config.instance = Config();
        }
        return Config.instance;
    }

    func get(key: String) -> String? {
        return self.settings[key];
    }

    func set(key: String, value: String) {
        self.settings[key] = value;
    }

    func loadFromFile(path: String) {
        // Load settings from disk
        var content = read(path);
        // Parse and populate self.settings...
        Say("Loaded settings from " + path);
    }

    func saveToFile(path: String) {
        // Save settings to disk
        Say("Saved settings to " + path);
    }
}

// Usage anywhere in the program
func updateUserPreferences() {
    var config = Config.getInstance();
    config.set("theme", "light");
    config.saveToFile("settings.json");
}

func getVolumeLevel() -> String {
    var config = Config.getInstance();
    return config.get("volume") ?? "50";
}
```

Every part of your program accesses the same configuration object, seeing the same settings.

### When to Use Singleton

**Use when:**
- You need exactly one instance (not "probably one" or "usually one")
- That instance needs global access
- Examples: logging, configuration, hardware interfaces, thread pools

**Don't use when:**
- You might need multiple instances later
- You can pass the object as a parameter instead
- You're using it just to avoid passing arguments

### The Dark Side of Singletons

Singletons are the most controversial pattern. They work, but they have real drawbacks:

**Global state**: Singletons are essentially global variables with extra steps. Any code can access and modify them, making it hard to track what's happening.

**Testing difficulty**: How do you test code that uses `Config.getInstance()`? You can't easily substitute a test configuration.

**Hidden dependencies**: When a function uses a singleton internally, callers don't know about that dependency. The function signature lies about what it needs.

**Concurrency issues**: In multi-threaded programs, two threads might call `getInstance()` simultaneously and create two instances.

Many experienced programmers avoid singletons when possible, preferring to pass dependencies explicitly. The pattern exists and works, but use it sparingly.

---

## Factory: Creating Objects Without Specifying Types

### The Problem

You're building a game with different enemy types: goblins, orcs, dragons. Your level loading code reads a file that specifies which enemies to place:

```
enemy: goblin at 10, 20
enemy: orc at 30, 40
enemy: dragon at 100, 100
```

How does your code create these enemies? The naive approach:

```rust
func loadLevel(data: String) {
    // Parse data...
    for line in data.split("\n") {
        var parts = parseLine(line);
        var type = parts.type;
        var x = parts.x;
        var y = parts.y;

        // Ugly: your level loader knows about every enemy type
        if type == "goblin" {
            enemies.push(Goblin(x, y));
        } else if type == "orc" {
            enemies.push(Orc(x, y));
        } else if type == "dragon" {
            enemies.push(Dragon(x, y));
        }
    }
}
```

This has problems:
- The level loader is cluttered with creation logic
- Adding a new enemy type requires modifying the level loader
- The same creation logic might be duplicated elsewhere
- Testing is hard because you can't substitute different implementations

### Real-World Analogy

Think of a restaurant kitchen. You don't walk into the kitchen and cook your own food. You tell the waiter "I'll have the pasta" and the kitchen creates it. You don't know or care which chef prepares it or exactly how. You just get pasta that meets the restaurant's standards.

A factory is like that kitchen. You ask for what you want; it handles the creation details.

### The Solution

The Factory pattern centralizes object creation:

```rust
bind Viper.Terminal;
bind Viper.Math;

interface Enemy {
    func attack();
    func getHealth() -> Integer;
    func getName() -> String;
}

entity Goblin implements Enemy {
    x: Number;
    y: Number;

    expose func init(x: Number, y: Number) {
        self.x = x;
        self.y = y;
    }

    func attack() {
        Say("Goblin scratches!");
    }

    func getHealth() -> Integer {
        return 30;
    }

    func getName() -> String {
        return "Goblin";
    }
}

entity Orc implements Enemy {
    x: Number;
    y: Number;

    expose func init(x: Number, y: Number) {
        self.x = x;
        self.y = y;
    }

    func attack() {
        Say("Orc smashes!");
    }

    func getHealth() -> Integer {
        return 50;
    }

    func getName() -> String {
        return "Orc";
    }
}

entity Dragon implements Enemy {
    x: Number;
    y: Number;

    expose func init(x: Number, y: Number) {
        self.x = x;
        self.y = y;
    }

    func attack() {
        Say("Dragon breathes fire!");
    }

    func getHealth() -> Integer {
        return 200;
    }

    func getName() -> String {
        return "Dragon";
    }
}

// The Factory
entity EnemyFactory {
    static func create(type: String, x: Number, y: Number) -> Enemy {
        if type == "goblin" {
            return Goblin(x, y);
        } else if type == "orc" {
            return Orc(x, y);
        } else if type == "dragon" {
            return Dragon(x, y);
        } else {
            throw Error("Unknown enemy type: " + type);
        }
    }

    // Smarter creation based on game logic
    static func createForLevel(level: Integer, x: Number, y: Number) -> Enemy {
        if level < 3 {
            return Goblin(x, y);
        } else if level < 7 {
            // Mix of goblins and orcs
            if Math.random() > 0.5 {
                return Orc(x, y);
            }
            return Goblin(x, y);
        } else {
            // All enemy types possible
            var roll = Math.random();
            if roll < 0.3 {
                return Dragon(x, y);
            } else if roll < 0.6 {
                return Orc(x, y);
            }
            return Goblin(x, y);
        }
    }

    // Create multiple enemies
    static func createWave(count: Integer, level: Integer) -> [Enemy] {
        var enemies: [Enemy] = [];
        for i in 0..count {
            var x = Math.random() * 800.0;
            var y = Math.random() * 600.0;
            enemies.push(EnemyFactory.createForLevel(level, x, y));
        }
        return enemies;
    }
}
```

Now the level loader is clean:

```rust
func loadLevel(data: String) {
    for line in data.split("\n") {
        var parts = parseLine(line);
        var enemy = EnemyFactory.create(parts.type, parts.x, parts.y);
        enemies.push(enemy);
    }
}

// Or even simpler for procedural levels:
func generateLevel(levelNumber: Integer) {
    var enemies = EnemyFactory.createWave(10, levelNumber);
    // Use enemies...
}
```

The caller doesn't know or care about Goblin, Orc, or Dragon. It just asks the factory for enemies.

### Factory Benefits

**Centralized creation**: All enemy creation logic is in one place. Change it once, change it everywhere.

**Encapsulation**: Callers don't depend on concrete types. They work with the `Enemy` interface.

**Flexibility**: You can change what gets created without changing callers. Add a `Troll` enemy? Update the factory; callers don't change.

**Complex creation logic**: Factories can contain sophisticated logic (level-based selection, randomization, pooling) that would clutter calling code.

### When to Use Factory

**Use when:**
- Creation logic is complex and would clutter the client
- You want to hide concrete types behind interfaces
- Creation logic might change independently from usage
- You need centralized control over what gets created

**Don't use when:**
- Construction is simple and obvious
- There's only one type (no interface/abstraction needed)
- The "factory" would just be `return new Thing()`

**Overkill alert**: If your factory's `create` method just calls a constructor with the same arguments, you probably don't need a factory.

---

## Builder: Constructing Complex Objects Step by Step

### The Problem

Imagine creating a character in a role-playing game. Characters have many attributes:

```rust
var hero = Character(
    "Arthus",           // name
    "Warrior",          // class
    100,                // health
    50,                 // mana
    15,                 // strength
    8,                  // dexterity
    12,                 // intelligence
    "human",            // race
    true,               // canUseMagic
    false,              // isUndead
    "Excalibur",        // weapon
    "Plate Mail",       // armor
    "None"              // mount
);
```

Problems with this approach:
- You have to remember the order of 13 parameters
- Many values have sensible defaults you don't want to specify every time
- It's unclear what each value means without reading the definition
- Adding a new attribute means changing every call site

### Real-World Analogy

Think of ordering a custom pizza. You don't say "Give me a pizza with tomato sauce, mozzarella, pepperoni, mushrooms, olives, medium size, thin crust, extra cheese." You say:

"I'd like a medium pizza... thin crust... add pepperoni... add mushrooms... extra cheese please."

You build the order step by step, specifying only what matters to you, with sensible defaults for everything else.

### The Solution

The Builder pattern constructs objects step by step:

```rust
bind Viper.Terminal;

entity Character {
    name: String;
    characterClass: String;
    health: Integer;
    mana: Integer;
    strength: Integer;
    dexterity: Integer;
    intelligence: Integer;
    race: String;
    canUseMagic: Boolean;
    isUndead: Boolean;
    weapon: String;
    armor: String;
    mount: String;

    // Internal constructor — don't call directly
    expose func init(
        name: String, characterClass: String, health: Integer, mana: Integer,
        strength: Integer, dexterity: Integer, intelligence: Integer, race: String,
        canUseMagic: Boolean, isUndead: Boolean, weapon: String, armor: String,
        mount: String
    ) {
        self.name = name;
        self.characterClass = characterClass;
        self.health = health;
        self.mana = mana;
        self.strength = strength;
        self.dexterity = dexterity;
        self.intelligence = intelligence;
        self.race = race;
        self.canUseMagic = canUseMagic;
        self.isUndead = isUndead;
        self.weapon = weapon;
        self.armor = armor;
        self.mount = mount;
    }

    func describe() {
        Say(self.name + " the " + self.race + " " + self.characterClass);
        Say("  HP: " + self.health + " MP: " + self.mana);
        Say("  STR: " + self.strength + " DEX: " + self.dexterity + " INT: " + self.intelligence);
        Say("  Weapon: " + self.weapon + " Armor: " + self.armor);
    }
}

entity CharacterBuilder {
    // All fields with defaults
    hide name: String = "Unnamed";
    hide characterClass: String = "Adventurer";
    hide health: Integer = 100;
    hide mana: Integer = 50;
    hide strength: Integer = 10;
    hide dexterity: Integer = 10;
    hide intelligence: Integer = 10;
    hide race: String = "human";
    hide canUseMagic: Boolean = false;
    hide isUndead: Boolean = false;
    hide weapon: String = "Fists";
    hide armor: String = "Clothes";
    hide mount: String = "None";

    expose func init() {
        // Defaults are set in field declarations
    }

    // Each setter returns self for chaining
    func named(name: String) -> CharacterBuilder {
        self.name = name;
        return self;
    }

    func ofClass(characterClass: String) -> CharacterBuilder {
        self.characterClass = characterClass;
        return self;
    }

    func withHealth(health: Integer) -> CharacterBuilder {
        self.health = health;
        return self;
    }

    func withMana(mana: Integer) -> CharacterBuilder {
        self.mana = mana;
        return self;
    }

    func withStrength(strength: Integer) -> CharacterBuilder {
        self.strength = strength;
        return self;
    }

    func withDexterity(dexterity: Integer) -> CharacterBuilder {
        self.dexterity = dexterity;
        return self;
    }

    func withIntelligence(intelligence: Integer) -> CharacterBuilder {
        self.intelligence = intelligence;
        return self;
    }

    func ofRace(race: String) -> CharacterBuilder {
        self.race = race;
        return self;
    }

    func withMagic() -> CharacterBuilder {
        self.canUseMagic = true;
        return self;
    }

    func asUndead() -> CharacterBuilder {
        self.isUndead = true;
        return self;
    }

    func wielding(weapon: String) -> CharacterBuilder {
        self.weapon = weapon;
        return self;
    }

    func wearing(armor: String) -> CharacterBuilder {
        self.armor = armor;
        return self;
    }

    func riding(mount: String) -> CharacterBuilder {
        self.mount = mount;
        return self;
    }

    // Preset configurations
    func asWarrior() -> CharacterBuilder {
        self.characterClass = "Warrior";
        self.strength = 15;
        self.dexterity = 10;
        self.intelligence = 5;
        self.health = 150;
        self.mana = 20;
        return self;
    }

    func asMage() -> CharacterBuilder {
        self.characterClass = "Mage";
        self.strength = 5;
        self.dexterity = 8;
        self.intelligence = 15;
        self.health = 80;
        self.mana = 150;
        self.canUseMagic = true;
        return self;
    }

    func asRogue() -> CharacterBuilder {
        self.characterClass = "Rogue";
        self.strength = 8;
        self.dexterity = 15;
        self.intelligence = 10;
        self.health = 100;
        self.mana = 50;
        return self;
    }

    // Build the final object
    func build() -> Character {
        return Character(
            self.name, self.characterClass, self.health, self.mana,
            self.strength, self.dexterity, self.intelligence, self.race,
            self.canUseMagic, self.isUndead, self.weapon, self.armor, self.mount
        );
    }
}
```

Now character creation is readable:

```rust
func start() {
    // Create a specific character
    var hero = CharacterBuilder()
        .named("Arthus")
        .asWarrior()
        .ofRace("human")
        .wielding("Excalibur")
        .wearing("Plate Mail")
        .build();

    hero.describe();
    // Arthus the human Warrior
    //   HP: 150 MP: 20
    //   STR: 15 DEX: 10 INT: 5
    //   Weapon: Excalibur Armor: Plate Mail

    // Create a different character
    var villain = CharacterBuilder()
        .named("Zorgoth")
        .asMage()
        .ofRace("elf")
        .asUndead()
        .withMagic()
        .build();

    villain.describe();

    // Quick character with mostly defaults
    var npc = CharacterBuilder()
        .named("Town Guard")
        .build();

    npc.describe();
}
```

Each method call makes the intent clear. You specify only what differs from defaults. The code documents itself.

### When to Use Builder

**Use when:**
- Objects have many parameters (more than 4-5)
- Many parameters are optional with sensible defaults
- Object construction has multiple steps or configurations
- You want readable, self-documenting construction code

**Don't use when:**
- Objects are simple with few parameters
- All parameters are required and obvious
- Construction is straightforward

**Overkill alert**: A builder for a `Point(x, y)` is unnecessary. Builders shine for complex objects, not simple ones.

---

## Behavioral Patterns

Behavioral patterns deal with how objects communicate and interact. They define clear responsibilities and flexible ways for objects to work together.

---

## Strategy: Swappable Algorithms

### The Problem

You're building a navigation app. You need to calculate routes, but there are different ways to travel:
- Driving: Prefer highways, avoid pedestrian areas
- Walking: Use sidewalks, cut through parks
- Biking: Use bike lanes, avoid steep hills
- Public transit: Follow bus/train routes

You could write one giant method with if-else for each mode:

```rust
func calculateRoute(start: Point, end: Point, mode: String) {
    if mode == "driving" {
        // 200 lines of driving logic
    } else if mode == "walking" {
        // 200 lines of walking logic
    } else if mode == "biking" {
        // 200 lines of biking logic
    } else if mode == "transit" {
        // 200 lines of transit logic
    }
}
```

This is a mess. The method is enormous. Adding a new mode means modifying this core function. Testing one mode requires understanding the whole thing.

### Real-World Analogy

Think of paying for something. You can use cash, credit card, mobile payment, or gift card. The store doesn't care *how* you pay — they just need payment. Each payment method is a "strategy" for accomplishing the same goal. You choose which strategy to use, and you can change your mind (put away your card, get out cash).

### The Solution

The Strategy pattern defines a family of algorithms, encapsulates each one, and makes them interchangeable:

```rust
bind Viper.Terminal;

interface RouteStrategy {
    func calculateRoute(start: Point, end: Point) -> [Point];
    func getName() -> String;
    func getEstimatedTime(distance: Number) -> Number;
}

entity DrivingStrategy implements RouteStrategy {
    func calculateRoute(start: Point, end: Point) -> [Point] {
        Say("Calculating driving route...");
        // Prefer highways, avoid pedestrian zones
        // Returns list of waypoints
        return [start, end];  // Simplified
    }

    func getName() -> String {
        return "Driving";
    }

    func getEstimatedTime(distance: Number) -> Number {
        return distance / 50.0;  // 50 km/h average
    }
}

entity WalkingStrategy implements RouteStrategy {
    func calculateRoute(start: Point, end: Point) -> [Point] {
        Say("Calculating walking route...");
        // Use sidewalks, can cut through parks
        return [start, end];
    }

    func getName() -> String {
        return "Walking";
    }

    func getEstimatedTime(distance: Number) -> Number {
        return distance / 5.0;  // 5 km/h
    }
}

entity BikingStrategy implements RouteStrategy {
    func calculateRoute(start: Point, end: Point) -> [Point] {
        Say("Calculating biking route...");
        // Use bike lanes, avoid steep hills
        return [start, end];
    }

    func getName() -> String {
        return "Biking";
    }

    func getEstimatedTime(distance: Number) -> Number {
        return distance / 15.0;  // 15 km/h
    }
}

entity TransitStrategy implements RouteStrategy {
    func calculateRoute(start: Point, end: Point) -> [Point] {
        Say("Calculating transit route...");
        // Follow bus and train routes
        return [start, end];
    }

    func getName() -> String {
        return "Public Transit";
    }

    func getEstimatedTime(distance: Number) -> Number {
        return distance / 25.0;  // 25 km/h with stops
    }
}

// The Navigator uses whatever strategy is set
entity Navigator {
    hide strategy: RouteStrategy;

    expose func init(strategy: RouteStrategy) {
        self.strategy = strategy;
    }

    func setStrategy(strategy: RouteStrategy) {
        self.strategy = strategy;
        Say("Switched to " + strategy.getName());
    }

    func navigate(start: Point, end: Point) {
        var route = self.strategy.calculateRoute(start, end);
        var distance = self.calculateDistance(route);
        var time = self.strategy.getEstimatedTime(distance);

        Say("Route via " + self.strategy.getName());
        Say("Distance: " + distance + " km");
        Say("Estimated time: " + time + " hours");
    }

    hide func calculateDistance(route: [Point]) -> Number {
        // Sum distances between waypoints
        return 10.0;  // Simplified
    }
}
```

Using the navigator:

```rust
func start() {
    var home = Point(0.0, 0.0);
    var office = Point(10.0, 5.0);

    var navigator = Navigator(DrivingStrategy());
    navigator.navigate(home, office);
    // Calculating driving route...
    // Route via Driving
    // Distance: 10 km
    // Estimated time: 0.2 hours

    // User wants to bike today
    navigator.setStrategy(BikingStrategy());
    navigator.navigate(home, office);
    // Switched to Biking
    // Calculating biking route...
    // Route via Biking
    // Distance: 10 km
    // Estimated time: 0.67 hours

    // Compare all options
    var strategies: [RouteStrategy] = [
        DrivingStrategy(),
        WalkingStrategy(),
        BikingStrategy(),
        TransitStrategy()
    ];

    for strategy in strategies {
        navigator.setStrategy(strategy);
        navigator.navigate(home, office);
        Say("---");
    }
}
```

### Strategy Benefits

**Algorithms are interchangeable**: Switch strategies without changing the Navigator.

**Easy to add new strategies**: Create a new entity implementing the interface. No existing code changes.

**Algorithms are isolated**: Each strategy is a self-contained entity. Test them independently.

**No conditionals**: Instead of if-else chains based on mode, the strategy object handles behavior directly.

### Another Example: Sorting Strategies

```rust
interface SortStrategy {
    func sort(items: [Integer]) -> [Integer];
    func getName() -> String;
}

entity BubbleSort implements SortStrategy {
    func sort(items: [Integer]) -> [Integer] {
        // Bubble sort implementation
        var result = items.copy();
        var n = result.length;
        for i in 0..n {
            for j in 0..(n - i - 1) {
                if result[j] > result[j + 1] {
                    var temp = result[j];
                    result[j] = result[j + 1];
                    result[j + 1] = temp;
                }
            }
        }
        return result;
    }

    func getName() -> String { return "Bubble Sort"; }
}

entity QuickSort implements SortStrategy {
    func sort(items: [Integer]) -> [Integer] {
        // Quick sort implementation (simplified)
        if items.length <= 1 {
            return items;
        }
        // ... actual quicksort logic
        return items;
    }

    func getName() -> String { return "Quick Sort"; }
}

entity DataProcessor {
    hide sortStrategy: SortStrategy;

    expose func init(strategy: SortStrategy) {
        self.sortStrategy = strategy;
    }

    func setSortStrategy(strategy: SortStrategy) {
        self.sortStrategy = strategy;
    }

    func processData(data: [Integer]) -> [Integer] {
        Say("Sorting with " + self.sortStrategy.getName());
        return self.sortStrategy.sort(data);
    }
}
```

### When to Use Strategy

**Use when:**
- You have multiple algorithms for the same task
- You want to switch algorithms at runtime
- Related algorithms have different implementations but same interface
- You want to avoid conditional logic for algorithm selection

**Don't use when:**
- There's only one algorithm (no variation)
- The algorithm never changes at runtime
- The algorithms are trivially different

---

## Observer: Notify Me When Things Change

### The Problem

You're building a spreadsheet. Cell A1 contains a value. Cells B1, C1, and D1 all have formulas that depend on A1. When the user changes A1, all three formula cells need to recalculate.

How does A1 tell the formula cells to update? If A1 directly calls methods on B1, C1, D1, it needs to know about all of them. But cells might be added or removed. A1 shouldn't have to track every cell that depends on it.

Or imagine a weather monitoring system. When temperature data arrives, multiple systems need to react: the display updates, the alert system checks for dangerous conditions, the logger records the reading, the mobile app pushes notifications. The temperature sensor shouldn't know about all these systems.

### Real-World Analogy

Think of subscribing to a YouTube channel. You don't constantly check if new videos exist. Instead, you subscribe once, and YouTube notifies you when new content appears. Many people can subscribe to the same channel. The creator doesn't need to know who's subscribed — they just upload, and all subscribers get notified.

This is the publish-subscribe model. The channel "publishes" content; subscribers receive it automatically.

### The Solution

The Observer pattern defines a one-to-many dependency: when one object changes, all dependent objects are notified:

```rust
interface Observer {
    func onUpdate(event: String, data: any);
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
        // Remove observer from list
        var newList: [Observer] = [];
        for obs in self.observers {
            if obs != observer {
                newList.push(obs);
            }
        }
        self.observers = newList;
    }

    func notify(event: String, data: any) {
        for observer in self.observers {
            observer.onUpdate(event, data);
        }
    }
}
```

Now let's build a stock price monitoring system:

```rust
bind Viper.Terminal;
bind Viper.Math;
bind Viper.Time;

// The subject: stock price
entity StockPrice extends Subject {
    hide symbol: String;
    hide price: Number;

    expose func init(symbol: String, initialPrice: Number) {
        super();
        self.symbol = symbol;
        self.price = initialPrice;
    }

    func getPrice() -> Number {
        return self.price;
    }

    func getSymbol() -> String {
        return self.symbol;
    }

    func setPrice(newPrice: Number) {
        var oldPrice = self.price;
        self.price = newPrice;

        // Notify all observers
        self.notify("price_change", {
            "symbol": self.symbol,
            "oldPrice": oldPrice,
            "newPrice": newPrice,
            "change": newPrice - oldPrice
        });
    }
}

// Various observers
entity PriceDisplay implements Observer {
    hide name: String;

    expose func init(name: String) {
        self.name = name;
    }

    func onUpdate(event: String, data: any) {
        if event == "price_change" {
            Say(
                "[" + self.name + "] " +
                data.symbol + ": $" + data.newPrice +
                " (" + (data.change >= 0 ? "+" : "") + data.change + ")"
            );
        }
    }
}

entity AlertSystem implements Observer {
    hide threshold: Number;

    expose func init(threshold: Number) {
        self.threshold = threshold;
    }

    func onUpdate(event: String, data: any) {
        if event == "price_change" {
            if abs(data.change) > self.threshold {
                Say(
                    "ALERT: Large price movement in " + data.symbol +
                    " (change: " + data.change + ")"
                );
            }
        }
    }
}

entity TradeLogger implements Observer {
    hide logFile: String;

    expose func init(logFile: String) {
        self.logFile = logFile;
    }

    func onUpdate(event: String, data: any) {
        if event == "price_change" {
            // In real code, use Time.DateTime.Now() for timestamp
            var logEntry = data.symbol + "," + data.oldPrice + "," + data.newPrice;
            Say("[LOG] " + logEntry);
            // In real code: append(self.logFile, logEntry);
        }
    }
}

entity AutoTrader implements Observer {
    hide buyThreshold: Number;
    hide sellThreshold: Number;

    expose func init(buyThreshold: Number, sellThreshold: Number) {
        self.buyThreshold = buyThreshold;
        self.sellThreshold = sellThreshold;
    }

    func onUpdate(event: String, data: any) {
        if event == "price_change" {
            if data.newPrice < self.buyThreshold {
                Say(
                    "AUTO-BUY: " + data.symbol + " at $" + data.newPrice
                );
            } else if data.newPrice > self.sellThreshold {
                Say(
                    "AUTO-SELL: " + data.symbol + " at $" + data.newPrice
                );
            }
        }
    }
}
```

Using the system:

```rust
func start() {
    // Create a stock
    var apple = StockPrice("AAPL", 150.0);

    // Subscribe various observers
    apple.subscribe(PriceDisplay("Main Screen"));
    apple.subscribe(PriceDisplay("Mobile App"));
    apple.subscribe(AlertSystem(5.0));  // Alert on changes > $5
    apple.subscribe(TradeLogger("trades.csv"));
    apple.subscribe(AutoTrader(140.0, 160.0));

    // Price changes — all observers react automatically
    apple.setPrice(152.0);
    // [Main Screen] AAPL: $152 (+2)
    // [Mobile App] AAPL: $152 (+2)
    // [LOG] 2024-01-15 10:30:00,AAPL,150,152

    Say("---");

    apple.setPrice(158.0);
    // [Main Screen] AAPL: $158 (+6)
    // [Mobile App] AAPL: $158 (+6)
    // ALERT: Large price movement in AAPL (change: 6)
    // [LOG] 2024-01-15 10:31:00,AAPL,152,158

    Say("---");

    apple.setPrice(161.0);
    // [Main Screen] AAPL: $161 (+3)
    // [Mobile App] AAPL: $161 (+3)
    // [LOG] 2024-01-15 10:32:00,AAPL,158,161
    // AUTO-SELL: AAPL at $161
}
```

The `StockPrice` doesn't know what observers do. The observers don't know about each other. They're completely decoupled, communicating only through the event system.

### Observer in Game Events

```rust
entity GameEventSystem extends Subject {
    // Singleton for global game events
    hide static instance: GameEventSystem? = null;

    hide func init() {
        super();
    }

    static func getInstance() -> GameEventSystem {
        if GameEventSystem.instance == null {
            GameEventSystem.instance = GameEventSystem();
        }
        return GameEventSystem.instance;
    }

    // Convenience methods for common events
    func playerDied() {
        self.notify("player_died", {});
    }

    func enemyKilled(enemyType: String, points: Integer) {
        self.notify("enemy_killed", {
            "type": enemyType,
            "points": points
        });
    }

    func levelCompleted(level: Integer, time: Number) {
        self.notify("level_completed", {
            "level": level,
            "time": time
        });
    }
}

entity ScoreManager implements Observer {
    hide score: Integer = 0;

    func onUpdate(event: String, data: any) {
        if event == "enemy_killed" {
            self.score += data.points;
            Say("Score: " + self.score);
        } else if event == "level_completed" {
            var bonus = 1000 - (data.time as Integer * 10);
            if bonus > 0 {
                self.score += bonus;
                Say("Time bonus: " + bonus);
            }
        }
    }
}

entity SoundManager implements Observer {
    func onUpdate(event: String, data: any) {
        if event == "player_died" {
            Say("Playing: death_sound.wav");
        } else if event == "enemy_killed" {
            Say("Playing: hit_sound.wav");
        } else if event == "level_completed" {
            Say("Playing: victory_fanfare.wav");
        }
    }
}

entity AchievementSystem implements Observer {
    hide enemiesKilled: Integer = 0;

    func onUpdate(event: String, data: any) {
        if event == "enemy_killed" {
            self.enemiesKilled += 1;
            if self.enemiesKilled == 10 {
                Say("Achievement Unlocked: Novice Hunter");
            } else if self.enemiesKilled == 100 {
                Say("Achievement Unlocked: Veteran Slayer");
            }
        }
    }
}
```

### When to Use Observer

**Use when:**
- Multiple objects need to react to another object's changes
- You don't know in advance how many observers there will be
- Observers might be added or removed at runtime
- You want loose coupling between the subject and its dependents

**Don't use when:**
- There's only one observer (just call a method directly)
- The relationship is known and fixed at compile time
- Notification order matters (observer order is typically undefined)

---

## Command: Actions as Objects

### The Problem

You're building a text editor. Users can type, delete, cut, paste. They also expect to undo and redo actions. How do you implement undo?

You could save the entire document after every change, then restore previous versions. That works but wastes memory.

Better: remember what actions were taken and how to reverse them. But actions are operations, not data. How do you "remember" an action?

Also consider: you might want to queue actions, log them, replay them, or execute them later. Actions feel like things you'd want to manipulate as objects.

### Real-World Analogy

Think of a restaurant order. When you order food, the waiter writes it on a ticket. That ticket is an *object* representing the command "make this food." The ticket goes to the kitchen queue. Later, a cook picks up the ticket and executes the command. The ticket can also be used to verify the order, cancel it, or even reverse it ("I changed my mind, cancel the pasta").

The order ticket turns an action (make food) into an object (the written ticket).

### The Solution

The Command pattern encapsulates actions as objects, enabling undo, queueing, and logging:

```rust
bind Viper.Terminal;

interface Command {
    func execute();
    func undo();
    func describe() -> String;
}

entity TextEditor {
    hide content: String;

    expose func init() {
        self.content = "";
    }

    func getText() -> String {
        return self.content;
    }

    func insertAt(position: Integer, text: String) {
        var before = self.content.substring(0, position);
        var after = self.content.substring(position);
        self.content = before + text + after;
    }

    func deleteAt(position: Integer, length: Integer) {
        var before = self.content.substring(0, position);
        var after = self.content.substring(position + length);
        self.content = before + after;
    }

    func display() {
        Say("Document: \"" + self.content + "\"");
    }
}

// Insert text command
entity InsertCommand implements Command {
    hide editor: TextEditor;
    hide position: Integer;
    hide text: String;

    expose func init(editor: TextEditor, position: Integer, text: String) {
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

    func describe() -> String {
        return "Insert \"" + self.text + "\" at position " + self.position;
    }
}

// Delete text command
entity DeleteCommand implements Command {
    hide editor: TextEditor;
    hide position: Integer;
    hide deletedText: String;  // Remember what was deleted for undo

    expose func init(editor: TextEditor, position: Integer, length: Integer) {
        self.editor = editor;
        self.position = position;
        // Save the text we're about to delete
        self.deletedText = editor.getText().substring(position, position + length);
    }

    func execute() {
        self.editor.deleteAt(self.position, self.deletedText.length);
    }

    func undo() {
        self.editor.insertAt(self.position, self.deletedText);
    }

    func describe() -> String {
        return "Delete \"" + self.deletedText + "\" at position " + self.position;
    }
}

// Replace text command
entity ReplaceCommand implements Command {
    hide editor: TextEditor;
    hide position: Integer;
    hide oldText: String;
    hide newText: String;

    expose func init(editor: TextEditor, position: Integer, length: Integer, newText: String) {
        self.editor = editor;
        self.position = position;
        self.oldText = editor.getText().substring(position, position + length);
        self.newText = newText;
    }

    func execute() {
        self.editor.deleteAt(self.position, self.oldText.length);
        self.editor.insertAt(self.position, self.newText);
    }

    func undo() {
        self.editor.deleteAt(self.position, self.newText.length);
        self.editor.insertAt(self.position, self.oldText);
    }

    func describe() -> String {
        return "Replace \"" + self.oldText + "\" with \"" + self.newText + "\"";
    }
}

// Command history manager
entity CommandHistory {
    hide commands: [Command];
    hide undoneCommands: [Command];  // For redo

    expose func init() {
        self.commands = [];
        self.undoneCommands = [];
    }

    func execute(command: Command) {
        command.execute();
        self.commands.push(command);
        // Clear redo stack when new command is executed
        self.undoneCommands = [];
        Say("Executed: " + command.describe());
    }

    func undo() {
        if self.commands.length == 0 {
            Say("Nothing to undo");
            return;
        }
        var command = self.commands.pop();
        command.undo();
        self.undoneCommands.push(command);
        Say("Undone: " + command.describe());
    }

    func redo() {
        if self.undoneCommands.length == 0 {
            Say("Nothing to redo");
            return;
        }
        var command = self.undoneCommands.pop();
        command.execute();
        self.commands.push(command);
        Say("Redone: " + command.describe());
    }

    func showHistory() {
        Say("Command history:");
        for i, cmd in self.commands.enumerate() {
            Say("  " + i + ": " + cmd.describe());
        }
    }
}
```

Using the editor:

```rust
bind Viper.Terminal;

func start() {
    var editor = TextEditor();
    var history = CommandHistory();

    // Type some text
    history.execute(InsertCommand(editor, 0, "Hello"));
    editor.display();  // Document: "Hello"

    history.execute(InsertCommand(editor, 5, " World"));
    editor.display();  // Document: "Hello World"

    history.execute(InsertCommand(editor, 5, ","));
    editor.display();  // Document: "Hello, World"

    // Replace "World" with "Viper"
    history.execute(ReplaceCommand(editor, 7, 5, "Viper"));
    editor.display();  // Document: "Hello, Viper"

    Say("--- Undo ---");

    history.undo();
    editor.display();  // Document: "Hello, World"

    history.undo();
    editor.display();  // Document: "Hello World"

    Say("--- Redo ---");

    history.redo();
    editor.display();  // Document: "Hello, World"

    Say("--- History ---");
    history.showHistory();
}
```

### Command for Macro Recording

Commands can be grouped and replayed:

```rust
entity MacroCommand implements Command {
    hide commands: [Command];
    hide name: String;

    expose func init(name: String) {
        self.name = name;
        self.commands = [];
    }

    func add(command: Command) {
        self.commands.push(command);
    }

    func execute() {
        for cmd in self.commands {
            cmd.execute();
        }
    }

    func undo() {
        // Undo in reverse order
        for i in (self.commands.length - 1)..(-1) {
            self.commands[i].undo();
        }
    }

    func describe() -> String {
        return "Macro: " + self.name + " (" + self.commands.length + " commands)";
    }
}

// Record a macro
func recordMacro(editor: TextEditor) -> MacroCommand {
    var macro = MacroCommand("Add greeting");
    macro.add(InsertCommand(editor, 0, "Dear Sir or Madam,\n\n"));
    macro.add(InsertCommand(editor, editor.getText().length, "\n\nSincerely,\nYour Name"));
    return macro;
}
```

### When to Use Command

**Use when:**
- You need undo/redo functionality
- You want to queue operations for later execution
- You want to log or audit actions
- You need to support macros (groups of commands)
- Operations need to be parameterized

**Don't use when:**
- Operations don't need to be undone or queued
- The action is simple and direct
- You don't need the flexibility of command objects

---

## State: Behavior Changes with State

### The Problem

You're programming a vending machine. Its behavior depends entirely on its state:

- **Idle**: Waiting for coins. Accepts coins, displays items.
- **Has Money**: Coins inserted. Can select item or return money.
- **Dispensing**: Item selected. Vends item, makes change, returns to idle.
- **Out of Stock**: No items. Refuses coins, displays message.

You could handle this with flags and conditionals:

```rust
func insertCoin(amount: Integer) {
    if self.state == "idle" {
        self.balance += amount;
        self.state = "has_money";
    } else if self.state == "has_money" {
        self.balance += amount;
    } else if self.state == "dispensing" {
        // Reject coin
    } else if self.state == "out_of_stock" {
        // Reject and return coin
    }
}

func selectItem(item: String) {
    if self.state == "idle" {
        // Display message: insert coins first
    } else if self.state == "has_money" {
        if self.balance >= self.getPrice(item) {
            self.state = "dispensing";
            self.dispense(item);
            // ...
        }
    }
    // ... more conditionals
}
```

Every method needs to check the state and behave differently. As states and actions grow, this becomes a tangled mess of nested conditionals.

### Real-World Analogy

Think of a person's relationship with work. When you're employed, "go to work" means commute to the office and do your job. When you're unemployed, "go to work" means search job boards and send applications. When you're retired, "go to work" might mean volunteer or garden.

Same action, completely different behavior based on state. The state determines what actions mean.

### The Solution

The State pattern represents each state as an object that handles behavior for that state:

```rust
bind Viper.Terminal;

interface VendingState {
    func insertCoin(machine: VendingMachine, amount: Integer);
    func selectItem(machine: VendingMachine, item: String);
    func dispense(machine: VendingMachine);
    func returnCoins(machine: VendingMachine);
    func getName() -> String;
}

entity IdleState implements VendingState {
    func insertCoin(machine: VendingMachine, amount: Integer) {
        machine.addBalance(amount);
        Say("Inserted: $" + amount + ". Balance: $" + machine.getBalance());
        machine.setState(HasMoneyState());
    }

    func selectItem(machine: VendingMachine, item: String) {
        Say("Please insert coins first");
    }

    func dispense(machine: VendingMachine) {
        Say("No item selected");
    }

    func returnCoins(machine: VendingMachine) {
        Say("No coins to return");
    }

    func getName() -> String { return "Idle"; }
}

entity HasMoneyState implements VendingState {
    func insertCoin(machine: VendingMachine, amount: Integer) {
        machine.addBalance(amount);
        Say("Added: $" + amount + ". Balance: $" + machine.getBalance());
    }

    func selectItem(machine: VendingMachine, item: String) {
        var price = machine.getPrice(item);
        if price == null {
            Say("Item not found: " + item);
            return;
        }

        if machine.getBalance() < price {
            Say("Insufficient funds. Need $" + price + ", have $" + machine.getBalance());
            return;
        }

        if machine.getStock(item) == 0 {
            Say("Sorry, " + item + " is out of stock");
            return;
        }

        machine.setSelectedItem(item);
        machine.setState(DispensingState());
        machine.getState().dispense(machine);  // Trigger dispensing
    }

    func dispense(machine: VendingMachine) {
        Say("Please select an item first");
    }

    func returnCoins(machine: VendingMachine) {
        var balance = machine.getBalance();
        machine.setBalance(0);
        Say("Returning $" + balance);
        machine.setState(IdleState());
    }

    func getName() -> String { return "Has Money"; }
}

entity DispensingState implements VendingState {
    func insertCoin(machine: VendingMachine, amount: Integer) {
        Say("Please wait, dispensing in progress");
        // Could queue the coin for later
    }

    func selectItem(machine: VendingMachine, item: String) {
        Say("Please wait, dispensing in progress");
    }

    func dispense(machine: VendingMachine) {
        var item = machine.getSelectedItem();
        var price = machine.getPrice(item);

        // Vend the item
        machine.decrementStock(item);
        Say("Dispensing: " + item);

        // Calculate change
        var change = machine.getBalance() - price;
        machine.setBalance(0);
        machine.setSelectedItem(null);

        if change > 0 {
            Say("Change: $" + change);
        }

        // Check if we should go to out-of-stock state
        if machine.isEmpty() {
            machine.setState(OutOfStockState());
        } else {
            machine.setState(IdleState());
        }
    }

    func returnCoins(machine: VendingMachine) {
        Say("Please wait, dispensing in progress");
    }

    func getName() -> String { return "Dispensing"; }
}

entity OutOfStockState implements VendingState {
    func insertCoin(machine: VendingMachine, amount: Integer) {
        Say("Sorry, machine is empty. Returning your $" + amount);
    }

    func selectItem(machine: VendingMachine, item: String) {
        Say("Sorry, machine is empty");
    }

    func dispense(machine: VendingMachine) {
        Say("Nothing to dispense");
    }

    func returnCoins(machine: VendingMachine) {
        Say("No coins inserted");
    }

    func getName() -> String { return "Out of Stock"; }
}

entity VendingMachine {
    hide state: VendingState;
    hide balance: Integer;
    hide selectedItem: String?;
    hide inventory: {String: Integer};  // item -> quantity
    hide prices: {String: Integer};     // item -> price

    expose func init() {
        self.state = IdleState();
        self.balance = 0;
        self.selectedItem = null;
        self.inventory = {};
        self.prices = {};
    }

    func stock(item: String, quantity: Integer, price: Integer) {
        self.inventory[item] = quantity;
        self.prices[item] = price;

        // If we were out of stock, go back to idle
        if self.state.getName() == "Out of Stock" {
            self.state = IdleState();
        }
    }

    // Delegate all actions to current state
    func insertCoin(amount: Integer) {
        Say("[" + self.state.getName() + "] Insert coin: $" + amount);
        self.state.insertCoin(self, amount);
    }

    func selectItem(item: String) {
        Say("[" + self.state.getName() + "] Select: " + item);
        self.state.selectItem(self, item);
    }

    func returnCoins() {
        Say("[" + self.state.getName() + "] Return coins");
        self.state.returnCoins(self);
    }

    // Internal methods used by states
    func setState(newState: VendingState) {
        self.state = newState;
    }

    func getState() -> VendingState {
        return self.state;
    }

    func getBalance() -> Integer { return self.balance; }
    func setBalance(amount: Integer) { self.balance = amount; }
    func addBalance(amount: Integer) { self.balance += amount; }

    func getSelectedItem() -> String? { return self.selectedItem; }
    func setSelectedItem(item: String?) { self.selectedItem = item; }

    func getPrice(item: String) -> Integer? { return self.prices[item]; }
    func getStock(item: String) -> Integer { return self.inventory[item] ?? 0; }

    func decrementStock(item: String) {
        self.inventory[item] = self.inventory[item] - 1;
    }

    func isEmpty() -> Boolean {
        for item, qty in self.inventory {
            if qty > 0 { return false; }
        }
        return true;
    }
}
```

Using the vending machine:

```rust
func start() {
    var machine = VendingMachine();
    machine.stock("Cola", 3, 150);    // $1.50
    machine.stock("Chips", 2, 100);   // $1.00
    machine.stock("Candy", 5, 75);    // $0.75

    // Try to select without coins
    machine.selectItem("Cola");
    // [Idle] Select: Cola
    // Please insert coins first

    // Insert coins
    machine.insertCoin(100);
    // [Idle] Insert coin: $100
    // Inserted: $100. Balance: $100

    machine.insertCoin(100);
    // [Has Money] Insert coin: $100
    // Added: $100. Balance: $200

    // Buy Cola
    machine.selectItem("Cola");
    // [Has Money] Select: Cola
    // Dispensing: Cola
    // Change: $50

    // Machine is back to idle
    machine.selectItem("Chips");
    // [Idle] Select: Chips
    // Please insert coins first

    // Insert exact change
    machine.insertCoin(100);
    machine.selectItem("Chips");
    // Dispensing: Chips

    // Change mind, get coins back
    machine.insertCoin(200);
    machine.returnCoins();
    // [Has Money] Return coins
    // Returning $200
}
```

### State vs. Strategy

State and Strategy look similar (both delegate to an interface), but they solve different problems:

- **Strategy**: You *choose* which algorithm to use. The algorithm doesn't change by itself.
- **State**: The object's state changes based on events, often automatically. States trigger transitions to other states.

In State, the state objects often change the context's state. In Strategy, the client sets the strategy.

### When to Use State

**Use when:**
- Object behavior changes dramatically based on internal state
- State transitions follow defined rules
- You have many conditional branches based on state
- States are well-defined and distinct

**Don't use when:**
- You only have 2-3 states with simple behavior
- State changes are rare or simple
- The conditionals are clearer than state objects

---

## Structural Patterns

Structural patterns deal with how objects are composed — how you build larger structures from smaller parts.

---

## Decorator: Adding Behavior Dynamically

### The Problem

You run a coffee shop with a software ordering system. You start with simple coffee:

```rust
entity Coffee {
    func cost() -> Number { return 2.00; }
    func description() -> String { return "Coffee"; }
}
```

But customers want options: milk, sugar, whipped cream, vanilla, caramel, oat milk, extra shot... You could create subentities:

```rust
entity CoffeeWithMilk extends Coffee { ... }
entity CoffeeWithSugar extends Coffee { ... }
entity CoffeeWithMilkAndSugar extends Coffee { ... }
entity CoffeeWithMilkAndVanilla extends Coffee { ... }
entity CoffeeWithMilkAndSugarAndVanilla extends Coffee { ... }
// ... explosion of combinations
```

With 10 options, you'd need hundreds of entities to cover all combinations. That's unmanageable.

### Real-World Analogy

Think of a plain t-shirt. You can add things to it: iron-on patches, embroidery, screen printing, rhinestones. Each addition *decorates* the shirt without changing the base shirt. You can combine them however you like. And crucially, you're still selling a t-shirt — it has all the properties of a t-shirt plus the decorations.

### The Solution

The Decorator pattern wraps objects to add behavior dynamically:

```rust
bind Viper.Terminal;

interface Beverage {
    func cost() -> Number;
    func description() -> String;
}

// Base beverages
entity Espresso implements Beverage {
    func cost() -> Number { return 2.00; }
    func description() -> String { return "Espresso"; }
}

entity HouseBlend implements Beverage {
    func cost() -> Number { return 1.50; }
    func description() -> String { return "House Blend Coffee"; }
}

entity Decaf implements Beverage {
    func cost() -> Number { return 1.75; }
    func description() -> String { return "Decaf Coffee"; }
}

entity Tea implements Beverage {
    func cost() -> Number { return 1.25; }
    func description() -> String { return "Tea"; }
}

// Abstract decorator
entity BeverageDecorator implements Beverage {
    hide beverage: Beverage;

    expose func init(beverage: Beverage) {
        self.beverage = beverage;
    }

    func cost() -> Number {
        return self.beverage.cost();
    }

    func description() -> String {
        return self.beverage.description();
    }
}

// Concrete decorators
entity Milk extends BeverageDecorator {
    expose func init(beverage: Beverage) {
        super(beverage);
    }

    func cost() -> Number {
        return self.beverage.cost() + 0.50;
    }

    func description() -> String {
        return self.beverage.description() + ", Milk";
    }
}

entity Sugar extends BeverageDecorator {
    expose func init(beverage: Beverage) {
        super(beverage);
    }

    func cost() -> Number {
        return self.beverage.cost() + 0.20;
    }

    func description() -> String {
        return self.beverage.description() + ", Sugar";
    }
}

entity WhippedCream extends BeverageDecorator {
    expose func init(beverage: Beverage) {
        super(beverage);
    }

    func cost() -> Number {
        return self.beverage.cost() + 0.75;
    }

    func description() -> String {
        return self.beverage.description() + ", Whipped Cream";
    }
}

entity Vanilla extends BeverageDecorator {
    expose func init(beverage: Beverage) {
        super(beverage);
    }

    func cost() -> Number {
        return self.beverage.cost() + 0.60;
    }

    func description() -> String {
        return self.beverage.description() + ", Vanilla";
    }
}

entity ExtraShot extends BeverageDecorator {
    expose func init(beverage: Beverage) {
        super(beverage);
    }

    func cost() -> Number {
        return self.beverage.cost() + 0.80;
    }

    func description() -> String {
        return self.beverage.description() + ", Extra Shot";
    }
}
```

Now any combination is possible:

```rust
bind Viper.Terminal;

func start() {
    // Simple espresso
    var drink1: Beverage = Espresso();
    Say(drink1.description() + " - $" + drink1.cost());
    // Espresso - $2.00

    // Espresso with milk
    var drink2: Beverage = Espresso();
    drink2 = Milk(drink2);
    Say(drink2.description() + " - $" + drink2.cost());
    // Espresso, Milk - $2.50

    // Fancy latte: espresso + milk + vanilla + whipped cream
    var drink3: Beverage = Espresso();
    drink3 = Milk(drink3);
    drink3 = Vanilla(drink3);
    drink3 = WhippedCream(drink3);
    Say(drink3.description() + " - $" + drink3.cost());
    // Espresso, Milk, Vanilla, Whipped Cream - $3.85

    // Double shot with milk and extra sugar
    var drink4: Beverage = Espresso();
    drink4 = ExtraShot(drink4);
    drink4 = Milk(drink4);
    drink4 = Sugar(drink4);
    drink4 = Sugar(drink4);  // Extra sugar!
    Say(drink4.description() + " - $" + drink4.cost());
    // Espresso, Extra Shot, Milk, Sugar, Sugar - $4.00

    // Tea with milk
    var drink5: Beverage = Tea();
    drink5 = Milk(drink5);
    Say(drink5.description() + " - $" + drink5.cost());
    // Tea, Milk - $1.75
}
```

Each decorator wraps a beverage, adds its own cost and description, and delegates to the wrapped object. You can stack as many as you want.

### Decorator for Feature Composition

Decorators work beyond pricing. Here's an I/O example:

```rust
bind Viper.Terminal;

interface DataStream {
    func write(data: String);
    func read() -> String;
}

entity FileStream implements DataStream {
    hide content: String = "";

    func write(data: String) {
        self.content += data;
    }

    func read() -> String {
        return self.content;
    }
}

entity EncryptionDecorator implements DataStream {
    hide stream: DataStream;

    expose func init(stream: DataStream) {
        self.stream = stream;
    }

    func write(data: String) {
        var encrypted = self.encrypt(data);
        self.stream.write(encrypted);
    }

    func read() -> String {
        var encrypted = self.stream.read();
        return self.decrypt(encrypted);
    }

    hide func encrypt(data: String) -> String {
        // Simple Caesar cipher for demonstration
        var result = "";
        for char in data {
            result += (char.code + 3).toChar();
        }
        return result;
    }

    hide func decrypt(data: String) -> String {
        var result = "";
        for char in data {
            result += (char.code - 3).toChar();
        }
        return result;
    }
}

entity CompressionDecorator implements DataStream {
    hide stream: DataStream;

    expose func init(stream: DataStream) {
        self.stream = stream;
    }

    func write(data: String) {
        var compressed = self.compress(data);
        self.stream.write(compressed);
    }

    func read() -> String {
        var compressed = self.stream.read();
        return self.decompress(compressed);
    }

    hide func compress(data: String) -> String {
        // Simplified run-length encoding
        return "[compressed:" + data.length + "]" + data;
    }

    hide func decompress(data: String) -> String {
        // Extract original from compressed format
        return data.substring(data.indexOf("]") + 1);
    }
}

entity LoggingDecorator implements DataStream {
    hide stream: DataStream;

    expose func init(stream: DataStream) {
        self.stream = stream;
    }

    func write(data: String) {
        Say("Writing " + data.length + " bytes");
        self.stream.write(data);
    }

    func read() -> String {
        var data = self.stream.read();
        Say("Read " + data.length + " bytes");
        return data;
    }
}

// Usage
func start() {
    // Plain file stream
    var stream: DataStream = FileStream();

    // Add encryption
    stream = EncryptionDecorator(stream);

    // Add compression
    stream = CompressionDecorator(stream);

    // Add logging
    stream = LoggingDecorator(stream);

    // Now writing goes through: logging -> compression -> encryption -> file
    stream.write("Secret message");
    // Writing 14 bytes

    // Reading goes through: file -> decryption -> decompression -> logging
    var message = stream.read();
    // Read 14 bytes
    Say(message);  // Secret message
}
```

### When to Use Decorator

**Use when:**
- You want to add responsibilities to objects dynamically
- Subentity explosion would be impractical
- You need flexible combinations of features
- You want to add/remove features at runtime

**Don't use when:**
- Combinations are fixed and known at compile time
- You only have a few features (subentities might be simpler)
- The wrapping overhead matters for performance

---

## Patterns Working Together

Real systems don't use patterns in isolation. They combine multiple patterns to solve complex problems. Let's look at how patterns work together.

### A Game Entity System

This example combines Factory, Strategy, Observer, and State:

```rust
bind Viper.Terminal;
bind Viper.Math;

// Observer for game events
interface GameObserver {
    func onEvent(event: String, data: any);
}

entity GameEvents {
    hide static instance: GameEvents? = null;
    hide observers: [GameObserver];

    hide func init() {
        self.observers = [];
    }

    static func getInstance() -> GameEvents {
        if GameEvents.instance == null {
            GameEvents.instance = GameEvents();
        }
        return GameEvents.instance;
    }

    func subscribe(observer: GameObserver) {
        self.observers.push(observer);
    }

    func emit(event: String, data: any) {
        for obs in self.observers {
            obs.onEvent(event, data);
        }
    }
}

// Strategy for AI behavior
interface AIStrategy {
    func decide(entity: GameEntity, world: World) -> String;
}

entity AggressiveAI implements AIStrategy {
    func decide(entity: GameEntity, world: World) -> String {
        var player = world.getPlayer();
        if entity.distanceTo(player) < 100 {
            return "attack";
        }
        return "chase";
    }
}

entity DefensiveAI implements AIStrategy {
    func decide(entity: GameEntity, world: World) -> String {
        var player = world.getPlayer();
        if entity.health < 30 {
            return "flee";
        }
        if entity.distanceTo(player) < 50 {
            return "attack";
        }
        return "patrol";
    }
}

entity PassiveAI implements AIStrategy {
    func decide(entity: GameEntity, world: World) -> String {
        return "wander";
    }
}

// State for entity condition
interface EntityState {
    func update(entity: GameEntity, world: World);
    func getName() -> String;
}

entity AliveState implements EntityState {
    func update(entity: GameEntity, world: World) {
        if entity.health <= 0 {
            entity.setState(DeadState());
            GameEvents.getInstance().emit("entity_died", { "entity": entity });
            return;
        }

        // Use AI strategy to decide action
        var action = entity.ai.decide(entity, world);
        entity.executeAction(action);
    }

    func getName() -> String { return "Alive"; }
}

entity DeadState implements EntityState {
    func update(entity: GameEntity, world: World) {
        // Dead entities don't update
    }

    func getName() -> String { return "Dead"; }
}

entity StunnedState implements EntityState {
    hide duration: Integer;

    expose func init(duration: Integer) {
        self.duration = duration;
    }

    func update(entity: GameEntity, world: World) {
        self.duration -= 1;
        if self.duration <= 0 {
            entity.setState(AliveState());
        }
        // Stunned entities can't act
    }

    func getName() -> String { return "Stunned"; }
}

// The game entity
entity GameEntity {
    name: String;
    health: Integer;
    x: Number;
    y: Number;
    hide ai: AIStrategy;
    hide state: EntityState;

    expose func init(name: String, health: Integer, ai: AIStrategy) {
        self.name = name;
        self.health = health;
        self.ai = ai;
        self.state = AliveState();
        self.x = 0.0;
        self.y = 0.0;
    }

    func setState(newState: EntityState) {
        Say(self.name + " state: " + self.state.getName() + " -> " + newState.getName());
        self.state = newState;
    }

    func setAI(ai: AIStrategy) {
        self.ai = ai;
    }

    func update(world: World) {
        self.state.update(self, world);
    }

    func takeDamage(amount: Integer) {
        self.health -= amount;
        Say(self.name + " takes " + amount + " damage. Health: " + self.health);
    }

    func distanceTo(other: GameEntity) -> Number {
        var dx = self.x - other.x;
        var dy = self.y - other.y;
        return Math.sqrt(dx * dx + dy * dy);
    }

    func executeAction(action: String) {
        Say(self.name + " performs: " + action);
    }
}

// Factory for creating entities
entity EntityFactory {
    static func createGoblin(x: Number, y: Number) -> GameEntity {
        var goblin = GameEntity("Goblin", 30, AggressiveAI());
        goblin.x = x;
        goblin.y = y;
        return goblin;
    }

    static func createOrc(x: Number, y: Number) -> GameEntity {
        var orc = GameEntity("Orc", 50, DefensiveAI());
        orc.x = x;
        orc.y = y;
        return orc;
    }

    static func createSlime(x: Number, y: Number) -> GameEntity {
        var slime = GameEntity("Slime", 20, PassiveAI());
        slime.x = x;
        slime.y = y;
        return slime;
    }

    static func createByType(type: String, x: Number, y: Number) -> GameEntity {
        if type == "goblin" { return EntityFactory.createGoblin(x, y); }
        if type == "orc" { return EntityFactory.createOrc(x, y); }
        if type == "slime" { return EntityFactory.createSlime(x, y); }
        throw Error("Unknown entity type: " + type);
    }
}

// Observer for scoring
entity ScoreTracker implements GameObserver {
    hide score: Integer = 0;

    func onEvent(event: String, data: any) {
        if event == "entity_died" {
            self.score += 100;
            Say("Score: " + self.score);
        }
    }
}
```

This system demonstrates:
- **Factory**: Creates different entities with appropriate configurations
- **Strategy**: Each entity has an AI strategy that can be swapped
- **State**: Entities have states (Alive, Dead, Stunned) that control behavior
- **Observer**: Game events notify interested parties (score tracker)
- **Singleton**: GameEvents provides global event system access

### Patterns Build on Patterns

Notice how the patterns support each other:
- The Factory decides which Strategy to assign
- The State delegates to the Strategy for decisions
- State changes emit events through the Observer
- Everything coordinates through a Singleton event system

This is how real software works. No single pattern solves everything, but together they create flexible, maintainable systems.

---

## Pattern Selection Guide

When facing a design problem, use this guide to identify applicable patterns:

| Problem | Pattern |
|---------|---------|
| Need exactly one instance | Singleton |
| Complex object creation | Factory |
| Many optional parameters | Builder |
| React to changes | Observer |
| Multiple algorithms, swap at runtime | Strategy |
| Undo/redo, command queuing | Command |
| Behavior varies with state | State |
| Combine features flexibly | Decorator |

But remember: patterns are tools, not goals. The question isn't "which pattern should I use?" but "do I have a problem that a pattern solves?"

---

## Common Anti-Patterns

Learning patterns is valuable, but misusing them is common. Watch for these anti-patterns:

### Pattern Overengineering

Creating a Factory for a single class that never changes. Building an Observer system for two objects that could just call methods. Adding Command objects for actions that never need undo.

**Symptom**: More infrastructure than actual logic.

**Cure**: Start simple. Add patterns when they solve real problems, not hypothetical ones.

### Golden Hammer

Using your favorite pattern everywhere. "We used Strategy on the last project, let's use it here too!" Every problem looks like a nail when you're holding a hammer.

**Symptom**: Same pattern appears throughout, even where simpler solutions exist.

**Cure**: Learn multiple patterns. Match the pattern to the problem, not vice versa.

### Speculative Generality

"What if we need to add five more enemy types later? Let's build a complex Factory with plugin registration."

**Symptom**: Elaborate systems for features that might never exist.

**Cure**: YAGNI — You Aren't Gonna Need It. Build what you need now. Refactor when requirements change.

### Cargo Cult Patterns

Copying pattern implementations without understanding why. "The tutorial used Observer here, so I will too."

**Symptom**: Patterns applied where they don't fit. Confusion about what the pattern achieves.

**Cure**: Understand the *problem* each pattern solves. If you can't explain why you need it, you probably don't.

---

## Summary

Design patterns are proven solutions to common problems:

- **Singleton**: Ensure one instance with global access. Use sparingly — it's global state.

- **Factory**: Centralize object creation. Hide concrete types behind interfaces.

- **Builder**: Construct complex objects step by step with readable, fluent API.

- **Strategy**: Encapsulate algorithms as interchangeable objects. Swap behavior at runtime.

- **Observer**: Notify multiple objects of changes. Decouple publishers from subscribers.

- **Command**: Turn actions into objects. Enable undo, queuing, and logging.

- **State**: Let objects change behavior when internal state changes. Replace conditional sprawl.

- **Decorator**: Add behavior by wrapping objects. Avoid subentity explosion.

Patterns are powerful, but use them wisely:
- Apply patterns to solve real problems you actually have
- Prefer simple solutions over pattern-heavy ones
- Combine patterns as needed — real systems use several together
- Don't force patterns where they don't fit

Learning patterns gives you a vocabulary, speeds up design, and connects you to decades of software wisdom. But the goal is always clear, maintainable code — patterns are a means to that end, not the end itself.

---

## Exercises

**Exercise 18.1**: Implement a Logger singleton that tracks messages with timestamps. Add methods for `info()`, `warn()`, and `error()` log levels.

**Exercise 18.2**: Create a DocumentFactory that creates different document types (PDF, Word, PlainText) based on file extension. Each document type should implement a common `Document` interface.

**Exercise 18.3**: Build a PizzaBuilder with methods like `size()`, `addTopping()`, `setCrust()`, `addCheese()`. The final `build()` method should return a complete Pizza with calculated price.

**Exercise 18.4**: Implement a weather station using Observer. The WeatherStation subject should notify DisplayPanel, AlertSystem, and StatisticsTracker observers when temperature, humidity, or pressure changes.

**Exercise 18.5**: Create a sorting system using Strategy. Implement at least three strategies (BubbleSort, QuickSort, MergeSort). A DataAnalyzer entity should use the strategy to sort data, with ability to switch strategies.

**Exercise 18.6**: Build a text editor with Command pattern. Implement InsertCharacter, DeleteCharacter, and ReplaceText commands. Include CommandHistory with undo and redo functionality.

**Exercise 18.7**: Create a media player with State pattern. States: Stopped, Playing, Paused. Actions: play, pause, stop. Each state should handle actions differently and transition to appropriate next states.

**Exercise 18.8**: Design a notification system using Decorator. Start with a basic Notification. Add decorators for: SMS, Email, Push, Slack. Each decorator adds its own delivery method. Test combining multiple decorators.

**Exercise 18.9** (Challenge): Build a simple game enemy system that combines Factory (create enemies), Strategy (AI behavior), State (alive/dead/stunned), and Observer (notify score system of kills). Create at least three enemy types with different AI strategies.

**Exercise 18.10** (Challenge): Design a document processing pipeline using Decorator. Start with a TextDocument. Create decorators for: SpellCheck, GrammarCheck, FormatHeaders, AddWatermark. Each decorator modifies the document content. Allow arbitrary combinations.

---

*We've completed Part III! You now understand object-oriented programming: entities, inheritance, interfaces, polymorphism, and common design patterns.*

*Part IV puts everything together to build real applications: graphics, games, networking, and more.*

*[Continue to Part IV: Real Applications ->](../part4-applications/19-graphics.md)*
