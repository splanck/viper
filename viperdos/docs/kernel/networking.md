# Networking Stack

ViperDOS provides networking through a layered architecture that supports both kernel-mode and microkernel-mode
operation.

## Architecture Modes

### Microkernel Mode (Default)

In microkernel mode (`VIPER_MICROKERNEL_MODE=1`, `VIPER_KERNEL_ENABLE_NET=0`), networking runs entirely in user space:

```
┌─────────────────────────────────────────────────────────────┐
│                    User Applications                         │
│           (vinit, ssh, fetch, etc.)                         │
├─────────────────────────────────────────────────────────────┤
│    libc          │    libtls      │    libssh              │
│   socket()       │   TLS 1.3      │   SSH-2 + SFTP         │
│   connect()      │   X.509        │   Ed25519/RSA          │
├──────────────────┴────────────────┴─────────────────────────┤
│                     netd Server                              │
│   VirtIO-net  │  Ethernet  │  ARP  │  IPv4  │  TCP/UDP     │
│   ~3,200 SLOC including libnetclient                        │
├─────────────────────────────────────────────────────────────┤
│                    Microkernel                               │
│        IPC channels, device syscalls, shared memory          │
└─────────────────────────────────────────────────────────────┘
```

**Key components:**

- **netd server** (`user/servers/netd/`): User-space TCP/IP stack with VirtIO-net driver
- **libtls** (`user/libtls/`): TLS 1.3 client library
- **libhttp** (`user/libhttp/`): HTTP/1.1 client
- **libssh** (`user/libssh/`): SSH-2 client with SFTP support
- **libnetclient** (`user/libnetclient/`): IPC client library for netd

### Kernel Mode (Optional)

When `VIPER_KERNEL_ENABLE_NET=1`, the full network stack runs in kernel space. This mode is useful for debugging
and development but is not the default.

Key files (kernel mode):

- `kernel/net/network.*`: Network initialization and polling
- `kernel/net/eth/*`: Ethernet and ARP
- `kernel/net/ip/*`: IPv4, ICMP, UDP, TCP
- `kernel/net/dns/*`: DNS resolver
- `kernel/net/http/*`: HTTP client
- `kernel/net/tls/*`: TLS 1.3 (when `VIPER_KERNEL_ENABLE_TLS=1`)

## netd Server Protocol

Applications communicate with netd via IPC channels using a message-based protocol:

### Socket Operations

| Message              | Code | Description                |
|----------------------|------|----------------------------|
| `NET_SOCKET_CREATE`  | 0x01 | Create TCP/UDP socket      |
| `NET_SOCKET_CONNECT` | 0x02 | Connect to remote host     |
| `NET_SOCKET_BIND`    | 0x03 | Bind to local address      |
| `NET_SOCKET_LISTEN`  | 0x04 | Listen for connections     |
| `NET_SOCKET_ACCEPT`  | 0x05 | Accept incoming connection |
| `NET_SOCKET_SEND`    | 0x06 | Send data                  |
| `NET_SOCKET_RECV`    | 0x07 | Receive data               |
| `NET_SOCKET_CLOSE`   | 0x08 | Close socket               |
| `NET_SOCKET_POLL`    | 0x09 | Poll socket for events     |

### DNS Operations

| Message           | Code | Description            |
|-------------------|------|------------------------|
| `NET_DNS_RESOLVE` | 0x14 | Resolve hostname to IP |

### Network Information

| Message        | Code | Description               |
|----------------|------|---------------------------|
| `NET_PING`     | 0x28 | ICMP echo request         |
| `NET_GET_INFO` | 0x29 | Get interface information |

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

## Device Syscalls for netd

The netd server uses device syscalls to access hardware:

| Syscall        | Number | Description                     |
|----------------|--------|---------------------------------|
| `map_device`   | 0x100  | Map VirtIO MMIO into user space |
| `irq_register` | 0x101  | Register for network IRQ        |
| `irq_wait`     | 0x102  | Wait for network interrupt      |
| `irq_ack`      | 0x103  | Acknowledge interrupt           |
| `dma_alloc`    | 0x104  | Allocate DMA buffer             |
| `virt_to_phys` | 0x106  | Get physical address for DMA    |

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

