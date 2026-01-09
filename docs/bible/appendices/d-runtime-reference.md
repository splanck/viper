# Appendix D: Runtime Library Reference

A quick reference for Viper's runtime library modules.

---

## Viper.Terminal

Console input/output operations.

```rust
// Output
Viper.Terminal.Say("message");              // Print with newline
Viper.Terminal.Write("message");            // Print without newline
Viper.Terminal.SayError("error");           // Print to stderr

// Input
var name = Viper.Terminal.Ask("Prompt: ");  // Read line
var char = Viper.Terminal.GetChar();        // Read single character
var key = Viper.Terminal.GetKey();          // Read key (with arrows, etc.)

// Formatting
Viper.Terminal.Clear();                     // Clear screen
Viper.Terminal.SetColor(Color.RED);         // Set text color
Viper.Terminal.ResetColor();                // Reset to default
Viper.Terminal.MoveCursor(x, y);            // Position cursor
Viper.Terminal.HideCursor();
Viper.Terminal.ShowCursor();
```

---

## Viper.File

File system operations.

```rust
// Reading
var text = Viper.File.readText("file.txt");
var bytes = Viper.File.readBytes("file.bin");
var lines = Viper.File.readLines("file.txt");

// Writing
Viper.File.writeText("file.txt", "content");
Viper.File.writeBytes("file.bin", bytes);
Viper.File.appendText("file.txt", "more content");

// File operations
Viper.File.exists("file.txt")               // -> bool
Viper.File.delete("file.txt");
Viper.File.rename("old.txt", "new.txt");
Viper.File.copy("src.txt", "dst.txt");
Viper.File.size("file.txt")                 // -> i64 (bytes)
Viper.File.modifiedTime("file.txt")         // -> DateTime

// Directory operations
Viper.File.listDir("path/")                 // -> [string]
Viper.File.createDir("path/");
Viper.File.deleteDir("path/");
Viper.File.isDir("path")                    // -> bool
Viper.File.isFile("path")                   // -> bool

// Path operations
Viper.File.join("path", "file.txt")         // -> "path/file.txt"
Viper.File.basename("/path/file.txt")       // -> "file.txt"
Viper.File.dirname("/path/file.txt")        // -> "/path"
Viper.File.extension("file.txt")            // -> ".txt"
Viper.File.absolutePath("relative")         // -> "/absolute/path"
```

---

## Viper.Math

Mathematical functions and constants.

```rust
// Constants
Viper.Math.PI                               // 3.14159265358979...
Viper.Math.E                                // 2.71828182845904...
Viper.Math.TAU                              // 6.28318530717958... (2*PI)

// Basic functions
Viper.Math.abs(x)                           // Absolute value
Viper.Math.sign(x)                          // -1, 0, or 1
Viper.Math.min(a, b)                        // Minimum
Viper.Math.max(a, b)                        // Maximum
Viper.Math.clamp(x, min, max)               // Clamp to range

// Rounding
Viper.Math.floor(x)                         // Round down
Viper.Math.ceil(x)                          // Round up
Viper.Math.round(x)                         // Round to nearest
Viper.Math.trunc(x)                         // Truncate toward zero

// Powers and roots
Viper.Math.sqrt(x)                          // Square root
Viper.Math.cbrt(x)                          // Cube root
Viper.Math.pow(base, exp)                   // Power
Viper.Math.exp(x)                           // e^x
Viper.Math.log(x)                           // Natural log
Viper.Math.log10(x)                         // Base-10 log
Viper.Math.log2(x)                          // Base-2 log

// Trigonometry (radians)
Viper.Math.sin(x)
Viper.Math.cos(x)
Viper.Math.tan(x)
Viper.Math.asin(x)
Viper.Math.acos(x)
Viper.Math.atan(x)
Viper.Math.atan2(y, x)

// Hyperbolic
Viper.Math.sinh(x)
Viper.Math.cosh(x)
Viper.Math.tanh(x)

// Conversion
Viper.Math.toRadians(degrees)
Viper.Math.toDegrees(radians)

// Random
Viper.Math.random()                         // 0.0 to 1.0
Viper.Math.randomInt(min, max)              // Inclusive range
Viper.Math.randomSeed(seed)                 // Set seed
```

---

## Viper.Time

Time and date operations.

```rust
// Current time
Viper.Time.millis()                         // Milliseconds since epoch
Viper.Time.nanos()                          // Nanoseconds since epoch
Viper.Time.now()                            // -> DateTime

// Sleeping
Viper.Time.sleep(milliseconds)              // Pause execution

// DateTime operations
var dt = DateTime.now();
dt.year                                     // -> i64
dt.month                                    // -> i64 (1-12)
dt.day                                      // -> i64 (1-31)
dt.hour                                     // -> i64 (0-23)
dt.minute                                   // -> i64 (0-59)
dt.second                                   // -> i64 (0-59)
dt.dayOfWeek                                // -> i64 (0=Sunday)
dt.dayOfYear                                // -> i64 (1-366)

DateTime.parse("2024-01-15")                // Parse ISO date
DateTime.parse("2024-01-15T10:30:00")       // Parse ISO datetime
dt.format("YYYY-MM-DD")                     // Format string
dt.addDays(5)                               // -> new DateTime
dt.addHours(2)
dt.addMinutes(30)
dt.diffDays(other)                          // -> f64
```

---

## Viper.Collections

Collection types beyond basic arrays.

### Map
```rust
var map = Map<string, i64>.new();
map.set("key", 42);
var value = map.get("key");                 // -> i64?
map.has("key")                              // -> bool
map.delete("key");
map.clear();
map.size                                    // -> i64
map.keys()                                  // -> [string]
map.values()                                // -> [i64]

for key, value in map {
    // iterate
}
```

### Set
```rust
var set = Set<string>.new();
set.add("item");
set.contains("item")                        // -> bool
set.remove("item");
set.clear();
set.size                                    // -> i64

set.union(other)                            // -> Set
set.intersection(other)                     // -> Set
set.difference(other)                       // -> Set

for item in set {
    // iterate
}
```

### Queue
```rust
var queue = Queue<string>.new();
queue.enqueue("item");
var item = queue.dequeue();                 // -> string?
var front = queue.peek();                   // -> string?
queue.isEmpty()                             // -> bool
queue.size                                  // -> i64
```

### Stack
```rust
var stack = Stack<string>.new();
stack.push("item");
var item = stack.pop();                     // -> string?
var top = stack.peek();                     // -> string?
stack.isEmpty()                             // -> bool
stack.size                                  // -> i64
```

### PriorityQueue
```rust
var pq = PriorityQueue<Task>.new(compareFn);
pq.enqueue(task);
var highest = pq.dequeue();                 // -> Task?
```

---

## Viper.Network

Networking operations.

### HTTP
```rust
// Simple requests
var response = Http.get(url);
var response = Http.post(url, { body: data, headers: {} });
var response = Http.put(url, { body: data });
var response = Http.delete(url);

// Response object
response.ok                                 // -> bool
response.statusCode                         // -> i64
response.body                               // -> string
response.headers                            // -> Map<string, string>
```

### TCP
```rust
// Client
var socket = TcpSocket.connect(host, port);
socket.write(data);
var response = socket.read(bufferSize);
var line = socket.readLine();
socket.close();

// Server
var server = TcpServer.listen(port);
var client = server.accept();               // Blocks until connection
client.write("Hello");
client.close();
server.close();
```

### UDP
```rust
var socket = UdpSocket.create();
socket.send(data, address, port);
var packet = socket.receive();              // -> { data, address, port }
socket.close();
```

---

## Viper.JSON

JSON parsing and generation.

```rust
// Parsing
var data = JSON.parse(jsonString);
var value = data["key"].asString();
var num = data["count"].asInt();
var arr = data["items"].asArray();

// Creating
var obj = JSON.object();
obj.set("name", "Alice");
obj.set("age", 30);

var arr = JSON.array();
arr.add("item1");
arr.add("item2");

var json = obj.toString();                  // Compact
var json = obj.toPrettyString();            // Formatted
```

---

## Viper.Threading

Concurrency primitives.

```rust
// Threads
var thread = Thread.spawn(func() {
    // work
});
thread.join();                              // Wait for completion
var result = thread.result();               // Get return value

// Mutex
var mutex = Mutex.create();
mutex.lock();
// critical section
mutex.unlock();

mutex.synchronized(func() {
    // automatically locked/unlocked
});

// Atomics
var counter = Atomic<i64>.create(0);
counter.increment();
counter.decrement();
counter.add(5);
var value = counter.get();
counter.set(100);
counter.compareAndSwap(old, new);

// Channels
var channel = Channel<string>.create();
channel.send("message");
var msg = channel.receive();                // Blocks
channel.close();

// Thread pool
var pool = ThreadPool.create(numWorkers);
pool.submit(func() { /* work */ });
var future = pool.submitWithResult(func() -> T { /* work */ });
var result = future.get();                  // Blocks until ready
pool.waitAll();
pool.shutdown();
```

---

## Viper.Graphics

Graphics and game development.

```rust
// Canvas
var canvas = Canvas(width, height);
canvas.setTitle("Window Title");
canvas.isOpen()                             // -> bool
canvas.show();                              // Display buffer
canvas.waitForClose();

// Colors
canvas.setColor(Color.RED);
canvas.setColor(Color(r, g, b));
canvas.setColor(Color(r, g, b, a));

// Shapes
canvas.fillRect(x, y, width, height);
canvas.drawRect(x, y, width, height);
canvas.fillCircle(centerX, centerY, radius);
canvas.drawCircle(centerX, centerY, radius);
canvas.fillEllipse(x, y, width, height);
canvas.drawLine(x1, y1, x2, y2);
canvas.drawPolygon(points);
canvas.fillPolygon(points);
canvas.setPixel(x, y);

// Text
canvas.setFont(name, size);
canvas.drawText(x, y, text);

// Images
var image = Image.load("sprite.png");
canvas.drawImage(image, x, y);
canvas.drawImageScaled(image, x, y, width, height);
```

---

## Viper.Input

Input handling for games and interactive applications.

```rust
// Keyboard
Input.isKeyDown(Key.SPACE)                  // Currently held
Input.wasKeyPressed(Key.SPACE)              // Just pressed this frame
Input.wasKeyReleased(Key.SPACE)             // Just released this frame

// Mouse
Input.mouseX()                              // -> f64
Input.mouseY()                              // -> f64
Input.isMouseDown(MouseButton.LEFT)
Input.wasMousePressed(MouseButton.LEFT)
Input.mouseScroll()                         // -> f64 (wheel delta)

// Game controller
Input.isControllerConnected(index)          // -> bool
Input.controllerAxis(index, Axis.LEFT_X)    // -> f64 (-1 to 1)
Input.isControllerButtonDown(index, ControllerButton.A)
```

---

## Viper.Crypto

Cryptographic functions.

```rust
// Hashing
Viper.Crypto.md5(data)                      // -> string (hex)
Viper.Crypto.sha1(data)                     // -> string (hex)
Viper.Crypto.sha256(data)                   // -> string (hex)
Viper.Crypto.sha512(data)                   // -> string (hex)

// Random
Viper.Crypto.randomBytes(length)            // -> [u8]
Viper.Crypto.randomHex(length)              // -> string

// Encoding
Viper.Crypto.base64Encode(data)             // -> string
Viper.Crypto.base64Decode(encoded)          // -> [u8]
Viper.Crypto.hexEncode(data)                // -> string
Viper.Crypto.hexDecode(hex)                 // -> [u8]
```

---

## Viper.Environment

Environment and system information.

```rust
// Environment variables
Viper.Environment.get("PATH")               // -> string?
Viper.Environment.get("VAR", "default")     // With default
Viper.Environment.set("VAR", "value");
Viper.Environment.getAll()                  // -> Map<string, string>

// System info
Viper.Environment.os                        // "windows", "macos", "linux"
Viper.Environment.arch                      // "x64", "arm64"
Viper.Environment.cpuCount                  // -> i64
Viper.Environment.homeDir                   // -> string
Viper.Environment.currentDir                // -> string
Viper.Environment.tempDir                   // -> string

// Process
Viper.Environment.args                      // Command line arguments
Viper.Environment.exit(code);               // Exit program
```

---

## Viper.Regex

Regular expressions.

```rust
var regex = Regex.compile("\\d+");

regex.matches("abc123def")                  // -> bool
regex.find("abc123def")                     // -> Match?
regex.findAll("a1b2c3")                     // -> [Match]
regex.replace("abc123", "X")                // -> "abcX"
regex.replaceAll("a1b2", "X")               // -> "aXbX"
regex.split("a,b,c")                        // -> [string]

// Match object
match.value                                 // Matched text
match.start                                 // Start index
match.end                                   // End index
match.groups                                // Capture groups
```

---

## Viper.Process

Running external processes.

```rust
// Simple execution
var result = Process.run("ls", ["-la"]);
result.exitCode                             // -> i64
result.stdout                               // -> string
result.stderr                               // -> string

// With options
var result = Process.run("cmd", args, {
    cwd: "/path",
    env: { "VAR": "value" },
    timeout: 5000
});

// Background process
var process = Process.spawn("server", []);
process.write("input");
var output = process.read();
process.kill();
process.wait();
```

---

## Viper.Test

Testing utilities.

```rust
// Assertions
assert condition;
assert condition, "message";
assertEqual(actual, expected);
assertNotEqual(a, b);
assertNull(value);
assertNotNull(value);
assertClose(actual, expected, tolerance);
assertThrows(func() { ... });
assertContains(collection, item);
assertEmpty(collection);
assertLength(collection, length);

// Test structure
test "description" {
    // test code
}

setup {
    // before each test
}

teardown {
    // after each test
}
```

---

*[Back to Table of Contents](../README.md) | [Prev: Appendix C](c-pascal-reference.md) | [Next: Appendix E: Error Messages →](e-error-messages.md)*
