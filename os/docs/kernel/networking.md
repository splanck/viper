# Networking Stack (Ethernet → TCP, DNS, HTTP, TLS)

ViperOS includes a bring-up networking stack designed to work on QEMU’s `virt` machine with virtio-net:

- Ethernet framing and ARP
- IPv4 + ICMP
- UDP + TCP
- DNS resolution
- HTTP client
- TLS (for HTTPS)

This page tells the “packet journey” story and points to the code paths that implement each layer.

## Big picture: polling-driven receive loop

The entrypoint for network bring-up is `net::network_init()` in `kernel/net/network.cpp`.

When a virtio-net device exists:

1. `netif_init()` sets up interface state (MAC, IP configuration, etc.).
2. Each protocol layer is initialized in order: Ethernet → ARP → IP → ICMP/UDP/TCP → DNS → HTTP.

Receiving is driven by polling:

- `net::network_poll()` drains the virtio receive queue into a static buffer and calls `eth::rx_frame(...)` to
  demultiplex the frame.
- The timer interrupt handler calls `net::network_poll()` periodically, so incoming traffic is processed “in the
  background” during bring-up.

Key files:

- `kernel/net/network.hpp`
- `kernel/net/network.cpp`
- `kernel/drivers/virtio/net.*`
- `kernel/arch/aarch64/timer.cpp`

## Layer by layer: what happens to an incoming frame?

### 1) Ethernet

`kernel/net/eth/ethernet.*` parses Ethernet headers and dispatches based on ethertype (e.g. ARP vs IPv4).

### 2) ARP

`kernel/net/eth/arp.*` implements ARP requests/replies to map IPv4 addresses to MAC addresses on the local network
segment.

This is how the stack learns “what destination MAC do I put on an IPv4 packet to reach X?”.

### 3) IPv4

`kernel/net/ip/ipv4.*` parses IPv4 packets and dispatches to ICMP, UDP, or TCP.

### 4) ICMP (ping)

`kernel/net/ip/icmp.*` provides ping functionality used in `kernel/main.cpp` as a bring-up test (pinging the QEMU SLiRP
gateway `10.0.2.2`).

### 5) UDP

`kernel/net/ip/udp.*` implements UDP framing and is used by DNS.

### 6) TCP

`kernel/net/ip/tcp.*` implements TCP sockets with a kernel-facing API that the syscall layer exposes to user space.

The syscalls in `kernel/syscall/dispatch.cpp` call into `net::tcp::socket_create/connect/send/...`.

Key files:

- `kernel/net/ip/tcp.hpp`
- `kernel/net/ip/tcp.cpp`
- `kernel/syscall/dispatch.cpp`

## DNS and HTTP: “higher-level client” functionality

DNS is implemented in `kernel/net/dns/dns.*` and typically uses UDP to query a resolver and parse responses.

HTTP is implemented in `kernel/net/http/http.*` and uses TCP sockets plus basic request/response parsing.

`kernel/main.cpp` includes a bring-up demo that resolves `example.com` and fetches `/` over HTTP.

## TLS: enabling HTTPS

TLS support lives in `kernel/net/tls/*`:

- record layer handling
- handshake machinery (bring-up level)
- crypto primitives as implemented in the kernel tree (and entropy from virtio-rng)

TLS is still an evolving area; expect this code to change as the v0.2.0 goals are implemented and hardened.

Key files:

- `kernel/net/tls/tls.hpp`
- `kernel/net/tls/tls.cpp`
- `kernel/drivers/virtio/rng.*`

## Current limitations and next steps

- The stack is polling-driven and largely single-threaded in its receive path.
- Buffer management is intentionally simple (static receive buffer in `net::network.cpp`).
- TCP and TLS are bring-up implementations; correctness, retransmission edge cases, and security hardening are ongoing
  work.

