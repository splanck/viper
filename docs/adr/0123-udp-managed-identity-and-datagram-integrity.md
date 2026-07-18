---
status: active
audience: contributors
last-verified: 2026-07-17
---

# ADR 0123: Preserve UDP Managed Identity and Atomic Datagram Integrity

Status: Accepted

Date: 2026-07-17

## Context

`Zanna.Network.Udp` objects were allocated with class id zero and every native
entry point cast an opaque `void *` receiver directly to the private UDP
structure. A wrong-class, undersized, or uninitialized managed object could
therefore be interpreted as a socket descriptor. Returning trap hooks made the
unchecked boundary capable of continuing after a diagnostic.

UDP constructors acquired or bound a native socket before managed object
allocation. Because allocation traps can transfer control with `longjmp`, the
ordinary NULL checks after `rt_obj_new_i64` could not close the socket or free
the staged address on the recovered-trap path.

The receive path allocated a maximum-sized `Bytes`, consumed a datagram, and
then allocated an exact-sized result. Failure of the second allocation leaked
the first object. Truncation and native receive errors raised a trap but did not
return, so a returning trap hook could continue through freed storage. On
platforms with `FIONREAD`, an oversized datagram was rejected before receive and
left at the head of the queue, causing every later receive to fail on the same
packet. Negative sizes were treated like zero, and empty sends skipped the
native call even though UDP supports real zero-length datagrams.

Stable identity changes the runtime C ABI contract and therefore requires this
ADR.

## Decision

- Assign `RT_UDP_CLASS_ID` and require managed heap kind, stable class id,
  complete payload size, and private initialization magic at every non-null UDP
  receiver boundary.
- Publish `New`, `Bind`, and `BindAt` through one ownership-transfer helper with
  a local trap recovery frame. Allocation failure closes the native socket and
  frees the optional address buffer before re-raising the original diagnostic.
- Require `SendTo` payloads to be real `Bytes` and `SendToStr` payloads to be
  real runtime strings. Preserve embedded NUL bytes, send zero-length payloads
  through the OS, cap datagrams at 65,507 bytes, and reject any unexpected
  partial native send.
- Reject negative receive sizes. A zero maximum remains an immediate empty
  operation that consumes no packet.
- Receive an oversized datagram so the OS removes it from the queue, discard the
  truncated prefix, and raise `Err_ProtocolError`. Do not preflight with
  `FIONREAD` because that leaves the offending atomic message queued.
- Protect the post-receive right-sizing allocation with local trap recovery.
  Release the maximum-sized staging `Bytes` on every allocation, truncation,
  timeout, and socket-error path. Publish sender metadata only after a complete
  result exists.
- Capture native socket errors before managed cleanup and classify the saved
  value afterward, so allocator/registry work cannot overwrite timeout or
  network error state.
- Make persistent timeout changes transactional: validate an open socket, apply
  `SO_RCVTIMEO`, then update stored state only after native success.
- Keep UDP operations externally serialized. This decision hardens identity and
  single-operation ownership but does not add internal locking around mutable
  sender metadata, multicast options, or close.

## Consequences

- Forged managed values cannot be interpreted as UDP descriptors, including
  when an embedder installs a returning trap hook.
- Recoverable managed-allocation failure after socket creation, bind, or receive
  does not leak the native socket, address string, or staging `Bytes`.
- A rejected oversized datagram is consumed exactly once. The next receive can
  make progress instead of repeatedly trapping on the same queue head.
- Empty `Bytes` and strings now transmit standards-compliant zero-length UDP
  datagrams. `RecvFor` distinguishes such a datagram (non-null empty `Bytes`)
  from timeout (NULL); persistent-timeout `Recv` retains its documented
  ambiguity.
- Sender host/port describes only the last successfully published datagram.
- UDP payload layout gains a private magic field. The structure remains opaque;
  only the stable class identifier is part of the native runtime contract.

## Alternatives Considered

- Keep class id zero and validate only payload size: rejected because unrelated
  same-sized managed objects would still pass the boundary.
- Peek the next datagram size and leave oversized packets queued: rejected
  because callers have no discard API and the socket becomes permanently
  wedged behind one bad packet.
- Grow the result beyond the caller's maximum: rejected because it violates the
  explicit receive bound and enables peer-controlled allocation.
- Return a truncated prefix with a flag: rejected because the current public API
  has no truncation metadata and silently changing atomic-message semantics is
  unsafe.
- Treat empty sends as no-ops: rejected because zero-length datagrams are valid
  protocol messages and `RecvFor` can already represent them distinctly from a
  timeout.
