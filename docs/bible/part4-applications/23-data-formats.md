# Chapter 23: Data Formats

Every program you have ever used handles data. Your web browser downloads pages. Your text editor saves documents. Your game remembers your progress. Your music player reads song files. But here is a question that might seem trivial until you really think about it: *how does data get from one place to another?*

When you save a document and close your word processor, the document does not vanish. It persists on your hard drive, waiting for you to open it again tomorrow, next week, or next year. When you share that document with a friend, they can open it on a completely different computer running completely different software. How is this possible?

The answer is **data formats** --- agreed-upon ways to represent information as bytes. A data format is like a language that different programs speak. If two programs understand JSON, they can exchange JSON data. If your game saves its state in a well-defined format, a completely different program could read that save file.

This chapter is about the most important skill in practical programming: moving data between the world inside your program (variables, objects, arrays) and the world outside (files, networks, databases, other programs). This process has names: **serialization** (turning program data into a storable format) and **parsing** (turning stored data back into program data). Master these concepts, and you can build programs that remember, communicate, and interoperate.

---

## Why Data Formats Matter

Imagine you are building a game. Your player has a name, a level, health points, an inventory of items, and a position on the map:

```rust
value Player {
    name: string;
    level: i64;
    health: f64;
    inventory: [string];
    position: Position;
}

value Position {
    x: f64;
    y: f64;
}
```

Now imagine the player saves their game and quits. Tomorrow, they want to continue where they left off. How do you make that work?

The `Player` value exists in your computer's memory --- it is a structured arrangement of bytes that your program understands. But when your program ends, that memory is released. The data is gone. To persist it, you must write it to a file. But you cannot just dump raw memory to disk and hope for the best. That memory layout is specific to your program, your programming language, your computer's architecture. A different program could not read it. Even a future version of your own program might not read it if the data structure changes.

Instead, you need to translate your data into a **portable, well-defined format** that any program can understand. You need to agree on how to represent "this player is named Hero, is level 5, has 87.5 health points, carries a sword and shield, and stands at coordinates (100, 200)."

This is the fundamental challenge: bridging the gap between the structured world inside your program and the unstructured world of bytes that can be stored and transmitted.

---

## Mental Models for Serialization

Before we dive into specific formats, let us build intuition about what serialization actually means.

### The Suitcase Metaphor

Imagine you are packing for a trip. At home, your clothes live in drawers, organized by type: socks in one drawer, shirts in another, pants in a third. This is like data in your program --- structured, organized, easy to access.

But you cannot bring your dresser on the plane. You need to pack everything into a suitcase. You fold clothes, arrange them efficiently, and close the suitcase. This is **serialization**: taking structured data and packing it into a portable format.

When you arrive at your destination, you unpack. You take clothes out of the suitcase and organize them again --- maybe in a hotel dresser, maybe in a different arrangement than at home. This is **deserialization** (or **parsing**): taking the portable format and recreating structured data from it.

Key insights from this metaphor:
- **Packing loses some structure.** Your suitcase does not have drawers. The organization is simpler.
- **Unpacking requires knowing how things were packed.** If you packed shirts on top of pants, you expect that layout when unpacking.
- **Different trips might need different packing.** A weekend trip and a month-long journey require different approaches.

### The Translation Metaphor

Think of serialization as translation between languages. Your program thinks in "Zia" --- it has entities, values, arrays, and functions. A JSON file thinks in "JSON" --- it has objects, arrays, strings, and numbers. An XML file thinks in "XML" --- it has elements, attributes, and text content.

To save your player to JSON, you must translate from Zia to JSON:
- A `value` becomes a JSON object
- A `string` becomes a JSON string
- An `i64` becomes a JSON number
- An array becomes a JSON array

To load a player from JSON, you translate back:
- A JSON object becomes values for constructing a `Player`
- JSON strings become Zia strings
- JSON numbers become integers or floats

Some translations are lossy. JSON does not distinguish between integers and floating-point numbers --- they are all just "numbers." XML represents everything as text, so numbers like `42` are actually the characters `'4'` and `'2'`. When translating back, you must interpret the text as a number.

### The Contract Metaphor

A data format is a contract between the writer and the reader. The writer promises to produce data in a specific structure. The reader promises to interpret that structure correctly. If either party breaks the contract, communication fails.

When you design a save file format, you are establishing a contract:
- "The first field will be the player's name, a string."
- "The second field will be the player's level, an integer."
- "The inventory will be an array of item names."

Anyone who wants to read your save files must honor this contract. If you change the contract (add new fields, rename existing ones, reorder things), old readers might break. This is why versioning data formats matters --- you can check "is this file version 1 or version 2?" and handle each appropriately.

---

## The Landscape of Structured Data

Before examining specific formats, let us understand what "structured data" means and the fundamental choices every format makes.

### What Is Structured Data?

Structured data has organization. It is not just a soup of bytes or a stream of text --- it has identifiable parts with relationships between them.

Consider a contact:
```
Name: Alice Smith
Phone: 555-1234
Email: alice@example.com
```

This data has three fields: name, phone, and email. Each field has a label and a value. If we wanted to store multiple contacts, we would need a way to separate them. If contacts could have multiple phone numbers, we would need a way to represent lists.

Structured data typically involves:
- **Fields** (also called properties or attributes): named pieces of data
- **Values**: the actual data (strings, numbers, booleans)
- **Nesting**: data containing other data (a contact has an address, which has street, city, and zip)
- **Collections**: lists or sets of data (a person has multiple phone numbers)

### Text vs. Binary Formats

Every data format makes a fundamental choice: represent data as human-readable text or as compact binary.

**Text formats** (JSON, XML, CSV, INI) use printable characters. You can open them in a text editor and read them. They are easy to debug, easy to edit manually, and work across different computer systems because text is universal.

```json
{"name": "Hero", "level": 5, "health": 87.5}
```

You can see exactly what this says. If something is wrong, you can spot it.

**Binary formats** use raw bytes that do not correspond to printable characters. They are compact and fast but inscrutable to humans. Open a binary file in a text editor and you see gibberish.

```
56 49 50 52 00 00 00 01 00 00 00 05 48 65 72 6F
```

This might encode the same information but requires documentation to decode.

The choice depends on your needs:
- **Text formats** when humans need to read, edit, or debug the data
- **Binary formats** when file size or parsing speed is critical

For most applications, start with text formats. Their human-readability is invaluable for debugging and development. Only switch to binary when you have measured a performance problem.

### Schemas: Defining Expected Structure

A **schema** defines what structure data should have. It is like a form template: the form specifies which fields exist, what types they should be, which are required, and what values are acceptable.

Some formats have built-in schema support:
- **XML** can use XSD (XML Schema Definition) to define valid structures
- **JSON** can use JSON Schema to define valid structures

Some formats have no built-in schema:
- **CSV** is just rows and columns; the meaning is up to interpretation
- **INI** files have sections and key-value pairs but no formal types

Even without formal schemas, you have implicit expectations about structure. When your code reads `data["player"]["name"]`, you expect `data` to be an object with a `player` field that is also an object with a `name` field. These expectations form an informal schema.

Why schemas matter:
- **Validation**: Check that data matches expectations before processing
- **Documentation**: The schema tells you what data should look like
- **Interoperability**: If two systems agree on a schema, they can exchange data confidently

---

## JSON: The Universal Data Language

JSON (JavaScript Object Notation) has become the de facto standard for data exchange. APIs use it. Configuration files use it. Databases store it. If you learn one data format well, make it JSON.

### The Philosophy of JSON

JSON was designed to be simple. It has only a handful of concepts, all borrowed from common programming constructs:
- Objects (like dictionaries or entities)
- Arrays (ordered lists)
- Strings, numbers, booleans, and null

This simplicity is JSON's superpower. Every programming language can represent these concepts, so every programming language can work with JSON. There is no impedance mismatch, no awkward translation.

JSON is also human-readable. You can open a JSON file, understand its structure, and edit it by hand. This makes debugging vastly easier compared to binary formats.

### JSON Syntax Deep Dive

Let us examine JSON syntax precisely, because small details matter.

**Objects** are enclosed in curly braces `{ }` and contain key-value pairs:

```json
{
    "name": "Hero",
    "level": 5
}
```

Rules for objects:
- Keys must be strings (always in double quotes)
- Values can be any JSON type
- Key-value pairs are separated by commas
- No trailing comma after the last pair (unlike some languages)
- Order of keys is not guaranteed to be preserved

**Arrays** are enclosed in square brackets `[ ]` and contain ordered values:

```json
["sword", "shield", "potion"]
```

Rules for arrays:
- Elements can be any JSON type
- Elements are separated by commas
- No trailing comma after the last element
- Order is preserved

**Strings** are enclosed in double quotes:

```json
"Hello, World!"
```

Rules for strings:
- Must use double quotes, not single quotes
- Special characters must be escaped: `\"` for quote, `\\` for backslash, `\n` for newline, `\t` for tab
- Unicode characters can be escaped as `\uXXXX`

**Numbers** are written directly, with no quotes:

```json
42
3.14159
-17
2.5e10
```

Rules for numbers:
- No leading zeros (except for `0` itself or before decimal point)
- Decimal point must have digits on both sides
- Exponents use `e` or `E`
- No distinction between integers and floats at the format level

**Booleans** are the literals `true` and `false` (lowercase):

```json
true
false
```

**Null** is the literal `null` (lowercase):

```json
null
```

### Nesting: The Power of Composition

JSON shines when structures contain other structures:

```json
{
    "name": "Hero",
    "level": 5,
    "health": 87.5,
    "inventory": ["sword", "shield", "potion"],
    "position": {
        "x": 100.0,
        "y": 200.0
    },
    "skills": [
        {"name": "Slash", "damage": 10, "cooldown": 1.5},
        {"name": "Block", "damage": 0, "cooldown": 3.0},
        {"name": "Heal", "damage": -20, "cooldown": 10.0}
    ],
    "isAlive": true,
    "guild": null
}
```

This single JSON document represents complex, deeply nested data:
- The player has a `position` object nested inside
- The `inventory` is an array of strings
- The `skills` array contains objects, each with their own fields
- The `guild` is null, indicating the player is not in a guild

### Parsing JSON in Zia

Parsing means reading JSON text and creating program data structures from it:

```rust
import Viper.JSON;

func start() {
    var jsonText = '{"name": "Alice", "score": 100, "active": true}';

    // Parse the JSON text into a JSONValue
    var data = JSON.parse(jsonText);

    // Extract values with type conversions
    var name = data["name"].asString();
    var score = data["score"].asInt();
    var active = data["active"].asBool();

    Viper.Terminal.Say("Player: " + name);
    Viper.Terminal.Say("Score: " + score);
    Viper.Terminal.Say("Active: " + active);
}
```

Let us trace through this step by step:

1. **`JSON.parse(jsonText)`**: Takes the raw text and builds a tree structure representing the JSON. This returns a `JSONValue`, which is a generic container that could hold any JSON type.

2. **`data["name"]`**: Accesses the value associated with the key `"name"`. This returns another `JSONValue`.

3. **`.asString()`**: Converts the `JSONValue` to a Zia string. This is necessary because JSON is dynamically typed --- the parser does not know at compile time what types you will find.

The `.as*` methods perform type conversions:
- `.asString()` --- returns the value as a string
- `.asInt()` --- returns the value as an integer
- `.asFloat()` --- returns the value as a floating-point number
- `.asBool()` --- returns the value as a boolean
- `.asArray()` --- returns the value as an iterable array of JSONValues

### Creating JSON in Zia

To save program data as JSON, you build a JSON structure programmatically:

```rust
import Viper.JSON;

func start() {
    // Create a JSON object
    var player = JSON.object();

    // Add simple values
    player.set("name", "Hero");
    player.set("level", 5);
    player.set("health", 87.5);
    player.set("alive", true);

    // Create and add an array
    var inventory = JSON.array();
    inventory.add("sword");
    inventory.add("shield");
    inventory.add("potion");
    player.set("inventory", inventory);

    // Create and add a nested object
    var position = JSON.object();
    position.set("x", 100.0);
    position.set("y", 200.0);
    player.set("position", position);

    // Convert to JSON text
    var jsonText = player.toString();
    Viper.Terminal.Say(jsonText);
}
```

Output:
```json
{"name":"Hero","level":5,"health":87.5,"alive":true,"inventory":["sword","shield","potion"],"position":{"x":100.0,"y":200.0}}
```

The output is compact --- no unnecessary whitespace. This is efficient for storage and transmission.

### Pretty-Printing for Readability

Compact JSON is hard to read. For debugging, logs, or configuration files that humans edit, use pretty-printing:

```rust
var jsonText = player.toPrettyString();
Viper.Terminal.Say(jsonText);
```

Output:
```json
{
    "name": "Hero",
    "level": 5,
    "health": 87.5,
    "alive": true,
    "inventory": [
        "sword",
        "shield",
        "potion"
    ],
    "position": {
        "x": 100.0,
        "y": 200.0
    }
}
```

This is the same data, just formatted with indentation and newlines for human eyes.

### Converting Between Entities and JSON

For clean code, add serialization methods to your entities:

```rust
entity Player {
    name: string;
    level: i64;
    health: f64;
    inventory: [string];
    x: f64;
    y: f64;

    // Convert this Player to a JSON representation
    func toJSON() -> JSONValue {
        var obj = JSON.object();
        obj.set("name", self.name);
        obj.set("level", self.level);
        obj.set("health", self.health);

        var inv = JSON.array();
        for item in self.inventory {
            inv.add(item);
        }
        obj.set("inventory", inv);

        var pos = JSON.object();
        pos.set("x", self.x);
        pos.set("y", self.y);
        obj.set("position", pos);

        return obj;
    }

    // Create a Player from a JSON representation
    static func fromJSON(data: JSONValue) -> Player {
        var player = Player();
        player.name = data["name"].asString();
        player.level = data["level"].asInt();
        player.health = data["health"].asFloat();

        player.inventory = [];
        for item in data["inventory"].asArray() {
            player.inventory.push(item.asString());
        }

        player.x = data["position"]["x"].asFloat();
        player.y = data["position"]["y"].asFloat();

        return player;
    }
}
```

Usage becomes elegant:

```rust
// Save a player
func saveGame(player: Player, filename: string) {
    var json = player.toJSON().toPrettyString();
    Viper.File.writeText(filename, json);
}

// Load a player
func loadGame(filename: string) -> Player {
    var json = Viper.File.readText(filename);
    var data = JSON.parse(json);
    return Player.fromJSON(data);
}
```

Let us trace through saving and loading:

**Saving:**
1. `player.toJSON()` creates a JSONValue representing the player
2. `.toPrettyString()` converts that to formatted text
3. `File.writeText()` writes the text to disk

**Loading:**
1. `File.readText()` reads the raw text from disk
2. `JSON.parse()` parses the text into a JSONValue tree
3. `Player.fromJSON()` extracts values and constructs a Player

### When to Use JSON

JSON excels in these situations:

- **APIs and web services**: JSON is the standard for REST APIs
- **Configuration files**: Human-readable and easy to edit
- **Data exchange between programs**: Nearly universal support
- **Logging structured data**: Easy to parse logs later
- **NoSQL databases**: Many store JSON documents directly

JSON struggles in these situations:

- **Tabular data**: Use CSV instead
- **Performance-critical applications**: Consider binary formats
- **When comments are needed**: JSON has no comment syntax
- **When exact numeric precision matters**: JSON numbers are inherently imprecise

---

## CSV: Simple Tabular Data

CSV (Comma-Separated Values) is the simplest widely-used format for tabular data --- data that fits in rows and columns like a spreadsheet.

### What CSV Looks Like

```csv
name,level,health,x,y
Hero,5,87.5,100.0,200.0
Wizard,3,65.0,150.0,180.0
Warrior,7,120.0,80.0,220.0
```

The first row typically contains column headers (the names of the fields). Each subsequent row is one record. Values are separated by commas.

### The Simplicity and Limits of CSV

CSV is popular because it is simple. You can create it in any text editor. Spreadsheet programs like Excel read and write it. It is easy to generate with a loop.

But CSV has significant limitations:

**No hierarchy**: CSV is flat. You cannot nest objects inside objects. There is no way to represent "a player has multiple skills, each with its own properties."

**No types**: Everything is text. The number `5` and the string `"5"` look identical. You must decide when parsing whether to interpret something as a number.

**No standardization**: Despite the name, CSV files might use tabs, semicolons, or other delimiters. They might or might not have headers. They might handle special characters differently.

**Escaping is tricky**: What if a value contains a comma? What if it contains a newline? The rules for escaping exist but vary between implementations.

### Reading CSV

```rust
import Viper.CSV;

func start() {
    var csv = CSV.load("players.csv");

    for row in csv.rows() {
        var name = row["name"];
        var level = row["level"].toInt();
        var health = row["health"].toFloat();

        Viper.Terminal.Say(name + " (Level " + level + ", HP: " + health + ")");
    }
}
```

Step by step:
1. `CSV.load()` reads the file and parses it
2. `.rows()` returns an iterator over data rows (skipping the header)
3. Each row is a dictionary-like object where you access columns by name
4. String values like `level` must be explicitly converted to integers

### Writing CSV

```rust
import Viper.CSV;

func start() {
    // Create a CSV with column headers
    var csv = CSV.create(["name", "level", "health", "x", "y"]);

    // Add rows as arrays of strings
    csv.addRow(["Hero", "5", "87.5", "100.0", "200.0"]);
    csv.addRow(["Wizard", "3", "65.0", "150.0", "180.0"]);
    csv.addRow(["Warrior", "7", "120.0", "80.0", "220.0"]);

    // Save to file
    csv.save("players.csv");
}
```

Notice that all values must be strings. CSV does not have a concept of numbers --- they are just text that looks like numbers.

### Handling Edge Cases

Real-world CSV data is messy. What if a value contains a comma?

```
"Doe, John",5,87.5
```

Values with commas must be quoted. What if a value contains a quote?

```
"He said ""hello""",5,87.5
```

Quotes inside quoted strings are doubled. What if a value spans multiple lines?

```
"This is a
multi-line note",5,87.5
```

The entire multi-line value is quoted.

The CSV library handles these automatically:

```rust
var row = csv.createRow();
row.set("name", "Doe, John");       // Has comma, will be quoted
row.set("quote", "He said \"hi\""); // Has quotes, will be escaped
row.set("notes", "Line 1\nLine 2"); // Has newline, will be quoted
```

When writing CSV, trust the library to escape properly. When reading, the library unescapes automatically.

### When to Use CSV

CSV is ideal for:

- **Exporting data to spreadsheets**: Excel and Google Sheets open CSV natively
- **Simple tabular data**: Lists of records with uniform fields
- **Data migration**: Moving data between systems
- **Large datasets**: CSV parsers are fast and memory-efficient

Avoid CSV for:

- **Nested or hierarchical data**: Use JSON or XML instead
- **Data with complex types**: CSV only represents strings
- **Data interchange between programs**: JSON is more robust
- **Data that will be edited by non-technical users**: Spreadsheet formats are safer

---

## Step-by-Step Traces: Serialization and Parsing in Action

Understanding data formats deeply requires tracing through the process step by step. Let us do that for both serialization and parsing.

### Trace: Serializing a Player to JSON

We start with a Player entity in memory:

```rust
var player = Player();
player.name = "Hero";
player.level = 5;
player.health = 87.5;
player.inventory = ["sword", "shield"];
player.x = 100.0;
player.y = 200.0;
```

Memory visualization:
```
player:
  name -----> "Hero"
  level ----> 5
  health ---> 87.5
  inventory -> ["sword", "shield"]
  x ---------> 100.0
  y ---------> 200.0
```

Now we call `player.toJSON()`. Here is what happens:

**Step 1**: Create an empty JSON object
```
obj: {}
```

**Step 2**: `obj.set("name", self.name)` --- Add the name
```
obj: {"name": "Hero"}
```

**Step 3**: `obj.set("level", self.level)` --- Add the level
```
obj: {"name": "Hero", "level": 5}
```

**Step 4**: `obj.set("health", self.health)` --- Add the health
```
obj: {"name": "Hero", "level": 5, "health": 87.5}
```

**Step 5**: Create an empty JSON array for inventory
```
inv: []
```

**Step 6**: Loop through inventory, adding each item
```
inv.add("sword")  -> inv: ["sword"]
inv.add("shield") -> inv: ["sword", "shield"]
```

**Step 7**: `obj.set("inventory", inv)` --- Attach inventory to player
```
obj: {"name": "Hero", "level": 5, "health": 87.5, "inventory": ["sword", "shield"]}
```

**Step 8**: Create position object
```
pos: {}
pos.set("x", 100.0) -> pos: {"x": 100.0}
pos.set("y", 200.0) -> pos: {"x": 100.0, "y": 200.0}
```

**Step 9**: `obj.set("position", pos)` --- Attach position
```
obj: {"name": "Hero", "level": 5, "health": 87.5, "inventory": ["sword", "shield"], "position": {"x": 100.0, "y": 200.0}}
```

**Step 10**: `.toPrettyString()` --- Convert the structure to formatted text

The final result is a string containing the JSON representation.

### Trace: Parsing JSON into a Player

Now the reverse. We have this JSON text:

```json
{
    "name": "Hero",
    "level": 5,
    "health": 87.5,
    "inventory": ["sword", "shield"],
    "position": {"x": 100.0, "y": 200.0}
}
```

We call `JSON.parse(jsonText)`:

**Step 1**: The parser reads the opening `{` and knows this is an object. It creates an empty JSONValue of object type.

**Step 2**: It reads `"name"` (a key), then `:`, then `"Hero"` (a string value). It stores `"name" -> "Hero"` in the object.

**Step 3**: It reads the comma, then `"level"`, then `:`, then `5` (a number). It stores `"level" -> 5`.

**Step 4**: Continue for `"health"`, storing `"health" -> 87.5`.

**Step 5**: It reads `"inventory"`, then `:`, then `[`. This starts an array. It reads `"sword"`, comma, `"shield"`, then `]`. It stores `"inventory" -> ["sword", "shield"]`.

**Step 6**: It reads `"position"`, then `:`, then `{`. This is a nested object. It parses `"x": 100.0` and `"y": 200.0`, then `}`. It stores `"position" -> {"x": 100.0, "y": 200.0}`.

**Step 7**: It reads the final `}`. Parsing is complete.

The result is a tree structure in memory:

```
data:
  "name" -----> JSONValue(string: "Hero")
  "level" ----> JSONValue(number: 5)
  "health" ---> JSONValue(number: 87.5)
  "inventory" -> JSONValue(array: [
                     JSONValue(string: "sword"),
                     JSONValue(string: "shield")
                   ])
  "position" -> JSONValue(object:
                   "x" -> JSONValue(number: 100.0)
                   "y" -> JSONValue(number: 200.0)
                 )
```

Now we call `Player.fromJSON(data)`:

**Step 1**: Create an empty Player
```
player: Player()
```

**Step 2**: `data["name"]` navigates to the JSONValue for "name", `.asString()` extracts "Hero"
```
player.name = "Hero"
```

**Step 3**: `data["level"].asInt()` extracts 5
```
player.level = 5
```

**Step 4**: `data["health"].asFloat()` extracts 87.5
```
player.health = 87.5
```

**Step 5**: `data["inventory"].asArray()` returns an array we can iterate:
```
for item in array:
    item = JSONValue(string: "sword") -> .asString() -> "sword"
    player.inventory.push("sword")

    item = JSONValue(string: "shield") -> .asString() -> "shield"
    player.inventory.push("shield")
```

**Step 6**: `data["position"]["x"].asFloat()` chains access:
```
data["position"] -> JSONValue(object with x and y)
["x"] -> JSONValue(number: 100.0)
.asFloat() -> 100.0

player.x = 100.0
player.y = 200.0
```

**Step 7**: Return the fully populated Player.

The data has completed its round trip: program data to text and back to program data.

---

## Comparing Data Formats

Different formats suit different needs. Here is a comprehensive comparison:

| Aspect | JSON | CSV | XML | INI | Binary |
|--------|------|-----|-----|-----|--------|
| Human-readable | Yes | Yes | Yes | Yes | No |
| Hierarchical | Yes | No | Yes | Limited | Yes |
| Types | String, Number, Bool, Null | Strings only | Strings only | Strings only | Any |
| Size | Medium | Small | Large | Small | Very small |
| Parse speed | Fast | Very fast | Slow | Fast | Fastest |
| Schema support | Via JSON Schema | No | Via XSD | No | Custom |
| Comments | No | No | Yes | Yes | N/A |
| Best for | APIs, configs | Spreadsheets | Documents | Simple config | Performance |

### Choosing the Right Format

**Use JSON when:**
- Exchanging data with web services
- Creating configuration files developers will edit
- Storing hierarchical data
- Interoperability across languages and platforms matters

**Use CSV when:**
- Data is inherently tabular (rows and columns)
- Users will open files in spreadsheets
- Simplicity outweighs flexibility
- Dealing with very large datasets where parsing speed matters

**Use XML when:**
- Working with enterprise systems that expect XML
- You need document-oriented data with mixed content
- Schema validation and namespaces are required
- Building configuration for tools that expect XML

**Use INI when:**
- Very simple key-value configuration
- Users need to edit files manually
- You want something simpler than JSON for basic settings

**Use binary when:**
- File size is critical (games with many assets)
- Parsing speed is critical (real-time systems)
- Security through obscurity is acceptable (though not robust security)
- You control both the writer and reader completely

---

## Practical Example: A Contact Manager

Let us build a complete application that manages contacts, demonstrating serialization and parsing in a real context:

```rust
module ContactManager;

import Viper.JSON;
import Viper.File;

// ============================================
// Data Model
// ============================================

entity Contact {
    name: string;
    phone: string;
    email: string;

    func toJSON() -> JSONValue {
        var obj = JSON.object();
        obj.set("name", self.name);
        obj.set("phone", self.phone);
        obj.set("email", self.email);
        return obj;
    }

    static func fromJSON(data: JSONValue) -> Contact {
        var contact = Contact();
        contact.name = data["name"].asString();
        contact.phone = data["phone"].asString();
        contact.email = data["email"].asString();
        return contact;
    }
}

entity ContactBook {
    hide contacts: [Contact];
    hide filename: string;

    expose func init(filename: string) {
        self.filename = filename;
        self.contacts = [];
        self.load();
    }

    // Add a new contact
    func add(contact: Contact) {
        self.contacts.push(contact);
        self.save();
    }

    // Find contacts by name (partial match)
    func search(query: string) -> [Contact] {
        var results: [Contact] = [];
        var lowerQuery = query.toLower();

        for contact in self.contacts {
            if contact.name.toLower().contains(lowerQuery) {
                results.push(contact);
            }
        }

        return results;
    }

    // Remove a contact by name (exact match)
    func remove(name: string) -> bool {
        var newContacts: [Contact] = [];
        var found = false;

        for contact in self.contacts {
            if contact.name == name {
                found = true;
            } else {
                newContacts.push(contact);
            }
        }

        if found {
            self.contacts = newContacts;
            self.save();
        }

        return found;
    }

    // List all contacts
    func all() -> [Contact] {
        return self.contacts;
    }

    // Persist to disk
    hide func save() {
        var arr = JSON.array();
        for contact in self.contacts {
            arr.add(contact.toJSON());
        }

        var json = arr.toPrettyString();
        File.writeText(self.filename, json);
    }

    // Load from disk
    hide func load() {
        if !File.exists(self.filename) {
            return;  // No file yet, start empty
        }

        var json = File.readText(self.filename);
        var arr = JSON.parse(json);

        for item in arr.asArray() {
            self.contacts.push(Contact.fromJSON(item));
        }
    }
}

// ============================================
// User Interface
// ============================================

func printContact(contact: Contact) {
    Viper.Terminal.Say("  Name:  " + contact.name);
    Viper.Terminal.Say("  Phone: " + contact.phone);
    Viper.Terminal.Say("  Email: " + contact.email);
}

func printHelp() {
    Viper.Terminal.Say("Commands:");
    Viper.Terminal.Say("  add     - Add a new contact");
    Viper.Terminal.Say("  list    - Show all contacts");
    Viper.Terminal.Say("  search  - Search for contacts");
    Viper.Terminal.Say("  remove  - Remove a contact");
    Viper.Terminal.Say("  quit    - Exit the program");
}

func start() {
    var book = ContactBook("contacts.json");

    Viper.Terminal.Say("=== Contact Manager ===");
    Viper.Terminal.Say("Type 'help' for commands.");
    Viper.Terminal.Say("");

    while true {
        Viper.Terminal.Print("> ");
        var command = Viper.Terminal.ReadLine().trim();

        if command == "quit" {
            Viper.Terminal.Say("Goodbye!");
            break;
        } else if command == "help" {
            printHelp();
        } else if command == "add" {
            // Gather contact information
            Viper.Terminal.Print("Name: ");
            var name = Viper.Terminal.ReadLine().trim();
            Viper.Terminal.Print("Phone: ");
            var phone = Viper.Terminal.ReadLine().trim();
            Viper.Terminal.Print("Email: ");
            var email = Viper.Terminal.ReadLine().trim();

            // Create and add the contact
            var contact = Contact();
            contact.name = name;
            contact.phone = phone;
            contact.email = email;
            book.add(contact);

            Viper.Terminal.Say("Contact added.");
        } else if command == "list" {
            var contacts = book.all();
            if contacts.length == 0 {
                Viper.Terminal.Say("No contacts yet.");
            } else {
                Viper.Terminal.Say("All contacts:");
                for i in 0..contacts.length {
                    Viper.Terminal.Say("");
                    Viper.Terminal.Say((i + 1) + ".");
                    printContact(contacts[i]);
                }
            }
        } else if command == "search" {
            Viper.Terminal.Print("Search for: ");
            var query = Viper.Terminal.ReadLine().trim();

            var results = book.search(query);
            if results.length == 0 {
                Viper.Terminal.Say("No contacts found.");
            } else {
                Viper.Terminal.Say("Found " + results.length + " contact(s):");
                for contact in results {
                    Viper.Terminal.Say("");
                    printContact(contact);
                }
            }
        } else if command == "remove" {
            Viper.Terminal.Print("Name to remove: ");
            var name = Viper.Terminal.ReadLine().trim();

            if book.remove(name) {
                Viper.Terminal.Say("Contact removed.");
            } else {
                Viper.Terminal.Say("Contact not found.");
            }
        } else {
            Viper.Terminal.Say("Unknown command. Type 'help' for options.");
        }

        Viper.Terminal.Say("");
    }
}
```

This example demonstrates:
- **Entity-to-JSON serialization** with `toJSON()` methods
- **JSON-to-entity parsing** with `fromJSON()` methods
- **Automatic persistence** that saves after every change
- **Graceful startup** that loads existing data if available
- **Clean separation** between data model and user interface

The resulting `contacts.json` file looks like:

```json
[
    {
        "name": "Alice Smith",
        "phone": "555-1234",
        "email": "alice@example.com"
    },
    {
        "name": "Bob Jones",
        "phone": "555-5678",
        "email": "bob@example.com"
    }
]
```

Users could even edit this file manually if needed --- that is the power of human-readable formats.

---

## Common Mistakes and How to Avoid Them

Data format handling is rife with subtle bugs. Here are the most common mistakes and how to prevent them.

### Mistake: Not Validating Input

```rust
// DANGEROUS: Assumes the structure exists
var name = data["player"]["stats"]["name"].asString();  // Crash if missing!
```

If `data` does not have a `"player"` key, or if `player` does not have `"stats"`, your program crashes. Always validate:

```rust
// SAFE: Check before accessing
if data.has("player") && data["player"].has("stats") {
    if data["player"]["stats"].has("name") {
        var name = data["player"]["stats"]["name"].asString();
        // Now safe to use name
    }
}

// BETTER: Use a helper function with defaults
func getNestedString(data: JSONValue, path: string, defaultValue: string) -> string {
    var parts = path.split(".");
    var current = data;

    for part in parts {
        if !current.has(part) {
            return defaultValue;
        }
        current = current[part];
    }

    return current.asString();
}

var name = getNestedString(data, "player.stats.name", "Unknown");
```

### Mistake: Ignoring Type Mismatches

JSON numbers can be integers or floats, but your code might expect one or the other:

```rust
// PROBLEM: What if level is "5" (string) instead of 5 (number)?
var level = data["level"].asInt();  // Might fail or return 0
```

Defensive approach:

```rust
func safeGetInt(data: JSONValue, key: string, defaultValue: i64) -> i64 {
    if !data.has(key) {
        return defaultValue;
    }

    var value = data[key];

    // Handle both number and string representations
    if value.isNumber() {
        return value.asInt();
    } else if value.isString() {
        return Viper.Parse.Int(value.asString());
    }

    return defaultValue;
}
```

### Mistake: Forgetting Character Encoding

Text files have encodings. The same bytes can mean different characters depending on the encoding:

```rust
// PROBLEM: Assumes UTF-8
var text = File.readBytes(filename).toString();

// SOLUTION: Specify encoding explicitly
var text = File.readText(filename, Encoding.UTF8);
```

Modern systems generally use UTF-8, but legacy files might use Latin-1, Windows-1252, or other encodings. If your data contains non-ASCII characters (accented letters, emoji, characters from other languages), encoding matters.

### Mistake: Not Handling Missing Files

```rust
// CRASH: What if the file doesn't exist?
var json = File.readText("config.json");
var data = JSON.parse(json);
```

Always check:

```rust
if File.exists("config.json") {
    var json = File.readText("config.json");
    var data = JSON.parse(json);
    // Use data
} else {
    // Use defaults
    var data = JSON.object();
}
```

### Mistake: Not Versioning Your Formats

Your data format will evolve. Today's save file format might not match tomorrow's:

```rust
// VERSION 1: Just name and level
{"name": "Hero", "level": 5}

// VERSION 2: Added health
{"name": "Hero", "level": 5, "health": 100}

// VERSION 3: Added skills
{"name": "Hero", "level": 5, "health": 100, "skills": [...]}
```

If version 3 code reads a version 1 file, it will fail when looking for `"health"` and `"skills"`.

Solution: Include a version number:

```json
{
    "version": 3,
    "name": "Hero",
    "level": 5,
    "health": 100,
    "skills": [...]
}
```

```rust
func loadPlayer(data: JSONValue) -> Player {
    var version = 1;  // Default for old files without version
    if data.has("version") {
        version = data["version"].asInt();
    }

    var player = Player();
    player.name = data["name"].asString();
    player.level = data["level"].asInt();

    if version >= 2 {
        player.health = data["health"].asFloat();
    } else {
        player.health = 100.0;  // Default for old saves
    }

    if version >= 3 {
        // Load skills
    } else {
        player.skills = [];  // Default for old saves
    }

    return player;
}
```

### Mistake: Malformed Input Crashes the Program

What if someone gives you invalid JSON?

```rust
// CRASH: Invalid JSON causes parse to fail
var data = JSON.parse("this is { not valid json");
```

Handle parse errors:

```rust
var result = JSON.tryParse(jsonText);
if result.isError() {
    Viper.Terminal.Say("Invalid JSON: " + result.errorMessage());
    return;
}
var data = result.value();
// Now safe to use data
```

### Mistake: Trusting User-Provided Data

Never trust data from external sources:

```rust
// DANGEROUS: User might provide negative level or huge numbers
var level = data["level"].asInt();
player.level = level;

// SAFE: Validate the data
var level = data["level"].asInt();
if level < 1 {
    level = 1;
}
if level > 100 {
    level = 100;
}
player.level = level;
```

This is especially critical for data from files users can edit, API responses, or network input.

---

## Debugging Data Format Issues

When data does not parse correctly, systematic debugging helps.

### Technique 1: Print the Raw Data

Before parsing, see exactly what you received:

```rust
var jsonText = File.readText("data.json");
Viper.Terminal.Say("=== RAW JSON ===");
Viper.Terminal.Say(jsonText);
Viper.Terminal.Say("================");

var data = JSON.parse(jsonText);
```

Often you will spot the problem immediately: missing quotes, trailing commas, or completely unexpected content.

### Technique 2: Validate JSON Externally

Many tools validate JSON:
- Online validators like jsonlint.com
- Command-line tools like `jq`
- IDE plugins that highlight syntax errors

Paste your JSON into a validator to get precise error messages.

### Technique 3: Inspect the Parsed Structure

After parsing, inspect what you got:

```rust
func debugJSON(data: JSONValue, indent: string) {
    if data.isObject() {
        Viper.Terminal.Say(indent + "{");
        for key in data.keys() {
            Viper.Terminal.Say(indent + "  \"" + key + "\":");
            debugJSON(data[key], indent + "    ");
        }
        Viper.Terminal.Say(indent + "}");
    } else if data.isArray() {
        Viper.Terminal.Say(indent + "[");
        for item in data.asArray() {
            debugJSON(item, indent + "  ");
        }
        Viper.Terminal.Say(indent + "]");
    } else if data.isString() {
        Viper.Terminal.Say(indent + "string: \"" + data.asString() + "\"");
    } else if data.isNumber() {
        Viper.Terminal.Say(indent + "number: " + data.asFloat());
    } else if data.isBool() {
        Viper.Terminal.Say(indent + "bool: " + data.asBool());
    } else if data.isNull() {
        Viper.Terminal.Say(indent + "null");
    }
}

debugJSON(data, "");
```

This shows you the structure and types of everything in the parsed data.

### Technique 4: Test Round-Trips

Create data, serialize it, parse it back, and compare:

```rust
var original = Player();
original.name = "Test";
original.level = 5;
// ... set all fields

var json = original.toJSON().toString();
var parsed = JSON.parse(json);
var restored = Player.fromJSON(parsed);

// Compare
if original.name != restored.name {
    Viper.Terminal.Say("Name mismatch!");
}
if original.level != restored.level {
    Viper.Terminal.Say("Level mismatch!");
}
// ... check all fields
```

If round-trip works, your serialization is correct. If it fails, you know where to look.

### Technique 5: Handle Errors Gracefully

Instead of crashing on bad data, log the problem and continue:

```rust
func loadPlayerSafe(filename: string) -> Player? {
    if !File.exists(filename) {
        Viper.Terminal.Say("Warning: Save file not found, using defaults");
        return null;
    }

    var json = File.readText(filename);
    var parseResult = JSON.tryParse(json);

    if parseResult.isError() {
        Viper.Terminal.Say("Warning: Invalid save file format");
        Viper.Terminal.Say("Error: " + parseResult.errorMessage());
        return null;
    }

    var data = parseResult.value();

    if !data.has("name") || !data.has("level") {
        Viper.Terminal.Say("Warning: Save file missing required fields");
        return null;
    }

    return Player.fromJSON(data);
}

func start() {
    var player = loadPlayerSafe("save.json");
    if player == null {
        player = Player();  // Start fresh
        player.name = "New Player";
        player.level = 1;
    }
    // Continue with game
}
```

---

## The Three Languages

**Zia**
```rust
import Viper.JSON;

var data = JSON.parse('{"name": "test", "value": 42}');
var name = data["name"].asString();

var obj = JSON.object();
obj.set("score", 100);
var json = obj.toString();
```

**BASIC**
```basic
DIM data AS JSONValue
data = JSON_PARSE("{""name"": ""test"", ""value"": 42}")
DIM name AS STRING
name = JSON_GET_STRING(data, "name")

DIM obj AS JSONValue
obj = JSON_OBJECT()
JSON_SET obj, "score", 100
DIM json AS STRING
json = JSON_TOSTRING(obj)
```

BASIC uses functions rather than methods and requires escaping double quotes by doubling them.

**Pascal**
```pascal
uses ViperJSON;
var
    data, obj: TJSONValue;
    name, json: string;
begin
    data := JSONParse('{"name": "test", "value": 42}');
    name := data['name'].AsString;

    obj := JSONObject;
    obj.SetValue('score', 100);
    json := obj.ToString;
end.
```

Pascal uses bracket notation for JSON access and methods for type conversion, similar to Zia but with Pascal syntax conventions.

---

## Binary Formats: When Text Is Not Enough

Sometimes text formats are too slow or too large. Binary formats pack data efficiently at the cost of human readability.

### When to Consider Binary

- **Games with large save files**: A player's complete world state might be megabytes
- **Real-time data**: Parsing text takes time that matters in performance-critical paths
- **Bandwidth-constrained networks**: Every byte counts on mobile or slow connections
- **Proprietary formats**: When you do not want users casually editing files

### Designing a Binary Format

A well-designed binary format includes:

**1. Magic number** --- A unique identifier that tells you "this is a ViperSave file"
```rust
final MAGIC = 0x56535631;  // "VSV1" in ASCII
```

**2. Version number** --- Allows format evolution
```rust
final VERSION = 1;
```

**3. Predictable layout** --- Fields in a consistent order with known sizes

**4. Length prefixes** --- For variable-length data like strings and arrays

Here is a complete binary save example:

```rust
import Viper.IO;

value GameSave {
    version: i32;
    playerName: string;
    level: i32;
    health: f32;
    x: f32;
    y: f32;
    inventory: [string];
}

final MAGIC = 0x56535631;  // "VSV1"
final VERSION = 1;

func saveBinary(save: GameSave, filename: string) {
    var writer = BinaryWriter.create(filename);

    // Header
    writer.writeUInt32(MAGIC);
    writer.writeUInt32(VERSION);

    // Player data
    writer.writeString(save.playerName);
    writer.writeInt32(save.level);
    writer.writeFloat32(save.health);
    writer.writeFloat32(save.x);
    writer.writeFloat32(save.y);

    // Inventory (length-prefixed array)
    writer.writeInt32(save.inventory.length);
    for item in save.inventory {
        writer.writeString(item);
    }

    writer.close();
}

func loadBinary(filename: string) -> GameSave? {
    var reader = BinaryReader.open(filename);

    // Verify magic number
    var magic = reader.readUInt32();
    if magic != MAGIC {
        Viper.Terminal.Say("Error: Not a valid save file");
        reader.close();
        return null;
    }

    // Check version
    var version = reader.readUInt32();
    if version > VERSION {
        Viper.Terminal.Say("Error: Save file is from a newer version");
        reader.close();
        return null;
    }

    // Load data
    var save = GameSave();
    save.version = version;
    save.playerName = reader.readString();
    save.level = reader.readInt32();
    save.health = reader.readFloat32();
    save.x = reader.readFloat32();
    save.y = reader.readFloat32();

    var inventoryCount = reader.readInt32();
    save.inventory = [];
    for i in 0..inventoryCount {
        save.inventory.push(reader.readString());
    }

    reader.close();
    return save;
}
```

The resulting file is much smaller than JSON and loads faster, but you cannot open it in a text editor to see what is inside.

---

## A Complete Example: Configuration System

Here is a production-quality configuration system that demonstrates everything we have learned:

```rust
module ConfigSystem;

import Viper.JSON;
import Viper.File;

entity Config {
    hide data: JSONValue;
    hide filename: string;
    hide dirty: bool;

    // Initialize with a filename, loading existing data if available
    expose func init(filename: string) {
        self.filename = filename;
        self.dirty = false;

        if File.exists(filename) {
            var result = JSON.tryParse(File.readText(filename));
            if result.isError() {
                Viper.Terminal.Say("Warning: Config file invalid, using defaults");
                self.data = JSON.object();
            } else {
                self.data = result.value();
            }
        } else {
            self.data = JSON.object();
        }
    }

    // Navigate to a nested value using dot notation (e.g., "audio.volume")
    hide func getPath(key: string) -> JSONValue? {
        var parts = key.split(".");
        var current = self.data;

        for part in parts {
            if !current.isObject() || !current.has(part) {
                return null;
            }
            current = current[part];
        }

        return current;
    }

    // Set a nested value, creating intermediate objects as needed
    hide func setPath(key: string, value: JSONValue) {
        var parts = key.split(".");
        var current = self.data;

        // Navigate/create path to parent
        for i in 0..(parts.length - 1) {
            var part = parts[i];
            if !current.has(part) {
                current.set(part, JSON.object());
            }
            current = current[part];
        }

        // Set the final value
        current.set(parts[parts.length - 1], value);
        self.dirty = true;
    }

    // Get values with type-safe defaults
    func getString(key: string, defaultValue: string) -> string {
        var value = self.getPath(key);
        if value == null || !value.isString() {
            return defaultValue;
        }
        return value.asString();
    }

    func getInt(key: string, defaultValue: i64) -> i64 {
        var value = self.getPath(key);
        if value == null || !value.isNumber() {
            return defaultValue;
        }
        return value.asInt();
    }

    func getFloat(key: string, defaultValue: f64) -> f64 {
        var value = self.getPath(key);
        if value == null || !value.isNumber() {
            return defaultValue;
        }
        return value.asFloat();
    }

    func getBool(key: string, defaultValue: bool) -> bool {
        var value = self.getPath(key);
        if value == null || !value.isBool() {
            return defaultValue;
        }
        return value.asBool();
    }

    // Set values
    func setString(key: string, value: string) {
        self.setPath(key, JSON.string(value));
    }

    func setInt(key: string, value: i64) {
        self.setPath(key, JSON.number(value));
    }

    func setFloat(key: string, value: f64) {
        self.setPath(key, JSON.number(value));
    }

    func setBool(key: string, value: bool) {
        self.setPath(key, JSON.bool(value));
    }

    // Save to disk only if there are unsaved changes
    func save() {
        if self.dirty {
            var text = self.data.toPrettyString();
            File.writeText(self.filename, text);
            self.dirty = false;
        }
    }

    // Force reload from disk, discarding unsaved changes
    func reload() {
        if File.exists(self.filename) {
            var result = JSON.tryParse(File.readText(self.filename));
            if !result.isError() {
                self.data = result.value();
            }
        }
        self.dirty = false;
    }
}

// Usage example
func start() {
    var config = Config("game_settings.json");

    // Read with defaults (works even if file doesn't exist or keys are missing)
    var volume = config.getFloat("audio.volume", 0.8);
    var fullscreen = config.getBool("graphics.fullscreen", false);
    var playerName = config.getString("player.name", "New Player");
    var difficulty = config.getInt("game.difficulty", 2);

    Viper.Terminal.Say("Current settings:");
    Viper.Terminal.Say("  Volume: " + volume);
    Viper.Terminal.Say("  Fullscreen: " + fullscreen);
    Viper.Terminal.Say("  Player: " + playerName);
    Viper.Terminal.Say("  Difficulty: " + difficulty);

    // Modify settings
    config.setFloat("audio.volume", 0.5);
    config.setBool("graphics.fullscreen", true);
    config.setString("player.name", "Hero");

    // Persist changes
    config.save();

    Viper.Terminal.Say("");
    Viper.Terminal.Say("Settings saved!");
}
```

The resulting configuration file:

```json
{
    "audio": {
        "volume": 0.5
    },
    "graphics": {
        "fullscreen": true
    },
    "player": {
        "name": "Hero"
    },
    "game": {
        "difficulty": 2
    }
}
```

This system demonstrates:
- **Graceful handling of missing files and invalid data**
- **Nested key navigation using dot notation**
- **Auto-creation of intermediate objects**
- **Dirty tracking to avoid unnecessary writes**
- **Type-safe getters with defaults**

---

## Summary

Data formats are the bridge between your program and the outside world. Here is what we learned:

**Core Concepts:**
- **Serialization** converts program data to a storable format
- **Parsing** converts stored data back to program data
- **Formats are contracts** between writers and readers

**JSON:**
- Universal format for structured data
- Objects, arrays, strings, numbers, booleans, null
- Human-readable and well-supported
- Use `.toJSON()` and `.fromJSON()` patterns for clean code

**CSV:**
- Simple format for tabular data
- Rows and columns, no nesting
- Great for spreadsheet interoperability
- Watch out for escaping special characters

**Binary Formats:**
- Compact and fast but not human-readable
- Include magic numbers and version fields
- Document your format carefully

**Best Practices:**
- Always validate input data
- Use versioning for format evolution
- Handle errors gracefully
- Test round-trips (serialize then parse)
- Choose the right format for your use case

---

## Exercises

**Exercise 23.1**: Write a program that reads a JSON file containing a list of books (title, author, year, pages) and prints them sorted by year. Handle the case where the file does not exist or is invalid JSON.

**Exercise 23.2**: Create a program that converts JSON to CSV and vice versa. For JSON-to-CSV, assume the JSON is an array of objects with uniform keys. For CSV-to-JSON, use the first row as headers and convert each subsequent row to an object.

**Exercise 23.3**: Build a contact manager (like the example in this chapter) but add support for contacts having multiple phone numbers and multiple email addresses. Design the JSON structure to support this.

**Exercise 23.4**: Create a settings system that supports inheritance: load `default.json` first, then override with values from `user.json`. Values in user.json should replace the same keys from default.json, but keys only in default.json should still appear.

**Exercise 23.5**: Design a binary save format for a simple RPG character (name, class, level, stats array, inventory list). Include a magic number and version. Write save and load functions with version checking.

**Exercise 23.6**: Write a JSON validator that checks whether a player save file has all required fields (`name`, `level`, `health`), whether numeric fields are within valid ranges, and reports all problems found (not just the first one).

**Exercise 23.7** (Challenge): Build a data migration tool. Given a "version 1" save file format and a "version 2" format (you define the differences), write code that reads version 1 files and converts them to version 2 format.

**Exercise 23.8** (Challenge): Create a CSV parser from scratch --- without using the CSV library. Handle quoted fields, escaped quotes, and fields containing newlines. This teaches you how complex "simple" formats really are.

**Exercise 23.9** (Challenge): Implement a simple JSON schema validator. Define a schema format (e.g., `{"name": {"type": "string", "required": true}, "age": {"type": "number", "min": 0}}`) and write code that validates JSON data against such schemas.

---

*We understand how to move data between our programs and the outside world. But what about doing multiple things at once? Next, we explore concurrency --- running tasks in parallel, handling events, and building responsive applications.*

*[Continue to Chapter 24: Concurrency](24-concurrency.md)*
