# Chapter 23: Data Formats

Programs need to exchange data — with files, networks, databases, other programs. But data must be structured in a way both sides understand. This chapter covers the most important data formats: JSON, XML, CSV, and binary protocols.

---

## Why Data Formats Matter

Imagine you want to save a player's game state:

```viper
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

How do you write this to a file? How do you send it over a network? You need a format — a standard way to represent structured data as text or bytes.

---

## JSON: The Web's Favorite

JSON (JavaScript Object Notation) is everywhere. APIs use it. Configuration files use it. It's human-readable and well-supported.

### JSON Basics

```json
{
    "name": "Hero",
    "level": 5,
    "health": 87.5,
    "inventory": ["sword", "shield", "potion"],
    "position": {
        "x": 100.0,
        "y": 200.0
    }
}
```

JSON supports:
- Strings: `"hello"`
- Numbers: `42`, `3.14`
- Booleans: `true`, `false`
- Null: `null`
- Arrays: `[1, 2, 3]`
- Objects: `{"key": "value"}`

### Parsing JSON

```viper
import Viper.JSON;

func start() {
    var jsonText = '{"name": "Alice", "score": 100}';

    var data = JSON.parse(jsonText);

    var name = data["name"].asString();
    var score = data["score"].asInt();

    Viper.Terminal.Say("Player: " + name + ", Score: " + score);
}
```

### Creating JSON

```viper
import Viper.JSON;

func start() {
    var player = JSON.object();
    player.set("name", "Hero");
    player.set("level", 5);
    player.set("health", 87.5);

    var inventory = JSON.array();
    inventory.add("sword");
    inventory.add("shield");
    player.set("inventory", inventory);

    var position = JSON.object();
    position.set("x", 100.0);
    position.set("y", 200.0);
    player.set("position", position);

    var jsonText = player.toString();
    Viper.Terminal.Say(jsonText);
}
```

Output:
```json
{"name":"Hero","level":5,"health":87.5,"inventory":["sword","shield"],"position":{"x":100.0,"y":200.0}}
```

### Pretty Printing

```viper
var jsonText = player.toPrettyString();
```

Output:
```json
{
    "name": "Hero",
    "level": 5,
    "health": 87.5,
    "inventory": [
        "sword",
        "shield"
    ],
    "position": {
        "x": 100.0,
        "y": 200.0
    }
}
```

### Converting Objects to JSON

For cleaner code, add serialization methods to your classes:

```viper
entity Player {
    name: string;
    level: i64;
    health: f64;
    inventory: [string];
    x: f64;
    y: f64;

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

// Usage
func saveGame(player: Player, filename: string) {
    var json = player.toJSON().toPrettyString();
    Viper.File.writeText(filename, json);
}

func loadGame(filename: string) -> Player {
    var json = Viper.File.readText(filename);
    var data = JSON.parse(json);
    return Player.fromJSON(data);
}
```

---

## XML: The Enterprise Standard

XML (Extensible Markup Language) is older and more verbose than JSON, but still widely used in enterprise systems.

### XML Basics

```xml
<?xml version="1.0" encoding="UTF-8"?>
<player>
    <name>Hero</name>
    <level>5</level>
    <health>87.5</health>
    <inventory>
        <item>sword</item>
        <item>shield</item>
        <item>potion</item>
    </inventory>
    <position x="100.0" y="200.0"/>
</player>
```

XML uses:
- **Elements**: `<tag>content</tag>`
- **Attributes**: `<tag attr="value">`
- **Self-closing elements**: `<tag/>`

### Parsing XML

```viper
import Viper.XML;

func start() {
    var xmlText = "<player><name>Alice</name><score>100</score></player>";

    var doc = XML.parse(xmlText);
    var root = doc.root();  // <player>

    var name = root.child("name").text();
    var score = root.child("score").text().toInt();

    Viper.Terminal.Say("Player: " + name + ", Score: " + score);
}
```

### Creating XML

```viper
import Viper.XML;

func start() {
    var doc = XML.document();
    var player = doc.createElement("player");
    doc.setRoot(player);

    var name = doc.createElement("name");
    name.setText("Hero");
    player.appendChild(name);

    var level = doc.createElement("level");
    level.setText("5");
    player.appendChild(level);

    var position = doc.createElement("position");
    position.setAttribute("x", "100.0");
    position.setAttribute("y", "200.0");
    player.appendChild(position);

    Viper.Terminal.Say(doc.toString());
}
```

### Navigating XML

```viper
var doc = XML.parse(xmlText);
var root = doc.root();

// Get child by name
var name = root.child("name");

// Get all children with a name
var items = root.child("inventory").children("item");
for item in items {
    Viper.Terminal.Say(item.text());
}

// Get attribute
var x = root.child("position").attribute("x");

// XPath queries (powerful!)
var allItems = doc.xpath("//item");
var firstItem = doc.xpath("//inventory/item[1]");
```

---

## CSV: Simple Tabular Data

CSV (Comma-Separated Values) is perfect for spreadsheet-like data:

```csv
name,level,health,x,y
Hero,5,87.5,100.0,200.0
Wizard,3,65.0,150.0,180.0
Warrior,7,120.0,80.0,220.0
```

### Reading CSV

```viper
import Viper.CSV;

func start() {
    var csv = CSV.load("players.csv");

    for row in csv.rows() {
        var name = row["name"];
        var level = row["level"].toInt();
        var health = row["health"].toFloat();

        Viper.Terminal.Say(name + " (Level " + level + ")");
    }
}
```

### Writing CSV

```viper
import Viper.CSV;

func start() {
    var csv = CSV.create(["name", "level", "health", "x", "y"]);

    csv.addRow(["Hero", "5", "87.5", "100.0", "200.0"]);
    csv.addRow(["Wizard", "3", "65.0", "150.0", "180.0"]);

    csv.save("players.csv");
}
```

### Handling Edge Cases

CSV has quirks: commas in values, quotes, newlines:

```viper
// Values with commas need quoting
csv.addRow(["John Doe", "5", "87.5"]);       // name,level,health
csv.addRow(["Doe, John", "5", "87.5"]);      // "Doe, John",5,87.5

// The CSV library handles this automatically
var row = csv.createRow();
row.set("name", "Doe, John");  // Will be properly quoted
row.set("bio", "Line 1\nLine 2");  // Newlines handled
```

---

## INI Files: Simple Configuration

INI files are great for configuration:

```ini
[player]
name=Hero
level=5

[graphics]
width=1920
height=1080
fullscreen=true

[audio]
volume=0.8
music=true
```

### Reading INI

```viper
import Viper.INI;

func start() {
    var config = INI.load("settings.ini");

    var name = config.get("player", "name");
    var level = config.getInt("player", "level");

    var width = config.getInt("graphics", "width");
    var fullscreen = config.getBool("graphics", "fullscreen");

    Viper.Terminal.Say("Player: " + name);
    Viper.Terminal.Say("Resolution: " + width + "x" + config.getInt("graphics", "height"));
}
```

### Writing INI

```viper
import Viper.INI;

func start() {
    var config = INI.create();

    config.set("player", "name", "Hero");
    config.set("player", "level", "5");

    config.set("graphics", "width", "1920");
    config.set("graphics", "height", "1080");
    config.set("graphics", "fullscreen", "true");

    config.save("settings.ini");
}
```

---

## Binary Formats: Efficiency

Text formats are human-readable but large and slow. Binary formats are compact and fast.

### Writing Binary Data

```viper
import Viper.IO;

value GameSave {
    version: i32;
    playerName: string;
    level: i32;
    health: f32;
    x: f32;
    y: f32;
    inventoryCount: i32;
    inventory: [string];
}

func saveBinary(save: GameSave, filename: string) {
    var writer = BinaryWriter.create(filename);

    writer.writeInt32(save.version);
    writer.writeString(save.playerName);
    writer.writeInt32(save.level);
    writer.writeFloat32(save.health);
    writer.writeFloat32(save.x);
    writer.writeFloat32(save.y);

    writer.writeInt32(save.inventory.length);
    for item in save.inventory {
        writer.writeString(item);
    }

    writer.close();
}

func loadBinary(filename: string) -> GameSave {
    var reader = BinaryReader.open(filename);

    var save = GameSave();
    save.version = reader.readInt32();
    save.playerName = reader.readString();
    save.level = reader.readInt32();
    save.health = reader.readFloat32();
    save.x = reader.readFloat32();
    save.y = reader.readFloat32();

    var count = reader.readInt32();
    save.inventory = [];
    for i in 0..count {
        save.inventory.push(reader.readString());
    }

    reader.close();
    return save;
}
```

### Binary Protocol Design

When designing binary formats:

1. **Start with a magic number** — identifies file type
2. **Include a version** — allows format evolution
3. **Use fixed sizes where possible** — makes parsing predictable
4. **Document byte order** — little-endian or big-endian

```viper
final MAGIC = 0x56495052;  // "VIPR" in hex
final VERSION = 1;

func saveWithHeader(data: GameSave, filename: string) {
    var writer = BinaryWriter.create(filename);

    // Header
    writer.writeUInt32(MAGIC);
    writer.writeUInt32(VERSION);
    writer.writeUInt32(0);  // Reserved for future use

    // Data
    // ... write data ...

    writer.close();
}

func loadWithHeader(filename: string) -> GameSave? {
    var reader = BinaryReader.open(filename);

    // Verify header
    var magic = reader.readUInt32();
    if magic != MAGIC {
        Viper.Terminal.Say("Invalid file format");
        return null;
    }

    var version = reader.readUInt32();
    if version > VERSION {
        Viper.Terminal.Say("File is from a newer version");
        return null;
    }

    reader.readUInt32();  // Skip reserved

    // Read data based on version
    if version == 1 {
        return loadV1(reader);
    }

    return null;
}
```

---

## Choosing the Right Format

| Format | Pros | Cons | Best For |
|--------|------|------|----------|
| JSON | Readable, universal, flexible | Verbose, no comments | APIs, config, data exchange |
| XML | Structured, schemas, XPath | Very verbose | Enterprise, documents |
| CSV | Simple, spreadsheet-friendly | Limited structure | Tabular data, exports |
| INI | Simple, readable | Limited nesting | Configuration |
| Binary | Compact, fast | Not readable | Games, performance-critical |

---

## A Complete Example: Config System

```viper
module ConfigSystem;

import Viper.JSON;
import Viper.File;

entity Config {
    hide data: JSONValue;
    hide filename: string;
    hide dirty: bool;

    expose func init(filename: string) {
        self.filename = filename;
        self.dirty = false;

        if File.exists(filename) {
            var text = File.readText(filename);
            self.data = JSON.parse(text);
        } else {
            self.data = JSON.object();
        }
    }

    func get(key: string) -> JSONValue? {
        var parts = key.split(".");
        var current = self.data;

        for part in parts {
            if current.has(part) {
                current = current[part];
            } else {
                return null;
            }
        }

        return current;
    }

    func getString(key: string, defaultValue: string) -> string {
        var value = self.get(key);
        if value == null {
            return defaultValue;
        }
        return value.asString();
    }

    func getInt(key: string, defaultValue: i64) -> i64 {
        var value = self.get(key);
        if value == null {
            return defaultValue;
        }
        return value.asInt();
    }

    func getFloat(key: string, defaultValue: f64) -> f64 {
        var value = self.get(key);
        if value == null {
            return defaultValue;
        }
        return value.asFloat();
    }

    func getBool(key: string, defaultValue: bool) -> bool {
        var value = self.get(key);
        if value == null {
            return defaultValue;
        }
        return value.asBool();
    }

    func set(key: string, value: JSONValue) {
        var parts = key.split(".");
        var current = self.data;

        // Navigate to parent, creating objects as needed
        for i in 0..(parts.length - 1) {
            var part = parts[i];
            if !current.has(part) {
                current.set(part, JSON.object());
            }
            current = current[part];
        }

        // Set the value
        current.set(parts[parts.length - 1], value);
        self.dirty = true;
    }

    func setString(key: string, value: string) {
        self.set(key, JSON.string(value));
    }

    func setInt(key: string, value: i64) {
        self.set(key, JSON.number(value));
    }

    func setBool(key: string, value: bool) {
        self.set(key, JSON.bool(value));
    }

    func save() {
        if self.dirty {
            var text = self.data.toPrettyString();
            File.writeText(self.filename, text);
            self.dirty = false;
        }
    }
}

// Usage
func start() {
    var config = Config("game_settings.json");

    // Read with defaults
    var volume = config.getFloat("audio.volume", 0.8);
    var fullscreen = config.getBool("graphics.fullscreen", false);
    var playerName = config.getString("player.name", "Player");

    Viper.Terminal.Say("Volume: " + volume);
    Viper.Terminal.Say("Fullscreen: " + fullscreen);
    Viper.Terminal.Say("Player: " + playerName);

    // Modify settings
    config.setFloat("audio.volume", 0.5);
    config.setBool("graphics.fullscreen", true);

    // Save changes
    config.save();
}
```

---

## The Three Languages

**ViperLang**
```viper
import Viper.JSON;

var data = JSON.parse('{"name": "test"}');
var name = data["name"].asString();

var obj = JSON.object();
obj.set("value", 42);
var json = obj.toString();
```

**BASIC**
```basic
DIM data AS JSONValue
data = JSON_PARSE("{""name"": ""test""}")
DIM name AS STRING
name = JSON_GET_STRING(data, "name")

DIM obj AS JSONValue
obj = JSON_OBJECT()
JSON_SET obj, "value", 42
DIM json AS STRING
json = JSON_TOSTRING(obj)
```

**Pascal**
```pascal
uses ViperJSON;
var
    data, obj: TJSONValue;
    name, json: string;
begin
    data := JSONParse('{"name": "test"}');
    name := data['name'].AsString;

    obj := JSONObject;
    obj.SetValue('value', 42);
    json := obj.ToString;
end.
```

---

## Common Mistakes

**Not validating input**
```viper
// Bad: Assumes structure exists
var name = data["player"]["name"].asString();  // Crash if missing!

// Good: Check before accessing
if data.has("player") && data["player"].has("name") {
    var name = data["player"]["name"].asString();
}

// Or use defaults
var name = data.getPath("player.name", "Unknown");
```

**Forgetting encoding**
```viper
// Bad: Assumes UTF-8
var text = File.readBytes(filename).toString();

// Good: Specify encoding
var text = File.readText(filename, Encoding.UTF8);
```

**Hardcoding format versions**
```viper
// Bad: No version handling
var data = loadBinary(file);

// Good: Version-aware loading
var version = reader.readInt32();
if version == 1 {
    return loadV1(reader);
} else if version == 2 {
    return loadV2(reader);
}
```

---

## Summary

- **JSON**: Universal, readable, great for APIs and configuration
- **XML**: Verbose but structured, with schemas and XPath
- **CSV**: Simple tabular data for spreadsheets
- **INI**: Simple key-value configuration
- **Binary**: Compact and fast for performance-critical data

Choose formats based on:
- Who/what will read the data
- How often it's read/written
- Size constraints
- Need for human editing

---

## Exercises

**Exercise 23.1**: Create a program that converts JSON to XML and vice versa.

**Exercise 23.2**: Build a contact manager that stores contacts in JSON with add, list, search, and delete operations.

**Exercise 23.3**: Write a CSV to JSON converter that handles headers and different data types.

**Exercise 23.4**: Create a settings system that supports multiple config files with inheritance (default.json, user.json where user overrides default).

**Exercise 23.5**: Design a binary save format for a simple game and implement save/load functions with version handling.

**Exercise 23.6** (Challenge): Create a data format validator that checks JSON against a schema (required fields, types, value ranges).

---

*We understand data formats. But what about doing multiple things at once? Next, we explore concurrency — running tasks in parallel.*

*[Continue to Chapter 24: Concurrency →](24-concurrency.md)*
