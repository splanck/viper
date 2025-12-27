# IPC Subsystem

**Status:** Fully functional message-passing with capability transfer
**Location:** `kernel/ipc/`
**SLOC:** ~2,143

## Overview

The IPC subsystem provides inter-process communication through message-passing channels, polling primitives for waiting on readiness, and poll sets for monitoring multiple handles. The system supports capability transfer, allowing handles to be passed between processes.

---

## Components

### 1. Channels (`channel.cpp`, `channel.hpp`)

**Status:** Complete with capability transfer support

**Implemented:**
- Fixed-size channel table (64 channels)
- Circular message buffer (16 messages × 256 bytes)
- Bidirectional message passing
- Separate send/recv endpoints with reference counting
- Capability handle transfer (up to 4 handles per message)
- Blocking send/recv with task suspension
- Non-blocking try_send/try_recv
- Send/recv blocked task queues
- Channel close with blocked task wakeup
- Spinlock protection for thread safety

**Channel Structure:**
```
┌───────────────────────────────────────────────┐
│                 Channel                        │
│  ┌─────────┐  ┌───────────────────────────┐   │
│  │  State  │  │   Message Buffer (16)      │   │
│  │ id, refs│  │ [msg][msg][msg]...[msg]    │   │
│  └─────────┘  │  read_idx ↑     write_idx ↑│   │
│               └───────────────────────────┘   │
│  ┌─────────────────────────────────────────┐  │
│  │ send_blocked  │  recv_blocked           │  │
│  │ (waiting task)│  (waiting task)         │  │
│  └─────────────────────────────────────────┘  │
└───────────────────────────────────────────────┘
```

**Message Structure (per slot):**
| Field | Size | Description |
|-------|------|-------------|
| data | 256 | Message payload |
| size | 4 | Actual bytes used |
| sender_id | 4 | Sender task ID |
| handle_count | 4 | Transferred handles (0-4) |
| handles | 56 | TransferredHandle[4] |

**TransferredHandle:**
| Field | Description |
|-------|-------------|
| object | Kernel object pointer |
| kind | cap::Kind value |
| rights | Original rights mask |

**Channel States:**
| State | Description |
|-------|-------------|
| FREE | Slot available |
| OPEN | Active channel |
| CLOSED | Being torn down |

**Endpoint Rights:**
- **Send**: `CAP_WRITE | CAP_TRANSFER | CAP_DERIVE`
- **Recv**: `CAP_READ | CAP_TRANSFER | CAP_DERIVE`

**API:**
| Function | Description |
|----------|-------------|
| `create(ChannelPair*)` | Create channel, get endpoints |
| `try_send(ch, data, size, handles, count)` | Non-blocking send |
| `try_recv(ch, buf, size, handles, count)` | Non-blocking recv |
| `send(id, data, size)` | Blocking send (legacy) |
| `recv(id, buf, size)` | Blocking recv (legacy) |
| `close_endpoint(ch, is_send)` | Close one endpoint |
| `has_message(ch)` | Check if data available |
| `has_space(ch)` | Check if buffer has space |

**Handle Transfer Flow:**
```
Sender:                         Receiver:
  cap_table[h1] →
  cap_table[h2] →
       ↓ transfer
  [msg + handles]  ────────────→ [recv msg]
       ↓                              ↓
  cap_table[h1] REMOVED         cap_table[h1'] ← new handle
  cap_table[h2] REMOVED         cap_table[h2'] ← new handle
```

**Not Implemented:**
- Multicast channels
- Channel priority
- Message ordering guarantees across channels
- Per-channel memory limits
- Credential passing (uid/gid)
- Zero-copy transfer for large messages

**Recommendations:**
- Add channel capacity configuration
- Implement priority-based message ordering
- Add large message support (shared memory)

---

### 2. Poll (`poll.cpp`, `poll.hpp`)

**Status:** Complete with timer support

**Implemented:**
- Event-based polling for multiple handles
- Timer table (32 timers)
- Event types: channel read/write, timer, console input
- Timeout support (blocking, non-blocking, infinite)
- Sleep implementation via timers
- Periodic timer check from scheduler tick
- Test function for validation

**Event Types (bitmask):**
| Type | Value | Description |
|------|-------|-------------|
| NONE | 0 | No events |
| CHANNEL_READ | 1 | Channel has data |
| CHANNEL_WRITE | 2 | Channel has space |
| TIMER | 4 | Timer expired |
| CONSOLE_INPUT | 8 | Console has input |

**PollEvent Structure:**
| Field | Description |
|-------|-------------|
| handle | Channel/timer ID or pseudo-handle |
| events | Requested event mask (input) |
| triggered | Ready event mask (output) |

**Timer Structure:**
| Field | Description |
|-------|-------------|
| id | Timer handle |
| expire_time | Absolute ms when expires |
| active | Timer is in use |
| waiter | Blocked task (for sleep) |

**Constants:**
- `MAX_POLL_EVENTS`: 16 (per poll call)
- `MAX_TIMERS`: 32
- `HANDLE_CONSOLE_INPUT`: 0xFFFF0001 (pseudo-handle)

**API:**
| Function | Description |
|----------|-------------|
| `poll(events, count, timeout)` | Wait for events |
| `timer_create(timeout_ms)` | Create one-shot timer |
| `timer_expired(id)` | Check if timer fired |
| `timer_cancel(id)` | Cancel and free timer |
| `sleep_ms(ms)` | Sleep current task |
| `time_now_ms()` | Get monotonic time |
| `check_timers()` | Wake expired timers |

**Poll Timeout Behavior:**
| timeout_ms | Behavior |
|------------|----------|
| 0 | Non-blocking, immediate return |
| > 0 | Wait up to timeout ms |
| < 0 | Wait indefinitely |

**Not Implemented:**
- Edge-triggered events
- One-shot events (auto-remove)
- Event coalescing
- Per-event timeout
- Signal interruption

**Recommendations:**
- Add edge-triggered mode for efficiency
- Implement level/edge selection per entry

---

### 3. Poll Sets (`pollset.cpp`, `pollset.hpp`)

**Status:** Complete for user-space polling

**Implemented:**
- Fixed-size poll set table (16 sets)
- Fixed-size entry array per set (16 entries)
- Create/destroy poll sets
- Add/remove/update watched handles
- Blocking wait with timeout
- Owner task tracking
- Capability-based channel lookup
- Console input pseudo-handle support
- Event notification (wake on event)
- Test function for validation

**PollSet Structure:**
| Field | Description |
|-------|-------------|
| id | Poll set handle |
| active | Set is in use |
| owner_task_id | Creator task |
| entries[16] | Watched handles |
| entry_count | Active entries |

**PollEntry:**
| Field | Description |
|-------|-------------|
| handle | Channel/timer/pseudo handle |
| mask | Event types to watch |
| active | Entry is in use |

**API:**
| Function | Description |
|----------|-------------|
| `create()` | Create poll set |
| `add(id, handle, mask)` | Add/update entry |
| `remove(id, handle)` | Remove entry |
| `wait(id, out, max, timeout)` | Wait for events |
| `destroy(id)` | Destroy poll set |

**Readiness Checking:**
The `wait()` implementation:
1. For each active entry, check readiness
2. If any ready, fill output array and return
3. If timeout=0, return immediately
4. Otherwise yield and repeat

**Handle Types Supported:**
- `HANDLE_CONSOLE_INPUT` - keyboard/serial input
- Channel handles (via cap_table lookup)
- Timer handles (via timer table lookup)
- Legacy channel IDs (fallback)

**Not Implemented:**
- Per-task poll set isolation
- Poll set inheritance
- epoll-style level/edge modes

**Recommendations:**
- Add per-task poll set limits
- Add edge-triggered mode for efficiency

---

## Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                      User Space                              │
│   (poll_create, poll_add, poll_wait, channel_send, etc.)    │
└──────────────────────────────┬──────────────────────────────┘
                               │ Syscalls
                               ▼
┌─────────────────────────────────────────────────────────────┐
│                    Pollset Layer                             │
│  ┌─────────────────────────────────────────────────────┐    │
│  │ PollSet[16] → PollEntry[16] (handle + mask)         │    │
│  │ wait() → check_readiness() → yield loop             │    │
│  └─────────────────────────────────────────────────────┘    │
└──────────────────────────────┬──────────────────────────────┘
                               │
         ┌─────────────────────┼─────────────────────┐
         ▼                     ▼                     ▼
┌─────────────────┐  ┌─────────────────┐  ┌─────────────────┐
│    Channels     │  │     Timers      │  │  Console Input  │
│  Channel[64]    │  │   Timer[32]     │  │  (pseudo-handle)│
│  Message[16]    │  │  expire_time    │  │  has_char()     │
│  send/recv refs │  │  waiter task    │  │                 │
└────────┬────────┘  └────────┬────────┘  └─────────────────┘
         │                    │
         ▼                    ▼
┌─────────────────────────────────────────────────────────────┐
│                      Scheduler                               │
│       enqueue(waiter) when event ready                       │
└─────────────────────────────────────────────────────────────┘
```

---

## Syscall Interface

The IPC subsystem is accessed via these syscalls:

| Syscall | Number | Function |
|---------|--------|----------|
| channel_create | 0x10 | `channel::create()` |
| channel_send | 0x11 | `channel::send(id, data, size)` |
| channel_recv | 0x12 | `channel::recv(id, buf, size)` |
| channel_close | 0x13 | `channel::close(id)` |
| poll_create | 0x20 | `pollset::create()` |
| poll_add | 0x21 | `pollset::add(id, handle, mask)` |
| poll_remove | 0x22 | `pollset::remove(id, handle)` |
| poll_wait | 0x23 | `pollset::wait(id, out, max, timeout)` |

---

## Error Codes

| Code | Name | Description |
|------|------|-------------|
| 0 | VOK | Success |
| -1 | VERR_UNKNOWN | Unknown error |
| -2 | VERR_INVALID_ARG | Invalid argument |
| -3 | VERR_INVALID_HANDLE | Bad channel/handle |
| -4 | VERR_OUT_OF_MEMORY | No free slots |
| -5 | VERR_WOULD_BLOCK | Operation would block |
| -6 | VERR_CHANNEL_CLOSED | Channel was closed |
| -7 | VERR_NOT_FOUND | Handle not found |
| -8 | VERR_MSG_TOO_LARGE | Message exceeds limit |

---

## Testing

The IPC subsystem includes self-tests:
- `poll::test_poll()` - Channel polling behavior
- `pollset::test_pollset()` - Poll set operations

Tests verify:
- Empty channel is writable but not readable
- Channel with message is readable
- Poll correctly reports triggered events

---

## Files

| File | Lines | Description |
|------|-------|-------------|
| `channel.cpp` | ~740 | Channel implementation |
| `channel.hpp` | ~239 | Channel interface |
| `poll.cpp` | ~371 | Polling/timer implementation |
| `poll.hpp` | ~212 | Poll interface |
| `pollset.cpp` | ~441 | Poll set implementation |
| `pollset.hpp` | ~140 | Poll set interface |

---

## Priority Recommendations

1. **High:** Implement proper event notification instead of polling
2. **High:** Add per-task poll set isolation
3. **Medium:** Support zero-copy for large messages
4. **Medium:** Add multicast/broadcast channels
5. **Low:** Add edge-triggered event mode
6. **Low:** Implement channel priority ordering
