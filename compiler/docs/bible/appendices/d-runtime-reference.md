# Appendix D: Runtime Library Reference

A quick reference for Viper's runtime library modules.

---

## Viper.Terminal

Console input/output operations.

```viper
// Output
Viper.Terminal.Say("message");              // Print with newline
Viper.Terminal.Write("message");            // Print without newline
Viper.Terminal.SayError("error");           // Print to stderr

// Input
let name = Viper.Terminal.Ask("Prompt: ");  // Read line
let char = Viper.Terminal.GetChar();        // Read single character
let key = Viper.Terminal.GetKey();          // Read key (with arrows, etc.)

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

```viper
// Reading
let text = Viper.File.readText("file.txt");
let bytes = Viper.File.readBytes("file.bin");
let lines = Viper.File.readLines("file.txt");

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

```viper
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

```viper
// Current time
Viper.Time.millis()                         // Milliseconds since epoch
Viper.Time.nanos()                          // Nanoseconds since epoch
Viper.Time.now()                            // -> DateTime

// Sleeping
Viper.Time.sleep(milliseconds)              // Pause execution

// DateTime operations
let dt = DateTime.now();
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
```viper
let map = Map<string, i64>.new();
map.set("key", 42);
let value = map.get("key");                 // -> i64?
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
```viper
let set = Set<string>.new();
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
```viper
let queue = Queue<string>.new();
queue.enqueue("item");
let item = queue.dequeue();                 // -> string?
let front = queue.peek();                   // -> string?
queue.isEmpty()                             // -> bool
queue.size                                  // -> i64
```

### Stack
```viper
let stack = Stack<string>.new();
stack.push("item");
let item = stack.pop();                     // -> string?
let top = stack.peek();                     // -> string?
stack.isEmpty()                             // -> bool
stack.size                                  // -> i64
```

### PriorityQueue
```viper
let pq = PriorityQueue<Task>.new(compareFn);
pq.enqueue(task);
let highest = pq.dequeue();                 // -> Task?
```

---

## Viper.Network

Networking operations.

### HTTP
```viper
// Simple requests
let response = Http.get(url);
let response = Http.post(url, { body: data, headers: {} });
let response = Http.put(url, { body: data });
let response = Http.delete(url);

// Response object
response.ok                                 // -> bool
response.statusCode                         // -> i64
response.body                               // -> string
response.headers                            // -> Map<string, string>
```

### TCP
```viper
// Client
let socket = TcpSocket.connect(host, port);
socket.write(data);
let response = socket.read(bufferSize);
let line = socket.readLine();
socket.close();

// Server
let server = TcpServer.listen(port);
let client = server.accept();               // Blocks until connection
client.write("Hello");
client.close();
server.close();
```

### UDP
```viper
let socket = UdpSocket.create();
socket.send(data, address, port);
let packet = socket.receive();              // -> { data, address, port }
socket.close();
```

---

## Viper.JSON

JSON parsing and generation.

```viper
// Parsing
let data = JSON.parse(jsonString);
let value = data["key"].asString();
let num = data["count"].asInt();
let arr = data["items"].asArray();

// Creating
let obj = JSON.object();
obj.set("name", "Alice");
obj.set("age", 30);

let arr = JSON.array();
arr.add("item1");
arr.add("item2");

let json = obj.toString();                  // Compact
let json = obj.toPrettyString();            // Formatted
```

---

## Viper.Threading

Concurrency primitives.

```viper
// Threads
let thread = Thread.spawn(func() {
    // work
});
thread.join();                              // Wait for completion
let result = thread.result();               // Get return value

// Mutex
let mutex = Mutex.create();
mutex.lock();
// critical section
mutex.unlock();

mutex.synchronized(func() {
    // automatically locked/unlocked
});

// Atomics
let counter = Atomic<i64>.create(0);
counter.increment();
counter.decrement();
counter.add(5);
let value = counter.get();
counter.set(100);
counter.compareAndSwap(old, new);

// Channels
let channel = Channel<string>.create();
channel.send("message");
let msg = channel.receive();                // Blocks
channel.close();

// Thread pool
let pool = ThreadPool.create(numWorkers);
pool.submit(func() { /* work */ });
let future = pool.submitWithResult(func() -> T { /* work */ });
let result = future.get();                  // Blocks until ready
pool.waitAll();
pool.shutdown();
```

---

## Viper.Graphics

Graphics and game development.

```viper
// Canvas
let canvas = Canvas(width, height);
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
let image = Image.load("sprite.png");
canvas.drawImage(image, x, y);
canvas.drawImageScaled(image, x, y, width, height);
```

---

## Viper.Input

Input handling for games and interactive applications.

```viper
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

```viper
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

```viper
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

```viper
let regex = Regex.compile("\\d+");

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

```viper
// Simple execution
let result = Process.run("ls", ["-la"]);
result.exitCode                             // -> i64
result.stdout                               // -> string
result.stderr                               // -> string

// With options
let result = Process.run("cmd", args, {
    cwd: "/path",
    env: { "VAR": "value" },
    timeout: 5000
});

// Background process
let process = Process.spawn("server", []);
process.write("input");
let output = process.read();
process.kill();
process.wait();
```

---

## Viper.Test

Testing utilities.

```viper
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
