# Chapter 22: Networking

Every program you have written so far lives in isolation. It runs on your computer, works with local files, and interacts only with the person sitting at the keyboard. But the most transformative programs in history are not the ones that compute in solitude --- they are the ones that *connect*.

Think about the applications you use most. Email connects you to anyone in the world. Web browsers connect you to billions of pages of information. Multiplayer games connect you to friends and strangers across continents. Chat applications, video calls, social networks, online banking, streaming services, collaborative documents --- all of them share one essential capability: they communicate over a network.

Learning networking transforms you from someone who writes programs into someone who writes *systems*. A calculator computes. A chat application creates community. A weather app brings the world's meteorological data to your fingertips. A multiplayer game lets people who have never met share experiences. When your programs can talk to other computers, you can build things that no single computer could achieve alone.

This chapter teaches you to make your programs speak to the world. We will start with the concepts --- the mental models that make networking make sense --- and then build practical skills from simple web requests to full chat applications. By the end, you will understand how data flows across the internet and have the tools to build connected applications of your own.

---

## The Postal System Analogy

Before we look at any code, let's build a mental model. Networking can seem magical and opaque, but it follows rules that are remarkably similar to something you already understand: the postal system.

Imagine you want to send a letter to a friend in another city. You cannot simply think the message to them. You need infrastructure --- a system that physically carries your words from your location to theirs.

Here's what you do:

1. **Write the message** on paper
2. **Put it in an envelope** with your friend's address and your return address
3. **Drop it in a mailbox** (hand it to the postal system)
4. **The postal system routes it** through sorting facilities, trucks, and planes
5. **A mail carrier delivers it** to your friend's mailbox
6. **Your friend opens and reads it**

Network communication works almost identically:

1. **Your program creates data** (a message, a request, game state)
2. **It wraps the data in a packet** with a destination address and your address
3. **It hands the packet to the operating system** which manages the network hardware
4. **The packet travels through routers** and switches across the internet
5. **It arrives at the destination computer**
6. **A program on that computer receives and processes it**

This analogy extends further. Just as the postal system has different services (regular mail, express delivery, registered mail, packages), computer networks have different protocols for different needs. Just as you need to know your friend's address to send a letter, your program needs to know the other computer's address to send data.

### Addresses: Where Does the Letter Go?

In the postal system, an address has multiple parts: street address, city, state, country, postal code. This hierarchical structure lets the postal system route mail efficiently --- first to the right country, then the right city, then the right street.

Network addresses work similarly. Every computer on the internet has an **IP address** --- a numerical label like `192.168.1.100` or `172.217.14.206`. An IP address is like a street address for computers. When you want to send data to another computer, you need its IP address.

But here's a twist: a single computer might be running many programs that all want to receive network data. Your computer might be running a web browser, an email client, and a game simultaneously. How does incoming data find the right program?

This is where **ports** come in. A port is like an apartment number within a building. The IP address gets the data to the right computer (the building), and the port number gets it to the right program (the apartment).

```
IP Address: 192.168.1.100     (which computer)
Port:       8080              (which program on that computer)

Complete address: 192.168.1.100:8080

Analogy:
Street Address: 123 Main Street    (which building)
Apartment:      #4B                (which unit in the building)
```

Some ports have well-known purposes, like permanent businesses at fixed locations:
- Port 80: Web traffic (HTTP)
- Port 443: Secure web traffic (HTTPS)
- Port 25: Email (SMTP)
- Port 22: Secure shell (SSH)

When you visit a website, your browser automatically uses port 443 (HTTPS) or port 80 (HTTP) because those are the standard ports for web servers. It's like knowing that the post office is always in the government district of town --- you don't need to look up the address every time.

---

## Two Types of Mail: TCP vs UDP

The postal system offers different delivery options with different guarantees. Regular mail is cheap but slow, and you don't get confirmation that it arrived. Registered mail costs more but you get delivery confirmation and tracking. Express delivery prioritizes speed over cost.

Computer networks have similar options. The two most important are **TCP** (Transmission Control Protocol) and **UDP** (User Datagram Protocol). Understanding when to use each is crucial for building effective networked applications.

### TCP: Registered Mail with Receipts

TCP is like registered mail with delivery confirmation. When you send data using TCP, you get strong guarantees:

**Guaranteed delivery**: Every packet you send will arrive. If a packet gets lost in transit (and they do --- the internet is imperfect), TCP automatically detects the loss and resends it. You don't have to worry about missing data.

**Ordered arrival**: Packets arrive in the order you sent them. If you send "Hello" then "World", the receiver gets "Hello" first and "World" second, even if "World" actually traveled faster through the network. TCP holds "World" until "Hello" arrives and delivers them in order.

**Error checking**: TCP verifies that data wasn't corrupted in transit. If a cosmic ray flips a bit or electrical noise garbles some bytes, TCP catches it and requests a resend.

**Connection-based**: TCP requires establishing a connection before sending data, like a phone call. You dial, the other side answers, then you can talk back and forth until one of you hangs up.

Here's what TCP communication looks like conceptually:

```
Your Computer                                      Server
     |                                                |
     |-------- "Hello, I want to connect" ---------> |
     |<------- "OK, I acknowledge" ------------------ |
     |-------- "Great, connection established" ----> |
     |                                                |
     |  (Connection is now open, like a phone call)  |
     |                                                |
     |-------- "Please send me the homepage" ------> |
     |<------- "Here is the homepage (part 1)" ----- |
     |<------- "Here is the homepage (part 2)" ----- |
     |<------- "Here is the homepage (part 3)" ----- |
     |-------- "Got it all, thanks!" ---------------> |
     |                                                |
     |-------- "Goodbye" --------------------------> |
     |<------- "Goodbye" ---------------------------- |
     |                                                |
     |  (Connection is closed)                       |
```

The three-step process at the beginning is called the "three-way handshake." It ensures both sides are ready before data starts flowing.

**When to use TCP**: Any time you need reliable, ordered delivery. Web browsing, email, file transfers, remote login, database connections, chat applications, most APIs. If losing data or receiving it out of order would cause problems, use TCP.

### UDP: Postcards, No Tracking

UDP is like sending a postcard. It's simple, fast, and lightweight, but you get no guarantees:

**No guaranteed delivery**: Packets might get lost. UDP doesn't track what was sent or received. If a packet disappears, nobody automatically notices or resends it.

**No ordering**: Packets might arrive out of order. If you send "Hello" then "World", the receiver might get "World" first. Or might get neither. Or might get both in the right order. UDP doesn't care.

**No connection**: UDP is connectionless. You simply send data to an address and hope it arrives. No handshake, no confirmation, no ongoing session. Each packet is independent, like individual postcards.

**Lightweight and fast**: Because UDP skips all the reliability overhead, it's significantly faster. No waiting for acknowledgments, no retransmission delays, no ordering buffers.

```
Your Computer                                      Server
     |                                                |
     |-------- "Here's my position: x=10, y=20" ---> |
     |-------- "Here's my position: x=11, y=20" ---> |
     |-------- "Here's my position: x=12, y=21" ---> |
     |                (no responses needed)          |
     |                                                |
```

**When to use UDP**: When speed matters more than perfect reliability, and when you can tolerate or handle lost data yourself. Video streaming (a dropped frame is better than a delayed one), online games (old player positions are worthless, only current positions matter), voice calls (late audio is useless), DNS lookups, live broadcasts.

### The Tradeoff in Action

Imagine you're building a multiplayer game. You need to send two types of data:

1. **Player positions** (60 times per second) --- If a position update gets lost, who cares? Another update comes in 16 milliseconds. If an old position arrives late, it's worthless garbage. Use **UDP**.

2. **Chat messages** --- Losing a chat message means missing part of the conversation. Messages out of order make no sense. Delivery matters more than instant arrival. Use **TCP**.

Many games use both protocols simultaneously: UDP for time-sensitive game state, TCP for chat and reliable game events (score updates, player joined/left).

### Summary: TCP vs UDP

| Characteristic | TCP | UDP |
|---------------|-----|-----|
| Delivery guarantee | Yes | No |
| Order guarantee | Yes | No |
| Error checking | Yes | Minimal |
| Connection required | Yes | No |
| Speed | Slower | Faster |
| Overhead | Higher | Lower |
| Use when... | Reliability matters | Speed matters |
| Examples | Web, email, file transfer | Games, streaming, voice |

---

## Client-Server Architecture

When two computers communicate, they typically play different roles. One computer *provides* a service; the other *uses* it. The provider is called the **server** (it *serves* something). The user is called the **client** (like a customer at a restaurant).

Think about a restaurant. The kitchen prepares food and waits for orders. Customers come in, place orders, receive food, and leave. The kitchen doesn't seek out hungry people --- it waits for them to arrive. Customers don't cook --- they request from the kitchen.

```
CLIENT-SERVER MODEL

      +---------+                     +---------+
      |         |                     |         |
      | Client  |                     | Server  |
      |         |                     |         |
      | (asks)  |-------Request------>| (has    |
      |         |                     |  stuff) |
      |         |<------Response------|         |
      | (gets)  |                     | (gives) |
      |         |                     |         |
      +---------+                     +---------+

   Your web browser              Google's web server
   Your email app                Gmail's mail server
   Your game client              Game company's server
```

### The Server's Job

A server's job is to:
1. **Wait** for clients to connect (like a restaurant waiting for customers)
2. **Accept** incoming connections
3. **Process** requests from clients
4. **Send** responses back
5. **Handle multiple clients** (often simultaneously)

Servers typically run continuously. A web server runs 24/7, always ready to respond to browsers. A game server stays online so players can connect whenever they want.

### The Client's Job

A client's job is to:
1. **Know** the server's address (IP and port)
2. **Connect** to the server
3. **Send** requests (what do you want from the server?)
4. **Receive** responses (what did the server send back?)
5. **Disconnect** when finished

Clients are typically temporary. You open a browser, visit some pages, then close it. You launch a game, play for a while, then quit. The client exists only when you need it.

### A Visual Trace: What Happens When You Visit a Website

Let's trace through exactly what happens when you type `www.example.com` into your browser and press Enter:

```
Step 1: DNS Lookup (What's the IP address?)
+---------+                            +-----------+
| Browser |----"Where is example.com?"->| DNS      |
|         |<---"It's at 93.184.216.34"--|  Server   |
+---------+                            +-----------+

Step 2: TCP Connection (Let's establish communication)
+---------+                            +-----------+
| Browser |-------- SYN ---------------->| Web      |
|         |<------- SYN-ACK ------------|  Server   |
|         |-------- ACK ----------------->|          |
+---------+                            +-----------+
   (Three-way handshake complete)

Step 3: HTTP Request (What do you want?)
+---------+                            +-----------+
| Browser |----"GET / HTTP/1.1"-------->| Web      |
|         |    "Host: example.com"      |  Server   |
+---------+                            +-----------+

Step 4: Server Processing
           +-----------+
           | Web       |
           |  Server   |  "They want the homepage..."
           |           |  "Let me find that file..."
           |           |  "Preparing the response..."
           +-----------+

Step 5: HTTP Response (Here's what you asked for)
+---------+                            +-----------+
| Browser |<---"HTTP/1.1 200 OK"-------| Web      |
|         |    "Content-Type: text/html"|  Server   |
|         |    "<html><body>..."        |          |
+---------+                            +-----------+

Step 6: Connection Close
+---------+                            +-----------+
| Browser |-------- FIN ---------------->| Web      |
|         |<------- ACK ----------------|  Server   |
|         |<------- FIN ----------------|          |
|         |-------- ACK ----------------->|          |
+---------+                            +-----------+
```

All of this happens in milliseconds. The complexity is hidden behind simple functions like `Http.get()`.

---

## The Internet in 60 Seconds (Revisited)

Now that you understand addresses, protocols, and client-server architecture, let's revisit what happens when you access a website:

1. **DNS lookup**: Your computer asks "Where is example.com?" and gets back `93.184.216.34` (the IP address)
2. **TCP connection**: Your computer connects to that address on port 443 (HTTPS) or 80 (HTTP)
3. **Request**: Your browser sends "Give me the homepage"
4. **Processing**: The server finds and prepares the response
5. **Response**: The server sends back HTML, CSS, JavaScript, images
6. **Rendering**: Your browser assembles and displays everything
7. **Disconnect**: The connection closes (or stays open for more requests)

Programs can do all of this too. That's networking.

---

## Making HTTP Requests

The simplest networking is fetching web pages and APIs. HTTP (Hypertext Transfer Protocol) is the language of the web --- the format that browsers and servers use to communicate.

```rust
bind Viper.Network;
bind Viper.Terminal;

func start() {
    // Fetch a web page
    var response = Http.get("https://api.example.com/data");

    if response.ok {
        Terminal.Say("Got response:");
        Terminal.Say(response.body);
    } else {
        Terminal.Say("Error: " + response.statusCode);
    }
}
```

Let's trace through what this code actually does:

1. `Http.get()` is called with a URL
2. Internally, it parses the URL to extract the host (`api.example.com`) and path (`/data`)
3. It performs a DNS lookup to get the IP address
4. It opens a TCP connection to that IP on port 443 (HTTPS)
5. It sends an HTTP GET request
6. It waits for and reads the response
7. It packages the response (status code, headers, body) into a `response` value
8. It returns that value to your code

All of that complexity is hidden behind one function call. This is abstraction at work.

### Understanding HTTP Methods

HTTP defines several methods (also called verbs) for different purposes. Each method signals a different *intent*:

**GET**: Retrieve data. "Give me something." This should not modify anything on the server. Reading, not writing. Safe to repeat.

```rust
// GET - retrieve data
var users = Http.get("https://api.example.com/users");
```

**POST**: Create new data. "Here's something new." This typically modifies the server, adding new resources.

```rust
// POST - send data to create something new
var newUser = Http.post("https://api.example.com/users", {
    body: '{"name": "Alice", "email": "alice@example.com"}',
    headers: { "Content-Type": "application/json" }
});
```

**PUT**: Update existing data. "Replace what you have with this." Used to update resources that already exist.

```rust
// PUT - update existing data
var updated = Http.put("https://api.example.com/users/123", {
    body: '{"name": "Alice Smith"}',
    headers: { "Content-Type": "application/json" }
});
```

**DELETE**: Remove data. "Get rid of this." Deletes resources from the server.

```rust
// DELETE - remove data
var deleted = Http.delete("https://api.example.com/users/123");
```

These methods map to CRUD operations (Create, Read, Update, Delete) that are fundamental to most applications:

| Operation | HTTP Method | Example |
|-----------|-------------|---------|
| Create | POST | Add a new user |
| Read | GET | View user profile |
| Update | PUT | Change user email |
| Delete | DELETE | Remove user account |

### Working with JSON APIs

Most modern web APIs exchange data in JSON (JavaScript Object Notation) format. JSON is a text format for structured data that's easy for both humans and computers to read.

```rust
bind Viper.Network;
bind Viper.JSON;
bind Viper.Terminal;

value Weather {
    temperature: Number;
    conditions: String;
    humidity: Number;
}

func fetchWeather(city: String) -> Weather? {
    // Build the URL with the city parameter
    var url = "https://api.weather.example.com/current?city=" + city;

    // Make the request
    var response = Http.get(url);

    // Check if the request succeeded
    if !response.ok {
        Terminal.Say("Request failed with status: " + response.statusCode);
        return null;
    }

    // Parse the JSON response
    // The response body might look like:
    // {"temp": 72.5, "conditions": "Sunny", "humidity": 45.0}
    var data = JSON.parse(response.body);

    // Extract the fields we need
    return Weather {
        temperature: data["temp"].asFloat(),
        conditions: data["conditions"].asString(),
        humidity: data["humidity"].asFloat()
    };
}

func start() {
    var weather = fetchWeather("Seattle");

    if weather != null {
        Terminal.Say("Temperature: " + weather.temperature + "F");
        Terminal.Say("Conditions: " + weather.conditions);
        Terminal.Say("Humidity: " + weather.humidity + "%");
    } else {
        Terminal.Say("Could not fetch weather data");
    }
}
```

Let's trace through the JSON parsing:

```
Server Response (text):
{"temp": 72.5, "conditions": "Sunny", "humidity": 45.0}

After JSON.parse():
data is now a JSON object where:
  data["temp"] is a JSON number containing 72.5
  data["conditions"] is a JSON string containing "Sunny"
  data["humidity"] is a JSON number containing 45.0

After extracting values:
weather.temperature = 72.5
weather.conditions = "Sunny"
weather.humidity = 45.0
```

---

## TCP: The Foundation

HTTP is built on top of TCP. When you need more control than HTTP provides, or when you're building your own protocol, you work directly with TCP sockets.

A **socket** is an endpoint for communication --- like a telephone in the phone call analogy. You create a socket, connect it to another socket, and then you can send and receive data.

### TCP Client

Here's how to connect to a server and communicate:

```rust
bind Viper.Network;
bind Viper.Terminal;

func start() {
    // Connect to a server
    // This is like dialing a phone number
    var socket = TcpSocket.connect("example.com", 80);

    // Check if connection succeeded
    if socket == null {
        Terminal.Say("Connection failed");
        return;
    }

    Terminal.Say("Connected!");

    // Send data (write to the socket)
    // This is like talking into the phone
    socket.write("GET / HTTP/1.1\r\nHost: example.com\r\n\r\n");

    // Receive response (read from the socket)
    // This is like listening to the other person
    var response = socket.readAll();
    Terminal.Say("Received " + response.length + " bytes");
    Terminal.Say(response);

    // Close the connection (hang up the phone)
    socket.close();
}
```

Let's trace through what happens:

```
Step 1: TcpSocket.connect("example.com", 80)
   - DNS lookup: "example.com" -> 93.184.216.34
   - Create a socket (OS allocates resources)
   - TCP three-way handshake with 93.184.216.34:80
   - Return the connected socket

Step 2: socket.write("GET / HTTP/1.1\r\n...")
   - Convert string to bytes
   - Pass bytes to OS network stack
   - OS sends TCP packet(s) to server
   - Wait for acknowledgment

Step 3: socket.readAll()
   - Wait for incoming data
   - Receive TCP packets from server
   - Reassemble into continuous byte stream
   - Return as string when connection closes or all data received

Step 4: socket.close()
   - Send TCP FIN packet
   - Wait for server's FIN
   - Release OS resources
   - Socket can no longer be used
```

### TCP Server

A server listens for incoming connections instead of initiating them:

```rust
bind Viper.Network;
bind Viper.Terminal;

func start() {
    // Create a server socket that listens on port 8080
    // This is like setting up a phone line that can receive calls
    var server = TcpServer.listen(8080);
    Terminal.Say("Server listening on port 8080");

    // Server loop: accept and handle connections forever
    while true {
        // Wait for a client to connect
        // This is like waiting for the phone to ring, then answering
        Terminal.Say("Waiting for client...");
        var client = server.accept();

        Terminal.Say("Client connected from " + client.remoteAddress());

        // Read what the client sent
        var message = client.readLine();
        Terminal.Say("Client said: " + message);

        // Send a response
        client.write("Hello, client! You said: " + message + "\n");

        // Close this client connection
        // The server keeps running, ready for more clients
        client.close();
        Terminal.Say("Client disconnected");
    }
}
```

The server's lifecycle:

```
                +-------------------+
                |   TcpServer.      |
                |   listen(8080)    |
                +--------+----------+
                         |
                         v
                +--------+----------+
                |                   |
                |   server.accept() |<--+
                |   (waits...)      |   |
                +--------+----------+   |
                         |              |
                         | client       |
                         | connects     |
                         v              |
                +--------+----------+   |
                |  Handle client:   |   |
                |  - Read message   |   |
                |  - Send response  |   |
                |  - Close client   |   |
                +--------+----------+   |
                         |              |
                         +--------------+
                    (loop back to accept)
```

---

## Building a Chat Application

Let's apply what we've learned to build something real: a chat system where multiple clients can send messages that everyone sees. This demonstrates many networking concepts working together.

### Understanding the Architecture

```
         +----------------+
         |  Chat Server   |
         |  (port 9000)   |
         +-------+--------+
                 |
     +-----------+-----------+
     |           |           |
+----+----+ +----+----+ +----+----+
| Client1 | | Client2 | | Client3 |
| (Alice) | |  (Bob)  | | (Carol) |
+---------+ +---------+ +---------+

When Alice sends "Hello":
1. Alice's client sends "Hello" to server
2. Server receives "Hello" from Alice
3. Server broadcasts "Alice: Hello" to ALL clients
4. Bob and Carol (and Alice) see "Alice: Hello"
```

### Chat Server

```rust
module ChatServer;

bind Viper.Network;
bind Viper.Collections;
bind Viper.Terminal;
bind Viper.Time;

entity ChatServer {
    // The server socket that accepts new connections
    hide server: TcpServer;

    // List of all connected clients
    hide clients: [TcpSocket];

    // Flag to control the server loop
    hide running: Boolean;

    expose func init(port: Integer) {
        // Start listening on the specified port
        self.server = TcpServer.listen(port);
        self.clients = [];
        self.running = true;
        Terminal.Say("Chat server started on port " + port);
    }

    // Main server loop
    func run() {
        while self.running {
            // Step 1: Check for new clients trying to connect
            // acceptNonBlocking returns null immediately if no one is waiting
            // (blocking accept would freeze the server until someone connects)
            var newClient = self.server.acceptNonBlocking();

            if newClient != null {
                // A new client connected!
                self.clients.push(newClient);
                self.broadcast("*** A new user has joined ***");
                Terminal.Say("New client connected. Total clients: " + self.clients.length);
            }

            // Step 2: Check each client for incoming messages
            var i = 0;
            while i < self.clients.length {
                var client = self.clients[i];

                // hasData() checks if the client sent anything without blocking
                if client.hasData() {
                    var message = client.readLine();

                    if message == null || message == "/quit" {
                        // Client disconnected or wants to leave
                        self.clients.remove(i);
                        self.broadcast("*** A user has left ***");
                        client.close();
                        Terminal.Say("Client disconnected. Total clients: " + self.clients.length);
                        // Don't increment i; the next client shifted into this position
                        continue;
                    }

                    // Broadcast the message to everyone
                    self.broadcast(message);
                    Terminal.Say("Broadcast: " + message);
                }

                i += 1;
            }

            // Small sleep to avoid consuming 100% CPU
            Time.Clock.Sleep(10);
        }
    }

    // Send a message to all connected clients
    func broadcast(message: String) {
        for client in self.clients {
            client.write(message + "\n");
        }
    }

    // Gracefully shut down the server
    func stop() {
        self.running = false;

        // Close all client connections
        for client in self.clients {
            client.write("*** Server shutting down ***\n");
            client.close();
        }

        // Close the server socket
        self.server.close();
        Terminal.Say("Server stopped");
    }
}

func start() {
    var server = ChatServer(9000);
    server.run();
}
```

Let's trace through a typical server session:

```
Time 0:00 - Server starts
  server.listen(9000) - now accepting connections
  clients = []

Time 0:05 - Alice connects
  acceptNonBlocking() returns Alice's socket
  clients = [Alice]
  broadcast("*** A new user has joined ***") -> Alice sees this

Time 0:10 - Bob connects
  acceptNonBlocking() returns Bob's socket
  clients = [Alice, Bob]
  broadcast("*** A new user has joined ***") -> Alice and Bob see this

Time 0:15 - Alice sends "Hi everyone!"
  Alice's socket.hasData() returns true
  message = "Hi everyone!"
  broadcast("Hi everyone!") -> Alice and Bob see this

Time 0:20 - Bob sends "Hello Alice!"
  Bob's socket.hasData() returns true
  message = "Hello Alice!"
  broadcast("Hello Alice!") -> Alice and Bob see this

Time 0:30 - Alice sends "/quit"
  Alice's socket.hasData() returns true
  message = "/quit"
  clients = [Bob] (Alice removed)
  broadcast("*** A user has left ***") -> Bob sees this
  Alice's socket closed
```

### Chat Client

```rust
module ChatClient;

bind Viper.Network;
bind Viper.Threading;
bind Viper.Terminal;
bind Viper.Time;

entity ChatClient {
    hide socket: TcpSocket;
    hide username: String;
    hide running: Boolean;

    expose func init(host: String, port: Integer, username: String) {
        self.username = username;
        self.running = true;

        // Connect to the server
        self.socket = TcpSocket.connect(host, port);

        if self.socket == null {
            Terminal.Say("Could not connect to server at " + host + ":" + port);
            self.running = false;
            return;
        }

        Terminal.Say("Connected to chat server!");
        Terminal.Say("Type messages and press Enter. Type /quit to exit.");
        Terminal.Say("");
    }

    func run() {
        if !self.running {
            return;
        }

        // Start a separate thread to receive messages
        // This lets us receive while also waiting for user input
        var receiver = Thread.spawn(self.receiveLoop);

        // Main thread handles sending messages
        while self.running {
            // Wait for user to type something
            var input = Terminal.Ask("");

            if input == "/quit" {
                self.running = false;
                self.socket.write("/quit\n");
                break;
            }

            // Send the message (prefixed with username)
            self.socket.write(self.username + ": " + input + "\n");
        }

        // Wait for the receiver thread to finish
        receiver.join();
        self.socket.close();
        Terminal.Say("Disconnected from server.");
    }

    // This runs in a separate thread
    func receiveLoop() {
        while self.running {
            // Check if server sent anything
            if self.socket.hasData() {
                var message = self.socket.readLine();

                if message == null {
                    // Server closed connection
                    Terminal.Say("*** Disconnected from server ***");
                    self.running = false;
                    break;
                }

                // Display the message
                Terminal.Say(message);
            }

            // Small sleep to avoid consuming CPU
            Time.Clock.Sleep(10);
        }
    }
}

func start() {
    var username = Terminal.Ask("Enter your username: ");
    var client = ChatClient("localhost", 9000, username);
    client.run();
}
```

The client uses two threads --- one for sending and one for receiving. This is necessary because:

1. **Reading blocks**: `readLine()` waits until data arrives. If we used one thread, we couldn't type while waiting for messages.
2. **Input blocks**: `Ask()` waits for user input. If we used one thread, we couldn't receive messages while typing.

```
Main Thread                     Receiver Thread
    |                               |
    |   Terminal.Ask()              |   socket.hasData()?
    |   (waiting for input...)      |   socket.readLine()
    |                               |   Terminal.Say()
    |                               |
    v                               v
User types "Hello"              Server sends "Bob: Hi"
    |                               |
    |   socket.write()              |   Terminal.Say("Bob: Hi")
    |                               |
```

---

## UDP: Fast but Unreliable

UDP trades reliability for speed. When you can tolerate lost packets or have your own way of handling them, UDP's lower overhead and faster delivery are attractive.

### Basic UDP Communication

```rust
bind Viper.Network;
bind Viper.Terminal;

// UDP sender
func sendUdpMessage() {
    // Create a UDP socket
    var socket = UdpSocket.create();

    // Send data - no connection needed!
    // Just specify destination address and port
    socket.send("Hello!", "192.168.1.100", 5000);

    // Can send to different destinations with same socket
    socket.send("Hi there!", "192.168.1.101", 5000);

    socket.close();
}

// UDP receiver
func receiveUdpMessages() {
    // Bind to port 5000 - we'll receive anything sent here
    var socket = UdpSocket.bind(5000);
    Terminal.Say("Listening for UDP messages on port 5000...");

    while true {
        // Wait for and receive a packet
        var packet = socket.receive();

        // Packet contains data and sender info
        Terminal.Say("From " + packet.address + ":" + packet.port);
        Terminal.Say("Message: " + packet.data);
    }
}
```

Notice the differences from TCP:
- No `connect()` or `accept()` --- just send to an address
- No persistent connection --- each packet is independent
- Receiver doesn't know who will send, just waits for any packet

### Game Networking with UDP

Games often need to send player state many times per second. Lost packets don't matter because new state arrives constantly. Here's a typical pattern:

```rust
bind Viper.Network;
bind Viper.Time;
bind Viper.Convert as Convert;

// Player state that we'll send frequently
value PlayerState {
    id: Integer;
    x: Number;
    y: Number;
    rotation: Number;
    health: Integer;
    timestamp: Integer;  // When this state was captured
}

entity GameNetwork {
    hide socket: UdpSocket;
    hide serverAddress: String;
    hide serverPort: Integer;

    expose func init(serverAddress: String, serverPort: Integer) {
        self.socket = UdpSocket.create();
        self.serverAddress = serverAddress;
        self.serverPort = serverPort;
    }

    // Send our current state to the server
    // Called 60 times per second
    func sendState(state: PlayerState) {
        var data = packPlayerState(state);
        self.socket.send(data, self.serverAddress, self.serverPort);
        // Note: we don't wait for acknowledgment!
        // If this packet is lost, we'll send another in 16ms
    }

    // Receive states of other players from the server
    func receiveStates() -> [PlayerState] {
        var states: [PlayerState] = [];

        // Process all available packets (non-blocking)
        while self.socket.hasData() {
            var packet = self.socket.receive();
            var state = unpackPlayerState(packet.data);

            // Only use recent states - discard old ones
            var now = Time.Clock.Ticks();
            if now - state.timestamp < 1000 {  // Less than 1 second old
                states.push(state);
            }
        }

        return states;
    }
}

// Convert state to transmittable format
func packPlayerState(state: PlayerState) -> String {
    // Simple text format (real games use compact binary)
    return state.id + "," + state.x + "," + state.y + "," +
           state.rotation + "," + state.health + "," + state.timestamp;
}

// Convert received data back to state
func unpackPlayerState(data: String) -> PlayerState {
    var parts = data.Split(",");
    return PlayerState {
        id: Convert.ToInt64(parts.Get(0)),
        x: Convert.ToDouble(parts.Get(1)),
        y: Convert.ToDouble(parts.Get(2)),
        rotation: Convert.ToDouble(parts.Get(3)),
        health: Convert.ToInt64(parts.Get(4)),
        timestamp: Convert.ToInt64(parts.Get(5))
    };
}
```

The timestamp field is crucial. If packet A (timestamp 100) arrives after packet B (timestamp 150), we should ignore A --- it's outdated information. TCP would give us packets in order, but with UDP, older packets might arrive late.

---

## WebSockets: Real-Time Web

HTTP was designed for request-response: the client asks, the server answers, done. But what if the server needs to send data to the client without being asked? What if you want continuous, two-way communication?

**WebSockets** provide full-duplex communication over a single connection. After an initial HTTP handshake, the connection stays open and either side can send messages at any time.

```rust
bind Viper.Network;
bind Viper.Terminal;

entity WebSocketClient {
    hide ws: WebSocket;
    hide connected: Boolean;

    func connect(url: String) {
        self.connected = false;

        // Initiate WebSocket connection
        self.ws = WebSocket.connect(url);

        // Set up event handlers
        // These functions will be called when events occur

        self.ws.onOpen(func() {
            Terminal.Say("Connected to server!");
            self.connected = true;
            // Now we can send messages
            self.ws.send("Hello server, I'm online!");
        });

        self.ws.onMessage(func(message: String) {
            // Server sent us something
            Terminal.Say("Server: " + message);
        });

        self.ws.onClose(func() {
            Terminal.Say("Connection closed");
            self.connected = false;
        });

        self.ws.onError(func(error: String) {
            Terminal.Say("Error: " + error);
        });
    }

    func send(message: String) {
        if self.connected {
            self.ws.send(message);
        } else {
            Terminal.Say("Not connected!");
        }
    }

    func close() {
        self.ws.close();
    }
}

func start() {
    var client = WebSocketClient();
    client.connect("wss://chat.example.com/room/general");

    // Now we can send messages anytime, and receive them anytime
    while true {
        var input = Terminal.Ask("");
        if input == "/quit" {
            client.close();
            break;
        }
        client.send(input);
    }
}
```

WebSockets are ideal for:
- Real-time dashboards that update continuously
- Live chat applications
- Collaborative editing (Google Docs style)
- Live sports scores, stock tickers
- Multiplayer web games

---

## Handling Network Errors

Networks are inherently unreliable. Connections drop. Servers go down. Packets get lost. WiFi cuts out. Your programs must handle these failures gracefully.

### The Many Ways Networks Fail

```rust
bind Viper.Network;
bind Viper.Terminal;

func demonstrateFailures() {
    // Failure 1: Cannot connect
    // Server might be down, address might be wrong
    var socket = TcpSocket.connect("nonexistent.example.com", 80);
    if socket == null {
        Terminal.Say("Could not connect - server unreachable");
    }

    // Failure 2: Connection drops mid-conversation
    // WiFi cuts out, server crashes, network cable unplugged
    var response = socket.readLine();
    if response == null {
        Terminal.Say("Connection lost while reading");
    }

    // Failure 3: Timeout - server too slow
    // Server overloaded, network congested
    // Without timeout, we might wait forever

    // Failure 4: Server returns error
    // We connected and communicated, but server said "no"
    var httpResponse = Http.get("https://api.example.com/resource");
    if httpResponse.statusCode == 404 {
        Terminal.Say("Resource not found");
    } else if httpResponse.statusCode == 500 {
        Terminal.Say("Server error");
    }
}
```

### Robust Networking with Retries

For important operations, implement retry logic with exponential backoff:

```rust
bind Viper.Network;
bind Viper.Terminal;
bind Viper.Time;

func robustFetch(url: String, maxRetries: Integer) -> String? {
    var retries = 0;

    while retries < maxRetries {
        try {
            // Set a timeout so we don't wait forever
            var response = Http.get(url, { timeout: 5000 });

            if response.ok {
                return response.body;  // Success!
            }

            if response.statusCode >= 500 {
                // Server error (5xx) - worth retrying
                // The server might recover
                retries += 1;
                Terminal.Say("Server error " + response.statusCode +
                             ", retrying... (" + retries + "/" + maxRetries + ")");

                // Exponential backoff: wait longer each retry
                // 1st retry: 1 second, 2nd: 2 seconds, 3rd: 4 seconds
                var waitTime = 1000 * (1 << retries);  // 2^retries * 1000ms
                Time.Clock.Sleep(waitTime);
                continue;
            }

            // Client error (4xx) - don't retry
            // Our request is wrong, retrying won't help
            Terminal.Say("Client error " + response.statusCode + " - not retrying");
            return null;

        } catch NetworkError as e {
            retries += 1;
            Terminal.Say("Network error: " + e.message +
                         ", retrying... (" + retries + "/" + maxRetries + ")");
            var waitTime = 1000 * (1 << retries);
            Time.Clock.Sleep(waitTime);
        }
    }

    Terminal.Say("Failed after " + maxRetries + " retries");
    return null;
}

func start() {
    var data = robustFetch("https://api.example.com/important-data", 5);

    if data != null {
        Terminal.Say("Got data: " + data);
    } else {
        Terminal.Say("Could not fetch data");
    }
}
```

### Why Exponential Backoff?

If a server is overloaded, hammering it with rapid retry attempts makes things worse. Exponential backoff (waiting longer after each failure) gives the server time to recover:

```
Retry 1: Wait 1 second
Retry 2: Wait 2 seconds
Retry 3: Wait 4 seconds
Retry 4: Wait 8 seconds
Retry 5: Wait 16 seconds
```

This pattern is used throughout the internet. It's polite and effective.

### Timeouts: Don't Wait Forever

Always set timeouts:

```rust
// Without timeout - could hang forever if server doesn't respond
var response = Http.get(url);  // Dangerous!

// With timeout - give up after 5 seconds
var response = Http.get(url, { timeout: 5000 });

// For sockets, set timeouts explicitly
var socket = TcpSocket.connect(host, port, { timeout: 3000 });
socket.setReadTimeout(10000);   // 10 seconds to read
socket.setWriteTimeout(5000);   // 5 seconds to write
```

How long should a timeout be? Long enough for normal operations, short enough to catch real problems. Common values:
- Connect timeout: 3-10 seconds
- Read timeout: 10-30 seconds
- Total request timeout: 30-60 seconds

---

## Common Mistakes in Network Programming

Network programming has unique pitfalls. Here are the mistakes beginners make most often, and how to avoid them.

### Mistake 1: Forgetting to Close Connections

Every open connection uses system resources. Leaking connections will eventually crash your program or the system.

```rust
// BAD: Connection leak!
func fetchData(host: String, port: Integer) -> String {
    var socket = TcpSocket.connect(host, port);
    var data = socket.readAll();
    return data;  // Socket never closed!
}
// If called 1000 times, you have 1000 open connections

// GOOD: Always close
func fetchData(host: String, port: Integer) -> String {
    var socket = TcpSocket.connect(host, port);
    var data = socket.readAll();
    socket.close();  // Clean up!
    return data;
}

// EVEN BETTER: Handle errors too
func fetchData(host: String, port: Integer) -> String? {
    var socket = TcpSocket.connect(host, port);

    if socket == null {
        return null;
    }

    try {
        var data = socket.readAll();
        return data;
    } finally {
        // 'finally' runs whether try succeeded or failed
        socket.close();
    }
}
```

### Mistake 2: Blocking the Main Thread

Network operations can take seconds. If your main thread waits for network responses, your program freezes.

```rust
// BAD: Freezes entire program during fetch
func onButtonClick() {
    var data = Http.get(slowUrl);  // User can't click anything for 10 seconds!
    updateDisplay(data);
}

// GOOD: Use a separate thread
func onButtonClick() {
    Thread.spawn(func() {
        var data = Http.get(slowUrl);

        // Update UI from main thread
        runOnMainThread(func() {
            updateDisplay(data);
        });
    });
}
// Button click returns immediately, fetch happens in background
```

This is especially important in GUI applications and games. A frozen UI is a terrible user experience.

### Mistake 3: Ignoring Partial Reads

When you read from a socket, you might not get all the data at once. Networks deliver data in chunks.

```rust
// BAD: Assumes all data comes at once
var message = socket.read(1024);  // Might get less than 1024 bytes!

// GOOD: Read until you have what you need
func readExactly(socket: TcpSocket, count: Integer) -> String {
    var result = "";

    while result.length < count {
        var chunk = socket.read(count - result.length);
        if chunk == null {
            // Connection closed before we got all data
            return null;
        }
        result += chunk;
    }

    return result;
}
```

### Mistake 4: Not Validating Input from the Network

Data from the network is untrusted. It might be malformed, malicious, or just wrong.

```rust
// BAD: Trusts network data blindly
var packet = socket.receive();
var index = packet.data.toInt();
myArray[index] = value;  // What if index is negative? Or huge?

// GOOD: Validate everything
var packet = socket.receive();

if packet.data.length > 100 {
    Terminal.Say("Packet too large, ignoring");
    return;
}

var index = packet.data.toInt();

if index < 0 || index >= myArray.length {
    Terminal.Say("Invalid index received, ignoring");
    return;
}

myArray[index] = value;
```

### Mistake 5: Sending/Receiving Without a Protocol

Both sides need to agree on message format. Without a clear protocol, you get gibberish.

```rust
// BAD: No clear message format
socket.write("Alice");
socket.write("25");
socket.write("Hello");
// Receiver gets "Alice25Hello" - how to separate?

// GOOD: Define a clear protocol
// Option 1: Newline-delimited
socket.write("Alice\n");
socket.write("25\n");
socket.write("Hello\n");

// Option 2: Length-prefixed
func sendMessage(socket: TcpSocket, message: String) {
    var length = message.length;
    socket.write(length + ":" + message);  // "5:Hello"
}

// Option 3: Use a standard format like JSON
socket.write('{"name":"Alice","age":25,"message":"Hello"}\n');
```

### Mistake 6: Not Handling Connection Resets

Servers restart. Connections break. Your code needs to detect this and recover.

```rust
// BAD: Assumes connection lasts forever
while true {
    var message = socket.readLine();
    process(message);
}
// If socket breaks, readLine returns null, and process(null) crashes

// GOOD: Detect disconnection and reconnect
bind Viper.Terminal;
bind Viper.Time;

func reliableConnection(host: String, port: Integer) {
    var socket: TcpSocket? = null;

    while true {
        // Connect if needed
        if socket == null {
            Terminal.Say("Connecting...");
            socket = TcpSocket.connect(host, port);

            if socket == null {
                Terminal.Say("Connection failed, retrying in 5 seconds");
                Time.Clock.Sleep(5000);
                continue;
            }

            Terminal.Say("Connected!");
        }

        // Try to read
        var message = socket.readLine();

        if message == null {
            // Connection broke
            Terminal.Say("Disconnected!");
            socket.close();
            socket = null;
            continue;  // Loop will reconnect
        }

        process(message);
    }
}
```

---

## Security Considerations

Network programming introduces security risks that don't exist in standalone programs. Data travels through untrusted networks where it can be observed or modified.

### Use HTTPS, Not HTTP

HTTP sends data in plain text. Anyone on the network path can read it. HTTPS encrypts the connection.

```rust
// BAD: Password sent in plain text!
Http.post("http://example.com/login", {
    body: '{"username": "alice", "password": "secret123"}'
});
// Anyone on the network can see "secret123"

// GOOD: Encrypted connection
Http.post("https://example.com/login", {
    body: '{"username": "alice", "password": "secret123"}'
});
// Data is encrypted, observers see gibberish
```

Always use `https://` for anything sensitive: logins, personal data, financial information.

### Validate Server Certificates

HTTPS uses certificates to prove the server is who it claims to be. Don't disable certificate validation!

```rust
// DANGEROUS: Disables security
Http.get(url, { verifyCertificate: false });
// You might be talking to an attacker pretending to be the server

// SAFE: Always verify (this is the default)
Http.get(url);  // Certificate verified automatically
```

### Don't Trust Network Input

Anything received from the network should be treated as potentially malicious.

```rust
// User sends a filename they want to download
var filename = socket.readLine();

// BAD: Might download any file!
var contents = File.read("/data/" + filename);
// If filename is "../../../etc/passwd", you're exposing system files

// GOOD: Validate and sanitize
var filename = socket.readLine();

// Check for path traversal attacks
if filename.contains("..") || filename.contains("/") || filename.contains("\\") {
    socket.write("Invalid filename\n");
    return;
}

// Only allow certain file extensions
if !filename.endsWith(".txt") && !filename.endsWith(".json") {
    socket.write("Invalid file type\n");
    return;
}

var contents = File.read("/data/" + filename);
```

### Rate Limiting

Without limits, attackers can flood your server with requests.

```rust
bind Viper.Time;

entity RateLimitedServer {
    // Track requests per IP address
    hide requestCounts: Map<String, Integer>;
    hide lastReset: Integer;
    hide maxRequestsPerMinute: Integer;

    expose func init() {
        self.requestCounts = Map.new();
        self.lastReset = Time.Clock.Ticks();
        self.maxRequestsPerMinute = 100;
    }

    func handleRequest(client: TcpSocket) {
        var ip = client.remoteAddress();
        var now = Time.Clock.Ticks();

        // Reset counts every minute
        if now - self.lastReset > 60000 {
            self.requestCounts = Map.new();
            self.lastReset = now;
        }

        // Check rate limit
        var count = self.requestCounts.get(ip, 0);

        if count >= self.maxRequestsPerMinute {
            client.write("Rate limit exceeded. Please slow down.\n");
            client.close();
            return;
        }

        // Record this request
        self.requestCounts.set(ip, count + 1);

        // Process normally
        processRequest(client);
    }
}
```

### Keep Secrets Out of Code

Never embed passwords, API keys, or tokens in your source code.

```rust
// BAD: API key in source code
var response = Http.get("https://api.example.com/data?key=sk_live_abc123xyz");
// If someone sees your code, they have your key

// GOOD: Load from environment or config
var apiKey = Viper.Environment.get("API_KEY");
var response = Http.get("https://api.example.com/data?key=" + apiKey);
```

---

## Debugging Network Issues

Network bugs are notoriously hard to track down. The problem might be in your code, in the network, or in the server you're talking to. Here's how to investigate.

### Print Everything

When network code doesn't work, add logging at every step:

```rust
bind Viper.Terminal;

func debugFetch(url: String) -> String? {
    Terminal.Say("[DEBUG] Starting fetch of: " + url);

    try {
        Terminal.Say("[DEBUG] Making HTTP request...");
        var response = Http.get(url, { timeout: 5000 });

        Terminal.Say("[DEBUG] Response status: " + response.statusCode);
        Terminal.Say("[DEBUG] Response headers: " + response.headers);
        Terminal.Say("[DEBUG] Response body length: " + response.body.length);
        Terminal.Say("[DEBUG] Response body (first 200 chars): " +
                     response.body.substring(0, 200));

        if response.ok {
            Terminal.Say("[DEBUG] Success!");
            return response.body;
        } else {
            Terminal.Say("[DEBUG] Request failed with status " + response.statusCode);
            return null;
        }

    } catch NetworkError as e {
        Terminal.Say("[DEBUG] Network error: " + e.message);
        Terminal.Say("[DEBUG] Error type: " + e.type);
        return null;
    }
}
```

### Test Each Layer Separately

Network communication involves multiple layers. Test each one:

1. **Can you reach the host at all?**
```rust
bind Viper.Terminal;

var socket = TcpSocket.connect(host, port, { timeout: 3000 });
if socket == null {
    Terminal.Say("Cannot connect to " + host + ":" + port);
    Terminal.Say("Check: Is the server running? Is the address correct?");
    Terminal.Say("Check: Firewall blocking? Network connected?");
}
```

2. **Can you send data?**
```rust
bind Viper.Terminal;

socket.write("test\n");
Terminal.Say("Data sent successfully");
// If this fails, connection might have dropped
```

3. **Can you receive data?**
```rust
bind Viper.Terminal;

var response = socket.readLine();
if response == null {
    Terminal.Say("No response from server");
    Terminal.Say("Check: Is server expecting different input format?");
}
```

### Use Simple Test Tools

Before debugging your code, verify the server works with known-good tools:

```rust
// If Http.get("https://api.example.com") fails...

// Step 1: Can you reach the server at all?
// Use ping or a web browser

// Step 2: Is it your code or the server?
// Try the same URL in a browser
// If browser works but your code doesn't, problem is in your code

// Step 3: Are you sending what you think you're sending?
// Print the exact URL, headers, and body before sending
Terminal.Say("URL: " + url);
Terminal.Say("Headers: " + headers);
Terminal.Say("Body: " + body);
```

### Common Network Error Messages and Their Meanings

| Error | Likely Cause |
|-------|--------------|
| "Connection refused" | Server not running, or wrong port |
| "Connection timed out" | Server unreachable, firewall blocking |
| "Host not found" | DNS failure, hostname wrong |
| "Connection reset" | Server crashed or closed forcefully |
| "Broken pipe" | Tried to write to closed connection |
| "Network unreachable" | No route to destination, no internet |
| "Address already in use" | Port already used by another program |

### The "Works on My Machine" Problem

Network code often works locally but fails when deployed. Common causes:

1. **Firewall differences**: Your machine allows connections that the server blocks
2. **DNS differences**: Your machine resolves names differently
3. **Timing differences**: Network latency varies dramatically
4. **Load differences**: Server handles 1 request fine, fails at 1000

Always test with realistic network conditions before deploying.

---

## A Complete Example: Weather Dashboard

Let's tie everything together with a complete, well-structured networking application:

```rust
module WeatherDashboard;

bind Viper.Network;
bind Viper.JSON;
bind Viper.Time;
bind Viper.Terminal;

// Data type for weather information
value CityWeather {
    city: String;
    temperature: Number;
    conditions: String;
    humidity: Number;
    windSpeed: Number;
    lastUpdated: Integer;
}

// Service entity that handles weather API calls
entity WeatherService {
    hide apiKey: String;
    hide baseUrl: String;
    hide cache: Map<String, CityWeather>;
    hide cacheTimeout: Integer;

    expose func init(apiKey: String) {
        self.apiKey = apiKey;
        self.baseUrl = "https://api.weather.example.com/v1";
        self.cache = Map.new();
        self.cacheTimeout = 300000;  // 5 minutes
    }

    // Fetch weather for a city, using cache when possible
    func getWeather(city: String) -> CityWeather? {
        // Check cache first
        if self.cache.has(city) {
            var cached = self.cache.get(city);
            var age = Time.Clock.Ticks() - cached.lastUpdated;

            if age < self.cacheTimeout {
                Terminal.Say("(Using cached data for " + city + ")");
                return cached;
            }
        }

        // Fetch from API
        var url = self.baseUrl + "/current?city=" +
                  Network.urlEncode(city) + "&key=" + self.apiKey;

        try {
            var response = Http.get(url, { timeout: 10000 });

            if !response.ok {
                Terminal.Say("API error for " + city + ": " + response.statusCode);

                // If we have stale cache data, use it rather than nothing
                if self.cache.has(city) {
                    Terminal.Say("(Using stale cached data)");
                    return self.cache.get(city);
                }

                return null;
            }

            var data = JSON.parse(response.body);

            var weather = CityWeather {
                city: city,
                temperature: data["main"]["temp"].asFloat(),
                conditions: data["weather"][0]["description"].asString(),
                humidity: data["main"]["humidity"].asFloat(),
                windSpeed: data["wind"]["speed"].asFloat(),
                lastUpdated: Time.Clock.Ticks()
            };

            // Update cache
            self.cache.set(city, weather);

            return weather;

        } catch NetworkError as e {
            Terminal.Say("Network error for " + city + ": " + e.message);

            // Return stale cache if available
            if self.cache.has(city) {
                Terminal.Say("(Using stale cached data)");
                return self.cache.get(city);
            }

            return null;

        } catch JSONError as e {
            Terminal.Say("Parse error for " + city + ": " + e.message);
            return null;
        }
    }
}

// Display functions
func displayWeather(weather: CityWeather) {
    Terminal.Say("");
    Terminal.Say("+----------------------------------+");
    Terminal.Say("|  " + padRight(weather.city, 32) + "|");
    Terminal.Say("+----------------------------------+");
    Terminal.Say("|  Temperature: " + padRight(weather.temperature + "F", 17) + "|");
    Terminal.Say("|  Conditions:  " + padRight(weather.conditions, 17) + "|");
    Terminal.Say("|  Humidity:    " + padRight(weather.humidity + "%", 17) + "|");
    Terminal.Say("|  Wind:        " + padRight(weather.windSpeed + " mph", 17) + "|");
    Terminal.Say("+----------------------------------+");
}

func padRight(s: String, width: Integer) -> String {
    while s.length < width {
        s = s + " ";
    }
    return s;
}

func displayHeader() {
    Terminal.Say("========================================");
    Terminal.Say("         WEATHER DASHBOARD              ");
    Terminal.Say("========================================");
}

// Main program
func start() {
    // In a real app, load this from environment
    var service = WeatherService("your-api-key-here");

    var cities = ["Seattle", "New York", "London", "Tokyo", "Sydney"];

    displayHeader();

    // Fetch weather for all cities
    for city in cities {
        var weather = service.getWeather(city);

        if weather != null {
            displayWeather(weather);
        } else {
            Terminal.Say("");
            Terminal.Say("Could not fetch weather for " + city);
        }
    }

    Terminal.Say("");
    Terminal.Say("Dashboard complete. Data cached for 5 minutes.");
}
```

This example demonstrates:
- HTTP requests with error handling
- JSON parsing
- Caching for efficiency
- Graceful degradation (use stale cache when fresh data unavailable)
- Timeouts
- Clean separation between data fetching and display

---

## The Two Languages

**Zia**
```rust
bind Viper.Network;
bind Viper.Terminal;

// HTTP request
var response = Http.get("https://api.example.com/data");
if response.ok {
    Terminal.Say(response.body);
}

// TCP client
var socket = TcpSocket.connect("example.com", 80);
socket.write("Hello\n");
var reply = socket.readLine();
socket.close();

// UDP
var udp = UdpSocket.create();
udp.send("Hello", "192.168.1.100", 5000);
udp.close();
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

DIM udp AS UdpSocket
udp = UDP_CREATE()
UDP_SEND udp, "Hello", "192.168.1.100", 5000
UDP_CLOSE udp
```

---

## Summary

Networking transforms programs from isolated calculators into connected systems. Here's what we covered:

- **The postal analogy**: Network communication is like sending letters --- addresses, routes, delivery guarantees
- **IP addresses and ports**: Addresses identify computers, ports identify programs on those computers
- **TCP vs UDP**: TCP is reliable and ordered (like registered mail), UDP is fast and unreliable (like postcards)
- **Client-server architecture**: Servers wait for and respond to clients
- **HTTP**: The protocol of the web, with methods like GET, POST, PUT, DELETE
- **TCP sockets**: Raw connections for custom protocols
- **UDP sockets**: Fast, connectionless communication for games and real-time data
- **WebSockets**: Real-time bidirectional web communication
- **Error handling**: Networks fail in many ways --- always handle errors
- **Security**: Use HTTPS, validate input, don't trust the network
- **Debugging**: Log everything, test each layer, understand error messages

The ability to make programs communicate opens vast possibilities. Web services, multiplayer games, chat applications, IoT devices, distributed systems --- all become possible when your programs can talk to the world.

---

## Exercises

**Exercise 22.1 (Mimic)**: Modify the weather dashboard to display temperature in Celsius instead of Fahrenheit. Add a helper function to convert between them.

**Exercise 22.2 (Mimic)**: Write a program that fetches a web page and counts how many times a specific word appears. The program should ask the user for the URL and the word to search for.

**Exercise 22.3 (Extend)**: Create a simple HTTP client that can download files and save them locally. Handle large files by reading in chunks rather than all at once.

**Exercise 22.4 (Extend)**: Build an echo server: whatever a client sends, the server sends back. Then modify it to reverse the text before echoing.

**Exercise 22.5 (Create)**: Create a "quote of the day" server. It should maintain a list of quotes and send a random one to each client that connects. Bonus: let administrators add new quotes via a special command.

**Exercise 22.6 (Create)**: Build a simple port scanner. Given a host and a range of ports (e.g., 1-1000), try to connect to each port and report which ones are open (connection succeeds) and which are closed (connection fails).

**Exercise 22.7 (Create)**: Implement a UDP-based "ping pong" game for two players. Each player has a paddle they can move up and down, and a ball bounces between them. Player positions and ball position are sent via UDP.

**Exercise 22.8 (Challenge)**: Create a multiplayer tic-tac-toe game. One player hosts (acts as server), another connects as client. They take turns, and both see the board update in real-time.

**Exercise 22.9 (Challenge)**: Build a simple HTTP server that serves files from a directory. When a client requests `/file.txt`, read `file.txt` from the local directory and return it. Handle missing files with a 404 response. Be careful about security --- don't allow `../` in paths!

**Exercise 22.10 (Challenge)**: Implement a reliable message protocol on top of UDP. Each message should have a sequence number, and the receiver should send acknowledgments. The sender should retransmit messages that aren't acknowledged within a timeout. This is essentially implementing a simplified version of TCP!

---

*We can communicate over networks. But what format should our data take? Next, we explore data formats: JSON, XML, and binary protocols --- the languages that let different programs understand each other.*

*[Continue to Chapter 23: Data Formats](23-data-formats.md)*
