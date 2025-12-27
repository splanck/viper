# Networking Subsystem

**Status:** Production-ready TCP/IP stack with TLS 1.3, congestion control, and interrupt-driven I/O
**Location:** `kernel/net/`
**SLOC:** ~15,500

## Overview

The networking subsystem provides a complete TCP/IP stack including Ethernet framing, ARP, IPv4, ICMP, UDP, TCP with RFC 5681 congestion control and out-of-order reassembly, DNS resolution, TLS 1.3, and HTTP client. The implementation supports interrupt-driven packet reception via GIC-registered IRQs for efficient blocking I/O.

---

## Protocol Layers

### 1. Ethernet (`eth/ethernet.cpp`, `ethernet.hpp`)

**Status:** Complete Layer 2 framing

**Implemented:**
- Ethernet II frame construction and transmission
- Frame reception and validation
- Ethertype-based demultiplexing (IPv4, ARP)
- Minimum frame padding (to 60 bytes)
- Destination MAC filtering (unicast, broadcast, multicast)

**Frame Structure:**
```
┌──────────┬──────────┬───────────┬─────────────┐
│  Dst MAC │  Src MAC │ Ethertype │   Payload   │
│  6 bytes │  6 bytes │  2 bytes  │ 46-1500 B   │
└──────────┴──────────┴───────────┴─────────────┘
```

**Supported Ethertypes:**
| Value | Protocol |
|-------|----------|
| 0x0800 | IPv4 |
| 0x0806 | ARP |
| 0x86DD | IPv6 (defined, not supported) |

---

### 2. ARP (`eth/arp.cpp`, `arp.hpp`)

**Status:** Complete IPv4-over-Ethernet ARP

**Implemented:**
- ARP cache with 32 entries
- ARP request generation
- ARP reply processing
- ARP request handling (reply to queries for our IP)
- Cache entry timeout (300 seconds)
- Gateway MAC resolution

**ARP Cache Entry:**
| Field | Description |
|-------|-------------|
| ip | IPv4 address |
| mac | MAC address |
| valid | Entry in use |
| timestamp | Last update time |

---

### 3. IPv4 (`ip/ipv4.cpp`, `ipv4.hpp`)

**Status:** Complete basic IPv4

**Implemented:**
- IPv4 header parsing and validation
- Header checksum computation/verification
- Protocol demultiplexing (ICMP=1, UDP=17, TCP=6)
- Packet transmission with ARP resolution
- TTL handling
- Destination address filtering

**IPv4 Header:**
```
┌────────┬────────┬───────────┬───────────────┐
│Ver/IHL │TOS/DSCP│Total Len  │Identification │
├────────┴────────┴───────────┴───────────────┤
│Flags │ Fragment Offset │ TTL │ Protocol    │
├────────────────────────┴─────┴──────────────┤
│        Header Checksum                      │
├─────────────────────────────────────────────┤
│              Source IP Address              │
├─────────────────────────────────────────────┤
│           Destination IP Address            │
└─────────────────────────────────────────────┘
```

**Not Implemented:**
- IP fragmentation/reassembly
- IP options processing
- Multicast routing
- IGMP

---

### 4. ICMP (`ip/icmp.cpp`, `icmp.hpp`)

**Status:** Complete echo request/reply

**Implemented:**
- Echo request (ping) generation
- Echo reply processing
- Ping with callback
- ICMP checksum computation
- Basic error message handling

**Supported Types:**
| Type | Description |
|------|-------------|
| 0 | Echo Reply |
| 8 | Echo Request |

---

### 5. UDP (`ip/udp.cpp`, `udp.hpp`)

**Status:** Complete for DNS support

**Implemented:**
- UDP socket table (16 sockets)
- Port binding
- Datagram send/receive
- UDP checksum computation
- Non-blocking receive with polling

**UDP Socket:**
| Field | Description |
|-------|-------------|
| local_port | Bound port |
| in_use | Socket allocated |
| rx_buffer | Received datagram buffer |
| rx_len | Received data length |
| rx_src_ip | Source IP of last packet |
| rx_src_port | Source port of last packet |

---

### 6. TCP (`ip/tcp.cpp`, `tcp.hpp`)

**Status:** Production-ready with congestion control and OOO reassembly

**Implemented:**
- TCP socket table (16 sockets)
- Full state machine (CLOSED through TIME_WAIT)
- Active open (connect)
- Passive open (listen/accept)
- Data transmission with segmentation
- Receive ring buffer (4KB per socket)
- Sequence number tracking
- ACK generation
- FIN/RST handling
- MSS option negotiation (send and receive)
- Retransmission with exponential backoff
- Unacknowledged data tracking (1460 bytes)
- Configurable RTO (1-60 seconds)
- Connection statistics (active count, listen count)
- **RFC 5681 Congestion Control:**
  - Slow start (cwnd < ssthresh)
  - Congestion avoidance (cwnd >= ssthresh)
  - Fast retransmit on 3 duplicate ACKs
  - Fast recovery with cwnd inflation
  - ssthresh adjustment on loss
- **Out-of-Order Reassembly:**
  - 8-segment OOO queue per socket
  - Segment buffering by sequence number
  - Automatic delivery when gaps fill
  - Duplicate segment detection
- **Interrupt-Driven Receive:**
  - Blocking socket_recv with task wait queue
  - IRQ-triggered wakeup on packet arrival

**TCP States:**
```
CLOSED ─────────────────────────────────────────┐
   │                                            │
   ├──► LISTEN (passive open)                   │
   │       │                                    │
   │       └──► SYN_RECEIVED ──► ESTABLISHED ◄─┤
   │                                   │       │
   ├──► SYN_SENT (active open) ────────┘       │
   │                                           │
   │  ESTABLISHED                              │
   │       │                                   │
   │       ├──► FIN_WAIT_1 ──► FIN_WAIT_2 ─────┤
   │       │                                   │
   │       └──► CLOSE_WAIT ──► LAST_ACK ───────┤
   │                                           │
   └───────────────────────────────────────────┘
```

**TcpSocket Structure:**
| Field | Description |
|-------|-------------|
| state | Current TCP state |
| local_port | Local port |
| remote_port | Remote port |
| remote_ip | Remote IP address |
| snd_una | Send unacknowledged |
| snd_nxt | Send next |
| rcv_nxt | Receive next expected |
| rx_buffer[4096] | Receive ring buffer |
| tx_buffer[4096] | Transmit buffer |
| cwnd | Congestion window (bytes) |
| ssthresh | Slow start threshold |
| dup_acks | Duplicate ACK counter |
| ooo_queue[8] | Out-of-order segment queue |

**Congestion Control (RFC 5681):**
| Phase | cwnd Update | Trigger |
|-------|-------------|---------|
| Slow Start | cwnd += MSS per ACK | cwnd < ssthresh |
| Congestion Avoidance | cwnd += MSS*MSS/cwnd | cwnd >= ssthresh |
| Fast Retransmit | retransmit + fast recovery | 3 dup ACKs |
| Timeout | cwnd = MSS, ssthresh = cwnd/2 | RTO expiry |

**Out-of-Order Queue:**
| Field | Description |
|-------|-------------|
| seq | Sequence number of segment |
| len | Segment data length |
| valid | Slot in use |
| data[1460] | Segment payload |

**Socket API:**
| Function | Description |
|----------|-------------|
| `socket_create()` | Allocate socket |
| `socket_bind(sock, port)` | Bind to port |
| `socket_listen(sock)` | Enter listening state |
| `socket_accept(sock)` | Accept connection |
| `socket_connect(sock, ip, port)` | Connect to server |
| `socket_send(sock, data, len)` | Send data |
| `socket_recv(sock, buf, len)` | Receive data (blocking) |
| `socket_close(sock)` | Close socket |
| `socket_connected(sock)` | Check if connected |
| `socket_available(sock)` | Bytes in rx buffer |

**Not Implemented:**
- TCP window scaling option
- TCP timestamps option
- TIME_WAIT timer (2MSL)
- Urgent data
- Keep-alive
- SACK (Selective Acknowledgment)

---

### 7. DNS (`dns/dns.cpp`, `dns.hpp`)

**Status:** Complete A record resolver

**Implemented:**
- DNS query construction (A records)
- UDP-based resolution
- DNS response parsing
- TTL-based caching (16 entries)
- QEMU default DNS server (10.0.2.3)
- Name compression handling

**Cache Entry:**
| Field | Description |
|-------|-------------|
| hostname[64] | Queried hostname |
| addr | Resolved IPv4 |
| expires | Expiration timestamp |
| valid | Entry in use |

**API:**
| Function | Description |
|----------|-------------|
| `dns_init()` | Initialize resolver |
| `resolve(hostname, result, timeout)` | Resolve A record |

---

### 8. TLS 1.3 (`tls/`)

**Status:** Complete TLS 1.3 client with certificate verification

This is a substantial subsystem (~8,000 lines) providing:

**Implemented:**
- TLS 1.3 handshake (client-only)
- X25519 key exchange
- ChaCha20-Poly1305 AEAD
- AES-128-GCM AEAD
- SHA-256 and SHA-384 hashing
- HKDF key derivation
- TLS record layer (framing, encryption)
- Application data send/receive
- X.509 certificate parsing (ASN.1/DER)
- Certificate chain verification
- RSA signature verification (PKCS#1 v1.5)
- CA certificate store (roots.der)
- SNI (Server Name Indication)
- close_notify alert

**Crypto Components:**
| Component | Location | Description |
|-----------|----------|-------------|
| SHA-256 | `crypto/sha256.cpp` | Hash function |
| SHA-384 | `crypto/sha384.cpp` | Hash function |
| X25519 | `crypto/x25519.cpp` | Key exchange |
| ChaCha20-Poly1305 | `crypto/chacha20.cpp` | AEAD cipher |
| AES-GCM | `crypto/aes_gcm.cpp` | AEAD cipher |
| HKDF | `crypto/hkdf.cpp` | Key derivation |
| Random | `crypto/random.cpp` | RNG (virtio-rng) |

**Certificate Handling:**
| Component | Location | Description |
|-----------|----------|-------------|
| ASN.1 | `asn1/asn1.cpp` | DER parser |
| X.509 | `cert/x509.cpp` | Certificate parsing |
| CA Store | `cert/ca_store.cpp` | Root CA loading |
| Verify | `cert/verify.cpp` | Chain verification |

**TlsSession Structure:**
| Field | Description |
|-------|-------------|
| socket_fd | Underlying TCP socket |
| state | Handshake state |
| config | SNI, verify settings |
| client_private_key[32] | X25519 private key |
| client_public_key[32] | X25519 public key |
| transcript | SHA-256 handshake hash |
| handshake_secret[32] | Derived secret |
| cipher_suite | Negotiated suite |

**TLS API:**
| Function | Description |
|----------|-------------|
| `tls_init(session, sock, config)` | Initialize session |
| `tls_handshake(session)` | Perform handshake |
| `tls_send(session, data, len)` | Send app data |
| `tls_recv(session, buf, len)` | Receive app data |
| `tls_close(session)` | Close session |
| `tls_is_connected(session)` | Check connection |

**Cipher Suites Supported:**
- TLS_CHACHA20_POLY1305_SHA256 (0x1303)
- TLS_AES_128_GCM_SHA256 (0x1301)

**Not Implemented:**
- TLS 1.2 or earlier
- Server mode
- ECDSA certificate verification
- Client certificates
- Session resumption (PSK)
- Early data (0-RTT)
- Renegotiation

---

### 9. HTTP (`http/http.cpp`, `http.hpp`)

**Status:** Complete HTTP/1.0 client

**Implemented:**
- DNS resolution of hostname
- TCP connection to port 80
- GET request generation
- Response parsing (status, headers, body)
- Content-Type extraction
- Body buffering (up to 4KB)
- Timeout handling

**HttpResponse:**
| Field | Description |
|-------|-------------|
| status_code | HTTP status (e.g., 200) |
| content_type[64] | Content-Type header |
| content_length | Content-Length value |
| body[4096] | Response body |
| body_len | Actual body length |
| success | Request succeeded |
| error | Error message |

**API:**
| Function | Description |
|----------|-------------|
| `get(host, path, response, timeout)` | HTTP GET |
| `fetch(host, path)` | GET and print |

**Not Implemented:**
- HTTPS (use TLS layer directly)
- POST/PUT/DELETE
- Chunked transfer encoding
- Keep-alive connections
- Redirects
- Cookie handling

---

## Network Interface (`netif.cpp`, `netif.hpp`)

**Status:** Complete single-interface configuration

**Implemented:**
- Network interface structure
- QEMU virt default configuration:
  - IP: 10.0.2.15
  - Netmask: 255.255.255.0
  - Gateway: 10.0.2.2
  - DNS: 10.0.2.3
- MAC address from virtio-net

---

## Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                     Application Layer                        │
│              HTTP Client / HTTPS (via TLS)                   │
└──────────────────────────────┬──────────────────────────────┘
                               │
         ┌─────────────────────┼─────────────────────┐
         ▼                     ▼                     ▼
┌─────────────────┐  ┌─────────────────────┐  ┌─────────────┐
│      DNS        │  │     TLS 1.3         │  │   Direct    │
│  (UDP port 53)  │  │ (ChaCha20/AES-GCM)  │  │     TCP     │
└────────┬────────┘  └────────┬────────────┘  └──────┬──────┘
         │                    │                      │
         ▼                    ▼                      ▼
┌─────────────────────────────────────────────────────────────┐
│                    Transport Layer                           │
│           ┌───────────────┐    ┌───────────────┐            │
│           │      TCP      │    │      UDP      │            │
│           │ (16 sockets)  │    │ (16 sockets)  │            │
│           └───────────────┘    └───────────────┘            │
└──────────────────────────────┬──────────────────────────────┘
                               │
                               ▼
┌─────────────────────────────────────────────────────────────┐
│                    Network Layer                             │
│    ┌─────────────────────┐      ┌─────────────────────┐     │
│    │        IPv4         │      │        ICMP         │     │
│    │  (no fragmentation) │      │    (echo/reply)     │     │
│    └─────────────────────┘      └─────────────────────┘     │
└──────────────────────────────┬──────────────────────────────┘
                               │
                               ▼
┌─────────────────────────────────────────────────────────────┐
│                      Link Layer                              │
│    ┌─────────────────────┐      ┌─────────────────────┐     │
│    │      Ethernet       │      │        ARP          │     │
│    │   (14-byte header)  │      │   (32-entry cache)  │     │
│    └─────────────────────┘      └─────────────────────┘     │
└──────────────────────────────┬──────────────────────────────┘
                               │
                               ▼
┌─────────────────────────────────────────────────────────────┐
│                   VirtIO Network Device                      │
│                  (virtio-net RX/TX queues)                   │
└─────────────────────────────────────────────────────────────┘
```

---

## I/O Model

The network stack supports both polled and interrupt-driven operation:

### Interrupt-Driven Mode (Default)
1. VirtIO-net device registers IRQ with GIC (IRQ 48 + device offset)
2. On packet arrival, GIC triggers interrupt
3. IRQ handler calls `rx_irq_handler()`:
   - Process received packets
   - Wake blocked tasks in RX wait queue
4. Tasks using `socket_recv()` block until data arrives

### Polled Mode (Fallback)
`network_poll()` called from:
- Timer interrupt handler (background processing)
- Blocking TCP operations (active waiting)
- DNS resolution wait loops

### Packet Processing Sequence:
```
IRQ/Poll → virtio::net_poll() → eth::rx_frame()
                                      ↓
                              ip::rx_packet()
                                      ↓
                    ┌─────────────────┴─────────────────┐
                    ↓                                   ↓
           tcp::rx_segment()              udp::rx_datagram()
```

### RX Wait Queue
| Field | Description |
|-------|-------------|
| rx_waiters_[4] | Array of blocked tasks |
| rx_waiter_count_ | Number of waiting tasks |

Tasks calling `socket_recv()` with no data:
1. Register in RX wait queue
2. Set state to Blocked
3. Yield CPU
4. IRQ handler wakes when data arrives

---

## Files

| Directory | Description |
|-----------|-------------|
| `eth/` | Ethernet, ARP |
| `ip/` | IPv4, ICMP, UDP, TCP |
| `dns/` | DNS resolver |
| `http/` | HTTP client |
| `tls/` | TLS 1.3 stack |
| `tls/crypto/` | Cryptographic primitives |
| `tls/cert/` | X.509, CA store, verification |
| `tls/asn1/` | ASN.1/DER parser |

---

## Priority Recommendations

1. **Medium:** Add TCP window scaling for high-bandwidth connections
2. **Medium:** Implement IP fragmentation/reassembly
3. **Medium:** Add SACK support for improved loss recovery
4. **Low:** IPv6 support
5. **Low:** TLS session resumption
6. **Low:** TIME_WAIT timer with proper 2MSL delay
