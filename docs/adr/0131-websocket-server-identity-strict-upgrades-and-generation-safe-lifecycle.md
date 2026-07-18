---
status: active
audience: contributors
last-verified: 2026-07-17
---

# ADR 0131: Give WebSocket Servers Stable Identity and Generation-Safe Lifecycles

## Status

Accepted

## Context

The plain and TLS WebSocket servers allocated managed payloads with class
identifier zero and cast public receivers directly. Forged, stale, or
undersized handles could therefore be interpreted as listener pointers,
credentials, mutexes, worker pools, and client slots. String and Bytes
arguments were also inspected through unchecked layouts.

Lifecycle state was not serialized. Constructors eagerly created potentially
large worker pools, Start and Stop could race, and Stop drained a pool without
shutting down its idle workers. Accepted sockets became visible only after both
TLS and HTTP handshakes, so queued or silent clients could make Stop wait for a
full transport timeout. Broadcast snapshots carried only raw pointers; slot
reuse could turn a delayed send or cleanup into an ABA operation against a new
client. WSS narrowed or manipulated native sockets outside the shared adapter
policy in some paths.

The HTTP upgrade parser accepted bare LF, obsolete folding, duplicate
security-sensitive singleton fields, non-canonical nonce encodings, ambiguous
authorities, and body-bearing upgrades. It bounded individual lines and field
count but not total request bytes. Frame handling did not validate UTF-8 across
fragment boundaries or cap the aggregate fragmented message. The plain server
also built several temporary managed Bytes objects for every received frame.

Stable class identifiers and a new native socket interruption helper change
the runtime C ABI. Slot ownership, TLS handoff, thread-pool shutdown, and
strict protocol parsing cross runtime layers, so these decisions require an
ADR. Existing WS/WSS construction, Start/Stop, subprotocol, broadcast, binary,
ping/pong, close, fragmentation, TLS, and 128-slot features remain available.

## Decision

- Reserve `RT_WS_SERVER_CLASS_ID` as `-0x720215` and
  `RT_WSS_SERVER_CLASS_ID` as `-0x720216`. Every non-null public server
  receiver validates stable identity and the complete private payload before
  reading state. String, Bytes, TCP-client, credential, and subprotocol inputs
  are identity- and exact-length-validated before payload access.
- Treat construction and Start as publication transactions. Credential and
  token staging precedes publication; partial finalizers tolerate every mutex
  initialization boundary. A lifecycle mutex serializes Start, Stop, and
  subprotocol mutation, while state getters take synchronized snapshots and
  never retain native staging across an uncaught managed allocation trap.
- Create worker pools lazily at Start, cap the CPU-aware pool at 32 workers,
  and shut down, join, and release the pool at every Stop. Restart creates a
  fresh pool. The fixed table still admits 128 visible accepted connections;
  connections beyond the active worker count remain queued until a worker is
  available.
- Reserve a client slot before submitting its task. Each reservation receives
  a nonzero 64-bit generation. Queued, handshaking, and upgraded transports are
  visible to Stop; broadcast and cleanup require the same slot, pointer, and
  generation before acting. A delayed worker can never clear or send through a
  reused generation.
- Give the task and slot explicit managed references during raw TCP ownership.
  WSS detaches the pointer-width native descriptor exactly once, publishes it
  as a pending TLS handoff, and recovers session-allocation traps by closing the
  descriptor and retiring the generation. Once TLS exists, the slot retains a
  managed session reference while the worker owns the closing reference.
- Add `rt_socket_shutdown_both(socket_t)` to the shared platform adapter. Stop
  uses bidirectional shutdown to interrupt in-progress TLS I/O without freeing
  TLS buffers concurrently with their worker. The worker performs the final
  protocol-aware close. POSIX and WinSock implementations preserve native
  descriptor width and ownership.
- Parse both plain and TLS upgrades through the same strict state machine.
  Require CRLF, valid HTTP token names/value bytes, exact HTTP/1.1 GET syntax,
  at most 100 fields and 16 KiB total request bytes, one Host, key, version,
  Origin, protocol, and Content-Length singleton, canonical 16-byte base64
  nonce encoding, valid bracketed IP literals or registered names, and strict
  Connection/Upgrade token lists. Reject obsolete folding, bare LF,
  transfer-coding, nonzero request bodies, malformed ports, and mismatched
  browser Origin.
- Require masked client frames, canonical lengths with the 64-bit high bit
  clear, valid opcodes/control frames/close payloads, and a 64 MiB frame and
  aggregate-message limit. Validate text incrementally across continuation
  frames so a UTF-8 scalar may cross frame boundaries without accepting an
  incomplete, overlong, surrogate, or out-of-range scalar.
- Receive plain frames directly into one native payload instead of allocating
  managed header, length, mask, and payload intermediates. Native allocation
  failure sends Close 1011 when possible. Managed message publication uses a
  local recovery frame so native assembly is freed before an allocation trap
  propagates.
- Serialize writes per client. Post-upgrade sends have a two-second bound; WSS
  restores blocking receive behavior after applying that send policy so idle
  clients are not disconnected merely for being quiet. Stop interrupts all
  queued and active I/O, joins every task, clears slots, and is restart-safe.

## Consequences

- Forged receivers and payloads cannot reach mutex, listener, TLS, or slot
  state, and returning trap hooks observe one failure without fallthrough.
- Stop latency no longer depends on a silent peer completing TLS or HTTP
  handshakes. Detached WSS descriptors remain cancellable without double-close
  or use-after-free.
- Pointer reuse cannot redirect broadcast failure cleanup to another client.
  ClientCount continues to report only fully upgraded active clients.
- Upgrade syntax now has one interpretation for WS and WSS. Peers using bare
  LF, folded headers, duplicate singleton fields, non-canonical keys, request
  bodies, or malformed authorities are rejected.
- Fragmented text is validated correctly and aggregate size cannot bypass the
  per-frame limit. Plain receive removes several managed allocations and copies
  per frame.
- Stop releases worker threads rather than leaving one idle pool per stopped
  server. A later Start pays the intentional cost of constructing a fresh pool.
- Existing language names and signatures are unchanged. Native embedders that
  inspect managed identities or link the socket adapter must recognize the new
  identifiers and shutdown helper.

## Verification

- `test_rt_network_highlevel` covers canonical WS/WSS upgrades, duplicate and
  folded-header rejection, bare-LF and body-bearing rejection, canonical nonce
  enforcement, high-bit lengths, masking, forbidden close codes, UTF-8 split
  across frames, 1007/1002 close behavior, broadcast, stable WSS identity, and
  prompt Stop during incomplete HTTP and TLS handshakes.
- `test_rt_network_runtime` checks the stable plain-server class identifier,
  forged receivers, and exact tracked-object baselines for injected WS/WSS
  constructor allocation failures.
- `test_rt_trap_return_network` verifies forged WS/WSS server receivers and
  Bytes calls trap exactly once with a returning embedder hook.
- The POSIX socket-platform test covers the shared native adapter; Windows
  compilation exercises the WinSock `SD_BOTH` implementation and pointer-width
  `socket_t` signature.
- `docs/zannalib/network.md` documents strict upgrade syntax, fragmentation,
  worker/slot limits, shutdown, timeout, identity, and restart behavior.

## Alternatives Considered

- Keep class identifier zero and validate only payload size: rejected because
  unrelated zero-tagged objects can satisfy the same size threshold.
- Register clients only after upgrade: rejected because Stop cannot cancel
  work it cannot see.
- Close and free TLS sessions directly from Stop: rejected because a worker may
  still be reading or decrypting through that state.
- Rely only on the ten-second TLS timeout: rejected because deterministic Stop
  should actively interrupt local I/O rather than wait for hostile peers.
- Retain idle pools across Stop for faster restart: rejected because stopped
  servers otherwise accumulate native worker threads until garbage collection.
- Concatenate every fragmented text message only to validate UTF-8: rejected
  because incremental scalar state is exact and avoids an unnecessary second
  allocation for background-drained messages.
- Preserve permissive HTTP parsing for compatibility: rejected because an
  ambiguous upgrade changes the protocol interpretation of the same byte
  stream.
- Remove TLS, subprotocol, fragmentation, binary broadcast, restart, or the
  fixed client table: rejected because hardening must preserve existing
  features.
