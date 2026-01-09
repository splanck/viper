# Chapter 22: Networking

Your programs have worked with files on your computer. But the real power of modern computing is connection — reaching out to other computers, accessing web services, building multiplayer games, creating chat applications.

This chapter teaches you to make your programs talk to the world.

---

## The Internet in 60 Seconds

When you visit a website, here's what happens:

1. Your computer asks "Where is example.com?" (DNS lookup)
2. It gets an address like 93.184.216.34
3. Your computer connects to that address on port 80 (HTTP) or 443 (HTTPS)
4. It sends a request: "Give me the homepage"
5. The server sends back HTML, images, data
6. Your browser displays it

Programs can do all of this too. That's networking.

---

## Making HTTP Requests

The simplest networking is fetching web pages and APIs:

```rust
import Viper.Network;

func start() {
    // Fetch a web page
    var response = Http.get("https://api.example.com/data");

    if response.ok {
        Viper.Terminal.Say("Got response:");
        Viper.Terminal.Say(response.body);
    } else {
        Viper.Terminal.Say("Error: " + response.statusCode);
    }
}
```

### HTTP Methods

Different methods for different purposes:

```rust
// GET - retrieve data
var users = Http.get("https://api.example.com/users");

// POST - send data
var newUser = Http.post("https://api.example.com/users", {
    body: '{"name": "Alice", "email": "alice@example.com"}',
    headers: { "Content-Type": "application/json" }
});

// PUT - update data
var updated = Http.put("https://api.example.com/users/123", {
    body: '{"name": "Alice Smith"}',
    headers: { "Content-Type": "application/json" }
});

// DELETE - remove data
var deleted = Http.delete("https://api.example.com/users/123");
```

### Working with JSON APIs

Most modern APIs return JSON:

```rust
import Viper.Network;
import Viper.JSON;

func fetchWeather(city: string) -> Weather? {
    var url = "https://api.weather.example.com/current?city=" + city;
    var response = Http.get(url);

    if !response.ok {
        return null;
    }

    var data = JSON.parse(response.body);

    return Weather {
        temperature: data["temp"].asFloat(),
        conditions: data["conditions"].asString(),
        humidity: data["humidity"].asFloat()
    };
}

value Weather {
    temperature: f64;
    conditions: string;
    humidity: f64;
}

func start() {
    var weather = fetchWeather("Seattle");
    if weather != null {
        Viper.Terminal.Say("Temperature: " + weather.temperature + "°F");
        Viper.Terminal.Say("Conditions: " + weather.conditions);
    }
}
```

---

## TCP: The Foundation

HTTP is built on TCP (Transmission Control Protocol). TCP provides reliable, ordered delivery of data between computers.

### TCP Client

Connecting to a server:

```rust
import Viper.Network;

func start() {
    // Connect to a server
    var socket = TcpSocket.connect("example.com", 80);

    if socket == null {
        Viper.Terminal.Say("Connection failed");
        return;
    }

    // Send data
    socket.write("GET / HTTP/1.1\r\nHost: example.com\r\n\r\n");

    // Receive response
    var response = socket.readAll();
    Viper.Terminal.Say(response);

    // Clean up
    socket.close();
}
```

### TCP Server

Accepting connections:

```rust
import Viper.Network;

func start() {
    // Listen for connections on port 8080
    var server = TcpServer.listen(8080);
    Viper.Terminal.Say("Server listening on port 8080");

    while true {
        // Wait for a client
        var client = server.accept();
        Viper.Terminal.Say("Client connected from " + client.remoteAddress());

        // Read their message
        var message = client.readLine();
        Viper.Terminal.Say("Received: " + message);

        // Send a response
        client.write("Hello, client!\n");

        // Close connection
        client.close();
    }
}
```

---

## Building a Chat Application

Let's build something real: a simple chat system.

### Chat Server

```rust
module ChatServer;

import Viper.Network;
import Viper.Collections;

entity ChatServer {
    hide server: TcpServer;
    hide clients: [TcpSocket];
    hide running: bool;

    expose func init(port: i64) {
        self.server = TcpServer.listen(port);
        self.clients = [];
        self.running = true;
        Viper.Terminal.Say("Chat server started on port " + port);
    }

    func run() {
        while self.running {
            // Check for new connections (non-blocking)
            var newClient = self.server.acceptNonBlocking();
            if newClient != null {
                self.clients.push(newClient);
                self.broadcast("*** A new user has joined ***");
                Viper.Terminal.Say("New client connected");
            }

            // Check each client for messages
            var i = 0;
            while i < self.clients.length {
                var client = self.clients[i];

                if client.hasData() {
                    var message = client.readLine();

                    if message == null || message == "/quit" {
                        // Client disconnected
                        self.clients.remove(i);
                        self.broadcast("*** A user has left ***");
                        client.close();
                        continue;
                    }

                    self.broadcast(message);
                }

                i += 1;
            }

            Viper.Time.sleep(10);  // Don't spin too fast
        }
    }

    func broadcast(message: string) {
        for client in self.clients {
            client.write(message + "\n");
        }
    }

    func stop() {
        self.running = false;
        for client in self.clients {
            client.close();
        }
        self.server.close();
    }
}

func start() {
    var server = ChatServer(9000);
    server.run();
}
```

### Chat Client

```rust
module ChatClient;

import Viper.Network;
import Viper.Threading;

entity ChatClient {
    hide socket: TcpSocket;
    hide username: string;
    hide running: bool;

    expose func init(host: string, port: i64, username: string) {
        self.socket = TcpSocket.connect(host, port);
        self.username = username;
        self.running = true;

        if self.socket == null {
            Viper.Terminal.Say("Could not connect to server");
            return;
        }

        Viper.Terminal.Say("Connected to chat server");
    }

    func run() {
        // Start a thread to receive messages
        var receiver = Thread.spawn(self.receiveLoop);

        // Main thread handles sending
        while self.running {
            var input = Viper.Terminal.Ask("");

            if input == "/quit" {
                self.running = false;
                self.socket.write("/quit\n");
                break;
            }

            self.socket.write(self.username + ": " + input + "\n");
        }

        receiver.join();
        self.socket.close();
    }

    func receiveLoop() {
        while self.running {
            if self.socket.hasData() {
                var message = self.socket.readLine();
                if message != null {
                    Viper.Terminal.Say(message);
                }
            }
            Viper.Time.sleep(10);
        }
    }
}

func start() {
    var username = Viper.Terminal.Ask("Enter your username: ");
    var client = ChatClient("localhost", 9000, username);
    client.run();
}
```

---

## UDP: Fast but Unreliable

UDP (User Datagram Protocol) is faster than TCP but doesn't guarantee delivery. Good for games, video streaming, and real-time data where speed matters more than reliability.

```rust
import Viper.Network;

// UDP sender
func sendUdp() {
    var socket = UdpSocket.create();
    socket.send("Hello!", "192.168.1.100", 5000);
    socket.close();
}

// UDP receiver
func receiveUdp() {
    var socket = UdpSocket.bind(5000);

    while true {
        var packet = socket.receive();
        Viper.Terminal.Say("From " + packet.address + ": " + packet.data);
    }
}
```

### Game Networking with UDP

For multiplayer games, UDP is often preferred:

```rust
value PlayerState {
    id: i64;
    x: f64;
    y: f64;
    rotation: f64;
    health: i64;
}

entity GameNetwork {
    hide socket: UdpSocket;
    hide serverAddress: string;
    hide serverPort: i64;

    expose func init(serverAddress: string, serverPort: i64) {
        self.socket = UdpSocket.create();
        self.serverAddress = serverAddress;
        self.serverPort = serverPort;
    }

    func sendState(state: PlayerState) {
        // Pack state into bytes
        var data = packPlayerState(state);
        self.socket.send(data, self.serverAddress, self.serverPort);
    }

    func receiveStates() -> [PlayerState] {
        var states: [PlayerState] = [];

        while self.socket.hasData() {
            var packet = self.socket.receive();
            var state = unpackPlayerState(packet.data);
            states.push(state);
        }

        return states;
    }
}

func packPlayerState(state: PlayerState) -> string {
    // Simple text protocol (real games use binary)
    return state.id + "," + state.x + "," + state.y + "," +
           state.rotation + "," + state.health;
}

func unpackPlayerState(data: string) -> PlayerState {
    var parts = data.split(",");
    return PlayerState {
        id: parts[0].toInt(),
        x: parts[1].toFloat(),
        y: parts[2].toFloat(),
        rotation: parts[3].toFloat(),
        health: parts[4].toInt()
    };
}
```

---

## WebSockets: Real-Time Web

WebSockets provide full-duplex communication over HTTP, perfect for real-time web applications:

```rust
import Viper.Network;

entity WebSocketClient {
    hide ws: WebSocket;

    func connect(url: string) {
        self.ws = WebSocket.connect(url);

        self.ws.onOpen(func() {
            Viper.Terminal.Say("Connected!");
            self.ws.send("Hello server!");
        });

        self.ws.onMessage(func(message: string) {
            Viper.Terminal.Say("Received: " + message);
        });

        self.ws.onClose(func() {
            Viper.Terminal.Say("Disconnected");
        });

        self.ws.onError(func(error: string) {
            Viper.Terminal.Say("Error: " + error);
        });
    }

    func send(message: string) {
        self.ws.send(message);
    }

    func close() {
        self.ws.close();
    }
}
```

---

## Handling Network Errors

Networks are unreliable. Connections drop, servers go down, packets get lost. Always handle errors:

```rust
import Viper.Network;

func robustFetch(url: string, maxRetries: i64) -> string? {
    var retries = 0;

    while retries < maxRetries {
        try {
            var response = Http.get(url, { timeout: 5000 });

            if response.ok {
                return response.body;
            }

            if response.statusCode >= 500 {
                // Server error, worth retrying
                retries += 1;
                Viper.Terminal.Say("Server error, retrying... (" + retries + ")");
                Viper.Time.sleep(1000 * retries);  // Exponential backoff
                continue;
            }

            // Client error (4xx), don't retry
            Viper.Terminal.Say("Client error: " + response.statusCode);
            return null;

        } catch NetworkError as e {
            retries += 1;
            Viper.Terminal.Say("Network error: " + e.message + ", retrying...");
            Viper.Time.sleep(1000 * retries);
        }
    }

    Viper.Terminal.Say("Failed after " + maxRetries + " retries");
    return null;
}
```

### Timeouts

Always set timeouts to prevent hanging:

```rust
// With timeout
var response = Http.get(url, { timeout: 5000 });  // 5 second timeout

// For sockets
var socket = TcpSocket.connect(host, port, { timeout: 3000 });
socket.setReadTimeout(10000);  // 10 second read timeout
```

---

## A Complete Example: Weather Dashboard

```rust
module WeatherDashboard;

import Viper.Network;
import Viper.JSON;
import Viper.Time;

value CityWeather {
    city: string;
    temperature: f64;
    conditions: string;
    humidity: f64;
    windSpeed: f64;
    lastUpdated: i64;
}

entity WeatherService {
    hide apiKey: string;
    hide baseUrl: string;
    hide cache: Map<string, CityWeather>;
    hide cacheTimeout: i64;

    expose func init(apiKey: string) {
        self.apiKey = apiKey;
        self.baseUrl = "https://api.weather.example.com/v1";
        self.cache = Map.new();
        self.cacheTimeout = 300000;  // 5 minutes
    }

    func getWeather(city: string) -> CityWeather? {
        // Check cache first
        if self.cache.has(city) {
            var cached = self.cache.get(city);
            var age = Time.millis() - cached.lastUpdated;
            if age < self.cacheTimeout {
                return cached;
            }
        }

        // Fetch from API
        var url = self.baseUrl + "/current?city=" +
                  Network.urlEncode(city) + "&key=" + self.apiKey;

        try {
            var response = Http.get(url, { timeout: 10000 });

            if !response.ok {
                Viper.Terminal.Say("API error: " + response.statusCode);
                return null;
            }

            var data = JSON.parse(response.body);

            var weather = CityWeather {
                city: city,
                temperature: data["main"]["temp"].asFloat(),
                conditions: data["weather"][0]["description"].asString(),
                humidity: data["main"]["humidity"].asFloat(),
                windSpeed: data["wind"]["speed"].asFloat(),
                lastUpdated: Time.millis()
            };

            // Update cache
            self.cache.set(city, weather);

            return weather;

        } catch NetworkError as e {
            Viper.Terminal.Say("Network error: " + e.message);
            return null;
        } catch JSONError as e {
            Viper.Terminal.Say("Parse error: " + e.message);
            return null;
        }
    }
}

func displayWeather(weather: CityWeather) {
    Viper.Terminal.Say("");
    Viper.Terminal.Say("=== " + weather.city + " ===");
    Viper.Terminal.Say("Temperature: " + weather.temperature + "°F");
    Viper.Terminal.Say("Conditions:  " + weather.conditions);
    Viper.Terminal.Say("Humidity:    " + weather.humidity + "%");
    Viper.Terminal.Say("Wind:        " + weather.windSpeed + " mph");
}

func start() {
    var service = WeatherService("your-api-key-here");

    var cities = ["Seattle", "New York", "London", "Tokyo", "Sydney"];

    Viper.Terminal.Say("Weather Dashboard");
    Viper.Terminal.Say("=================");

    for city in cities {
        var weather = service.getWeather(city);
        if weather != null {
            displayWeather(weather);
        } else {
            Viper.Terminal.Say("Could not fetch weather for " + city);
        }
    }
}
```

---

## The Three Languages

**ViperLang**
```rust
import Viper.Network;

var response = Http.get("https://api.example.com/data");
if response.ok {
    Viper.Terminal.Say(response.body);
}

var socket = TcpSocket.connect("example.com", 80);
socket.write("Hello\n");
var reply = socket.readLine();
socket.close();
```

**BASIC**
```basic
DIM response AS HttpResponse
response = HTTP_GET("https://api.example.com/data")
IF response.Ok THEN
    PRINT response.Body
END IF

DIM sock AS TcpSocket
sock = TCP_CONNECT("example.com", 80)
TCP_WRITE sock, "Hello" + CHR$(10)
DIM reply AS STRING
reply = TCP_READLINE(sock)
TCP_CLOSE sock
```

**Pascal**
```pascal
uses ViperNetwork;
var
    response: THttpResponse;
    sock: TTcpSocket;
    reply: string;
begin
    response := HttpGet('https://api.example.com/data');
    if response.Ok then
        WriteLn(response.Body);

    sock := TcpConnect('example.com', 80);
    TcpWrite(sock, 'Hello'#10);
    reply := TcpReadLine(sock);
    TcpClose(sock);
end.
```

---

## Common Mistakes

**Forgetting to close connections**
```rust
// Bad: Connection leak!
func fetchData(url: string) -> string {
    var socket = TcpSocket.connect(host, port);
    var data = socket.readAll();
    return data;  // Socket never closed!
}

// Good: Always close
func fetchData(url: string) -> string {
    var socket = TcpSocket.connect(host, port);
    var data = socket.readAll();
    socket.close();
    return data;
}
```

**Not handling timeouts**
```rust
// Bad: Could hang forever
var response = Http.get(slowServer);

// Good: Set reasonable timeout
var response = Http.get(slowServer, { timeout: 10000 });
```

**Blocking the main thread**
```rust
// Bad: Freezes UI during network call
func onClick() {
    var data = Http.get(url);  // Blocks!
    updateUI(data);
}

// Good: Use async/threading
func onClick() {
    Thread.spawn(func() {
        var data = Http.get(url);
        runOnMainThread(func() {
            updateUI(data);
        });
    });
}
```

---

## Summary

- **HTTP** is the foundation of web communication (GET, POST, PUT, DELETE)
- **TCP** provides reliable, ordered data streams
- **UDP** is faster but unreliable — good for games and real-time data
- **WebSockets** enable real-time bidirectional web communication
- Always handle network errors gracefully
- Use timeouts to prevent hanging
- Close connections when done
- Cache responses when appropriate
- Don't block UI threads with network calls

---

## Exercises

**Exercise 22.1**: Write a program that fetches a web page and counts how many times a specific word appears.

**Exercise 22.2**: Create a simple HTTP client that can download files and save them locally.

**Exercise 22.3**: Build an echo server: whatever a client sends, the server sends back.

**Exercise 22.4**: Create a "quote of the day" server that sends a random quote to each client that connects.

**Exercise 22.5**: Build a simple port scanner that checks which ports are open on a given host.

**Exercise 22.6** (Challenge): Create a multiplayer tic-tac-toe game using sockets. One player hosts, another connects, and they take turns.

---

*We can communicate over networks. But what format should our data take? Next, we explore data formats: JSON, XML, and binary protocols.*

*[Continue to Chapter 23: Data Formats →](23-data-formats.md)*
