# Networking Subsystem

**Status:** Complete microkernel networking with user-space TCP/IP stack, TLS 1.3, HTTP, SSH-2
**Architecture:** User-space server (netd) + client libraries
**Total SLOC:** ~11,700

## Overview

In the ViperDOS microkernel architecture, networking is implemented entirely in user-space:

1. **netd server** (~3,200 SLOC): User-space TCP/IP stack with VirtIO-net driver
2. **libtls** (~2,150 SLOC): TLS 1.3 client library
3. **libhttp** (~560 SLOC): HTTP/1.1 client library
4. **libssh** (~5,800 SLOC): SSH-2 and SFTP client library

Applications use standard socket APIs (via libc), which route requests to netd via IPC channels.

---

## Architecture

```
┌─────────────────────────────────────────────────────────────────┐
│                       Applications                               │
│         (ssh, sftp, ping, curl-like tools, etc.)                │
└────────────────────────────────┬────────────────────────────────┘
                                 │
         ┌───────────────────────┼───────────────────────┐
         ▼                       ▼                       ▼
┌─────────────────┐  ┌─────────────────────┐  ┌─────────────────┐
│    libssh       │  │      libhttp        │  │    libtls       │
│  (SSH-2/SFTP)   │  │  (HTTP/1.1,HTTPS)   │  │   (TLS 1.3)     │
└────────┬────────┘  └────────┬────────────┘  └────────┬────────┘
         │                    │                        │
         └────────────────────┼────────────────────────┘
                              ▼
┌─────────────────────────────────────────────────────────────────┐
│                    libc (socket API)                             │
│         socket(), connect(), send(), recv(), etc.               │
└────────────────────────────────┬────────────────────────────────┘
                                 │ IPC (channels)
                                 ▼
┌─────────────────────────────────────────────────────────────────┐
│                         netd Server                              │
│  ┌─────────────────────────────────────────────────────────────┐│
│  │                    User-Space TCP/IP Stack                   ││
│  │  ┌───────────┐  ┌───────────┐  ┌───────────┐  ┌───────────┐ ││
│  │  │  TCP (32) │  │  UDP (16) │  │   ICMP    │  │    DNS    │ ││
│  │  └───────────┘  └───────────┘  └───────────┘  └───────────┘ ││
│  │  ┌─────────────────────────────────────────────────────────┐ ││
│  │  │                        IPv4                             │ ││
│  │  └─────────────────────────────────────────────────────────┘ ││
│  │  ┌───────────────────────────────┐  ┌───────────────────────┐││
│  │  │          Ethernet             │  │    ARP (16 entries)  │││
│  │  └───────────────────────────────┘  └───────────────────────┘││
│  └─────────────────────────────────────────────────────────────┘│
│                              │                                   │
│                    VirtIO-net Driver                            │
└────────────────────────────────┬────────────────────────────────┘
                                 │ MAP_DEVICE, IRQ
                                 ▼
┌─────────────────────────────────────────────────────────────────┐
│                        Kernel (EL1)                              │
│           Device primitives, IRQ routing, memory mapping         │
└─────────────────────────────────────────────────────────────────┘
                                 │
                                 ▼
┌─────────────────────────────────────────────────────────────────┐
│                    VirtIO-net Hardware                           │
└─────────────────────────────────────────────────────────────────┘
```

---

## netd Server

**Location:** `user/servers/netd/`
**SLOC:** ~3,200
**Registration:** `NETD:` (assign system)

### Components

| File           | Lines  | Description                              |
|----------------|--------|------------------------------------------|
| `main.cpp`     | ~920   | Server entry point, IPC message handling |
| `netstack.hpp` | ~600   | Network stack structures and API         |
| `netstack.cpp` | ~1,700 | TCP/IP stack implementation              |

### Initialization Sequence

1. Scan VirtIO MMIO range (0x0a000000-0x0a004000) for net device
2. Skip devices already claimed (status != 0)
3. Initialize VirtIO-net with IRQ registration
4. Configure network interface (IP, netmask, gateway, DNS)
5. Create service channel and register as "NETD:"
6. Enter server loop processing IPC requests

### Network Interface Configuration

QEMU virt default configuration:

| Parameter  | Value         |
|------------|---------------|
| IP Address | 10.0.2.15     |
| Netmask    | 255.255.255.0 |
| Gateway    | 10.0.2.2      |
| DNS Server | 10.0.2.3      |

### IPC Protocol

**Namespace:** `netproto`
**Message Size:** 256 bytes max (channel limit)

#### Socket Operations

| Message Type       | Value | Description                |
|--------------------|-------|----------------------------|
| NET_SOCKET_CREATE  | 1     | Create socket (TCP/UDP)    |
| NET_SOCKET_CONNECT | 2     | Connect to remote host     |
| NET_SOCKET_BIND    | 3     | Bind to local port         |
| NET_SOCKET_LISTEN  | 4     | Listen for connections     |
| NET_SOCKET_ACCEPT  | 5     | Accept incoming connection |
| NET_SOCKET_SEND    | 6     | Send data (inline or SHM)  |
| NET_SOCKET_RECV    | 7     | Receive data               |
| NET_SOCKET_CLOSE   | 8     | Close socket               |
| NET_SOCKET_STATUS  | 10    | Query socket readiness     |

#### DNS and Diagnostics

| Message Type         | Value | Description                |
|----------------------|-------|----------------------------|
| NET_DNS_RESOLVE      | 20    | Resolve hostname to IPv4   |
| NET_PING             | 40    | ICMP echo request          |
| NET_STATS            | 41    | Get network statistics     |
| NET_INFO             | 42    | Get network configuration  |
| NET_SUBSCRIBE_EVENTS | 43    | Subscribe to socket events |

#### Data Transfer Modes

**Inline Data (≤200 bytes):**

- Data included directly in message payload
- Used for most small transfers

**Shared Memory (>200 bytes):**

- SHM handle transferred with message
- netd maps SHM, reads/writes, unmaps
- Efficient for large transfers

---

## Protocol Stack (netd)

### Ethernet Layer

- Frame construction and transmission
- Ethertype-based demultiplexing (IPv4=0x0800, ARP=0x0806)
- Minimum frame padding to 60 bytes
- MAC address filtering

### ARP Layer

- 16-entry ARP cache
- ARP request generation
- ARP reply processing
- Gateway MAC resolution

### IPv4 Layer

- Header parsing and validation
- Header checksum computation/verification
- Protocol demultiplexing (ICMP=1, TCP=6, UDP=17)
- TTL handling
- Routing via gateway

### ICMP

- Echo request (ping) generation
- Echo reply processing
- Ping with timeout callback

### UDP

- 16 socket table
- Port binding
- Datagram send/receive
- UDP checksum computation
- Used for DNS resolution

### TCP

**Connections:** 32 maximum

**Features:**

- Full state machine (CLOSED through TIME_WAIT)
- Active open (connect)
- Passive open (listen/accept)
- 8-connection backlog per listening socket
- Data transmission with segmentation
- 8KB receive ring buffer per connection
- 8KB send buffer per connection
- Sequence number tracking
- ACK generation
- FIN/RST handling

**TCP States:**

| State        | Description                  |
|--------------|------------------------------|
| CLOSED       | No connection                |
| LISTEN       | Waiting for SYN              |
| SYN_SENT     | SYN sent, awaiting SYN-ACK   |
| SYN_RECEIVED | SYN received, SYN-ACK sent   |
| ESTABLISHED  | Data transfer active         |
| FIN_WAIT_1/2 | Active close in progress     |
| CLOSE_WAIT   | Remote closed, local pending |
| CLOSING      | Both sides closing           |
| LAST_ACK     | Awaiting final ACK           |
| TIME_WAIT    | Waiting before reuse         |

### DNS

- A record resolution
- UDP-based queries to configured DNS server
- Transaction ID tracking
- Single pending query at a time

---

## libtls (TLS 1.3)

**Location:** `user/libtls/`
**SLOC:** ~2,150

### Features

- TLS 1.3 client (RFC 8446)
- ChaCha20-Poly1305 AEAD cipher
- X25519 key exchange
- SHA-256 hashing
- Server Name Indication (SNI)
- Certificate verification (optional)

### Crypto Components

| File       | Lines  | Description                         |
|------------|--------|-------------------------------------|
| `crypto.c` | ~1,170 | ChaCha20, Poly1305, X25519, SHA-256 |
| `tls.c`    | ~980   | TLS handshake, record layer         |

### API

```c
/* Create session over existing socket */
tls_session_t *tls_new(int socket_fd, const tls_config_t *config);

/* Perform TLS 1.3 handshake */
int tls_handshake(tls_session_t *session);

/* Send/receive encrypted data */
long tls_send(tls_session_t *session, const void *data, size_t len);
long tls_recv(tls_session_t *session, void *buffer, size_t len);

/* Close session */
void tls_close(tls_session_t *session);

/* Convenience: connect + handshake */
tls_session_t *tls_connect(const char *host, uint16_t port,
                            const tls_config_t *config);
```

### Configuration

```c
typedef struct tls_config {
    const char *hostname;  /* SNI hostname */
    int verify_cert;       /* 1 = verify (default), 0 = skip */
    int timeout_ms;        /* Timeout (0 = default) */
} tls_config_t;
```

---

## libhttp (HTTP Client)

**Location:** `user/libhttp/`
**SLOC:** ~560

### Features

- HTTP/1.1 client
- HTTPS via libtls
- GET, POST, PUT, DELETE, HEAD methods
- Header parsing
- Chunked transfer encoding
- Redirect following (configurable)
- Custom headers

### API

```c
/* Simple GET request */
int http_get(const char *url, http_response_t *response);

/* Full request with configuration */
int http_request(const http_request_t *request, http_response_t *response);

/* Free response resources */
void http_response_free(http_response_t *response);
```

### Response Structure

```c
typedef struct http_response {
    int status_code;           /* e.g., 200 */
    char status_text[64];      /* e.g., "OK" */
    http_header_t headers[32]; /* Response headers */
    int header_count;
    char *body;                /* Response body (malloc'd) */
    size_t body_len;
    size_t content_length;
    char content_type[128];
    int chunked;
} http_response_t;
```

---

## libssh (SSH-2/SFTP)

**Location:** `user/libssh/`
**SLOC:** ~5,800

### Features

- SSH-2 protocol (RFC 4253)
- Password authentication
- Public key authentication (Ed25519)
- Interactive channel
- SFTP subsystem (RFC 4254)

### Components

| File            | Lines  | Description                      |
|-----------------|--------|----------------------------------|
| `ssh.c`         | ~1,370 | SSH connection, handshake        |
| `ssh_auth.c`    | ~860   | Authentication methods           |
| `ssh_channel.c` | ~740   | Channel management               |
| `ssh_crypto.c`  | ~1,300 | Crypto (AES-CTR, SHA-1, Ed25519) |
| `sftp.c`        | ~1,500 | SFTP protocol                    |

### Crypto Algorithms

| Algorithm   | Usage                 |
|-------------|-----------------------|
| AES-128-CTR | Bulk encryption       |
| SHA-1       | Legacy hashing        |
| SHA-256     | Key exchange hash     |
| Ed25519     | Host key verification |
| Curve25519  | Key exchange          |

### SFTP Operations

- Directory listing (ls)
- File download (get)
- File upload (put)
- File deletion (rm)
- Directory creation (mkdir)
- Directory removal (rmdir)
- Rename (mv)
- Stat (file info)

---

## libc Integration

The libc socket functions route to netd via IPC:

**Location:** `user/libc/src/netd_backend.cpp`

### Socket API Mapping

| libc Function  | netd Message       |
|----------------|--------------------|
| socket()       | NET_SOCKET_CREATE  |
| connect()      | NET_SOCKET_CONNECT |
| bind()         | NET_SOCKET_BIND    |
| listen()       | NET_SOCKET_LISTEN  |
| accept()       | NET_SOCKET_ACCEPT  |
| send()/write() | NET_SOCKET_SEND    |
| recv()/read()  | NET_SOCKET_RECV    |
| close()        | NET_SOCKET_CLOSE   |
| poll()         | NET_SOCKET_STATUS  |

### Connection Flow

1. Application calls `socket(AF_INET, SOCK_STREAM, 0)`
2. libc sends NET_SOCKET_CREATE to netd
3. netd allocates TcpConnection slot, returns socket ID
4. libc returns socket ID as file descriptor
5. Subsequent operations use this socket ID

---

## Statistics

netd tracks and reports:

| Counter     | Description            |
|-------------|------------------------|
| tx_packets  | Packets transmitted    |
| rx_packets  | Packets received       |
| tx_bytes    | Bytes transmitted      |
| rx_bytes    | Bytes received         |
| tcp_conns   | Active TCP connections |
| udp_sockets | Active UDP sockets     |

---

## Performance

### Latency (QEMU)

| Operation           | Typical Time |
|---------------------|--------------|
| Socket create (IPC) | ~50μs        |
| TCP connect (local) | ~1-5ms       |
| DNS resolution      | ~10-50ms     |
| TLS handshake       | ~50-200ms    |
| Socket send/recv    | ~100μs       |

### Limitations

| Resource            | Limit     |
|---------------------|-----------|
| TCP connections     | 32        |
| UDP sockets         | 16        |
| ARP cache entries   | 16        |
| Inline message data | 200 bytes |
| TCP RX buffer       | 8KB       |
| TCP TX buffer       | 8KB       |
| UDP RX buffer       | 4KB       |

---

## Recent Improvements

- **TCP Flow Control**: Proper window management for reliable data transfer
- **TCP Congestion Control**: Basic congestion window management
- **TCP Retransmission**: Timeout-based retransmission with exponential backoff
- **Improved SSH/SFTP**: More stable connections with proper window handling

## Not Implemented

### High Priority

- ~~TCP window scaling (RFC 7323)~~ ✓ Basic flow control implemented
- ~~TCP congestion control (RFC 5681)~~ ✓ Basic implementation
- TCP SACK (RFC 2018)
- IP fragmentation/reassembly

### Medium Priority

- IPv6
- ~~TCP retransmission with backoff~~ ✓ Implemented
- TCP TIME_WAIT with 2MSL
- Keep-alive

### Low Priority

- Raw sockets
- Multicast
- DHCP client
- TLS 1.2 fallback
- TLS session resumption

---

## Priority Recommendations: Next 5 Steps

### 1. IPv6 Support

**Impact:** Modern network compatibility

- IPv6 header parsing and generation
- ICMPv6 for neighbor discovery (NDP)
- Stateless address autoconfiguration (SLAAC)
- Dual-stack operation (IPv4 + IPv6)

### 2. TCP SACK (Selective Acknowledgment)

**Impact:** Better performance on lossy networks

- RFC 2018 SACK option parsing
- Selective retransmission of lost segments
- Improved throughput on high-latency links
- Required for modern TCP performance

### 3. DHCP Client

**Impact:** Automatic network configuration

- DHCP discover/offer/request/ack
- Obtain IP, gateway, DNS automatically
- Lease renewal handling
- Zero-config network setup

### 4. TLS Session Resumption

**Impact:** Faster subsequent HTTPS connections

- Session ID caching for resumption
- 0-RTT data with early data
- Reduced handshake latency
- Better user experience for web access

### 5. SO_RCVBUF/SO_SNDBUF Socket Options

**Impact:** Application-controlled buffer sizing

- Per-socket buffer configuration
- setsockopt()/getsockopt() support
- Better memory utilization
- Performance tuning for specific workloads
