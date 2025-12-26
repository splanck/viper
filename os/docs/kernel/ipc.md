# IPC: Channels, Poll, and Timers

ViperOS’ IPC is built from a small set of primitives:

- **Channels**: bounded message queues with optional handle transfer
- **Poll**: a readiness/polling loop for channels and timers
- **Timers/sleep**: one-shot timers used to implement `sleep` and poll timeouts

Together, these provide the “coordination substrate” for both kernel test tasks and user processes.

## Channels: a bounded message queue

The channel subsystem lives in `kernel/ipc/channel.*`.

### Mental model

A channel is:

- a fixed-size ring buffer of messages
- a small amount of state for blocked sender/receiver tasks (bring-up model)
- reference counts for the two endpoints (send vs receive) in the “new” capability-based API

The subsystem is implemented as a global fixed-size table of channels, which keeps memory usage predictable and makes
debugging easier during early development.

### Message shape

Messages include:

- a byte payload (`MAX_MSG_SIZE`)
- sender task id
- a list of transferred handles (optional)

### Non-blocking operations

The syscall layer treats most channel operations as non-blocking:

- `try_send` returns `VERR_WOULD_BLOCK` if the channel is full
- `try_recv` returns `VERR_WOULD_BLOCK` if the channel is empty

Blocking is done at a higher level (via poll or explicit waiting loops) so the kernel doesn’t have to implement a
complex scheduler-integrated wait mechanism right away.

Key files:

- `kernel/ipc/channel.hpp`
- `kernel/ipc/channel.cpp`

## Handle transfer: moving authority across tasks

Channels can carry capabilities:

1. The sender provides handles to transfer along with the message.
2. The kernel validates each handle in the sender’s cap table and checks `CAP_TRANSFER`.
3. The kernel removes the handle from the sender’s cap table and stores the underlying `(object, kind, rights)` in the
   message.
4. The receiver, upon message receipt, gets newly inserted handles in its own cap table.

This provides a concrete mechanism for “capability delegation”: the sender can give the receiver exactly the authority
encoded in the handle(s).

## Poll: readiness waiting without deep blocking primitives

The poll subsystem in `kernel/ipc/poll.*` provides a small “wait until ready” loop:

- it can watch:
    - channel readability (`CHANNEL_READ`)
    - channel writability (`CHANNEL_WRITE`)
    - timer expiry (`TIMER`)
- it returns when at least one requested condition becomes true

### How it waits

The poll loop is intentionally simple:

- check all conditions
- if nothing is ready:
    - if timeout is 0: return immediately
    - if deadline passed: return 0
    - otherwise: `task::yield()` and try again

This is a cooperative design: it relies on the scheduler to give other tasks a chance to make progress that might
satisfy the polled conditions.

Key files:

- `kernel/ipc/poll.hpp`
- `kernel/ipc/poll.cpp`

## Timers and `sleep`

Timers are stored in a small fixed-size timer table as one-shot “expires at time T” entries.

Time comes from the global tick counter in `kernel/arch/aarch64/timer.*` (1 ms ticks). `poll::time_now_ms()` returns
that tick count.

`poll::sleep_ms(ms)` is implemented by:

1. creating a timer entry with expiry = now + ms
2. marking the current task blocked and yielding until the timer is expired
3. canceling the timer entry and returning success

The periodic timer interrupt calls `poll::check_timers()` to wake tasks whose timer has expired.

Key files:

- `kernel/ipc/poll.cpp`
- `kernel/arch/aarch64/timer.cpp`

## Pollset: a higher-level grouping (bring-up)

`kernel/ipc/pollset.*` adds a “set of pollable things” abstraction on top of `poll`, mainly as scaffolding and testing
infrastructure during bring-up.

## Current limitations and next steps

- Channel table is global; a future capability-only design may move to heap-allocated channel objects (`kobj::Channel`)
  and remove raw numeric IDs.
- Polling loops and timer-table management are simple and not optimized for large numbers of waiters.
- There is no general “wait queue” framework yet; blocking semantics are specialized and cooperative in style.

