# Networking Stack

ViperDOS provides networking through a kernel-based TCP/IP stack with TLS 1.3 support.

## Architecture

The networking stack is implemented entirely in the kernel:

```
┌─────────────────────────────────────────────────────────────┐
│                    User Applications                         │
│           (vinit, ssh, sftp, ping, fetch, etc.)             │
├─────────────────────────────────────────────────────────────┤
│    libc          │    libtls      │    libssh              │
│   socket()       │   TLS API      │   SSH-2 + SFTP         │
│   connect()      │   wrapper      │   Ed25519/RSA          │
│      ↓           │      ↓         │                        │
│   Syscalls       │   Syscalls     │                        │
├──────────────────┴────────────────┴─────────────────────────┤
│                        Kernel                                │
│  ┌─────────────────────────────────────────────────────────┐│
│  │                    TLS 1.3 Stack                         ││
│  │   ChaCha20-Poly1305 │ X25519 │ SHA-256 │ Certs          ││
│  ├─────────────────────────────────────────────────────────┤│
│  │                    TCP/IP Stack                          ││
│  │   TCP (32 conns) │ UDP (16 sockets) │ ICMP │ DNS        ││
│  ├─────────────────────────────────────────────────────────┤│
│  │                    IPv4 Layer                            ││
│  │   Header parsing │ Checksum │ Routing                   ││
│  ├─────────────────────────────────────────────────────────┤│
│  │                    Ethernet/ARP                          ││
│  │   Frame handling │ ARP cache (16 entries)               ││
│  ├─────────────────────────────────────────────────────────┤│
│  │                    VirtIO-net Driver                     ││
│  │   TX/RX queues │ IRQ handling                           ││
│  └─────────────────────────────────────────────────────────┘│
└─────────────────────────────────────────────────────────────┘
```

**Key components:**

- **Kernel TCP/IP** (`kernel/net/`): Complete networking stack
- **Kernel TLS** (`kernel/net/tls/`): TLS 1.3 with certificate verification
- **VirtIO-net** (`kernel/drivers/virtio/net.*`): Network device driver
- **libtls** (`user/libtls/`): User-space TLS API wrapper
- **libhttp** (`user/libhttp/`): HTTP/1.1 client library
- **libssh** (`user/libssh/`): SSH-2 client with SFTP support

## Network Syscalls

Applications access networking via kernel syscalls:

### Socket Operations

| Syscall              | Number | Description            |
|----------------------|--------|------------------------|
| `SYS_SOCKET_CREATE`  | 0x50   | Create TCP socket      |
| `SYS_SOCKET_CONNECT` | 0x51   | Connect to remote host |
| `SYS_SOCKET_SEND`    | 0x52   | Send data              |
| `SYS_SOCKET_RECV`    | 0x53   | Receive data           |
| `SYS_SOCKET_CLOSE`   | 0x54   | Close socket           |
| `SYS_DNS_RESOLVE`    | 0x55   | Resolve hostname to IP |
| `SYS_SOCKET_POLL`    | 0x56   | Poll socket for events |

### TLS Operations

| Syscall             | Number | Description            |
|---------------------|--------|------------------------|
| `SYS_TLS_CREATE`    | 0xD0   | Create TLS session     |
| `SYS_TLS_HANDSHAKE` | 0xD1   | Perform TLS handshake  |
| `SYS_TLS_SEND`      | 0xD2   | Send encrypted data    |
| `SYS_TLS_RECV`      | 0xD3   | Receive decrypted data |
| `SYS_TLS_CLOSE`     | 0xD4   | Close TLS session      |
| `SYS_TLS_INFO`      | 0xD5   | Get session info       |

### Network Information

| Syscall         | Number | Description            |
|-----------------|--------|------------------------|
| `SYS_NET_STATS` | 0xE1   | Get network statistics |
| `SYS_PING`      | 0xE2   | ICMP ping with RTT     |

## Protocol Stack Layers

### Ethernet Layer

- Frame parsing and construction
- MAC address handling
- Ethertype dispatch (ARP: 0x0806, IPv4: 0x0800)

### ARP Layer

- Address resolution (IPv4 → MAC)
- ARP cache with timeout
- Gratuitous ARP support

### IPv4 Layer

- Packet parsing and validation
- Header checksum
- Protocol dispatch (ICMP: 1, TCP: 6, UDP: 17)
- Fragmentation (receive only)

### ICMP Layer

- Echo request/reply (ping)
- Destination unreachable handling

### UDP Layer

- Connectionless datagram service
- Used by DNS resolver

### TCP Layer

- Full connection state machine
- Sliding window flow control
- Retransmission with exponential backoff
- Congestion control (basic)

## libtls: TLS 1.3 Client

The TLS library provides secure connections:

**Supported cipher suites:**

- TLS_AES_128_GCM_SHA256 (0x1301)
- TLS_AES_256_GCM_SHA384 (0x1302)
- TLS_CHACHA20_POLY1305_SHA256 (0x1303)

**Key exchange:**

- X25519 (Curve25519 ECDH)

**Certificate verification:**

- X.509 parsing
- Chain validation
- Built-in root CA store

Key files:

- `user/libtls/src/tls.c`: TLS state machine
- `user/libtls/src/crypto.c`: Cryptographic primitives
- `user/libtls/src/x509.c`: Certificate parsing

## libssh: SSH-2 Client

The SSH library provides secure shell and file transfer:

**Supported algorithms:**

| Category     | Algorithms             |
|--------------|------------------------|
| Key Exchange | curve25519-sha256      |
| Host Key     | ssh-ed25519, ssh-rsa   |
| Encryption   | aes128-ctr, aes256-ctr |
| MAC          | hmac-sha256, hmac-sha1 |

**Features:**

- Password and public key authentication
- Interactive shell sessions
- Command execution
- SFTP v3 file transfer

Key files:

- `user/libssh/ssh.c`: Transport layer
- `user/libssh/ssh_auth.c`: Authentication
- `user/libssh/ssh_channel.c`: Channel management
- `user/libssh/sftp.c`: SFTP protocol

## Network Configuration

Default configuration for QEMU SLiRP networking:

| Parameter  | Value         |
|------------|---------------|
| IP Address | 10.0.2.15     |
| Netmask    | 255.255.255.0 |
| Gateway    | 10.0.2.2      |
| DNS Server | 10.0.2.3      |

## Current Limitations

- IPv6 not implemented
- TCP window scaling not implemented
- No DHCP client (static configuration only)
- Single network interface support
- No multicast/broadcast beyond ARP

