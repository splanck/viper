# Network

> TCP and UDP networking with HTTP/HTTPS client and DNS resolution support.

**Part of the [Viper Runtime Library](README.md)**

## Contents

- [Viper.Network.Tcp](#vipernetworktcp)
- [Viper.Network.TcpServer](#vipernetworktcpserver)
- [Viper.Network.Udp](#vipernetworkudp)
- [Viper.Network.Dns](#vipernetworkdns)
- [Viper.Network.Http](#vipernetworkhttp)
- [Viper.Network.HttpReq](#vipernetworkhttpreq)
- [Viper.Network.HttpRes](#vipernetworkhttpres)
- [Viper.Network.Url](#vipernetworkurl)
- [Viper.Network.WebSocket](#vipernetworkwebsocket)
- [Viper.Network.RestClient](#vipernetworkrestclient)
- [Viper.Network.RetryPolicy](#vipernetworkretrypolicy)
- [Viper.Network.RateLimiter](#vipernetworkratelimiter)

---

## Viper.Network.Tcp

TCP client connection for sending and receiving data over a network.

**Type:** Instance class

**Constructors:**

- `Viper.Network.Tcp.Connect(host, port)` - Connect to a remote host
- `Viper.Network.Tcp.ConnectFor(host, port, timeoutMs)` - Connect with a timeout

### Properties

| Property    | Type    | Description                                  |
|-------------|---------|----------------------------------------------|
| `Available` | Integer | Bytes available to read without blocking     |
| `Host`      | String  | Remote host name or IP address (read-only)   |
| `IsOpen`    | Boolean | True if connection is open (read-only)       |
| `LocalPort` | Integer | Local port number (read-only)                |
| `Port`      | Integer | Remote port number (read-only)               |

### Send Methods

| Method                | Returns | Description                                    |
|-----------------------|---------|------------------------------------------------|
| `Send(data)`          | Integer | Send Bytes, return number of bytes sent        |
| `SendAll(data)`       | void    | Send all bytes, block until complete           |
| `SendStr(text)`       | Integer | Send string as UTF-8 bytes, return bytes sent  |

### Receive Methods

| Method             | Returns | Description                                          |
|--------------------|---------|------------------------------------------------------|
| `Recv(maxBytes)`   | Bytes   | Receive up to maxBytes (may return fewer)            |
| `RecvExact(count)` | Bytes   | Receive exactly count bytes, block until complete    |
| `RecvLine()`       | String  | Receive until newline (LF or CRLF), strip newline    |
| `RecvStr(maxBytes)`| String  | Receive up to maxBytes as UTF-8 string               |

### Timeout and Close Methods

| Method                  | Returns | Description                               |
|-------------------------|---------|-------------------------------------------|
| `Close()`               | void    | Close the connection                      |
| `SetRecvTimeout(ms)`    | void    | Set receive timeout (0 = no timeout)      |
| `SetSendTimeout(ms)`    | void    | Set send timeout (0 = no timeout)         |

### Connection Options

The implementation enables the following socket options by default:

- **TCP_NODELAY:** Disables Nagle's algorithm for low-latency communication

### Zia Example

> Tcp requires an active network connection and is typically used in interactive programs. Use `bind Viper.Network.Tcp as Tcp;` and call `Tcp.Connect(host, port)`. Properties use get_ pattern: `conn.get_Host()`, `conn.get_IsOpen()`.

### BASIC Example

```basic
' Connect to a server
DIM conn AS OBJECT = Viper.Network.Tcp.Connect("example.com", 80)

' Check connection
IF conn.IsOpen THEN
    PRINT "Connected to "; conn.Host; ":"; conn.Port
    PRINT "Local port: "; conn.LocalPort
END IF

' Send HTTP request
DIM request AS STRING = "GET / HTTP/1.1" + CHR(13) + CHR(10) + _
                        "Host: example.com" + CHR(13) + CHR(10) + _
                        CHR(13) + CHR(10)
conn.SendStr(request)

' Receive response
DIM response AS STRING = conn.RecvStr(4096)
PRINT response

' Close connection
conn.Close()
```

### Line-Based Protocol Example

```basic
' Connect to a line-based server (e.g., SMTP, POP3)
DIM conn AS OBJECT = Viper.Network.Tcp.Connect("mail.example.com", 25)

' Read greeting
DIM greeting AS STRING = conn.RecvLine()
PRINT "Server: "; greeting

' Send HELO command
conn.SendStr("HELO localhost" + CHR(13) + CHR(10))

' Read response
DIM response AS STRING = conn.RecvLine()
PRINT "Response: "; response

conn.Close()
```

### Binary Data Example

```basic
' Connect to a binary protocol server
DIM conn AS OBJECT = Viper.Network.Tcp.Connect("192.168.1.100", 9000)

' Send binary packet
DIM packet AS OBJECT = Viper.Collections.Bytes.New(8)
packet.Set(0, &H01)  ' Message type
packet.Set(1, &H00)  ' Flags
packet.Set(2, &H00)  ' Length high
packet.Set(3, &H04)  ' Length low
packet.Set(4, &H48)  ' 'H'
packet.Set(5, &H45)  ' 'E'
packet.Set(6, &H4C)  ' 'L'
packet.Set(7, &H4F)  ' 'O'

conn.SendAll(packet)

' Receive response header (exactly 4 bytes)
DIM header AS OBJECT = conn.RecvExact(4)
DIM payloadLen AS INTEGER = header.Get(2) * 256 + header.Get(3)

' Receive payload
DIM payload AS OBJECT = conn.RecvExact(payloadLen)

conn.Close()
```

### Timeout Example

```basic
' Connect with timeout
DIM conn AS OBJECT = Viper.Network.Tcp.ConnectFor("slow-server.com", 8080, 5000)

' Set receive timeout
conn.SetRecvTimeout(3000)  ' 3 seconds

' Receive will trap if no data within 3 seconds
DIM data AS OBJECT = conn.Recv(1024)

conn.Close()
```

### Error Handling

Connection operations trap on errors:

- `Connect()` traps on connection refused, host not found, or network error
- `ConnectFor()` traps on timeout in addition to connection errors
- `Send()`/`SendAll()` trap if connection is closed
- `Recv()`/`RecvExact()` trap on receive errors
- `RecvLine()` traps if connection closes before newline

To handle potential connection failures gracefully, ensure the target host is reachable before connecting.

### Use Cases

- **HTTP clients:** Make simple HTTP requests
- **Line protocols:** Interact with SMTP, POP3, IMAP, FTP control
- **Binary protocols:** Communicate with custom binary servers
- **Game networking:** Real-time game client connections
- **IoT communication:** Connect to network devices

---

## Viper.Network.TcpServer

TCP server for accepting incoming client connections.

**Type:** Instance class

**Constructors:**

- `Viper.Network.TcpServer.Listen(port)` - Listen on all interfaces
- `Viper.Network.TcpServer.ListenAt(address, port)` - Listen on specific interface

### Properties

| Property      | Type    | Description                              |
|---------------|---------|------------------------------------------|
| `Address`     | String  | Bound address (read-only)                |
| `IsListening` | Boolean | True if actively listening (read-only)   |
| `Port`        | Integer | Listening port number (read-only)        |

### Methods

| Method             | Returns | Description                                      |
|--------------------|---------|--------------------------------------------------|
| `Accept()`         | Tcp     | Accept connection, block until client connects   |
| `AcceptFor(ms)`    | Tcp     | Accept with timeout, returns null on timeout     |
| `Close()`          | void    | Stop listening and close the server              |

### Zia Example

> TcpServer is accessible via `bind Viper.Network.TcpServer as TcpServer;`. Call `TcpServer.Listen(port)` or `TcpServer.ListenAt(addr, port)`. Properties use get_ pattern.

### BASIC Example

```basic
' Start a simple echo server on port 8080
DIM server AS OBJECT = Viper.Network.TcpServer.Listen(8080)

PRINT "Listening on port "; server.Port

' Accept one connection
DIM client AS OBJECT = server.Accept()
PRINT "Client connected from "; client.Host; ":"; client.Port

' Echo loop
DO WHILE client.IsOpen
    DIM data AS OBJECT = client.Recv(1024)
    IF client.Available = 0 AND data.Len = 0 THEN
        EXIT DO  ' Connection closed
    END IF
    client.SendAll(data)  ' Echo back
LOOP

PRINT "Client disconnected"
client.Close()
server.Close()
```

### Accept with Timeout Example

```basic
' Server that checks for shutdown periodically
DIM server AS OBJECT = Viper.Network.TcpServer.Listen(9000)
DIM shouldStop AS INTEGER = 0

DO WHILE NOT shouldStop
    ' Check for connection with 1 second timeout
    DIM client AS OBJECT = server.AcceptFor(1000)

    IF client IS NOT NULL THEN
        ' Handle client
        HandleClient(client)
        client.Close()
    END IF

    ' Check for shutdown signal
    shouldStop = CheckShutdownFlag()
LOOP

server.Close()
```

### Specific Interface Example

```basic
' Listen only on localhost (not accessible from network)
DIM server AS OBJECT = Viper.Network.TcpServer.ListenAt("127.0.0.1", 8080)

PRINT "Listening on "; server.Address; ":"; server.Port

' ... handle connections ...

server.Close()
```

### Multi-Client Example

```basic
' Simple server handling multiple sequential clients
DIM server AS OBJECT = Viper.Network.TcpServer.Listen(7000)

FOR i = 1 TO 10
    PRINT "Waiting for client "; i
    DIM client AS OBJECT = server.Accept()

    PRINT "Client "; i; " connected from "; client.Host

    ' Send greeting
    client.SendStr("Hello, client " + STR(i) + "!" + CHR(10))

    ' Close client
    client.Close()
NEXT i

server.Close()
```

### Error Handling

Server operations trap on errors:

- `Listen()` traps if port is already in use
- `Listen()` traps on permission denied (ports below 1024 require elevated privileges)
- `ListenAt()` traps if address is invalid
- `Accept()` traps if server is closed

The `AcceptFor()` method returns `NULL` on timeout instead of trapping, allowing graceful timeout handling.

### Platform Notes

The networking implementation uses the Berkeley sockets API:

| Platform | Notes                                         |
|----------|-----------------------------------------------|
| Unix     | Standard POSIX sockets                        |
| macOS    | Standard BSD sockets                          |
| Windows  | Winsock2 (WSAStartup called automatically)    |

### Use Cases

- **Web servers:** Accept HTTP connections
- **Game servers:** Handle multiple game clients
- **Service daemons:** Provide network services
- **Testing:** Create mock servers for testing
- **IoT hubs:** Receive data from devices

---

## Implementation Notes

### Threading

Each connection object is independent and can be used from a single thread. For multi-threaded servers handling concurrent clients, each client connection should be handled in a separate thread.

### Connection Limits

The server uses the system's default listen backlog (`SOMAXCONN`), which varies by platform:

| Platform | Typical Backlog |
|----------|-----------------|
| Linux    | 128             |
| macOS    | 128             |
| Windows  | 200             |

### Resource Cleanup

Always call `Close()` on connections and servers when done to release system resources. Connections that are garbage collected without being closed may leak file descriptors.

### IPv4 Only

The current implementation supports IPv4 only. IPv6 support may be added in a future release.

---

## Viper.Network.Udp

UDP datagram socket for connectionless communication.

**Type:** Instance class

**Constructors:**

- `Viper.Network.Udp.New()` - Create an unbound UDP socket
- `Viper.Network.Udp.Bind(port)` - Create and bind to port on all interfaces
- `Viper.Network.Udp.BindAt(address, port)` - Bind to specific interface

### Properties

| Property  | Type    | Description                             |
|-----------|---------|-----------------------------------------|
| `Address` | String  | Bound address (empty if unbound)        |
| `IsBound` | Boolean | True if socket is bound (read-only)     |
| `Port`    | Integer | Bound port number (0 if unbound)        |

### Send Methods

| Method                       | Returns | Description                           |
|------------------------------|---------|---------------------------------------|
| `SendTo(host, port, data)`   | Integer | Send Bytes to address, return bytes sent |
| `SendToStr(host, port, text)`| Integer | Send string as UTF-8, return bytes sent  |

### Receive Methods

| Method                     | Returns | Description                                     |
|----------------------------|---------|-------------------------------------------------|
| `Recv(maxBytes)`           | Bytes   | Receive packet (blocks)                         |
| `RecvFor(maxBytes, ms)`    | Bytes   | Receive with timeout, null on timeout           |
| `RecvFrom(maxBytes)`       | Bytes   | Receive and store sender info                   |
| `SenderHost()`             | String  | Host of last received packet (from RecvFrom)    |
| `SenderPort()`             | Integer | Port of last received packet (from RecvFrom)    |

### Options and Close

| Method                  | Returns | Description                                  |
|-------------------------|---------|----------------------------------------------|
| `Close()`               | void    | Close the socket                             |
| `JoinGroup(addr)`       | void    | Join multicast group (224.0.0.0-239.255.255.255) |
| `LeaveGroup(addr)`      | void    | Leave multicast group                        |
| `SetBroadcast(enable)`  | void    | Enable/disable broadcast                     |
| `SetRecvTimeout(ms)`    | void    | Set receive timeout (0 = no timeout)         |

### Zia Example

> Udp is accessible via `bind Viper.Network.Udp as Udp;`. Call `Udp.New()` to create a socket, then `Udp.Bind(port)` or use `SendTo`/`Recv`. Properties use get_ pattern.

### BASIC Example

```basic
' Simple UDP echo client/server

' Server side (receiver)
DIM server AS OBJECT = Viper.Network.Udp.Bind(9000)
PRINT "Listening for UDP on port "; server.Port

' Wait for a message
DIM data AS OBJECT = server.RecvFrom(1024)
PRINT "Received "; data.Len; " bytes from "; server.SenderHost(); ":"; server.SenderPort()

' Echo back
server.SendTo(server.SenderHost(), server.SenderPort(), data)

server.Close()
```

### Client Example

```basic
' UDP client
DIM sock AS OBJECT = Viper.Network.Udp.New()

' Send message
DIM msg AS STRING = "Hello UDP!"
sock.SendToStr("127.0.0.1", 9000, msg)

' Receive response (with timeout)
sock.SetRecvTimeout(5000)  ' 5 seconds
DIM response AS OBJECT = sock.Recv(1024)

IF response.Len > 0 THEN
    PRINT "Got response: "; response.Len; " bytes"
ELSE
    PRINT "No response (timeout)"
END IF

sock.Close()
```

### Broadcast Example

```basic
' Send broadcast message (requires SetBroadcast)
DIM sock AS OBJECT = Viper.Network.Udp.Bind(0)  ' Bind to any port
sock.SetBroadcast(TRUE)

' Send to broadcast address
sock.SendToStr("255.255.255.255", 9000, "Discover")

sock.Close()
```

### Multicast Example

```basic
' Join a multicast group
DIM sock AS OBJECT = Viper.Network.Udp.Bind(5000)
sock.JoinGroup("239.1.2.3")

' Receive multicast messages
DIM data AS OBJECT = sock.RecvFor(1024, 5000)
IF data IS NOT NULL THEN
    PRINT "Received multicast: "; data.Len; " bytes"
END IF

sock.LeaveGroup("239.1.2.3")
sock.Close()
```

### Error Handling

UDP operations trap on errors:

- `SendTo()` traps on host not found
- `SendTo()` traps if message is too large (>65507 bytes)
- `Recv()` traps if socket is closed
- `JoinGroup()` traps on invalid multicast address
- `Bind()` traps if port is in use or permission denied

The `RecvFor()` method returns `NULL` on timeout instead of trapping.

### UDP vs TCP

| Feature           | TCP                  | UDP                      |
|-------------------|----------------------|--------------------------|
| Connection        | Connection-oriented  | Connectionless           |
| Delivery          | Guaranteed, ordered  | Best-effort, unordered   |
| Flow control      | Yes                  | No                       |
| Overhead          | Higher               | Lower                    |
| Message boundary  | Stream (no boundary) | Preserved (datagrams)    |
| Use case          | Reliable transfer    | Low-latency, real-time   |

### Packet Size

- **Safe maximum:** 512 bytes (guaranteed no fragmentation)
- **Theoretical maximum:** 65507 bytes (65535 - 8 byte UDP header - 20 byte IP header)
- **MTU-safe:** 1472 bytes (Ethernet MTU 1500 - headers)

Packets larger than the network MTU will be fragmented and may be lost if any fragment is lost.

### Use Cases

- **Game networking:** Low-latency updates, player positions
- **DNS queries:** Simple request/response
- **DHCP:** Network configuration
- **Streaming:** Audio/video where some loss is acceptable
- **Discovery:** Finding services on local network
- **IoT sensors:** Lightweight telemetry

---

## Viper.Network.Dns

Static utility class for DNS resolution and IP address validation.

**Type:** Static class

### Resolution Methods

| Method                  | Returns | Description                                  |
|-------------------------|---------|----------------------------------------------|
| `Resolve(hostname)`     | String  | Resolve to first IPv4 address                |
| `Resolve4(hostname)`    | String  | Resolve to first IPv4 address only           |
| `Resolve6(hostname)`    | String  | Resolve to first IPv6 address only           |
| `ResolveAll(hostname)`  | Seq     | Resolve to all addresses (IPv4 and IPv6)     |
| `Reverse(ipAddress)`    | String  | Reverse DNS lookup, return hostname          |

### Validation Methods

| Method              | Returns | Description                                  |
|---------------------|---------|----------------------------------------------|
| `IsIP(address)`     | Boolean | Check if valid IPv4 or IPv6 address          |
| `IsIPv4(address)`   | Boolean | Check if string is valid IPv4 address        |
| `IsIPv6(address)`   | Boolean | Check if string is valid IPv6 address        |

### Local Info Methods

| Method         | Returns | Description                                  |
|----------------|---------|----------------------------------------------|
| `LocalHost()`  | String  | Get local machine hostname                   |
| `LocalAddrs()` | Seq     | Get all local IP addresses                   |

### Zia Example

```rust
module DnsDemo;

bind Viper.Terminal;
bind Viper.Network.Dns as Dns;
bind Viper.Fmt as Fmt;

func start() {
    // IP validation (no network required)
    Say("IsIP 1.2.3.4: " + Fmt.Bool(Dns.IsIP("1.2.3.4")));
    Say("IsIPv4 1.2.3.4: " + Fmt.Bool(Dns.IsIPv4("1.2.3.4")));
    Say("IsIPv6 ::1: " + Fmt.Bool(Dns.IsIPv6("::1")));
    Say("LocalHost: " + Dns.LocalHost());

    // Resolve localhost
    Say("localhost: " + Dns.Resolve("localhost"));
}
```

### BASIC Example

```basic
' Resolve a hostname
DIM ip AS STRING = Viper.Network.Dns.Resolve("example.com")
PRINT "example.com resolves to: "; ip

' Get all addresses for a hostname
DIM addrs AS OBJECT = Viper.Network.Dns.ResolveAll("google.com")
PRINT "Google.com addresses:"
FOR i = 0 TO addrs.Len - 1
    PRINT "  "; addrs.Get(i)
NEXT i
```

### IP Validation Example

```basic
' Check if input is a valid IP address
DIM input AS STRING = "192.168.1.1"

IF Viper.Network.Dns.IsIPv4(input) THEN
    PRINT input; " is a valid IPv4 address"
ELSE IF Viper.Network.Dns.IsIPv6(input) THEN
    PRINT input; " is a valid IPv6 address"
ELSE
    PRINT input; " is a hostname, resolving..."
    DIM resolved AS STRING = Viper.Network.Dns.Resolve(input)
    PRINT "Resolved to: "; resolved
END IF
```

### Local Network Info Example

```basic
' Get local machine info
PRINT "Local hostname: "; Viper.Network.Dns.LocalHost()

' Get all local IP addresses
DIM localAddrs AS OBJECT = Viper.Network.Dns.LocalAddrs()
PRINT "Local addresses:"
FOR i = 0 TO localAddrs.Len - 1
    DIM addr AS STRING = localAddrs.Get(i)
    IF Viper.Network.Dns.IsIPv4(addr) THEN
        PRINT "  IPv4: "; addr
    ELSE
        PRINT "  IPv6: "; addr
    END IF
NEXT i
```

### Reverse DNS Example

```basic
' Reverse lookup an IP address
DIM hostname AS STRING = Viper.Network.Dns.Reverse("8.8.8.8")
PRINT "8.8.8.8 is: "; hostname
```

### Error Handling

DNS operations trap on errors:

- `Resolve()` traps if hostname not found
- `Resolve4()` traps if no IPv4 address exists
- `Resolve6()` traps if no IPv6 address exists
- `Reverse()` traps if reverse lookup fails
- All methods trap on NULL/empty input

There is no way to distinguish between a non-existent domain (NXDOMAIN), a DNS server failure (SERVFAIL), or a network timeout — all result in a trap with the same message.

> **Blocking behavior:** DNS resolution is synchronous and may block the calling thread for up to ~10 seconds on unresponsive servers (OS-level retry behavior). There is no programmatic timeout for DNS operations.

Validation methods (`IsIPv4`, `IsIPv6`, `IsIP`) never trap and return `False` for invalid input.

### Address Formats

| Type | Format | Examples |
|------|--------|----------|
| IPv4 | Dotted decimal | `192.168.1.1`, `10.0.0.1`, `127.0.0.1` |
| IPv6 | Colon hex | `::1`, `2001:db8::1`, `fe80::1` |

### Use Cases

- **Pre-connection validation:** Validate IP addresses before connecting
- **Service discovery:** Find addresses for hostnames
- **Network diagnostics:** Get local network configuration
- **Load balancing:** Use `ResolveAll` to get multiple backend addresses
- **Logging:** Convert IPs to hostnames via `Reverse`

---

## Viper.Network.Http

Static HTTP client utilities for simple HTTP requests.

**Type:** Static class

### Methods

| Method                         | Returns | Description                                  |
|--------------------------------|---------|----------------------------------------------|
| `Download(url, destPath)`      | Boolean | Download file to destination path            |
| `Get(url)`                     | String  | GET request, return response body as string  |
| `GetBytes(url)`                | Bytes   | GET request, return response body as bytes   |
| `Head(url)`                    | Map     | HEAD request, return headers as Map          |
| `Post(url, body)`              | String  | POST request with string body (`Content-Type: text/plain; charset=utf-8`) |
| `PostBytes(url, body)`         | Bytes   | POST request with Bytes body (`Content-Type: application/octet-stream`) |

### Zia Example

> Http is accessible via `bind Viper.Network.Http as Http;`. Call `Http.Get(url)`, `Http.Post(url, body)`, etc. Requires network access.

### BASIC Example

```basic
' Simple GET request
DIM html AS STRING = Viper.Network.Http.Get("http://example.com/api/data")
PRINT html

' Download a file
DIM success AS INTEGER = Viper.Network.Http.Download("http://example.com/file.zip", "/tmp/file.zip")
IF success THEN
    PRINT "Download complete"
END IF

' POST with JSON body
DIM response AS STRING = Viper.Network.Http.Post("http://api.example.com/submit", _
    "{""name"": ""test"", ""value"": 42}")
PRINT response

' Get headers only
DIM headers AS OBJECT = Viper.Network.Http.Head("http://example.com/resource")
PRINT "Content-Type: "; headers.Get("content-type")
PRINT "Content-Length: "; headers.Get("content-length")
```

### Features

- **HTTP/1.1 support** - Standard HTTP/1.1 protocol
- **HTTPS support** - TLS 1.3 encryption (certificate validation planned; see HTTPS/TLS note below)
- **Redirect handling** - Automatically follows 301, 302, 307, 308 redirects (up to 5)
- **Content-Length** - Handles Content-Length bodies
- **Chunked encoding** - Handles Transfer-Encoding: chunked responses
- **Timeout** - Default 30 second timeout

### HTTPS/TLS Support

The HTTP client transparently supports HTTPS URLs using TLS 1.3:

- **Automatic upgrade** - URLs starting with `https://` automatically use TLS
- **Modern encryption** - TLS 1.3 with ChaCha20-Poly1305 cipher suite and X25519 key exchange
- **Encryption without authentication** - Traffic is encrypted, but certificate chain validation is not yet implemented. Server identity is not verified. Do not use HTTPS connections for sensitive credentials in production until certificate validation is added.
- **For custom TLS:** Use `Viper.Crypto.Tls` directly or use `HttpReq` with `SetTimeout()` for timeout control.

```basic
' HTTPS works exactly like HTTP
DIM data AS STRING = Viper.Network.Http.Get("https://api.example.com/secure")
PRINT data

' Download over HTTPS
Viper.Network.Http.Download("https://example.com/file.zip", "/tmp/file.zip")
```

### Error Handling

HTTP operations trap on errors:

- Traps on invalid URL format
- Traps on connection failure
- Traps on TLS handshake failure (protocol errors; certificate chain is not validated)
- Traps on timeout
- Traps on too many redirects (>5)

### Limitations

- **IPv4 only** - IPv6 addresses not supported in URLs
- **No cookies** - Cookie handling not included
- **No auth** - Use `HttpReq` for custom headers including Authorization
- **No client certificates** - Client-side TLS certificates not supported
- **Fixed Content-Type on Post** - `Http.Post()` always sends `Content-Type: text/plain; charset=utf-8`. For JSON or other content types, use `HttpReq` with `.SetHeader("Content-Type", "application/json")`

---

## Viper.Network.HttpReq

HTTP request builder for advanced requests with custom headers and options.

**Type:** Instance class

**Constructor:**

- `Viper.Network.HttpReq.New(method, url)` - Create request with method and URL

### Methods

| Method                    | Returns | Description                                  |
|---------------------------|---------|----------------------------------------------|
| `Send()`                  | HttpRes | Execute the request and return response      |
| `SetBody(data)`           | HttpReq | Set request body as Bytes (chainable)        |
| `SetBodyStr(text)`        | HttpReq | Set request body as string (chainable)       |
| `SetHeader(name, value)`  | HttpReq | Set a request header (chainable)             |
| `SetTimeout(ms)`          | HttpReq | Set request timeout in milliseconds          |

> **TLS configuration:** `HttpReq` is the recommended path for HTTPS requests that need custom timeouts. Use `SetTimeout(ms)` to control the overall request timeout. For raw TLS connections (without HTTP), use `Viper.Crypto.Tls` directly.

### Zia Example

> HttpReq is accessible via `bind Viper.Network.HttpReq as HttpReq;`. Call `HttpReq.New(method, url)` to create a request, configure with `SetHeader`/`SetBody`, then call `Send()`. Requires network access.

### BASIC Example

```basic
' Custom GET request with headers
DIM req AS OBJECT = Viper.Network.HttpReq.New("GET", "http://api.example.com/data")
req.SetHeader("Accept", "application/json")
req.SetHeader("X-API-Key", "my-secret-key")
req.SetTimeout(10000)  ' 10 seconds

DIM res AS OBJECT = req.Send()
IF res.IsOk() THEN
    PRINT "Response: "; res.BodyStr()
ELSE
    PRINT "Error: "; res.Status; " "; res.StatusText
END IF
```

### Chainable API

```basic
' All setters return the request object for chaining
DIM res AS OBJECT = Viper.Network.HttpReq.New("POST", "http://api.example.com/submit") _
    .SetHeader("Content-Type", "application/json") _
    .SetHeader("Authorization", "Bearer token123") _
    .SetBodyStr("{""data"": ""value""}") _
    .SetTimeout(5000) _
    .Send()
```

---

## Viper.Network.HttpRes

HTTP response object returned by `HttpReq.Send()`.

**Type:** Instance class

### Properties

| Property     | Type    | Description                              |
|--------------|---------|------------------------------------------|
| `Status`     | Integer | HTTP status code (e.g., 200, 404)        |
| `StatusText` | String  | HTTP status text (e.g., "OK", "Not Found") |
| `Headers`    | Map     | Response headers (lowercase keys)        |

### Methods

| Method          | Returns | Description                                  |
|-----------------|---------|----------------------------------------------|
| `Body()`        | Bytes   | Get response body as Bytes                   |
| `BodyStr()`     | String  | Get response body as UTF-8 string            |
| `Header(name)`  | String  | Get specific header value (case-insensitive) |
| `IsOk()`        | Boolean | True if status is 2xx (success)              |

### Zia Example

> HttpRes is returned by `HttpReq.Send()`. Access properties via get_ pattern: `res.get_Status()`, `res.get_StatusText()`. Call `res.BodyStr()`, `res.IsOk()`, `res.Header(name)`.

### BASIC Example

```basic
DIM res AS OBJECT = Viper.Network.HttpReq.New("GET", "http://example.com/api").Send()

' Check status
PRINT "Status: "; res.Status; " "; res.StatusText

' Check if successful
IF res.IsOk() THEN
    ' Get body as string
    DIM body AS STRING = res.BodyStr()
    PRINT "Body: "; body

    ' Get specific header
    DIM contentType AS STRING = res.Header("content-type")
    PRINT "Content-Type: "; contentType
ELSE
    PRINT "Request failed"
END IF
```

### Headers Access

```basic
' Access all headers
DIM headers AS OBJECT = res.Headers
DIM keys AS OBJECT = headers.Keys()

FOR i = 0 TO keys.Len - 1
    DIM key AS STRING = keys.Get(i)
    PRINT key; ": "; headers.Get(key)
NEXT i
```

### Binary Response

```basic
' Download binary data
DIM res AS OBJECT = Viper.Network.HttpReq.New("GET", "http://example.com/image.png").Send()

IF res.IsOk() THEN
    DIM data AS OBJECT = res.Body()
    PRINT "Downloaded "; data.Len; " bytes"

    ' Save to file
    Viper.IO.File.WriteBytes("/tmp/image.png", data)
END IF
```

### HTTP Status Codes

| Range   | Meaning       | Common Codes                              |
|---------|---------------|-------------------------------------------|
| 1xx     | Informational | 100 Continue                              |
| 2xx     | Success       | 200 OK, 201 Created, 204 No Content       |
| 3xx     | Redirection   | 301 Moved, 302 Found, 304 Not Modified    |
| 4xx     | Client Error  | 400 Bad Request, 401 Unauthorized, 404 Not Found |
| 5xx     | Server Error  | 500 Internal Error, 502 Bad Gateway       |

---

## Viper.Network.Url

URL parsing and construction following RFC 3986.

**Type:** Instance class

**Constructors:**

- `Viper.Network.Url.Parse(urlString)` - Parse a URL string into components
- `Viper.Network.Url.New()` - Create an empty URL for building

### Properties

All properties are read/write.

| Property   | Type    | Description                                      |
|------------|---------|--------------------------------------------------|
| `Fragment` | String  | Fragment (without leading #)                     |
| `Host`     | String  | Hostname or IP address                           |
| `Pass`     | String  | Password (optional)                              |
| `Path`     | String  | Path component (with leading /)                  |
| `Port`     | Integer | Port number (0 = not specified)                  |
| `Query`    | String  | Query string (without leading ?)                 |
| `Scheme`   | String  | URL scheme (http, https, ftp, etc.)              |
| `User`     | String  | Username (optional)                              |

### Computed Properties (Read-Only)

| Property    | Type   | Description                                         |
|-------------|--------|-----------------------------------------------------|
| `Authority` | String | Full authority: `user:pass@host:port`               |
| `HostPort`  | String | `host:port` (port omitted if default for scheme)    |
| `Full`      | String | Complete URL string                                 |

### Query Parameter Methods

| Method                           | Returns | Description                        |
|----------------------------------|---------|------------------------------------|
| `DelQueryParam(name)`            | Url     | Remove query parameter             |
| `GetQueryParam(name)`            | String  | Get query parameter value          |
| `HasQueryParam(name)`            | Boolean | Check if parameter exists          |
| `QueryMap()`                     | Map     | Get all parameters as Map          |
| `SetQueryParam(name, value)`     | Url     | Set or update query parameter      |

### Other Methods

| Method              | Returns | Description                                  |
|---------------------|---------|----------------------------------------------|
| `Clone()`           | Url     | Create a copy of this URL                    |
| `Resolve(relative)` | Url     | Resolve relative URL against this base       |

### Static Utility Methods

| Method                | Returns | Description                              |
|-----------------------|---------|------------------------------------------|
| `Decode(text)`        | String  | Decode percent-encoded text              |
| `DecodeQuery(query)`  | Map     | Parse query string to Map                |
| `Encode(text)`        | String  | Percent-encode text for URL              |
| `EncodeQuery(map)`    | String  | Encode Map as query string               |
| `IsValid(urlString)`  | Boolean | Check if URL string is valid             |

### Default Ports

| Scheme | Port |
|--------|------|
| http   | 80   |
| https  | 443  |
| ftp    | 21   |
| ssh    | 22   |
| ws     | 80   |
| wss    | 443  |

### Zia Example

```rust
module UrlDemo;

bind Viper.Terminal;
bind Viper.Network.Url as Url;
bind Viper.Fmt as Fmt;

func start() {
    // Parse a URL
    var u = Url.Parse("https://example.com:8080/path?key=value#section");
    Say("Scheme: " + u.get_Scheme());
    Say("Host: " + u.get_Host());
    Say("Port: " + Fmt.Int(u.get_Port()));
    Say("Path: " + u.get_Path());
    Say("Query: " + u.get_Query());
    Say("Fragment: " + u.get_Fragment());
    Say("Full: " + u.get_Full());

    // URL encoding/decoding
    Say("Encode: " + Url.Encode("hello world!"));
    Say("Decode: " + Url.Decode("hello%20world%21"));

    // Validation
    Say("IsValid: " + Fmt.Bool(Url.IsValid("https://example.com")));
}
```

> **Note:** Url properties use the get_/set_ pattern in Zia: `u.get_Scheme()`, `u.set_Host("new.com")`, etc.

### BASIC Example

```basic
' Parse a URL
DIM u AS OBJECT = Viper.Network.Url.Parse("https://example.com:8080/path?key=value#section")
PRINT "Scheme: "; u.Scheme
PRINT "Host: "; u.Host
PRINT "Port: "; u.Port
PRINT "Path: "; u.Path
PRINT "Query: "; u.Query
PRINT "Fragment: "; u.Fragment
PRINT "Full: "; u.Full

' URL encoding/decoding
PRINT "Encode: "; Viper.Network.Url.Encode("hello world!")
PRINT "Decode: "; Viper.Network.Url.Decode("hello%20world%21")

' Validation
PRINT "IsValid: "; Viper.Network.Url.IsValid("https://example.com")
```

### Parsing Example

```basic
' Parse a URL with all components
DIM url AS OBJECT = Viper.Network.Url.Parse("https://user:pass@api.example.com:8443/path?foo=bar#section")

PRINT "Scheme: "; url.Scheme      ' "https"
PRINT "User: "; url.User          ' "user"
PRINT "Host: "; url.Host          ' "api.example.com"
PRINT "Port: "; url.Port          ' 8443
PRINT "Path: "; url.Path          ' "/path"
PRINT "Query: "; url.Query        ' "foo=bar"
PRINT "Fragment: "; url.Fragment  ' "section"
PRINT "Full: "; url.Full          ' Complete URL string
```

### Building Example

```basic
' Build a URL from scratch
DIM url AS OBJECT = Viper.Network.Url.New()
url.Scheme = "https"
url.Host = "api.example.com"
url.Path = "/v1/users"
url.SetQueryParam("page", "1")
url.SetQueryParam("limit", "10")

PRINT url.Full  ' "https://api.example.com/v1/users?page=1&limit=10"
```

### Query Parameter Manipulation

```basic
DIM url AS OBJECT = Viper.Network.Url.Parse("http://example.com/?a=1&b=2")

' Check and get parameters
IF url.HasQueryParam("a") THEN
    PRINT "a = "; url.GetQueryParam("a")
END IF

' Modify parameters
url.SetQueryParam("c", "3")
url.DelQueryParam("a")

' Get all parameters as Map
DIM params AS OBJECT = url.QueryMap()
PRINT "Parameters: "; params.Len
```

### URL Resolution

```basic
DIM base AS OBJECT = Viper.Network.Url.Parse("http://example.com/a/b/c")

' Resolve absolute path
DIM r1 AS OBJECT = base.Resolve("/d/e")
PRINT r1.Full  ' "http://example.com/d/e"

' Resolve relative path
DIM r2 AS OBJECT = base.Resolve("d")
PRINT r2.Full  ' "http://example.com/a/b/d"

' Resolve full URL
DIM r3 AS OBJECT = base.Resolve("https://other.com/x")
PRINT r3.Full  ' "https://other.com/x"
```

### Encoding/Decoding

```basic
' Percent-encode special characters
DIM encoded AS STRING = Viper.Network.Url.Encode("hello world!")
PRINT encoded  ' "hello%20world%21"

' Decode percent-encoded string
DIM decoded AS STRING = Viper.Network.Url.Decode("hello%20world%21")
PRINT decoded  ' "hello world!"

' Encode Map as query string
DIM params AS OBJECT = Viper.Collections.Map.New()
params.Set("name", "John Doe")
params.Set("city", "New York")
DIM query AS STRING = Viper.Network.Url.EncodeQuery(params)
PRINT query  ' "name=John%20Doe&city=New%20York"

' Decode query string to Map
DIM parsed AS OBJECT = Viper.Network.Url.DecodeQuery("a=1&b=2")
PRINT parsed.Get("a")  ' "1"
```

---

## Viper.Network.WebSocket

WebSocket client for bidirectional, full-duplex communication over a single TCP connection. Supports both text and
binary messages following RFC 6455.

**Type:** Instance class

**Constructors:**

- `Viper.Network.WebSocket.Connect(url)` - Connect to WebSocket server
- `Viper.Network.WebSocket.ConnectFor(url, timeoutMs)` - Connect with timeout

### Properties

| Property      | Type    | Description                                        |
|---------------|---------|----------------------------------------------------|
| `CloseCode`   | Integer | Close status code (0 if not closed) (read-only)    |
| `CloseReason` | String  | Close reason message (empty if not closed)         |
| `IsOpen`      | Boolean | True if connection is open (read-only)             |
| `Url`         | String  | WebSocket URL (ws:// or wss://) (read-only)        |

### Send Methods

| Method            | Returns | Description                                  |
|-------------------|---------|----------------------------------------------|
| `Ping()`          | void    | Send ping frame (server responds with pong)  |
| `Send(text)`      | void    | Send text message (UTF-8)                    |
| `SendBytes(data)` | void    | Send binary message (Bytes)                  |

### Receive Methods

| Method               | Returns | Description                                          |
|----------------------|---------|------------------------------------------------------|
| `Recv()`             | String  | Receive text message (blocks)                        |
| `RecvBytes()`        | Bytes   | Receive binary message (blocks)                      |
| `RecvBytesFor(ms)`   | Bytes   | Receive binary with timeout (null on timeout)        |
| `RecvFor(ms)`        | String  | Receive text with timeout (returns `null` on timeout; traps on error)  |

### Close Methods

| Method               | Returns | Description                                          |
|----------------------|---------|------------------------------------------------------|
| `Close()`            | void    | Close connection with default code (1000 Normal)     |
| `CloseWith(code,msg)`| void    | Close connection with specific code and reason       |

### URL Format

WebSocket URLs follow this format:
```
ws://host[:port][/path]      # Unencrypted (port 80 default)
wss://host[:port][/path]     # TLS encrypted (port 443 default)
```

### Zia Example

> WebSocket is accessible via `bind Viper.Network.WebSocket as WS;`. Call `WS.Connect(url)` or `WS.ConnectFor(url, ms)`. Properties use get_ pattern: `ws.get_Url()`, `ws.get_IsOpen()`. Requires network access.

### BASIC Example

```basic
' Connect to a WebSocket server
DIM ws AS OBJECT = Viper.Network.WebSocket.Connect("wss://echo.websocket.org/")

IF ws.IsOpen THEN
    PRINT "Connected to "; ws.Url

    ' Send a text message
    ws.Send("Hello, WebSocket!")

    ' Receive response
    DIM response AS STRING = ws.Recv()
    PRINT "Received: "; response

    ' Send binary data
    DIM data AS OBJECT = Viper.Collections.Bytes.FromHex("deadbeef")
    ws.SendBytes(data)

    ' Receive binary response
    DIM binResponse AS OBJECT = ws.RecvBytes()
    PRINT "Binary: "; binResponse.ToHex()

    ' Clean close
    ws.Close()
END IF
```

### Receive with Timeout Example

```basic
DIM ws AS OBJECT = Viper.Network.WebSocket.Connect("ws://example.com/stream")

' Receive with 5 second timeout
DO WHILE ws.IsOpen
    DIM msg AS STRING = ws.RecvFor(5000)
    IF msg <> "" THEN
        PRINT "Message: "; msg
    ELSE
        PRINT "No message within timeout"
        ' Could send ping to keep connection alive
        ws.Ping()
    END IF
LOOP

PRINT "Connection closed: "; ws.CloseCode; " - "; ws.CloseReason
```

### Close Codes

| Code | Name              | Description                                    |
|------|-------------------|------------------------------------------------|
| 1000 | Normal Closure    | Normal close (default for `Close()`)           |
| 1001 | Going Away        | Server or client is shutting down              |
| 1002 | Protocol Error    | Protocol error received                        |
| 1003 | Unsupported Data  | Received data type not supported               |
| 1006 | Abnormal Closure  | Connection lost without close frame            |
| 1007 | Invalid Data      | Message data was invalid (e.g., bad UTF-8)     |
| 1008 | Policy Violation  | Message violates policy                        |
| 1009 | Message Too Big   | Message exceeds size limit                     |
| 1011 | Server Error      | Server encountered unexpected error            |

### Error Handling

WebSocket operations trap on errors:

- `Connect()` traps on invalid URL, connection refused, or handshake failure
- `ConnectFor()` traps on timeout in addition to connection errors
- `Send()`/`SendBytes()` trap if connection is closed
- `Recv()` traps on protocol errors
- Operations on `wss://` URLs trap on TLS errors

### Protocol Notes

- **Frame masking:** Client frames are automatically masked per RFC 6455
- **Ping/pong:** Pong frames are handled automatically; use `Ping()` to test connectivity
- **Message fragmentation:** Large messages are automatically fragmented/reassembled
- **UTF-8 validation:** Text messages are validated for proper UTF-8 encoding
- **Subprotocols:** Not currently supported; use headers via `HttpReq` if needed

### Use Cases

- **Real-time applications:** Chat, notifications, live updates
- **Gaming:** Multiplayer game state synchronization
- **Financial feeds:** Stock tickers, trading data
- **IoT:** Device monitoring and control
- **Collaborative tools:** Real-time document editing

---

## Viper.Network.RestClient

High-level REST API client with session management for consuming REST APIs. Maintains persistent headers, authentication,
and base URL configuration across multiple requests.

**Type:** Instance class

**Constructor:**

- `Viper.Network.RestClient.New(baseUrl)` - Create client with base URL (e.g., "https://api.example.com")

### Properties

| Property  | Type   | Description                                    |
|-----------|--------|------------------------------------------------|
| `BaseUrl` | String | Base URL for all requests (read-only)          |

### Configuration Methods

| Method                         | Returns | Description                                    |
|--------------------------------|---------|------------------------------------------------|
| `ClearAuth()`                  | void    | Remove authentication header                   |
| `DelHeader(name)`              | void    | Remove a default header                        |
| `SetAuthBasic(username, pass)` | void    | Set HTTP Basic authentication                  |
| `SetAuthBearer(token)`         | void    | Set Bearer token authentication                |
| `SetHeader(name, value)`       | void    | Set a default header for all requests          |
| `SetTimeout(ms)`               | void    | Set request timeout in milliseconds            |

### Raw HTTP Methods

Returns `HttpRes` response object for manual handling.

| Method                    | Returns | Description                                    |
|---------------------------|---------|------------------------------------------------|
| `Delete(path)`            | HttpRes | DELETE request                                 |
| `Get(path)`               | HttpRes | GET request to path                            |
| `Head(path)`              | HttpRes | HEAD request (headers only)                    |
| `Patch(path, body)`       | HttpRes | PATCH request with string body                 |
| `Post(path, body)`        | HttpRes | POST request with string body                  |
| `Put(path, body)`         | HttpRes | PUT request with string body                   |

### JSON Convenience Methods

Automatically sets `Content-Type` and `Accept` headers for JSON, parses response.

| Method                     | Returns | Description                                          |
|----------------------------|---------|------------------------------------------------------|
| `DeleteJson(path)`         | Object  | DELETE, return parsed JSON response or null          |
| `GetJson(path)`            | Object  | GET, return parsed JSON (Map/Seq) or null on error   |
| `PatchJson(path, json)`    | Object  | PATCH JSON body, return parsed response or null      |
| `PostJson(path, json)`     | Object  | POST JSON body, return parsed response or null       |
| `PutJson(path, json)`      | Object  | PUT JSON body, return parsed response or null        |

### Error Handling Methods

| Method           | Returns | Description                                          |
|------------------|---------|------------------------------------------------------|
| `LastOk()`       | Boolean | True if last status was 200-299                      |
| `LastResponse()` | HttpRes | Last response object (null if none)                  |
| `LastStatus()`   | Integer | HTTP status code of last request (0 if none)         |

### Zia Example

> **Note:** RestClient is not yet fully usable from Zia. The `New()` constructor fails with "no exported symbol 'New'" due to a frontend symbol resolution bug. This affects all instance classes that use `New` constructors in the Network module. See also: RetryPolicy, RateLimiter.

### BASIC Example

```basic
' Create a REST client
DIM api AS OBJECT = Viper.Network.RestClient.New("https://api.example.com")
PRINT "BaseUrl: "; api.BaseUrl

' Set common headers
api.SetHeader("User-Agent", "ViperDemo/1.0")

' Set timeout
api.SetTimeout(15000)

' Set Bearer auth
api.SetAuthBearer("test-token-123")

' Clear auth
api.ClearAuth()

' Check last status (no request yet, should be 0)
PRINT "LastStatus: "; api.LastStatus()
PRINT "LastOk: "; api.LastOk()
```

### Basic Example

```basic
' Create a REST client for an API
DIM api AS OBJECT = Viper.Network.RestClient.New("https://api.example.com")

' Set common headers
api.SetHeader("User-Agent", "MyApp/1.0")
api.SetTimeout(15000)  ' 15 seconds

' Make requests - base URL is prepended automatically
DIM res AS OBJECT = api.Get("/users")
IF res.IsOk() THEN
    PRINT "Users: "; res.BodyStr()
END IF
```

### Authentication Example

```basic
' API with Bearer token authentication
DIM api AS OBJECT = Viper.Network.RestClient.New("https://api.example.com/v1")
api.SetAuthBearer("eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9...")

' All requests now include Authorization header
DIM profile AS OBJECT = api.GetJson("/me")
IF profile IS NOT NULL THEN
    PRINT "Welcome, "; profile.Get("name")
END IF
```

### Basic Auth Example

```basic
' API with HTTP Basic authentication
DIM api AS OBJECT = Viper.Network.RestClient.New("https://api.example.com")
api.SetAuthBasic("username", "password")

' Credentials are base64-encoded in Authorization header
DIM data AS OBJECT = api.GetJson("/protected/resource")
```

### JSON API Example

```basic
' Complete CRUD example with JSON
DIM api AS OBJECT = Viper.Network.RestClient.New("https://jsonplaceholder.typicode.com")

' CREATE - POST with JSON body
DIM newPost AS OBJECT = Viper.Collections.Map.New()
newPost.Set("title", "My Post")
newPost.Set("body", "Post content here")
newPost.Set("userId", 1)

DIM created AS OBJECT = api.PostJson("/posts", newPost)
IF created IS NOT NULL THEN
    PRINT "Created post ID: "; created.Get("id")
END IF

' READ - GET JSON
DIM post AS OBJECT = api.GetJson("/posts/1")
IF post IS NOT NULL THEN
    PRINT "Title: "; post.Get("title")
END IF

' UPDATE - PUT JSON
post.Set("title", "Updated Title")
DIM updated AS OBJECT = api.PutJson("/posts/1", post)

' PARTIAL UPDATE - PATCH JSON
DIM patch AS OBJECT = Viper.Collections.Map.New()
patch.Set("title", "Patched Title")
DIM patched AS OBJECT = api.PatchJson("/posts/1", patch)

' DELETE
DIM deleted AS OBJECT = api.DeleteJson("/posts/1")
```

### Error Handling Example

```basic
DIM api AS OBJECT = Viper.Network.RestClient.New("https://api.example.com")

' Make a request that might fail
DIM result AS OBJECT = api.GetJson("/nonexistent")

IF result IS NULL THEN
    ' Check what went wrong
    IF api.LastStatus() = 404 THEN
        PRINT "Resource not found"
    ELSE IF api.LastStatus() = 401 THEN
        PRINT "Authentication required"
    ELSE IF api.LastStatus() >= 500 THEN
        PRINT "Server error: "; api.LastStatus()
    ELSE
        PRINT "Request failed with status: "; api.LastStatus()
    END IF

    ' Access full response if needed
    DIM res AS OBJECT = api.LastResponse()
    IF res IS NOT NULL THEN
        PRINT "Error body: "; res.BodyStr()
    END IF
END IF
```

### Multiple API Sessions

```basic
' Different APIs with different authentication
DIM publicApi AS OBJECT = Viper.Network.RestClient.New("https://public-api.example.com")

DIM privateApi AS OBJECT = Viper.Network.RestClient.New("https://private-api.example.com")
privateApi.SetAuthBearer(GetApiToken())

DIM legacyApi AS OBJECT = Viper.Network.RestClient.New("https://legacy.example.com")
legacyApi.SetAuthBasic("service", "password123")

' Each client maintains its own configuration
DIM pub AS OBJECT = publicApi.GetJson("/public/data")
DIM priv AS OBJECT = privateApi.GetJson("/secure/data")
DIM legacy AS OBJECT = legacyApi.GetJson("/api/v1/data")
```

### Features

- **Base URL:** All request paths are relative to the configured base URL
- **Persistent headers:** Headers set with `SetHeader` are sent with every request until removed with `DelHeader` or `ClearAuth`
- **Authentication:** Built-in support for Bearer tokens and HTTP Basic auth
- **Timeout:** Configurable timeout (default 30 seconds)
- **JSON helpers:** Automatic serialization/deserialization for JSON APIs
- **Last request tracking:** `LastResponse()` returns the most recent response object; it is replaced on every new request. `LastStatus()` returns the HTTP status code; `LastOk()` returns true for 2xx responses.

> **Lifecycle note:** The RestClient owns its headers map and last response. Resources are released automatically when the client object is garbage-collected. `LastResponse()` is only valid until the next request is made — do not cache the pointer across requests.

### RestClient vs HttpReq

| Feature              | RestClient                  | HttpReq                         |
|----------------------|-----------------------------|---------------------------------|
| Base URL             | Configured once             | Full URL each request           |
| Persistent headers   | Maintained across requests  | Set per request                 |
| Authentication       | Built-in helpers            | Manual header construction      |
| JSON handling        | Automatic with *Json methods| Manual serialization            |
| Use case             | REST API consumption        | One-off or custom requests      |

---

## Viper.Network.RetryPolicy

Configurable retry policy with backoff strategies for handling transient failures in network operations. Tracks attempt counts and computes delays between retries.

**Type:** Instance class

**Constructors:**

- `Viper.Network.RetryPolicy.New(maxRetries, baseDelayMs)` - Fixed delay retry policy
- `Viper.Network.RetryPolicy.Exponential(maxRetries, baseDelayMs, maxDelayMs)` - Exponential backoff

### Properties

| Property        | Type    | Description                                  |
|-----------------|---------|----------------------------------------------|
| `Attempt`       | Integer | Current attempt number (0-based)             |
| `CanRetry`      | Boolean | True if another retry is allowed (read-only) |
| `IsExhausted`   | Boolean | True if all retries have been used           |
| `MaxRetries`    | Integer | Maximum number of retries configured         |
| `TotalAttempts` | Integer | Total number of attempts made                |

### Methods

| Method          | Signature       | Description                                              |
|-----------------|-----------------|----------------------------------------------------------|
| `NextDelay()`   | `Integer()`     | Record attempt and get delay before next retry (-1 if exhausted) |
| `Reset()`       | `Void()`        | Reset the policy for reuse                               |

### Backoff Strategies

| Strategy     | Constructor     | Delay Pattern                                    |
|--------------|-----------------|--------------------------------------------------|
| Fixed        | `New()`         | Same delay every time: `base, base, base, ...`   |
| Exponential  | `Exponential()` | Doubles each time: `base, 2*base, 4*base, ...` (capped at max, with ±25% jitter) |

> **Attempt count semantics:** `RetryPolicy.New(n)` allows up to `n` calls to `NextDelay()` before the policy is exhausted. `Attempt` is 0-based and reflects how many `NextDelay()` calls have been made. The first `NextDelay()` call returns the initial delay (attempt 0); after `n` total calls the policy is exhausted and `NextDelay()` returns -1.

### Zia Example

> **Note:** RetryPolicy is not yet constructible from Zia. The `New()` and `Exponential()` constructors fail with "no exported symbol 'New'" due to a frontend symbol resolution bug affecting newer instance classes.

### Example

```basic
' Fixed delay retry (3 retries, 1 second between each)
DIM policy AS OBJECT = Viper.Network.RetryPolicy.New(3, 1000)

DO WHILE policy.CanRetry
    DIM result AS OBJECT = TryApiCall()
    IF result IS NOT NULL THEN
        PRINT "Success on attempt "; policy.Attempt
        EXIT DO
    END IF

    DIM delay AS INTEGER = policy.NextDelay()
    IF delay >= 0 THEN
        PRINT "Retry in "; delay; "ms"
        Viper.Time.Clock.Sleep(delay)
    END IF
LOOP

IF policy.IsExhausted THEN
    PRINT "All retries exhausted after "; policy.TotalAttempts; " attempts"
END IF
```

### Exponential Backoff Example

```basic
' Exponential backoff (5 retries, starting at 100ms, max 5 seconds)
DIM policy AS OBJECT = Viper.Network.RetryPolicy.Exponential(5, 100, 5000)

' Delays will be: 100, 200, 400, 800, 1600 (capped at 5000)
DO WHILE policy.CanRetry
    IF TryConnect() THEN EXIT DO

    DIM delay AS INTEGER = policy.NextDelay()
    IF delay >= 0 THEN
        Viper.Time.Clock.Sleep(delay)
    END IF
LOOP

' Reset for reuse
policy.Reset()
```

### Use Cases

- **API calls:** Retry failed HTTP requests with backoff
- **Database connections:** Reconnect with exponential backoff
- **File operations:** Retry failed file operations
- **Message queues:** Retry message processing with delays

---

## Viper.Network.RateLimiter

Token bucket rate limiter for controlling the rate of operations. Tokens refill continuously over time and are consumed by operations.

**Type:** Instance class
**Constructor:** `Viper.Network.RateLimiter.New(maxTokens, refillPerSec)`

### Properties

| Property    | Type    | Description                                    |
|-------------|---------|------------------------------------------------|
| `Available` | Integer | Current available tokens (after refill)        |
| `Max`       | Integer | Maximum token capacity                         |
| `Rate`      | Double  | Token refill rate (tokens per second)          |

### Methods

| Method              | Signature       | Description                                              |
|---------------------|-----------------|----------------------------------------------------------|
| `TryAcquire()`      | `Boolean()`     | Try to consume 1 token (returns false if none available) |
| `TryAcquireN(n)`    | `Boolean(Integer)` | Try to consume N tokens atomically                    |
| `Reset()`           | `Void()`        | Reset to full capacity                                   |

### How It Works

1. The limiter starts with `maxTokens` available tokens
2. Tokens refill continuously at `refillPerSec` rate, up to `maxTokens`
3. Each operation consumes one or more tokens
4. If insufficient tokens are available, the operation is denied (returns false)

> **Token precision:** Tokens refill as a floating-point value internally. The `Available` property returns the floor of the current token count. `TryAcquire` and `TryAcquireN` consume whole tokens only. Not thread-safe — external synchronization required for concurrent use.

### Zia Example

> **Note:** RateLimiter is not yet constructible from Zia. The `New()` constructor fails with "no exported symbol 'New'" due to a frontend symbol resolution bug affecting newer instance classes.

### Example

```basic
' Allow 10 requests per second with burst of 10
DIM limiter AS OBJECT = Viper.Network.RateLimiter.New(10, 10.0)

' Check before making API calls
FUNCTION MakeApiCall(url AS STRING) AS OBJECT
    IF limiter.TryAcquire() THEN
        RETURN Viper.Network.Http.Get(url)
    ELSE
        PRINT "Rate limited - try again later"
        RETURN NULL
    END IF
END FUNCTION

' Batch operations - acquire multiple tokens
IF limiter.TryAcquireN(5) THEN
    ' Process batch of 5 items
    ProcessBatch(items)
END IF

' Check available capacity
PRINT "Available: "; limiter.Available; " / "; limiter.Max
PRINT "Refill rate: "; limiter.Rate; " tokens/sec"

' Reset to full capacity
limiter.Reset()
```

### Token Bucket Algorithm

The token bucket algorithm works like a bucket that:
- Holds up to `maxTokens` tokens
- Refills at `refillPerSec` tokens per second (continuously)
- Operations take tokens from the bucket
- If the bucket is empty, operations are denied

This allows:
- **Burst capacity:** Up to `maxTokens` operations in a burst
- **Sustained rate:** `refillPerSec` operations per second on average
- **Smooth throttling:** Natural backpressure as tokens drain

### Use Cases

- **API rate limiting:** Enforce rate limits on outbound API calls
- **Request throttling:** Limit incoming request processing rate
- **Resource protection:** Prevent resource exhaustion from burst traffic
- **Fair scheduling:** Distribute capacity across multiple consumers

---

## See Also

- [Collections](collections/README.md) - `Bytes`, `Map`, `Seq` types used by network classes
- [Input/Output](io/README.md) - File operations for saving downloaded content
- [Cryptography](crypto.md) - `Tls` for secure connections

> **Note:** `Viper.Crypto.Tls` provides a low-level TLS 1.3 client API (connect/send/recv/close) that can be used independently of the HTTP layer. It supports ChaCha20-Poly1305 encryption with X25519 key exchange. Documentation for this class is in `crypto.md`. Certificate chain validation is not yet implemented; connections are encrypted but unauthenticated.

