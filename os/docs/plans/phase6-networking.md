# ViperOS Phase 6: Networking

## Detailed Implementation Plan (C++)

**Duration:** 12 weeks (Months 16-18)  
**Goal:** Network connectivity  
**Milestone:** Fetch a webpage from ViperOS  
**Prerequisites:** Phase 5 complete (input, line editing, polished shell)

---

## Executive Summary

Phase 6 connects ViperOS to the world. We implement a complete network stack from the virtio-net driver through TCP/IP
to a working HTTP client. By the end, you can ping hosts, resolve DNS names, and fetch web pages.

Key components:

1. **virtio-net Driver** — Ethernet frame transmission/reception
2. **Network Stack** — Ethernet, ARP, IPv4, ICMP, UDP, TCP
3. **Socket API** — Connection-oriented and datagram interfaces
4. **DNS Resolver** — Name-to-IP resolution
5. **HTTP Client** — Fetch web resources
6. **Network Commands** — ping, wget, ifconfig, netstat
7. **Viper.Net Library** — User-space networking API

---

## Architecture

```
┌────────────────────────────────────────────────────────────────────┐
│                          User Space                                 │
├────────────────────────────────────────────────────────────────────┤
│   Applications                                                      │
│   ┌──────────────┐  ┌──────────────┐  ┌──────────────┐            │
│   │ wget         │  │ ping         │  │ vsh (http)   │            │
│   └──────────────┘  └──────────────┘  └──────────────┘            │
│          │                │                  │                      │
│   Viper.Net Library                                                │
│   ┌────────────────────────────────────────────────────────────┐  │
│   │ TcpStream  UdpSocket  DnsResolver  HttpClient              │  │
│   └────────────────────────────────────────────────────────────┘  │
├────────────────────────────────────────────────────────────────────┤
│                          Syscalls                                   │
│   NetSocket  NetBind  NetConnect  NetSend  NetRecv  NetClose       │
├────────────────────────────────────────────────────────────────────┤
│                          Kernel                                     │
├────────────────────────────────────────────────────────────────────┤
│   Socket Layer                                                      │
│   ┌────────────────────────────────────────────────────────────┐  │
│   │ Socket Table  │  TCP Sockets  │  UDP Sockets  │  Raw       │  │
│   └────────────────────────────────────────────────────────────┘  │
│                              │                                      │
│   Transport Layer            │                                      │
│   ┌──────────────────────────┴─────────────────────────────────┐  │
│   │ TCP                           │  UDP                        │  │
│   │ ┌─────────────────────────┐   │  ┌─────────────────────┐   │  │
│   │ │ Connection State        │   │  │ Connectionless      │   │  │
│   │ │ Retransmission         │   │  │ Checksum            │   │  │
│   │ │ Flow Control           │   │  └─────────────────────┘   │  │
│   │ │ Congestion Control     │   │                             │  │
│   │ └─────────────────────────┘   │                             │  │
│   └────────────────────────────────────────────────────────────┘  │
│                              │                                      │
│   Network Layer              │                                      │
│   ┌──────────────────────────┴─────────────────────────────────┐  │
│   │ IPv4                          │  ICMP                       │  │
│   │ ┌─────────────────────────┐   │  ┌─────────────────────┐   │  │
│   │ │ Routing Table           │   │  │ Echo Request/Reply  │   │  │
│   │ │ Fragmentation          │   │  │ Dest Unreachable    │   │  │
│   │ └─────────────────────────┘   │  └─────────────────────┘   │  │
│   └────────────────────────────────────────────────────────────┘  │
│                              │                                      │
│   Link Layer                 │                                      │
│   ┌──────────────────────────┴─────────────────────────────────┐  │
│   │ Ethernet                      │  ARP                        │  │
│   │ ┌─────────────────────────┐   │  ┌─────────────────────┐   │  │
│   │ │ Frame TX/RX             │   │  │ ARP Cache           │   │  │
│   │ │ MAC Address             │   │  │ ARP Request/Reply   │   │  │
│   │ └─────────────────────────┘   │  └─────────────────────┘   │  │
│   └────────────────────────────────────────────────────────────┘  │
│                              │                                      │
│   virtio-net Driver          │                                      │
│   ┌──────────────────────────┴─────────────────────────────────┐  │
│   │ TX Virtqueue  │  RX Virtqueue  │  Control Virtqueue        │  │
│   └───────────────────────────────────────────────────────────┘   │
└────────────────────────────────────────────────────────────────────┘
```

---

## Project Structure

```
kernel/
├── drivers/
│   └── virtio/
│       └── net.cpp/.hpp            # virtio-net driver
├── net/
│   ├── netif.cpp/.hpp              # Network interface abstraction
│   ├── pbuf.cpp/.hpp               # Packet buffer management
│   ├── eth/
│   │   ├── ethernet.cpp/.hpp       # Ethernet framing
│   │   └── arp.cpp/.hpp            # ARP protocol
│   ├── ip/
│   │   ├── ipv4.cpp/.hpp           # IPv4 protocol
│   │   ├── icmp.cpp/.hpp           # ICMP protocol
│   │   └── route.cpp/.hpp          # Routing table
│   ├── transport/
│   │   ├── udp.cpp/.hpp            # UDP protocol
│   │   └── tcp.cpp/.hpp            # TCP protocol
│   ├── socket/
│   │   ├── socket.cpp/.hpp         # Socket abstraction
│   │   ├── tcp_socket.cpp/.hpp     # TCP socket implementation
│   │   └── udp_socket.cpp/.hpp     # UDP socket implementation
│   └── dns/
│       └── resolver.cpp/.hpp       # DNS resolver (kernel-side cache)
├── syscall/
│   └── net_syscalls.cpp            # Network syscalls

user/
├── lib/
│   ├── vnet/
│   │   ├── socket.cpp/.hpp         # Socket wrapper
│   │   ├── tcp.cpp/.hpp            # TcpStream
│   │   ├── udp.cpp/.hpp            # UdpSocket
│   │   ├── dns.cpp/.hpp            # DNS resolver
│   │   ├── http.cpp/.hpp           # HTTP client
│   │   └── url.cpp/.hpp            # URL parsing
│   └── Viper.Net.hpp               # Public API header
├── cmd/
│   ├── ping.cpp                    # Ping command
│   ├── wget.cpp                    # HTTP fetch command
│   ├── ifconfig.cpp                # Interface configuration
│   ├── netstat.cpp                 # Network statistics
│   └── nslookup.cpp                # DNS lookup
└── viper/
    └── vnetd.vpr                   # Network daemon (optional)
```

---

## Milestones

| # | Milestone         | Duration    | Deliverable               |
|---|-------------------|-------------|---------------------------|
| 1 | virtio-net Driver | Weeks 1-2   | Ethernet TX/RX            |
| 2 | Ethernet & ARP    | Week 3      | Frame handling, ARP cache |
| 3 | IPv4 & ICMP       | Week 4      | IP routing, ping works    |
| 4 | UDP               | Week 5      | Datagram sockets          |
| 5 | TCP               | Weeks 6-8   | Connection state machine  |
| 6 | Socket Syscalls   | Week 9      | User-space socket API     |
| 7 | DNS Resolver      | Week 10     | Name resolution           |
| 8 | HTTP & Commands   | Weeks 11-12 | wget, polish              |

---

## Milestone 1: virtio-net Driver

**Duration:** Weeks 1-2  
**Deliverable:** Send and receive Ethernet frames

### 1.1 virtio-net Device

```cpp
// kernel/drivers/virtio/net.hpp
#pragma once

#include "virtio.hpp"
#include "virtqueue.hpp"
#include "../../net/pbuf.hpp"

namespace viper::virtio {

// virtio-net feature bits
constexpr u64 VIRTIO_NET_F_MAC        = 1ULL << 5;
constexpr u64 VIRTIO_NET_F_STATUS     = 1ULL << 16;
constexpr u64 VIRTIO_NET_F_MRG_RXBUF  = 1ULL << 15;

// virtio-net header (prepended to every packet)
struct VirtioNetHeader {
    u8 flags;
    u8 gso_type;
    u16 hdr_len;
    u16 gso_size;
    u16 csum_start;
    u16 csum_offset;
    u16 num_buffers;  // Only if VIRTIO_NET_F_MRG_RXBUF
};

constexpr u8 VIRTIO_NET_HDR_GSO_NONE = 0;

// virtio-net config
struct VirtioNetConfig {
    u8 mac[6];
    u16 status;
    u16 max_virtqueue_pairs;
    u16 mtu;
};

class NetDevice : public Device {
public:
    bool init(VirtAddr base);
    
    // Get MAC address
    void get_mac(u8* mac);
    
    // Transmit a packet (takes ownership of pbuf)
    bool transmit(net::PacketBuffer* pbuf);
    
    // Receive packets (non-blocking)
    // Returns list of received pbufs
    net::PacketBuffer* receive();
    
    // Check link status
    bool link_up();
    
    // Statistics
    u64 tx_packets() const { return tx_packets_; }
    u64 rx_packets() const { return rx_packets_; }
    u64 tx_bytes() const { return tx_bytes_; }
    u64 rx_bytes() const { return rx_bytes_; }
    
private:
    VirtioNetConfig config_;
    Virtqueue rx_vq_;  // Queue 0: receive
    Virtqueue tx_vq_;  // Queue 1: transmit
    
    // Pre-allocated RX buffers
    static constexpr int RX_BUFFER_COUNT = 64;
    static constexpr int RX_BUFFER_SIZE = 2048;
    u8* rx_buffers_[RX_BUFFER_COUNT];
    PhysAddr rx_buffers_phys_[RX_BUFFER_COUNT];
    bool rx_buffer_used_[RX_BUFFER_COUNT];
    
    // Statistics
    u64 tx_packets_ = 0;
    u64 rx_packets_ = 0;
    u64 tx_bytes_ = 0;
    u64 rx_bytes_ = 0;
    
    void queue_rx_buffers();
    int alloc_rx_buffer();
    void free_rx_buffer(int idx);
};

// Global network device
NetDevice* net_device();
void net_init();

} // namespace viper::virtio
```

### 1.2 virtio-net Implementation

```cpp
// kernel/drivers/virtio/net.cpp
#include "net.hpp"
#include "../../lib/format.hpp"
#include "../../lib/string.hpp"
#include "../../mm/pmm.hpp"

namespace viper::virtio {

namespace {
    NetDevice net_dev;
    bool net_found = false;
}

NetDevice* net_device() { return net_found ? &net_dev : nullptr; }

bool NetDevice::init(VirtAddr base) {
    if (!Device::init(base)) return false;
    
    if (device_id_ != VIRTIO_DEV_NET) {
        return false;
    }
    
    // Reset
    write32(VIRTIO_MMIO_STATUS, 0);
    set_status(VIRTIO_STATUS_ACKNOWLEDGE);
    set_status(get_status() | VIRTIO_STATUS_DRIVER);
    
    // Read features
    u64 features = read32(VIRTIO_MMIO_DEVICE_FEATURES);
    
    // Negotiate features (we want MAC address)
    u64 required = VIRTIO_NET_F_MAC;
    if (!negotiate_features(required, features & required)) {
        kprintf("virtio-net: Feature negotiation failed\n");
        return false;
    }
    
    // Read MAC address from config
    volatile u8* config = reinterpret_cast<volatile u8*>(
        base_.raw() + VIRTIO_MMIO_CONFIG
    );
    for (int i = 0; i < 6; i++) {
        config_.mac[i] = config[i];
    }
    
    kprintf("virtio-net: MAC %02x:%02x:%02x:%02x:%02x:%02x\n",
            config_.mac[0], config_.mac[1], config_.mac[2],
            config_.mac[3], config_.mac[4], config_.mac[5]);
    
    // Initialize virtqueues
    if (!rx_vq_.init(this, 0, 128)) {
        kprintf("virtio-net: Failed to init RX queue\n");
        return false;
    }
    
    if (!tx_vq_.init(this, 1, 128)) {
        kprintf("virtio-net: Failed to init TX queue\n");
        return false;
    }
    
    // Allocate RX buffers
    for (int i = 0; i < RX_BUFFER_COUNT; i++) {
        auto result = pmm::alloc_page();
        if (!result.is_ok()) {
            kprintf("virtio-net: Failed to alloc RX buffer\n");
            return false;
        }
        rx_buffers_phys_[i] = result.unwrap();
        rx_buffers_[i] = pmm::phys_to_virt(result.unwrap()).as_ptr<u8>();
        rx_buffer_used_[i] = false;
    }
    
    // Queue RX buffers
    queue_rx_buffers();
    
    // Driver ready
    set_status(get_status() | VIRTIO_STATUS_DRIVER_OK);
    
    kprintf("virtio-net: Initialized\n");
    return true;
}

void NetDevice::get_mac(u8* mac) {
    memcpy(mac, config_.mac, 6);
}

void NetDevice::queue_rx_buffers() {
    for (int i = 0; i < RX_BUFFER_COUNT; i++) {
        if (rx_buffer_used_[i]) continue;
        
        i32 desc = rx_vq_.alloc_desc();
        if (desc < 0) break;
        
        rx_vq_.set_desc(desc, rx_buffers_phys_[i], RX_BUFFER_SIZE,
                       VRING_DESC_F_WRITE, 0);
        rx_vq_.submit(desc);
        rx_buffer_used_[i] = true;
    }
    rx_vq_.kick();
}

bool NetDevice::transmit(net::PacketBuffer* pbuf) {
    if (!pbuf) return false;
    
    // Allocate descriptors for header + data
    i32 hdr_desc = tx_vq_.alloc_desc();
    i32 data_desc = tx_vq_.alloc_desc();
    
    if (hdr_desc < 0 || data_desc < 0) {
        if (hdr_desc >= 0) tx_vq_.free_desc(hdr_desc);
        if (data_desc >= 0) tx_vq_.free_desc(data_desc);
        return false;
    }
    
    // Prepare virtio-net header
    VirtioNetHeader* hdr = pbuf->prepend_header<VirtioNetHeader>();
    memset(hdr, 0, sizeof(VirtioNetHeader));
    hdr->gso_type = VIRTIO_NET_HDR_GSO_NONE;
    
    // Set up descriptor chain
    tx_vq_.set_desc(hdr_desc, pbuf->phys_addr(), pbuf->total_len(),
                   VRING_DESC_F_NEXT, data_desc);
    tx_vq_.set_desc(data_desc, pbuf->phys_addr() + sizeof(VirtioNetHeader),
                   pbuf->total_len() - sizeof(VirtioNetHeader), 0, 0);
    
    // Submit
    tx_vq_.submit(hdr_desc);
    tx_vq_.kick();
    
    // Wait for completion (simple blocking for now)
    while (tx_vq_.poll_used() < 0) {
        asm volatile("yield");
    }
    
    // Update stats
    tx_packets_++;
    tx_bytes_ += pbuf->total_len() - sizeof(VirtioNetHeader);
    
    // Free descriptors
    tx_vq_.free_desc(hdr_desc);
    tx_vq_.free_desc(data_desc);
    
    return true;
}

net::PacketBuffer* NetDevice::receive() {
    net::PacketBuffer* head = nullptr;
    net::PacketBuffer* tail = nullptr;
    
    while (true) {
        i32 desc = rx_vq_.poll_used();
        if (desc < 0) break;
        
        // Find which buffer this was
        int buf_idx = desc % RX_BUFFER_COUNT;
        
        // Get length from used ring
        u32 len = rx_vq_.used_len(desc);
        
        if (len > sizeof(VirtioNetHeader)) {
            // Create packet buffer
            auto* pbuf = net::PacketBuffer::alloc(len - sizeof(VirtioNetHeader));
            if (pbuf) {
                memcpy(pbuf->data(),
                       rx_buffers_[buf_idx] + sizeof(VirtioNetHeader),
                       len - sizeof(VirtioNetHeader));
                pbuf->set_len(len - sizeof(VirtioNetHeader));
                
                // Link to list
                if (!head) {
                    head = tail = pbuf;
                } else {
                    tail->set_next(pbuf);
                    tail = pbuf;
                }
                
                rx_packets_++;
                rx_bytes_ += len - sizeof(VirtioNetHeader);
            }
        }
        
        // Re-queue buffer
        rx_buffer_used_[buf_idx] = false;
        rx_vq_.free_desc(desc);
    }
    
    // Refill RX buffers
    queue_rx_buffers();
    
    return head;
}

bool NetDevice::link_up() {
    // Check status if feature negotiated
    return true;  // Assume up for now
}

void net_init() {
    // Scan for virtio-net device
    for (u64 addr = 0x0a000000; addr < 0x0a004000; addr += 0x200) {
        VirtAddr va = pmm::phys_to_virt(PhysAddr{addr});
        
        u32 magic = *reinterpret_cast<volatile u32*>(va.raw());
        if (magic != VIRTIO_MAGIC) continue;
        
        u32 dev_id = *reinterpret_cast<volatile u32*>(va.raw() + 8);
        if (dev_id == VIRTIO_DEV_NET) {
            if (net_dev.init(va)) {
                net_found = true;
                return;
            }
        }
    }
    
    kprintf("virtio-net: No device found\n");
}

} // namespace viper::virtio
```

### 1.3 Packet Buffer

```cpp
// kernel/net/pbuf.hpp
#pragma once

#include "../lib/types.hpp"
#include "../mm/pmm.hpp"

namespace viper::net {

// Packet buffer - manages network packet memory
class PacketBuffer {
public:
    static constexpr usize MAX_PACKET = 2048;
    static constexpr usize HEADROOM = 128;  // For protocol headers
    
    // Allocate a packet buffer
    static PacketBuffer* alloc(usize size);
    
    // Free a packet buffer
    static void free(PacketBuffer* pbuf);
    
    // Data access
    u8* data() { return data_; }
    const u8* data() const { return data_; }
    usize len() const { return len_; }
    usize total_len() const { return len_; }
    
    void set_len(usize len) { len_ = len; }
    
    // Prepend header (moves data pointer back)
    template<typename T>
    T* prepend_header() {
        if (data_ - buffer_ < sizeof(T)) return nullptr;
        data_ -= sizeof(T);
        len_ += sizeof(T);
        return reinterpret_cast<T*>(data_);
    }
    
    // Remove header (moves data pointer forward)
    template<typename T>
    T* consume_header() {
        if (len_ < sizeof(T)) return nullptr;
        T* hdr = reinterpret_cast<T*>(data_);
        data_ += sizeof(T);
        len_ -= sizeof(T);
        return hdr;
    }
    
    // Physical address (for DMA)
    PhysAddr phys_addr() const {
        return pmm::virt_to_phys(VirtAddr{reinterpret_cast<u64>(data_)});
    }
    
    // Linked list
    PacketBuffer* next() const { return next_; }
    void set_next(PacketBuffer* n) { next_ = n; }
    
private:
    u8 buffer_[MAX_PACKET];
    u8* data_;
    usize len_;
    PacketBuffer* next_;
};

} // namespace viper::net
```

---

## Milestone 2: Ethernet & ARP

**Duration:** Week 3  
**Deliverable:** Ethernet frame handling, ARP resolution

### 2.1 Ethernet

```cpp
// kernel/net/eth/ethernet.hpp
#pragma once

#include "../../lib/types.hpp"
#include "../pbuf.hpp"

namespace viper::net::eth {

// Ethernet header
struct EthernetHeader {
    u8 dst[6];
    u8 src[6];
    u16 ethertype;  // Big-endian
} __attribute__((packed));

// EtherTypes
constexpr u16 ETHERTYPE_IPV4 = 0x0800;
constexpr u16 ETHERTYPE_ARP  = 0x0806;
constexpr u16 ETHERTYPE_IPV6 = 0x86DD;

// Broadcast MAC
constexpr u8 BROADCAST_MAC[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

// Process incoming Ethernet frame
void rx_frame(PacketBuffer* pbuf);

// Send Ethernet frame
bool tx_frame(PacketBuffer* pbuf, const u8* dst_mac, u16 ethertype);

// Get local MAC
void get_mac(u8* mac);

// Initialize Ethernet layer
void init();

} // namespace viper::net::eth
```

### 2.2 ARP

```cpp
// kernel/net/eth/arp.hpp
#pragma once

#include "../../lib/types.hpp"
#include "../pbuf.hpp"

namespace viper::net::arp {

// ARP header
struct ArpHeader {
    u16 hw_type;      // 1 = Ethernet
    u16 proto_type;   // 0x0800 = IPv4
    u8 hw_len;        // 6 for Ethernet
    u8 proto_len;     // 4 for IPv4
    u16 operation;    // 1=request, 2=reply
    u8 sender_mac[6];
    u8 sender_ip[4];
    u8 target_mac[6];
    u8 target_ip[4];
} __attribute__((packed));

constexpr u16 ARP_OP_REQUEST = 1;
constexpr u16 ARP_OP_REPLY   = 2;

// ARP cache entry
struct ArpEntry {
    u32 ip;
    u8 mac[6];
    u64 timestamp;
    bool valid;
    bool pending;  // Request sent, waiting for reply
};

// Initialize ARP
void init();

// Process incoming ARP packet
void rx_packet(PacketBuffer* pbuf);

// Resolve IP to MAC (may block or return false if pending)
bool resolve(u32 ip, u8* mac_out);

// Add static entry
void add_entry(u32 ip, const u8* mac);

// Clear cache
void clear_cache();

// Send ARP request
void send_request(u32 target_ip);

} // namespace viper::net::arp
```

### 2.3 ARP Implementation

```cpp
// kernel/net/eth/arp.cpp
#include "arp.hpp"
#include "ethernet.hpp"
#include "../netif.hpp"
#include "../../lib/format.hpp"
#include "../../timer/timer.hpp"

namespace viper::net::arp {

namespace {
    constexpr int ARP_CACHE_SIZE = 64;
    constexpr u64 ARP_TIMEOUT_NS = 300ULL * 1000000000ULL;  // 5 minutes
    constexpr u64 ARP_RETRY_NS = 1000000000ULL;  // 1 second
    
    ArpEntry cache[ARP_CACHE_SIZE];
    
    int find_entry(u32 ip) {
        for (int i = 0; i < ARP_CACHE_SIZE; i++) {
            if (cache[i].valid && cache[i].ip == ip) {
                return i;
            }
        }
        return -1;
    }
    
    int find_free_entry() {
        u64 oldest_time = ~0ULL;
        int oldest = 0;
        
        for (int i = 0; i < ARP_CACHE_SIZE; i++) {
            if (!cache[i].valid) return i;
            if (cache[i].timestamp < oldest_time) {
                oldest_time = cache[i].timestamp;
                oldest = i;
            }
        }
        return oldest;
    }
}

void init() {
    for (int i = 0; i < ARP_CACHE_SIZE; i++) {
        cache[i].valid = false;
    }
}

void rx_packet(PacketBuffer* pbuf) {
    if (pbuf->len() < sizeof(ArpHeader)) return;
    
    auto* arp = reinterpret_cast<ArpHeader*>(pbuf->data());
    
    // Validate
    if (ntohs(arp->hw_type) != 1) return;
    if (ntohs(arp->proto_type) != ETHERTYPE_IPV4) return;
    if (arp->hw_len != 6 || arp->proto_len != 4) return;
    
    u32 sender_ip = *reinterpret_cast<u32*>(arp->sender_ip);
    u32 target_ip = *reinterpret_cast<u32*>(arp->target_ip);
    u32 our_ip = netif::get_ip();
    
    // Update cache with sender info
    int idx = find_entry(sender_ip);
    if (idx < 0) idx = find_free_entry();
    
    cache[idx].ip = sender_ip;
    memcpy(cache[idx].mac, arp->sender_mac, 6);
    cache[idx].timestamp = timer::get_ns();
    cache[idx].valid = true;
    cache[idx].pending = false;
    
    // Handle request for our IP
    if (ntohs(arp->operation) == ARP_OP_REQUEST && target_ip == our_ip) {
        // Send reply
        auto* reply = PacketBuffer::alloc(sizeof(ArpHeader));
        if (!reply) return;
        
        auto* reply_arp = reinterpret_cast<ArpHeader*>(reply->data());
        reply_arp->hw_type = htons(1);
        reply_arp->proto_type = htons(ETHERTYPE_IPV4);
        reply_arp->hw_len = 6;
        reply_arp->proto_len = 4;
        reply_arp->operation = htons(ARP_OP_REPLY);
        
        eth::get_mac(reply_arp->sender_mac);
        memcpy(reply_arp->sender_ip, &our_ip, 4);
        memcpy(reply_arp->target_mac, arp->sender_mac, 6);
        memcpy(reply_arp->target_ip, arp->sender_ip, 4);
        
        reply->set_len(sizeof(ArpHeader));
        eth::tx_frame(reply, arp->sender_mac, ETHERTYPE_ARP);
        PacketBuffer::free(reply);
    }
}

bool resolve(u32 ip, u8* mac_out) {
    // Check cache
    int idx = find_entry(ip);
    if (idx >= 0 && !cache[idx].pending) {
        // Check timeout
        u64 now = timer::get_ns();
        if (now - cache[idx].timestamp < ARP_TIMEOUT_NS) {
            memcpy(mac_out, cache[idx].mac, 6);
            return true;
        }
        cache[idx].valid = false;
    }
    
    // Need to send request
    send_request(ip);
    return false;
}

void send_request(u32 target_ip) {
    auto* pbuf = PacketBuffer::alloc(sizeof(ArpHeader));
    if (!pbuf) return;
    
    auto* arp = reinterpret_cast<ArpHeader*>(pbuf->data());
    arp->hw_type = htons(1);
    arp->proto_type = htons(ETHERTYPE_IPV4);
    arp->hw_len = 6;
    arp->proto_len = 4;
    arp->operation = htons(ARP_OP_REQUEST);
    
    eth::get_mac(arp->sender_mac);
    u32 our_ip = netif::get_ip();
    memcpy(arp->sender_ip, &our_ip, 4);
    memset(arp->target_mac, 0, 6);
    memcpy(arp->target_ip, &target_ip, 4);
    
    pbuf->set_len(sizeof(ArpHeader));
    eth::tx_frame(pbuf, eth::BROADCAST_MAC, ETHERTYPE_ARP);
    PacketBuffer::free(pbuf);
    
    // Mark as pending
    int idx = find_entry(target_ip);
    if (idx < 0) idx = find_free_entry();
    cache[idx].ip = target_ip;
    cache[idx].valid = true;
    cache[idx].pending = true;
    cache[idx].timestamp = timer::get_ns();
}

void add_entry(u32 ip, const u8* mac) {
    int idx = find_entry(ip);
    if (idx < 0) idx = find_free_entry();
    
    cache[idx].ip = ip;
    memcpy(cache[idx].mac, mac, 6);
    cache[idx].timestamp = timer::get_ns();
    cache[idx].valid = true;
    cache[idx].pending = false;
}

} // namespace viper::net::arp
```

---

## Milestone 3: IPv4 & ICMP

**Duration:** Week 4  
**Deliverable:** IP packet handling, ping working

### 3.1 IPv4

```cpp
// kernel/net/ip/ipv4.hpp
#pragma once

#include "../../lib/types.hpp"
#include "../pbuf.hpp"

namespace viper::net::ip {

// IPv4 header
struct IPv4Header {
    u8 version_ihl;     // Version (4) + IHL (5)
    u8 dscp_ecn;
    u16 total_length;
    u16 identification;
    u16 flags_fragment;
    u8 ttl;
    u8 protocol;
    u16 checksum;
    u32 src_ip;
    u32 dst_ip;
} __attribute__((packed));

// Protocols
constexpr u8 PROTO_ICMP = 1;
constexpr u8 PROTO_TCP  = 6;
constexpr u8 PROTO_UDP  = 17;

// Initialize IPv4
void init();

// Process incoming IP packet
void rx_packet(PacketBuffer* pbuf);

// Send IP packet
bool tx_packet(PacketBuffer* pbuf, u32 dst_ip, u8 protocol);

// Compute checksum
u16 checksum(const void* data, usize len);

// IP address utilities
u32 make_ip(u8 a, u8 b, u8 c, u8 d);
void ip_to_str(u32 ip, char* buf);

} // namespace viper::net::ip
```

### 3.2 ICMP

```cpp
// kernel/net/ip/icmp.hpp
#pragma once

#include "../../lib/types.hpp"
#include "../pbuf.hpp"

namespace viper::net::icmp {

// ICMP header
struct IcmpHeader {
    u8 type;
    u8 code;
    u16 checksum;
    u16 identifier;
    u16 sequence;
} __attribute__((packed));

// ICMP types
constexpr u8 ICMP_ECHO_REPLY   = 0;
constexpr u8 ICMP_DEST_UNREACH = 3;
constexpr u8 ICMP_ECHO_REQUEST = 8;
constexpr u8 ICMP_TIME_EXCEEDED = 11;

// Initialize ICMP
void init();

// Process incoming ICMP packet
void rx_packet(PacketBuffer* pbuf, u32 src_ip);

// Send echo request (ping)
bool send_echo_request(u32 dst_ip, u16 id, u16 seq, const void* data, usize len);

// Callback for echo replies
using EchoCallback = void (*)(u32 src_ip, u16 id, u16 seq, u64 rtt_ns, void* ctx);
void set_echo_callback(EchoCallback cb, void* ctx);

} // namespace viper::net::icmp
```

### 3.3 ICMP Implementation

```cpp
// kernel/net/ip/icmp.cpp
#include "icmp.hpp"
#include "ipv4.hpp"
#include "../../timer/timer.hpp"
#include "../../lib/string.hpp"

namespace viper::net::icmp {

namespace {
    EchoCallback echo_callback = nullptr;
    void* echo_callback_ctx = nullptr;
    
    // Pending echo requests for RTT calculation
    struct PendingEcho {
        u32 dst_ip;
        u16 id;
        u16 seq;
        u64 send_time;
        bool active;
    };
    constexpr int MAX_PENDING = 16;
    PendingEcho pending[MAX_PENDING];
}

void init() {
    for (int i = 0; i < MAX_PENDING; i++) {
        pending[i].active = false;
    }
}

void rx_packet(PacketBuffer* pbuf, u32 src_ip) {
    if (pbuf->len() < sizeof(IcmpHeader)) return;
    
    auto* icmp = reinterpret_cast<IcmpHeader*>(pbuf->data());
    
    switch (icmp->type) {
        case ICMP_ECHO_REQUEST: {
            // Send echo reply
            auto* reply = PacketBuffer::alloc(pbuf->len());
            if (!reply) return;
            
            memcpy(reply->data(), pbuf->data(), pbuf->len());
            auto* reply_icmp = reinterpret_cast<IcmpHeader*>(reply->data());
            reply_icmp->type = ICMP_ECHO_REPLY;
            reply_icmp->checksum = 0;
            reply_icmp->checksum = ip::checksum(reply->data(), pbuf->len());
            
            reply->set_len(pbuf->len());
            ip::tx_packet(reply, src_ip, ip::PROTO_ICMP);
            PacketBuffer::free(reply);
            break;
        }
        
        case ICMP_ECHO_REPLY: {
            u16 id = ntohs(icmp->identifier);
            u16 seq = ntohs(icmp->sequence);
            u64 now = timer::get_ns();
            
            // Find matching pending request
            for (int i = 0; i < MAX_PENDING; i++) {
                if (pending[i].active &&
                    pending[i].dst_ip == src_ip &&
                    pending[i].id == id &&
                    pending[i].seq == seq) {
                    
                    u64 rtt = now - pending[i].send_time;
                    pending[i].active = false;
                    
                    if (echo_callback) {
                        echo_callback(src_ip, id, seq, rtt, echo_callback_ctx);
                    }
                    break;
                }
            }
            break;
        }
    }
}

bool send_echo_request(u32 dst_ip, u16 id, u16 seq, const void* data, usize len) {
    usize total = sizeof(IcmpHeader) + len;
    auto* pbuf = PacketBuffer::alloc(total);
    if (!pbuf) return false;
    
    auto* icmp = reinterpret_cast<IcmpHeader*>(pbuf->data());
    icmp->type = ICMP_ECHO_REQUEST;
    icmp->code = 0;
    icmp->identifier = htons(id);
    icmp->sequence = htons(seq);
    
    if (data && len > 0) {
        memcpy(pbuf->data() + sizeof(IcmpHeader), data, len);
    }
    
    icmp->checksum = 0;
    icmp->checksum = ip::checksum(pbuf->data(), total);
    
    pbuf->set_len(total);
    
    // Record pending request
    for (int i = 0; i < MAX_PENDING; i++) {
        if (!pending[i].active) {
            pending[i].dst_ip = dst_ip;
            pending[i].id = id;
            pending[i].seq = seq;
            pending[i].send_time = timer::get_ns();
            pending[i].active = true;
            break;
        }
    }
    
    bool ok = ip::tx_packet(pbuf, dst_ip, ip::PROTO_ICMP);
    PacketBuffer::free(pbuf);
    return ok;
}

void set_echo_callback(EchoCallback cb, void* ctx) {
    echo_callback = cb;
    echo_callback_ctx = ctx;
}

} // namespace viper::net::icmp
```

---

## Milestone 4: UDP

**Duration:** Week 5  
**Deliverable:** Datagram sockets for DNS

### 4.1 UDP Protocol

```cpp
// kernel/net/transport/udp.hpp
#pragma once

#include "../../lib/types.hpp"
#include "../pbuf.hpp"

namespace viper::net::udp {

// UDP header
struct UdpHeader {
    u16 src_port;
    u16 dst_port;
    u16 length;
    u16 checksum;
} __attribute__((packed));

// Initialize UDP
void init();

// Process incoming UDP packet
void rx_packet(PacketBuffer* pbuf, u32 src_ip, u32 dst_ip);

// Send UDP packet
bool tx_packet(u32 dst_ip, u16 src_port, u16 dst_port,
               const void* data, usize len);

// Bind a local port (returns handle or -1)
int bind(u16 port);

// Receive from bound port (non-blocking, returns bytes or -1)
int recv(int handle, void* buf, usize max_len,
         u32* src_ip_out, u16* src_port_out);

// Unbind
void unbind(int handle);

} // namespace viper::net::udp
```

---

## Milestone 5: TCP

**Duration:** Weeks 6-8  
**Deliverable:** Connection-oriented sockets

### 5.1 TCP Protocol

```cpp
// kernel/net/transport/tcp.hpp
#pragma once

#include "../../lib/types.hpp"
#include "../pbuf.hpp"
#include "../../sync/wait_queue.hpp"

namespace viper::net::tcp {

// TCP header
struct TcpHeader {
    u16 src_port;
    u16 dst_port;
    u32 seq_num;
    u32 ack_num;
    u8 data_offset;   // Upper 4 bits = offset in 32-bit words
    u8 flags;
    u16 window;
    u16 checksum;
    u16 urgent_ptr;
} __attribute__((packed));

// TCP flags
constexpr u8 TCP_FIN = 0x01;
constexpr u8 TCP_SYN = 0x02;
constexpr u8 TCP_RST = 0x04;
constexpr u8 TCP_PSH = 0x08;
constexpr u8 TCP_ACK = 0x10;
constexpr u8 TCP_URG = 0x20;

// TCP states
enum class TcpState {
    Closed,
    Listen,
    SynSent,
    SynReceived,
    Established,
    FinWait1,
    FinWait2,
    CloseWait,
    Closing,
    LastAck,
    TimeWait,
};

// TCP Control Block
struct TcpCb {
    TcpState state;
    
    // Local/remote endpoints
    u32 local_ip;
    u16 local_port;
    u32 remote_ip;
    u16 remote_port;
    
    // Sequence numbers
    u32 snd_una;      // Send unacknowledged
    u32 snd_nxt;      // Send next
    u32 snd_wnd;      // Send window
    u32 rcv_nxt;      // Receive next
    u32 rcv_wnd;      // Receive window
    u32 iss;          // Initial send sequence
    u32 irs;          // Initial receive sequence
    
    // Buffers
    u8* send_buf;
    usize send_len;
    usize send_capacity;
    u8* recv_buf;
    usize recv_len;
    usize recv_capacity;
    
    // Retransmission
    u64 rto;          // Retransmission timeout (ns)
    u64 last_send;    // Time of last send
    int retries;
    
    // Wait queues
    sync::WaitQueue connect_wait;
    sync::WaitQueue recv_wait;
    sync::WaitQueue send_wait;
    
    // State
    bool active;
    int error;
};

// Initialize TCP
void init();

// Process incoming TCP packet
void rx_packet(PacketBuffer* pbuf, u32 src_ip, u32 dst_ip);

// Timer tick (retransmission, TIME_WAIT)
void timer_tick();

// Socket operations
int socket_create();
int socket_connect(int sock, u32 ip, u16 port);
int socket_bind(int sock, u16 port);
int socket_listen(int sock, int backlog);
int socket_accept(int sock, u32* remote_ip, u16* remote_port);
int socket_send(int sock, const void* data, usize len);
int socket_recv(int sock, void* buf, usize max_len);
int socket_close(int sock);

// Get socket state
TcpState socket_state(int sock);

} // namespace viper::net::tcp
```

### 5.2 TCP State Machine (Simplified)

```cpp
// kernel/net/transport/tcp.cpp (excerpt - state machine)
#include "tcp.hpp"
#include "../ip/ipv4.hpp"
#include "../../lib/format.hpp"

namespace viper::net::tcp {

namespace {
    constexpr int MAX_SOCKETS = 64;
    TcpCb sockets[MAX_SOCKETS];
    
    constexpr usize SEND_BUF_SIZE = 16384;
    constexpr usize RECV_BUF_SIZE = 16384;
    constexpr u64 INITIAL_RTO = 1000000000ULL;  // 1 second
}

void process_established(TcpCb& tcb, TcpHeader* tcp, u8* data, usize len) {
    u32 seg_seq = ntohl(tcp->seq_num);
    u32 seg_ack = ntohl(tcp->ack_num);
    
    // Handle ACK
    if (tcp->flags & TCP_ACK) {
        if (seq_after(seg_ack, tcb.snd_una) && 
            seq_before_eq(seg_ack, tcb.snd_nxt)) {
            // Advance send window
            tcb.snd_una = seg_ack;
            tcb.send_wait.wake_one();
        }
    }
    
    // Handle incoming data
    if (len > 0 && seg_seq == tcb.rcv_nxt) {
        // Copy to receive buffer
        usize space = tcb.recv_capacity - tcb.recv_len;
        usize to_copy = len < space ? len : space;
        
        memcpy(tcb.recv_buf + tcb.recv_len, data, to_copy);
        tcb.recv_len += to_copy;
        tcb.rcv_nxt += to_copy;
        
        // Send ACK
        send_ack(tcb);
        tcb.recv_wait.wake_one();
    }
    
    // Handle FIN
    if (tcp->flags & TCP_FIN) {
        tcb.rcv_nxt++;
        send_ack(tcb);
        tcb.state = TcpState::CloseWait;
        tcb.recv_wait.wake_all();
    }
}

int socket_connect(int sock, u32 ip, u16 port) {
    if (sock < 0 || sock >= MAX_SOCKETS) return -1;
    TcpCb& tcb = sockets[sock];
    
    if (tcb.state != TcpState::Closed) return -1;
    
    // Assign local port
    tcb.local_port = alloc_ephemeral_port();
    tcb.local_ip = netif::get_ip();
    tcb.remote_ip = ip;
    tcb.remote_port = port;
    
    // Initialize sequence numbers
    tcb.iss = generate_isn();
    tcb.snd_una = tcb.iss;
    tcb.snd_nxt = tcb.iss + 1;
    tcb.rcv_wnd = RECV_BUF_SIZE;
    
    // Send SYN
    send_syn(tcb);
    tcb.state = TcpState::SynSent;
    
    // Wait for connection
    while (tcb.state == TcpState::SynSent) {
        tcb.connect_wait.wait();
    }
    
    if (tcb.state == TcpState::Established) {
        return 0;
    }
    
    return -1;
}

int socket_send(int sock, const void* data, usize len) {
    if (sock < 0 || sock >= MAX_SOCKETS) return -1;
    TcpCb& tcb = sockets[sock];
    
    if (tcb.state != TcpState::Established) return -1;
    
    const u8* src = static_cast<const u8*>(data);
    usize sent = 0;
    
    while (sent < len) {
        // Wait for send buffer space
        while (tcb.send_len >= tcb.send_capacity) {
            tcb.send_wait.wait();
            if (tcb.state != TcpState::Established) {
                return sent > 0 ? sent : -1;
            }
        }
        
        // Copy to send buffer
        usize space = tcb.send_capacity - tcb.send_len;
        usize to_copy = (len - sent) < space ? (len - sent) : space;
        
        memcpy(tcb.send_buf + tcb.send_len, src + sent, to_copy);
        tcb.send_len += to_copy;
        sent += to_copy;
        
        // Send data
        send_data(tcb);
    }
    
    return sent;
}

int socket_recv(int sock, void* buf, usize max_len) {
    if (sock < 0 || sock >= MAX_SOCKETS) return -1;
    TcpCb& tcb = sockets[sock];
    
    // Wait for data
    while (tcb.recv_len == 0) {
        if (tcb.state != TcpState::Established &&
            tcb.state != TcpState::CloseWait) {
            return 0;  // EOF
        }
        tcb.recv_wait.wait();
    }
    
    // Copy from receive buffer
    usize to_copy = tcb.recv_len < max_len ? tcb.recv_len : max_len;
    memcpy(buf, tcb.recv_buf, to_copy);
    
    // Shift remaining data
    memmove(tcb.recv_buf, tcb.recv_buf + to_copy, tcb.recv_len - to_copy);
    tcb.recv_len -= to_copy;
    
    return to_copy;
}

} // namespace viper::net::tcp
```

---

## Milestone 6: Socket Syscalls

**Duration:** Week 9  
**Deliverable:** User-space socket API

### 6.1 Socket Syscalls

```cpp
// kernel/syscall/net_syscalls.cpp
#include "dispatch.hpp"
#include "../net/socket/socket.hpp"
#include "../viper/viper.hpp"
#include "../cap/table.hpp"

namespace viper::syscall {

// Syscall numbers for networking (0x00Ax range)
// VSYS_NetSocket     = 0x00A0
// VSYS_NetBind       = 0x00A1
// VSYS_NetConnect    = 0x00A2
// VSYS_NetListen     = 0x00A3
// VSYS_NetAccept     = 0x00A4
// VSYS_NetSend       = 0x00A5
// VSYS_NetRecv       = 0x00A6
// VSYS_NetClose      = 0x00A7
// VSYS_NetGetAddr    = 0x00A8
// VSYS_DnsResolve    = 0x00B0

// NetSocket(type) -> handle
// type: 0=TCP, 1=UDP
SyscallResult sys_net_socket(proc::Viper* v, u32 type) {
    int sock;
    
    if (type == 0) {
        sock = net::tcp::socket_create();
    } else if (type == 1) {
        sock = net::udp::bind(0);  // Ephemeral port
    } else {
        return {VERR_INVALID_ARG, 0, 0, 0};
    }
    
    if (sock < 0) {
        return {VERR_OUT_OF_MEMORY, 0, 0, 0};
    }
    
    // Wrap in socket object
    auto* socket = new net::Socket;
    socket->type = type == 0 ? net::SocketType::Tcp : net::SocketType::Udp;
    socket->handle = sock;
    
    auto h = v->cap_table->insert(socket, cap::Kind::Socket,
                                  cap::CAP_READ | cap::CAP_WRITE);
    if (!h.is_ok()) {
        delete socket;
        return {h.get_error(), 0, 0, 0};
    }
    
    return {VOK, h.unwrap(), 0, 0};
}

// NetConnect(socket, ip, port)
SyscallResult sys_net_connect(proc::Viper* v, u32 h, u32 ip, u16 port) {
    auto* entry = v->cap_table->get_checked(h, cap::Kind::Socket);
    if (!entry) return {VERR_INVALID_HANDLE, 0, 0, 0};
    
    auto* socket = static_cast<net::Socket*>(entry->object);
    
    if (socket->type == net::SocketType::Tcp) {
        int err = net::tcp::socket_connect(socket->handle, ip, port);
        if (err < 0) return {VERR_IO, 0, 0, 0};
    } else {
        socket->remote_ip = ip;
        socket->remote_port = port;
    }
    
    return {VOK, 0, 0, 0};
}

// NetSend(socket, data, len) -> bytes_sent
SyscallResult sys_net_send(proc::Viper* v, u32 h, u64 data, u64 len) {
    auto* entry = v->cap_table->get_with_rights(h, cap::Kind::Socket, cap::CAP_WRITE);
    if (!entry) return {VERR_INVALID_HANDLE, 0, 0, 0};
    
    auto* socket = static_cast<net::Socket*>(entry->object);
    
    int n;
    if (socket->type == net::SocketType::Tcp) {
        n = net::tcp::socket_send(socket->handle, 
                                  reinterpret_cast<void*>(data), len);
    } else {
        n = net::udp::tx_packet(socket->remote_ip, socket->local_port,
                                socket->remote_port,
                                reinterpret_cast<void*>(data), len);
    }
    
    if (n < 0) return {VERR_IO, 0, 0, 0};
    return {VOK, static_cast<u64>(n), 0, 0};
}

// NetRecv(socket, buf, max_len) -> bytes_received
SyscallResult sys_net_recv(proc::Viper* v, u32 h, u64 buf, u64 max_len) {
    auto* entry = v->cap_table->get_with_rights(h, cap::Kind::Socket, cap::CAP_READ);
    if (!entry) return {VERR_INVALID_HANDLE, 0, 0, 0};
    
    auto* socket = static_cast<net::Socket*>(entry->object);
    
    int n;
    if (socket->type == net::SocketType::Tcp) {
        n = net::tcp::socket_recv(socket->handle,
                                  reinterpret_cast<void*>(buf), max_len);
    } else {
        u32 src_ip;
        u16 src_port;
        n = net::udp::recv(socket->handle, reinterpret_cast<void*>(buf),
                          max_len, &src_ip, &src_port);
    }
    
    if (n < 0) return {VERR_IO, 0, 0, 0};
    return {VOK, static_cast<u64>(n), 0, 0};
}

// DnsResolve(name, name_len) -> ip
SyscallResult sys_dns_resolve(proc::Viper* v, u64 name_ptr, u64 name_len) {
    char name[256];
    if (name_len > 255) return {VERR_INVALID_ARG, 0, 0, 0};
    
    memcpy(name, reinterpret_cast<void*>(name_ptr), name_len);
    name[name_len] = '\0';
    
    u32 ip = net::dns::resolve(name);
    if (ip == 0) return {VERR_NOT_FOUND, 0, 0, 0};
    
    return {VOK, ip, 0, 0};
}

} // namespace viper::syscall
```

---

## Milestone 7: DNS Resolver

**Duration:** Week 10  
**Deliverable:** Name resolution

### 7.1 DNS Resolver

```cpp
// kernel/net/dns/resolver.hpp
#pragma once

#include "../../lib/types.hpp"

namespace viper::net::dns {

// DNS configuration
void set_server(u32 ip);
u32 get_server();

// Resolve hostname to IP (blocking)
// Returns 0 on failure
u32 resolve(const char* hostname);

// Cache management
void clear_cache();

} // namespace viper::net::dns
```

### 7.2 DNS Implementation

```cpp
// kernel/net/dns/resolver.cpp
#include "resolver.hpp"
#include "../transport/udp.hpp"
#include "../../lib/string.hpp"
#include "../../timer/timer.hpp"

namespace viper::net::dns {

namespace {
    constexpr u16 DNS_PORT = 53;
    constexpr u64 DNS_TIMEOUT = 5000000000ULL;  // 5 seconds
    
    u32 dns_server = 0x08080808;  // 8.8.8.8 default
    
    // DNS header
    struct DnsHeader {
        u16 id;
        u16 flags;
        u16 qdcount;
        u16 ancount;
        u16 nscount;
        u16 arcount;
    } __attribute__((packed));
    
    constexpr u16 DNS_FLAG_QR     = 0x8000;
    constexpr u16 DNS_FLAG_RD     = 0x0100;
    constexpr u16 DNS_FLAG_RA     = 0x0080;
    constexpr u16 DNS_FLAG_RCODE  = 0x000F;
    
    // Simple cache
    struct CacheEntry {
        char name[128];
        u32 ip;
        u64 expires;
        bool valid;
    };
    constexpr int CACHE_SIZE = 32;
    CacheEntry cache[CACHE_SIZE];
    
    u16 next_id = 1;
    
    int encode_name(const char* name, u8* buf) {
        int pos = 0;
        const char* p = name;
        
        while (*p) {
            const char* dot = strchr(p, '.');
            int len = dot ? (dot - p) : strlen(p);
            
            buf[pos++] = len;
            memcpy(buf + pos, p, len);
            pos += len;
            
            if (dot) p = dot + 1;
            else break;
        }
        
        buf[pos++] = 0;  // Root label
        return pos;
    }
}

void set_server(u32 ip) {
    dns_server = ip;
}

u32 get_server() {
    return dns_server;
}

u32 resolve(const char* hostname) {
    // Check cache
    u64 now = timer::get_ns();
    for (int i = 0; i < CACHE_SIZE; i++) {
        if (cache[i].valid && 
            cache[i].expires > now &&
            strcmp(cache[i].name, hostname) == 0) {
            return cache[i].ip;
        }
    }
    
    // Check for IP address literal
    u32 ip = parse_ip(hostname);
    if (ip != 0) return ip;
    
    // Build query
    u8 query[512];
    DnsHeader* hdr = reinterpret_cast<DnsHeader*>(query);
    
    hdr->id = htons(next_id++);
    hdr->flags = htons(DNS_FLAG_RD);
    hdr->qdcount = htons(1);
    hdr->ancount = 0;
    hdr->nscount = 0;
    hdr->arcount = 0;
    
    int pos = sizeof(DnsHeader);
    pos += encode_name(hostname, query + pos);
    
    // QTYPE = A (1), QCLASS = IN (1)
    query[pos++] = 0;
    query[pos++] = 1;
    query[pos++] = 0;
    query[pos++] = 1;
    
    // Send query
    int sock = udp::bind(0);
    if (sock < 0) return 0;
    
    udp::tx_packet(dns_server, udp::get_port(sock), DNS_PORT, query, pos);
    
    // Wait for response
    u8 response[512];
    u32 src_ip;
    u16 src_port;
    u64 deadline = now + DNS_TIMEOUT;
    
    while (timer::get_ns() < deadline) {
        int n = udp::recv(sock, response, sizeof(response), &src_ip, &src_port);
        if (n > 0) {
            DnsHeader* resp_hdr = reinterpret_cast<DnsHeader*>(response);
            
            if (resp_hdr->id == hdr->id &&
                (ntohs(resp_hdr->flags) & DNS_FLAG_QR)) {
                
                // Parse answer
                int ancount = ntohs(resp_hdr->ancount);
                if (ancount > 0) {
                    // Skip question section
                    int offset = sizeof(DnsHeader);
                    while (response[offset] != 0) {
                        offset += response[offset] + 1;
                    }
                    offset += 5;  // Null + QTYPE + QCLASS
                    
                    // Parse first answer
                    // Skip name (may be compressed)
                    if ((response[offset] & 0xC0) == 0xC0) {
                        offset += 2;
                    } else {
                        while (response[offset] != 0) {
                            offset += response[offset] + 1;
                        }
                        offset++;
                    }
                    
                    u16 type = (response[offset] << 8) | response[offset + 1];
                    offset += 8;  // TYPE + CLASS + TTL
                    u16 rdlen = (response[offset] << 8) | response[offset + 1];
                    offset += 2;
                    
                    if (type == 1 && rdlen == 4) {  // A record
                        ip = *reinterpret_cast<u32*>(response + offset);
                        
                        // Cache it
                        for (int i = 0; i < CACHE_SIZE; i++) {
                            if (!cache[i].valid) {
                                strncpy(cache[i].name, hostname, 127);
                                cache[i].ip = ip;
                                cache[i].expires = now + 3600000000000ULL; // 1 hour
                                cache[i].valid = true;
                                break;
                            }
                        }
                        
                        udp::unbind(sock);
                        return ip;
                    }
                }
            }
        }
        
        // Small delay before retry
        sched::yield();
    }
    
    udp::unbind(sock);
    return 0;
}

void clear_cache() {
    for (int i = 0; i < CACHE_SIZE; i++) {
        cache[i].valid = false;
    }
}

} // namespace viper::net::dns
```

---

## Milestone 8: HTTP & Commands

**Duration:** Weeks 11-12  
**Deliverable:** wget command, milestone achieved

### 8.1 HTTP Client (User Space)

```cpp
// user/lib/vnet/http.hpp
#pragma once

#include <stdint.h>

namespace vnet::http {

struct Response {
    int status;
    char* headers;
    uint8_t* body;
    uint32_t body_len;
};

// Simple HTTP GET
// Returns nullptr on error
Response* get(const char* url);

// Free response
void free_response(Response* resp);

// Parse URL into components
struct Url {
    char host[256];
    uint16_t port;
    char path[512];
};
bool parse_url(const char* url, Url* out);

} // namespace vnet::http
```

### 8.2 HTTP Implementation

```cpp
// user/lib/vnet/http.cpp
#include "http.hpp"
#include "tcp.hpp"
#include "dns.hpp"
#include "../vsys.hpp"
#include <string.h>
#include <stdlib.h>

namespace vnet::http {

bool parse_url(const char* url, Url* out) {
    // Skip http://
    if (strncmp(url, "http://", 7) == 0) {
        url += 7;
    }
    
    // Find host end
    const char* slash = strchr(url, '/');
    const char* colon = strchr(url, ':');
    
    int host_len;
    if (colon && (!slash || colon < slash)) {
        host_len = colon - url;
        out->port = atoi(colon + 1);
    } else {
        host_len = slash ? (slash - url) : strlen(url);
        out->port = 80;
    }
    
    if (host_len > 255) return false;
    strncpy(out->host, url, host_len);
    out->host[host_len] = '\0';
    
    if (slash) {
        strncpy(out->path, slash, 511);
    } else {
        strcpy(out->path, "/");
    }
    out->path[511] = '\0';
    
    return true;
}

Response* get(const char* url) {
    Url parsed;
    if (!parse_url(url, &parsed)) {
        return nullptr;
    }
    
    // Resolve hostname
    uint32_t ip = dns::resolve(parsed.host);
    if (ip == 0) {
        return nullptr;
    }
    
    // Connect
    TcpStream stream;
    if (!stream.connect(ip, parsed.port)) {
        return nullptr;
    }
    
    // Send request
    char request[1024];
    int req_len = snprintf(request, sizeof(request),
        "GET %s HTTP/1.0\r\n"
        "Host: %s\r\n"
        "User-Agent: ViperOS/1.0\r\n"
        "Connection: close\r\n"
        "\r\n",
        parsed.path, parsed.host);
    
    if (stream.send(request, req_len) != req_len) {
        return nullptr;
    }
    
    // Receive response
    uint8_t* buffer = (uint8_t*)malloc(65536);
    if (!buffer) return nullptr;
    
    int total = 0;
    while (total < 65536) {
        int n = stream.recv(buffer + total, 65536 - total);
        if (n <= 0) break;
        total += n;
    }
    
    stream.close();
    
    if (total == 0) {
        free(buffer);
        return nullptr;
    }
    
    // Parse response
    Response* resp = new Response;
    resp->status = 0;
    resp->headers = nullptr;
    resp->body = nullptr;
    resp->body_len = 0;
    
    // Find status code
    if (strncmp((char*)buffer, "HTTP/", 5) == 0) {
        const char* space = strchr((char*)buffer, ' ');
        if (space) {
            resp->status = atoi(space + 1);
        }
    }
    
    // Find body (after \r\n\r\n)
    uint8_t* body_start = (uint8_t*)strstr((char*)buffer, "\r\n\r\n");
    if (body_start) {
        body_start += 4;
        resp->body_len = total - (body_start - buffer);
        resp->body = (uint8_t*)malloc(resp->body_len);
        memcpy(resp->body, body_start, resp->body_len);
    }
    
    free(buffer);
    return resp;
}

void free_response(Response* resp) {
    if (resp) {
        free(resp->headers);
        free(resp->body);
        delete resp;
    }
}

} // namespace vnet::http
```

### 8.3 wget Command

```cpp
// user/cmd/wget.cpp
#include "../lib/vsys.hpp"
#include "../lib/vnet/http.hpp"
#include <string.h>

int main(int argc, char** argv) {
    if (argc < 2) {
        print("Usage: wget <url> [output]\n");
        return 5;
    }
    
    const char* url = argv[1];
    const char* output = argc > 2 ? argv[2] : nullptr;
    
    print("Fetching ");
    print(url);
    print("...\n");
    
    auto* resp = vnet::http::get(url);
    if (!resp) {
        print("Error: Request failed\n");
        return 10;
    }
    
    print("Status: ");
    print_int(resp->status);
    print("\n");
    
    if (resp->status != 200) {
        vnet::http::free_response(resp);
        return 10;
    }
    
    print("Received ");
    print_int(resp->body_len);
    print(" bytes\n");
    
    if (output) {
        // Save to file
        uint32_t file = vio::open_path(output, VFS_WRITE | VFS_CREATE);
        if (file == 0) {
            print("Cannot create output file\n");
            vnet::http::free_response(resp);
            return 10;
        }
        
        vsys::io_write(file, resp->body, resp->body_len);
        vsys::fs_close(file);
        
        print("Saved to ");
        print(output);
        print("\n");
    } else {
        // Print to console
        vsys::debug_print((char*)resp->body, resp->body_len);
        print("\n");
    }
    
    vnet::http::free_response(resp);
    return 0;
}
```

### 8.4 ping Command

```cpp
// user/cmd/ping.cpp
#include "../lib/vsys.hpp"
#include "../lib/vnet/dns.hpp"
#include <string.h>

int main(int argc, char** argv) {
    if (argc < 2) {
        print("Usage: ping <host> [count]\n");
        return 5;
    }
    
    const char* host = argv[1];
    int count = argc > 2 ? atoi(argv[2]) : 4;
    
    // Resolve hostname
    uint32_t ip = vnet::dns::resolve(host);
    if (ip == 0) {
        print("Cannot resolve ");
        print(host);
        print("\n");
        return 10;
    }
    
    char ip_str[16];
    vnet::ip_to_str(ip, ip_str);
    print("PING ");
    print(host);
    print(" (");
    print(ip_str);
    print(")\n");
    
    for (int i = 0; i < count; i++) {
        uint64_t rtt;
        if (vsys::net_ping(ip, i, &rtt)) {
            print("Reply from ");
            print(ip_str);
            print(": seq=");
            print_int(i);
            print(" time=");
            print_int(rtt / 1000000);  // ms
            print("ms\n");
        } else {
            print("Request timeout\n");
        }
        
        if (i < count - 1) {
            vsys::sleep(1000);  // 1 second between pings
        }
    }
    
    return 0;
}
```

### 8.5 ifconfig Command

```cpp
// user/cmd/ifconfig.cpp
#include "../lib/vsys.hpp"
#include "../lib/vnet/netif.hpp"

int main(int argc, char** argv) {
    NetIfInfo info;
    if (!vnet::get_interface_info(&info)) {
        print("No network interface\n");
        return 10;
    }
    
    print("eth0:\n");
    
    print("  MAC: ");
    print_mac(info.mac);
    print("\n");
    
    print("  IP:  ");
    print_ip(info.ip);
    print("\n");
    
    print("  Mask: ");
    print_ip(info.netmask);
    print("\n");
    
    print("  Gateway: ");
    print_ip(info.gateway);
    print("\n");
    
    print("  DNS: ");
    print_ip(info.dns);
    print("\n");
    
    print("\n");
    print("  TX packets: ");
    print_int(info.tx_packets);
    print("  bytes: ");
    print_int(info.tx_bytes);
    print("\n");
    
    print("  RX packets: ");
    print_int(info.rx_packets);
    print("  bytes: ");
    print_int(info.rx_bytes);
    print("\n");
    
    return 0;
}
```

---

## Network Configuration

### QEMU Setup

```bash
# Run QEMU with network
qemu-system-aarch64 \
    -machine virt \
    -cpu cortex-a72 \
    -m 128M \
    -drive if=pflash,format=raw,readonly=on,file=AAVMF_CODE.fd \
    -drive format=raw,file=build/disk.img \
    -netdev user,id=net0,hostfwd=tcp::2222-:22 \
    -device virtio-net-device,netdev=net0 \
    -serial stdio
```

### Default Network Settings

```cpp
// Default configuration (QEMU user networking)
IP Address: 10.0.2.15
Netmask:    255.255.255.0
Gateway:    10.0.2.2
DNS:        10.0.2.3
```

---

## Weekly Schedule

| Week | Focus             | Deliverables                      |
|------|-------------------|-----------------------------------|
| 1    | virtio-net device | Device discovery, virtqueue setup |
| 2    | virtio-net TX/RX  | Packet send/receive               |
| 3    | Ethernet & ARP    | Frame handling, ARP cache         |
| 4    | IPv4 & ICMP       | Routing, ping works               |
| 5    | UDP               | Datagram sockets                  |
| 6    | TCP basics        | Connection setup, state machine   |
| 7    | TCP data transfer | Reliable delivery, flow control   |
| 8    | TCP teardown      | Connection close, retransmission  |
| 9    | Socket syscalls   | User-space API                    |
| 10   | DNS resolver      | Name resolution                   |
| 11   | HTTP client       | GET requests                      |
| 12   | Commands & polish | wget, ping, ifconfig              |

---

## Definition of Done (Phase 6 / v1.0)

- [ ] virtio-net sends/receives Ethernet frames
- [ ] ARP resolves IP to MAC
- [ ] IPv4 routes packets correctly
- [ ] ICMP ping works (`ping 10.0.2.2`)
- [ ] UDP sends/receives datagrams
- [ ] TCP establishes connections
- [ ] TCP transfers data reliably
- [ ] TCP closes connections cleanly
- [ ] Socket syscalls work from user space
- [ ] DNS resolves hostnames
- [ ] HTTP GET fetches pages
- [ ] `wget http://example.com` succeeds
- [ ] Network stable under load
- [ ] No memory leaks in network stack

---

## v1.0 Complete!

With Phase 6 complete, ViperOS v1.0 is achieved:

| Feature    | Status                    |
|------------|---------------------------|
| Boot       | ✅ UEFI, graphics console  |
| Memory     | ✅ PMM, VMM, heap          |
| Tasks      | ✅ Preemptive multitasking |
| IPC        | ✅ Channels, polling       |
| User Space | ✅ Vipers, capabilities    |
| Filesystem | ✅ ViperFS, VFS            |
| Shell      | ✅ vsh with editing        |
| Input      | ✅ Keyboard, line editor   |
| Network    | ✅ TCP/IP, HTTP            |

**ViperOS can now:**

- Boot to a graphical shell
- Run multiple programs
- Read/write files
- Accept keyboard input
- Connect to the internet
- Fetch web pages

---

## Beyond v1.0: Self-Hosting

The ultimate goal remains:

1. **ViperIDE** on ViperOS
2. **Viper Compiler** building natively
3. **Full runtime library**
4. **Viper Computer** hardware

But that's for after v1.0. 🎉

---

*"A network stack is where an OS joins the world."*
