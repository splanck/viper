# IPC Subsystem (Channels/Poll)

**Status:** Complete
**Location:** `kernel/ipc/`
**SLOC:** ~2,500

## Overview

The IPC subsystem provides message-passing primitives for inter-process communication in the ViperDOS microkernel. It
consists of two main components:

- **Channels**: Bidirectional message-passing with capability transfer
- **Poll**: Multiplexing and timer management for waiting on multiple events

This is the primary mechanism for communication between user-space servers and clients in the microkernel architecture.

## Architecture

```
┌─────────────────────────────────────────────────────────────────┐
│                       User Space                                 │
│  ┌───────────┐                              ┌───────────┐       │
│  │  Client   │──── send(req) ─────────────▶│  Server   │       │
│  │           │◀─── recv(reply) ────────────│           │       │
│  └───────────┘                              └───────────┘       │
│       │                                           │              │
│       │ send/recv handles                         │              │
└───────┼───────────────────────────────────────────┼──────────────┘
        │ SVC syscalls                              │
        ▼                                           ▼
┌─────────────────────────────────────────────────────────────────┐
│                     Kernel IPC Layer                             │
│  ┌─────────────────────────────────────────────────────────────┐│
│  │                    Channel Table                             ││
│  │  ┌─────────┐ ┌─────────┐ ┌─────────┐ ┌─────────┐            ││
│  │  │ Chan 0  │ │ Chan 1  │ │ Chan 2  │ │  ...    │            ││
│  │  │ ─────── │ │ ─────── │ │ ─────── │ │         │            ││
│  │  │ msgs[16]│ │ msgs[16]│ │ msgs[16]│ │         │            ││
│  │  │ wait_q  │ │ wait_q  │ │ wait_q  │ │         │            ││
│  │  └─────────┘ └─────────┘ └─────────┘ └─────────┘            ││
│  └─────────────────────────────────────────────────────────────┘│
│  ┌─────────────────────────────────────────────────────────────┐│
│  │                    Poll Subsystem                            ││
│  │  • Multiplexed waiting on channels/timers                   ││
│  │  • Timer management (create, expire, cancel)                ││
│  │  • Console/network pseudo-handles                           ││
│  └─────────────────────────────────────────────────────────────┘│
└─────────────────────────────────────────────────────────────────┘
```

---

## Channels

### Design

Channels are bidirectional message queues with two endpoints:

- **Send endpoint**: Has `CAP_WRITE` right, used for sending messages
- **Recv endpoint**: Has `CAP_READ` right, used for receiving messages

Each channel maintains:

- A circular buffer of messages (default 16, max 64)
- Wait queues for blocked senders and receivers
- Reference counts for each endpoint

### Constants

```cpp
namespace channel {
    constexpr u32 MAX_MSG_SIZE = 256;      // Bytes per message
    constexpr u32 MAX_CHANNELS = 64;       // Maximum concurrent channels
    constexpr u32 DEFAULT_PENDING = 16;    // Default queue depth
    constexpr u32 MAX_PENDING = 64;        // Maximum queue depth
    constexpr u32 MAX_HANDLES_PER_MSG = 4; // Handles per message
}
```

### Message Structure

```cpp
struct Message {
    u8 data[MAX_MSG_SIZE];     // Payload bytes
    u32 size;                   // Actual payload size
    u32 sender_id;              // Task ID of sender
    u32 handle_count;           // Number of transferred handles (0-4)
    TransferredHandle handles[4]; // Handles to transfer
};

struct TransferredHandle {
    void *object;  // Kernel object pointer
    u16 kind;      // cap::Kind value
    u32 rights;    // Original rights
};
```

### Channel Structure

```cpp
struct Channel {
    u32 id;
    ChannelState state;        // FREE, OPEN, CLOSED

    // Circular message buffer
    Message buffer[MAX_PENDING];
    u32 read_idx;
    u32 write_idx;
    u32 count;
    u32 capacity;

    // Wait queues
    WaitQueue send_waiters;    // Tasks blocked on send (buffer full)
    WaitQueue recv_waiters;    // Tasks blocked on recv (buffer empty)

    // Reference counts
    u32 send_refs;             // Number of send endpoint handles
    u32 recv_refs;             // Number of recv endpoint handles
    u32 owner_id;              // Creator task ID
};
```

### Capability Transfer

When handles are included in a message:

1. Handles are validated in sender's cap_table
2. Entries are removed from sender's cap_table
3. Object pointers, kinds, and rights are stored in message
4. On receive, entries are inserted into receiver's cap_table
5. Receiver gets new handle values for the transferred capabilities

This enables:

- Passing file/socket handles between processes
- Delegating access rights (service discovery)
- Capability-based security model

### API

```cpp
// Create channel and get both endpoints
i64 create(ChannelPair *out_pair, u32 capacity = 16);

struct ChannelPair {
    cap::Handle send_handle;  // CAP_WRITE | CAP_TRANSFER | CAP_DERIVE
    cap::Handle recv_handle;  // CAP_READ | CAP_TRANSFER | CAP_DERIVE
};

// Non-blocking operations
i64 try_send(Channel *ch, const void *data, u32 size,
             const cap::Handle *handles, u32 handle_count);
i64 try_recv(Channel *ch, void *data, u32 max_size,
             cap::Handle *out_handles, u32 *out_handle_count);

// Blocking operations
i64 send(Channel *ch, const void *data, u32 size,
         const cap::Handle *handles, u32 handle_count);
i64 recv(Channel *ch, void *data, u32 max_size,
         cap::Handle *out_handles, u32 *out_handle_count);

// Close endpoint (decrements ref count)
i64 close(Channel *ch, bool is_send_endpoint);
```

### Syscalls

| Syscall            | Number | Description                           |
|--------------------|--------|---------------------------------------|
| SYS_CHANNEL_CREATE | 0x10   | Create channel, return both endpoints |
| SYS_CHANNEL_SEND   | 0x11   | Send message with optional handles    |
| SYS_CHANNEL_RECV   | 0x12   | Receive message with optional handles |
| SYS_CHANNEL_CLOSE  | 0x13   | Close endpoint                        |

---

## Poll Subsystem

### Design

The poll subsystem provides:

- Multiplexed waiting on multiple handles (channels, timers)
- Timer creation and management
- Pseudo-handles for console input and network events

### Event Types

```cpp
enum class EventType : u32 {
    NONE = 0,
    CHANNEL_READ = (1 << 0),   // Channel has data to read
    CHANNEL_WRITE = (1 << 1),  // Channel has space to write
    TIMER = (1 << 2),          // Timer expired
    CONSOLE_INPUT = (1 << 3),  // Console has input ready
    NETWORK_RX = (1 << 4),     // Network has received data
};
```

### Poll Flags

```cpp
enum class PollFlags : u32 {
    NONE = 0,
    EDGE_TRIGGERED = (1 << 0), // Only report edge transitions
    ONESHOT = (1 << 1),        // Auto-remove after first trigger
};
```

### Special Pseudo-Handles

```cpp
constexpr u32 HANDLE_CONSOLE_INPUT = 0xFFFF0001;
constexpr u32 HANDLE_NETWORK_RX = 0xFFFF0002;
```

### Poll Event Structure

```cpp
struct PollEvent {
    u32 handle;           // Channel ID or timer handle
    EventType events;     // Requested events (input)
    EventType triggered;  // Triggered events (output)
};
```

### API

```cpp
// Initialize poll subsystem
void init();

// Poll for readiness on multiple handles
// timeout_ms: 0 = non-blocking, -1 = infinite, >0 = timeout
i64 poll(PollEvent *events, u32 count, i64 timeout_ms);

// Timer management
i64 timer_create(u64 timeout_ms);
bool timer_expired(u32 timer_id);
i64 timer_cancel(u32 timer_id);

// Time and sleep
u64 time_now_ms();
i64 sleep_ms(u64 ms);

// Wait queue integration
void register_wait(u32 handle, EventType events);
void notify_handle(u32 handle, EventType events);
void unregister_wait();
```

### Syscalls

| Syscall          | Number | Description                 |
|------------------|--------|-----------------------------|
| SYS_POLL_CREATE  | 0x20   | Create poll set             |
| SYS_POLL_ADD     | 0x21   | Add handle to poll set      |
| SYS_POLL_REMOVE  | 0x22   | Remove handle from poll set |
| SYS_POLL_WAIT    | 0x23   | Wait on poll set            |
| SYS_TIME_NOW     | 0x30   | Get current time in ms      |
| SYS_SLEEP        | 0x31   | Sleep for duration          |
| SYS_TIMER_CREATE | 0x32   | Create timer                |
| SYS_TIMER_CANCEL | 0x33   | Cancel timer                |

---

## Usage Patterns

### Request-Reply Pattern

```cpp
// Client side
void send_request(u32 server_channel, u32 reply_channel) {
    Request req{};
    req.type = MSG_TYPE_REQUEST;

    // Send request with reply channel handle
    cap::Handle handles[1] = { reply_channel };
    sys::channel_send(server_channel, &req, sizeof(req), handles, 1);

    // Wait for reply
    Reply reply;
    sys::channel_recv(reply_channel, &reply, sizeof(reply), nullptr, nullptr);
}

// Server side
void handle_requests(u32 service_channel) {
    while (true) {
        Request req;
        cap::Handle reply_handle;
        u32 handle_count;

        sys::channel_recv(service_channel, &req, sizeof(req),
                          &reply_handle, &handle_count);

        Reply reply = process_request(req);
        sys::channel_send(reply_handle, &reply, sizeof(reply), nullptr, 0);
        sys::channel_close(reply_handle); // Close received handle
    }
}
```

### Multiplexed Waiting

```cpp
void poll_multiple_channels() {
    poll::PollEvent events[3];
    events[0] = { .handle = channel_a, .events = EventType::CHANNEL_READ };
    events[1] = { .handle = channel_b, .events = EventType::CHANNEL_READ };
    events[2] = { .handle = HANDLE_CONSOLE_INPUT, .events = EventType::CONSOLE_INPUT };

    while (true) {
        i64 ready = poll::poll(events, 3, -1); // Block indefinitely

        for (u32 i = 0; i < 3; i++) {
            if (has_event(events[i].triggered, EventType::CHANNEL_READ)) {
                // Handle ready channel
            }
        }
    }
}
```

### Service Registration

```cpp
// Server registers itself
void start_server(const char *name) {
    channel::ChannelPair pair;
    channel::create(&pair, 32);

    // Register service
    sys::assign_set(name, pair.recv_handle);

    // Handle requests on recv endpoint
    handle_requests(pair.recv_handle);
}

// Client connects to server
u32 connect_to_server(const char *name) {
    u32 server_handle;
    sys::assign_get(name, &server_handle);
    return server_handle;
}
```

---

## Implementation Details

### Files

| File          | Lines | Description                |
|---------------|-------|----------------------------|
| `channel.hpp` | ~300  | Channel structures and API |
| `channel.cpp` | ~650  | Channel implementation     |
| `poll.hpp`    | ~300  | Poll structures and API    |
| `poll.cpp`    | ~450  | Poll implementation        |
| `pollset.hpp` | ~150  | Poll set management        |
| `pollset.cpp` | ~200  | Poll set implementation    |

### Blocking Behavior

When a channel operation would block:

1. Task is added to the channel's wait queue
2. Task state is set to BLOCKED
3. Scheduler runs other tasks
4. When condition is met, task is woken and resumed

### Thread Safety

- Channel operations are protected by per-channel spinlocks
- Poll operations use the scheduler's lock for wait queue management
- Timer operations are protected by a global timer lock

---

## Performance

### Message Passing Latency

| Operation                             | Typical Latency |
|---------------------------------------|-----------------|
| Non-blocking send (buffer not full)   | ~500ns          |
| Non-blocking recv (message available) | ~600ns          |
| Blocking send/recv (no contention)    | ~2-5μs          |
| Round-trip (request + reply)          | ~10-15μs        |

### Optimizations

- Inline message storage (no allocation per message)
- Fixed-size channel table (no dynamic allocation)
- Wait queue integration with scheduler
- Reference counting for endpoint lifecycle

---

## Limitations

- Maximum 64 concurrent channels
- Maximum 256 bytes per message
- Maximum 4 handles per message
- Fixed buffer sizes (no dynamic expansion)

---

## Priority Recommendations: Next 5 Steps

### 1. Pipe Object Implementation

**Impact:** Enables shell pipelines and stream-based IPC

- Create pipe() syscall returning read/write FD pair
- Integrate with FD table for read()/write() access
- Blocking semantics with fixed buffer size
- Foundation for `|` shell operator

### 2. Channel Buffer Resizing

**Impact:** Better performance for high-throughput scenarios

- Dynamic buffer expansion based on demand
- Shrink buffers when underutilized
- Per-channel capacity configuration
- Reduces message drops under load

### 3. Priority Message Queues

**Impact:** Quality of service for IPC messages

- Priority field in message header
- High-priority messages bypass queue
- Useful for interrupt/signal notifications
- Better real-time response guarantees

### 4. Scatter-Gather Channel Operations

**Impact:** Efficient multi-buffer message assembly

- sendv()/recvv() with iovec arrays
- Avoid buffer copies for fragmented data
- Protocol headers + payload in single operation
- Performance improvement for complex messages

### 5. Channel Debugging and Tracing

**Impact:** Easier IPC debugging

- Optional message logging per channel
- Statistics: message counts, sizes, latency
- Deadlock detection (circular wait chains)
- Foundation for system profiling tools
