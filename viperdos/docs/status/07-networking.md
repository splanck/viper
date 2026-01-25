# Networking Subsystem

**Status:** Complete kernel networking with TLS 1.3, HTTP, SSH-2
**Architecture:** Kernel-based TCP/IP stack + user-space libraries
**Total SLOC:** ~12,000 (kernel + libraries)

## Overview

ViperDOS implements networking directly in the kernel, with user-space libraries for higher-level protocols:

1. **Kernel TCP/IP stack**: Complete networking in kernel space
2. **Kernel TLS 1.3**: TLS encryption in kernel space
3. **libtls** (~2,150 SLOC): User-space TLS API wrapper
4. **libhttp** (~560 SLOC): HTTP/1.1 client library
5. **libssh** (~5,800 SLOC): SSH-2 and SFTP client library

Applications use standard socket syscalls, which are handled directly by the kernel.

---

## Architecture

```
┌─────────────────────────────────────────────────────────────────┐
│                       Applications                               │
│         (ssh, sftp, ping, fetch, vinit, etc.)                   │
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
                                 │ Syscalls (SVC)
                                 ▼
┌─────────────────────────────────────────────────────────────────┐
│                       Kernel (EL1)                               │
│  ┌─────────────────────────────────────────────────────────────┐│
│  │                    Kernel TCP/IP Stack                       ││
│  │  ┌───────────┐  ┌───────────┐  ┌───────────┐  ┌───────────┐ ││
│  │  │  TCP (32) │  │  UDP (16) │  │   ICMP    │  │    DNS    │ ││
│  │  └───────────┘  └───────────┘  └───────────┘  └───────────┘ ││
│  │  ┌───────────────────────────────────────────────────────┐   ││
│  │  │                   TLS 1.3 (kernel)                     │  ││
│  │  └───────────────────────────────────────────────────────┘   ││
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
                                 │
                                 ▼
┌─────────────────────────────────────────────────────────────────┐
│                    VirtIO-net Hardware                           │
└─────────────────────────────────────────────────────────────────┘
```

---

## Kernel Network Syscalls

| Syscall            | Number | Description                |
|--------------------|--------|----------------------------|
| SYS_SOCKET_CREATE  | 0x50   | Create TCP socket          |
| SYS_SOCKET_CONNECT | 0x51   | Connect to remote host     |
| SYS_SOCKET_SEND    | 0x52   | Send data on socket        |
| SYS_SOCKET_RECV    | 0x53   | Receive data from socket   |
| SYS_SOCKET_CLOSE   | 0x54   | Close socket               |
| SYS_DNS_RESOLVE    | 0x55   | Resolve hostname to IPv4   |
| SYS_SOCKET_POLL    | 0x56   | Poll socket for readiness  |
| SYS_TLS_CREATE     | 0xD0   | Create TLS session         |
| SYS_TLS_HANDSHAKE  | 0xD1   | Perform TLS handshake      |
| SYS_TLS_SEND       | 0xD2   | Send encrypted data        |
| SYS_TLS_RECV       | 0xD3   | Receive encrypted data     |
| SYS_TLS_CLOSE      | 0xD4   | Close TLS session          |
| SYS_TLS_INFO       | 0xD5   | Query TLS session info     |
| SYS_PING           | 0xE2   | ICMP ping with RTT         |
| SYS_NET_STATS      | 0xE1   | Get network statistics     |

---

## Kernel Protocol Stack

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
- Ping with timeout and RTT measurement

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
- Congestion control (slow start, congestion avoidance)
- Retransmission with exponential backoff

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
- Configurable DNS server (default: 10.0.2.3)

### TLS 1.3 (Kernel)

- TLS 1.3 client (RFC 8446)
- ChaCha20-Poly1305 AEAD cipher
- X25519 key exchange
- SHA-256 hashing
- Server Name Indication (SNI)
- Certificate verification with built-in root CAs

---

## Network Interface Configuration

QEMU virt default configuration:

| Parameter  | Value         |
|------------|---------------|
| IP Address | 10.0.2.15     |
| Netmask    | 255.255.255.0 |
| Gateway    | 10.0.2.2      |
| DNS Server | 10.0.2.3      |

---

## libtls (User-Space TLS API)

**Location:** `user/libtls/`
**SLOC:** ~2,150

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
- HTTPS via libtls (kernel TLS)
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

The libc socket functions call kernel syscalls directly:

**Location:** `user/libc/src/socket.c`

### Socket API Mapping

| libc Function  | Kernel Syscall     |
|----------------|--------------------|
| socket()       | SYS_SOCKET_CREATE  |
| connect()      | SYS_SOCKET_CONNECT |
| send()/write() | SYS_SOCKET_SEND    |
| recv()/read()  | SYS_SOCKET_RECV    |
| close()        | SYS_SOCKET_CLOSE   |
| poll()         | SYS_SOCKET_POLL    |

---

## Statistics

The kernel tracks and reports via SYS_NET_STATS:

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
| Socket create       | ~10μs        |
| TCP connect (local) | ~1-5ms       |
| DNS resolution      | ~10-50ms     |
| TLS handshake       | ~50-200ms    |
| Socket send/recv    | ~20μs        |

### Advantages of Kernel Networking

- Lower latency (no IPC overhead)
- Direct access to network buffers
- Integrated TLS acceleration
- Simpler application code

### Resource Limits

| Resource            | Limit     |
|---------------------|-----------|
| TCP connections     | 32        |
| UDP sockets         | 16        |
| ARP cache entries   | 16        |
| TCP RX buffer       | 8KB       |
| TCP TX buffer       | 8KB       |
| TLS sessions        | 16        |

---

## Shell Commands

The vinit shell provides the `Fetch` command for HTTP/HTTPS:

```
SYS:> Fetch example.com
SYS:> Fetch https://example.com
```

Network programs are run via the `Run` command:

```
SYS:> Run ping 10.0.2.2
SYS:> Run ssh user@example.com
SYS:> Run sftp user@example.com
SYS:> Run netstat
```

---

## Not Implemented

### High Priority

- IPv6
- TCP SACK (RFC 2018)
- IP fragmentation/reassembly

### Medium Priority

- TCP TIME_WAIT with 2MSL
- TCP keep-alive
- UDP multicast

### Low Priority

- Raw sockets
- DHCP client
- TLS 1.2 fallback
- TLS session resumption
- TLS server mode

---

## Priority Recommendations: Next Steps

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

### 3. DHCP Client

**Impact:** Automatic network configuration

- DHCP discover/offer/request/ack
- Obtain IP, gateway, DNS automatically
- Lease renewal handling

### 4. TLS Session Resumption

**Impact:** Faster subsequent HTTPS connections

- Session ticket caching
- 0-RTT early data
- Reduced handshake latency
