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
| `Host`      | String  | Remote host name or IP address (read-only)   |
| `Port`      | Integer | Remote port number (read-only)               |
| `LocalPort` | Integer | Local port number (read-only)                |
| `IsOpen`    | Boolean | True if connection is open (read-only)       |
| `Available` | Integer | Bytes available to read without blocking     |

### Send Methods

| Method                | Returns | Description                                    |
|-----------------------|---------|------------------------------------------------|
| `Send(data)`          | Integer | Send Bytes, return number of bytes sent        |
| `SendStr(text)`       | Integer | Send string as UTF-8 bytes, return bytes sent  |
| `SendAll(data)`       | void    | Send all bytes, block until complete           |

### Receive Methods

| Method             | Returns | Description                                          |
|--------------------|---------|------------------------------------------------------|
| `Recv(maxBytes)`   | Bytes   | Receive up to maxBytes (may return fewer)            |
| `RecvStr(maxBytes)`| String  | Receive up to maxBytes as UTF-8 string               |
| `RecvExact(count)` | Bytes   | Receive exactly count bytes, block until complete    |
| `RecvLine()`       | String  | Receive until newline (LF or CRLF), strip newline    |

### Timeout and Close Methods

| Method                  | Returns | Description                               |
|-------------------------|---------|-------------------------------------------|
| `SetRecvTimeout(ms)`    | void    | Set receive timeout (0 = no timeout)      |
| `SetSendTimeout(ms)`    | void    | Set send timeout (0 = no timeout)         |
| `Close()`               | void    | Close the connection                      |

### Connection Options

The implementation enables the following socket options by default:

- **TCP_NODELAY:** Disables Nagle's algorithm for low-latency communication

### Example

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
| `Port`        | Integer | Listening port number (read-only)        |
| `Address`     | String  | Bound address (read-only)                |
| `IsListening` | Boolean | True if actively listening (read-only)   |

### Methods

| Method             | Returns | Description                                      |
|--------------------|---------|--------------------------------------------------|
| `Accept()`         | Tcp     | Accept connection, block until client connects   |
| `AcceptFor(ms)`    | Tcp     | Accept with timeout, returns null on timeout     |
| `Close()`          | void    | Stop listening and close the server              |

### Example

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
| `Port`    | Integer | Bound port number (0 if unbound)        |
| `Address` | String  | Bound address (empty if unbound)        |
| `IsBound` | Boolean | True if socket is bound (read-only)     |

### Send Methods

| Method                       | Returns | Description                           |
|------------------------------|---------|---------------------------------------|
| `SendTo(host, port, data)`   | Integer | Send Bytes to address, return bytes sent |
| `SendToStr(host, port, text)`| Integer | Send string as UTF-8, return bytes sent  |

### Receive Methods

| Method                     | Returns | Description                                     |
|----------------------------|---------|-------------------------------------------------|
| `Recv(maxBytes)`           | Bytes   | Receive packet (blocks)                         |
| `RecvFrom(maxBytes)`       | Bytes   | Receive and store sender info                   |
| `RecvFor(maxBytes, ms)`    | Bytes   | Receive with timeout, null on timeout           |
| `SenderHost()`             | String  | Host of last received packet (from RecvFrom)    |
| `SenderPort()`             | Integer | Port of last received packet (from RecvFrom)    |

### Options and Close

| Method                  | Returns | Description                                  |
|-------------------------|---------|----------------------------------------------|
| `SetBroadcast(enable)`  | void    | Enable/disable broadcast                     |
| `JoinGroup(addr)`       | void    | Join multicast group (224.0.0.0-239.255.255.255) |
| `LeaveGroup(addr)`      | void    | Leave multicast group                        |
| `SetRecvTimeout(ms)`    | void    | Set receive timeout (0 = no timeout)         |
| `Close()`               | void    | Close the socket                             |

### Example

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
| `ResolveAll(hostname)`  | Seq     | Resolve to all addresses (IPv4 and IPv6)     |
| `Resolve4(hostname)`    | String  | Resolve to first IPv4 address only           |
| `Resolve6(hostname)`    | String  | Resolve to first IPv6 address only           |
| `Reverse(ipAddress)`    | String  | Reverse DNS lookup, return hostname          |

### Validation Methods

| Method              | Returns | Description                                  |
|---------------------|---------|----------------------------------------------|
| `IsIPv4(address)`   | Boolean | Check if string is valid IPv4 address        |
| `IsIPv6(address)`   | Boolean | Check if string is valid IPv6 address        |
| `IsIP(address)`     | Boolean | Check if valid IPv4 or IPv6 address          |

### Local Info Methods

| Method         | Returns | Description                                  |
|----------------|---------|----------------------------------------------|
| `LocalHost()`  | String  | Get local machine hostname                   |
| `LocalAddrs()` | Seq     | Get all local IP addresses                   |

### Example

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
| `Get(url)`                     | String  | GET request, return response body as string  |
| `GetBytes(url)`                | Bytes   | GET request, return response body as bytes   |
| `Post(url, body)`              | String  | POST request with string body                |
| `PostBytes(url, body)`         | Bytes   | POST request with Bytes body                 |
| `Download(url, destPath)`      | Boolean | Download file to destination path            |
| `Head(url)`                    | Map     | HEAD request, return headers as Map          |

### Example

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
- **HTTPS support** - TLS 1.3 with automatic certificate verification
- **Redirect handling** - Automatically follows 301, 302, 307, 308 redirects (up to 5)
- **Content-Length** - Handles Content-Length bodies
- **Chunked encoding** - Handles Transfer-Encoding: chunked responses
- **Timeout** - Default 30 second timeout

### HTTPS/TLS Support

The HTTP client transparently supports HTTPS URLs using TLS 1.3:

- **Automatic upgrade** - URLs starting with `https://` automatically use TLS
- **Certificate verification** - Server certificates are validated by default
- **Modern security** - Uses TLS 1.3 with ChaCha20-Poly1305 cipher suite
- **X25519 key exchange** - Secure elliptic curve Diffie-Hellman

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
- Traps on TLS handshake failure (certificate errors, protocol errors)
- Traps on timeout
- Traps on too many redirects (>5)

### Limitations

- **IPv4 only** - IPv6 addresses not supported in URLs
- **No cookies** - Cookie handling not included
- **No auth** - Use `HttpReq` for custom headers including Authorization
- **No client certificates** - Client-side TLS certificates not supported

---

## Viper.Network.HttpReq

HTTP request builder for advanced requests with custom headers and options.

**Type:** Instance class

**Constructor:**

- `Viper.Network.HttpReq.New(method, url)` - Create request with method and URL

### Methods

| Method                    | Returns | Description                                  |
|---------------------------|---------|----------------------------------------------|
| `SetHeader(name, value)`  | HttpReq | Set a request header (chainable)             |
| `SetBody(data)`           | HttpReq | Set request body as Bytes (chainable)        |
| `SetBodyStr(text)`        | HttpReq | Set request body as string (chainable)       |
| `SetTimeout(ms)`          | HttpReq | Set request timeout in milliseconds          |
| `Send()`                  | HttpRes | Execute the request and return response      |

### Example

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

### Example

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
| `Scheme`   | String  | URL scheme (http, https, ftp, etc.)              |
| `User`     | String  | Username (optional)                              |
| `Pass`     | String  | Password (optional)                              |
| `Host`     | String  | Hostname or IP address                           |
| `Port`     | Integer | Port number (0 = not specified)                  |
| `Path`     | String  | Path component (with leading /)                  |
| `Query`    | String  | Query string (without leading ?)                 |
| `Fragment` | String  | Fragment (without leading #)                     |

### Computed Properties (Read-Only)

| Property    | Type   | Description                                         |
|-------------|--------|-----------------------------------------------------|
| `Authority` | String | Full authority: `user:pass@host:port`               |
| `HostPort`  | String | `host:port` (port omitted if default for scheme)    |
| `Full`      | String | Complete URL string                                 |

### Query Parameter Methods

| Method                           | Returns | Description                        |
|----------------------------------|---------|------------------------------------|
| `SetQueryParam(name, value)`     | Url     | Set or update query parameter      |
| `GetQueryParam(name)`            | String  | Get query parameter value          |
| `HasQueryParam(name)`            | Boolean | Check if parameter exists          |
| `DelQueryParam(name)`            | Url     | Remove query parameter             |
| `QueryMap()`                     | Map     | Get all parameters as Map          |

### Other Methods

| Method              | Returns | Description                                  |
|---------------------|---------|----------------------------------------------|
| `Resolve(relative)` | Url     | Resolve relative URL against this base       |
| `Clone()`           | Url     | Create a copy of this URL                    |

### Static Utility Methods

| Method                | Returns | Description                              |
|-----------------------|---------|------------------------------------------|
| `Encode(text)`        | String  | Percent-encode text for URL              |
| `Decode(text)`        | String  | Decode percent-encoded text              |
| `EncodeQuery(map)`    | String  | Encode Map as query string               |
| `DecodeQuery(query)`  | Map     | Parse query string to Map                |
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

## See Also

- [Collections](collections.md) - `Bytes`, `Map`, `Seq` types used by network classes
- [Input/Output](io.md) - File operations for saving downloaded content

