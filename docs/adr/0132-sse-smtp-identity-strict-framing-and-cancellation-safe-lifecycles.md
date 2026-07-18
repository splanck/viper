---
status: active
audience: contributors
last-verified: 2026-07-17
---

# ADR 0132: Give SSE and SMTP Strict Framing and Cancellation-Safe Lifecycles

## Status

Accepted

## Context

The SSE and SMTP clients allocated zero-tagged managed payloads and cast public
receivers directly. Forged, stale, or undersized objects could consequently be
interpreted as socket, TLS, mutex, parser, credential, and diagnostic state.
Several String arguments were also read through unchecked layouts or C-string
views that could silently truncate embedded NUL bytes.

SSE parsed HTTP response heads through managed line helpers and accepted
ambiguous framing. Bare LF, folded fields, duplicate singleton headers,
`Transfer-Encoding` plus `Content-Length`, malformed chunks, and forbidden
framing trailers were not consistently rejected. Its per-read timeout could be
renewed by a trickling peer, timed reads could lose partial state, empty data
lines were not represented exactly, and an untyped event could expose the
previous event's type. Concurrent Recv and Close operations could race over
transport ownership.

SMTP read one managed Bytes object per response byte, accepted loose reply
syntax, and did not require a consistent code across multiline responses. It
built a sanitized subject, dot-stuffed body, and complete MIME message in
multiple message-sized allocations. Sends, setters, LastError, and Close read
or mutated shared transport and credential fields without a common ownership
model. Close could free TLS/TCP state while a send still used it, Result
construction could leak partial managed values after a trap, and credential
replacements were neither fully transactional nor wiped before release.

Stable class identifiers change the runtime C ABI. Parser limits, managed/native
ownership, cancellation, and Result recovery cross the runtime networking,
object, TLS, trap, and platform-adapter layers, so the decisions require an ADR.
Existing HTTP/HTTPS SSE, redirects, reconnect, Last-Event-ID, chunked streams,
SMTP plaintext, implicit TLS, STARTTLS, AUTH LOGIN, HTML, Boolean, Result,
LastError, and Close features remain available.

## Decision

- Reserve `RT_SSE_CLASS_ID` as `-0x720217` and `RT_SMTP_CLASS_ID` as
  `-0x720218`. Every non-null public receiver validates stable identity, the
  complete private payload, and native-lock initialization before state access.
  Runtime String arguments are identity- and exact-length-validated before
  their bytes are inspected.
- Construct both clients transactionally. Partial finalizers tolerate every
  native-lock initialization boundary, and nested managed allocation or
  transport traps release all temporary Strings, wrappers, sessions, and
  partially published objects before re-raising the saved diagnostic.
- Parse the SSE HTTP response head with a native buffered reader. Require CRLF,
  token-valid field names, valid field bytes, at most 100 fields and 16 KiB,
  strict status syntax, and at most eight informational responses. Reject
  folded lines, duplicate Content-Type/Content-Length/Transfer-Encoding/
  Content-Encoding/Location, conflicting transfer and content lengths,
  unsupported encodings, and non-EventSource media types.
- Support exact connection-close, Content-Length, and chunked SSE bodies.
  Chunk sizes and extensions are strict, individual chunks are capped at
  16 MiB, trailers have the same bounded grammar, framing fields are forbidden
  in trailers, and Content-Length must end exactly. Event lines are capped at
  64 KiB and accumulated event data at 4 MiB.
- Apply the EventSource line algorithm exactly: accept CRLF, lone CR, and lone
  LF in the event payload; remove one initial UTF-8 BOM; support colonless and
  empty fields; join all data fields, including empty ones, with LF; ignore IDs
  containing NUL; and reset the dispatched event type when an event omits it.
  Permanent 301/308 redirects replace the reconnect URL and safe nonempty IDs
  become `Last-Event-ID` headers.
- Serialize SSE receives. One monotonic deadline covers the whole event,
  including partial lines, chunk framing, and reconnect delay. Timeout preserves
  parser and event state. Close marks cancellation and uses native bidirectional
  shutdown to wake the active receive; only the receive owner frees transport
  state. Metadata getters copy native bytes while locked and allocate managed
  Strings after unlocking.
- Read SMTP transport data into a 4 KiB native buffer. Require reply lines to
  end in CRLF, contain valid bytes, and remain within RFC 5321's 510-byte content
  limit. Multiline replies are capped at 100 lines, keep one status code, and
  end only with `code SP`. Preserve parser errors rather than replacing them
  with generic EHLO diagnostics.
- Permit HELO fallback only after SMTP codes 500, 502, or 504 and only for a
  plaintext session without TLS or AUTH requirements. STARTTLS and AUTH LOGIN
  require advertised EHLO capabilities. AUTH is refused on plaintext.
- Serialize SMTP sends and configuration changes through an operation mutex.
  One send owns transport and reply-buffer state. Close marks cancellation and
  shuts down the native descriptor without freeing active TCP/TLS storage; the
  send owner performs final close and a later send reconnects from scratch.
  Implicit TLS and STARTTLS both publish the TLS owner before their blocking
  handshake so Close can interrupt it.
- Stream SMTP DATA through one 4 KiB native staging buffer. Replace C0/DEL
  header bytes with spaces, normalize CR/LF/CRLF body delimiters to CRLF, apply
  line-leading dot transparency, guarantee a final CRLF, and send the terminator
  without an allocation proportional to subject or body size.
- Store AUTH fields only after both bounded replacements are complete. Cap each
  credential at 16 KiB, encode AUTH LOGIN in bounded native storage, and wipe
  stored, replaced, final, and encoded secret bytes before release.
- Snapshot LastError under the state mutex and perform managed String allocation
  afterward. Boolean sends retain their compatibility result and LastError;
  Result sends catch lower receiver/String/transport/TLS traps only after send
  cleanup, then construct `OkI64(1)` or `ErrStr` under a fresh recovery frame.
  Temporary Strings and partial Results are released on every allocation trap.

## Consequences

- Forged receivers and Strings cannot reach socket, TLS, mutex, credential, or
  parser storage. Returning trap hooks observe one terminal local failure.
- SSE has one unambiguous HTTP/body/event interpretation. Timed reads are
  lossless, empty events remain distinguishable through Result, and Close no
  longer races transport destruction.
- SMTP rejects malformed or desynchronizing replies before another command is
  sent. Legacy HELO interoperability remains available within its safe scope.
- SMTP peak message-framing memory is constant instead of several multiples of
  the caller's body size. Large messages retain existing dot-stuffing and MIME
  behavior without temporary managed objects.
- Concurrent SSE receives and SMTP sends are serialized. Their Close methods
  are cancellation operations, not concurrent deallocators, and both clients
  remain deterministic under later cleanup or reconnect.
- Existing language names and signatures are unchanged. Native embedders that
  inspect managed class tags must recognize the two reserved identifiers.

## Verification

- `test_rt_network_highlevel` covers SSE strict HTTP/chunk framing, BOM and line
  semantics, whole-event timeout preservation, redirect/reconnect state,
  concurrent receives, and prompt Close. It also covers SMTP bare-LF,
  malformed-separator, changed-code, and overlong replies; permitted HELO
  fallback; STARTTLS failure cleanup; large streamed bodies; header sanitation;
  dot transparency; forwarding recipient codes; prompt Close and reconnect;
  and two concurrent serialized sends.
- `test_rt_network_runtime` checks stable SSE/SMTP class identifiers, forged
  receivers, and exact GC baselines when the first constructor allocation is
  injected to fail.
- `test_rt_trap_return_network` verifies forged SSE/SMTP receivers and SMTP
  String arguments trap exactly once when the embedder trap hook returns.
- `docs/zannalib/network.md` documents parser limits, event semantics, SMTP
  streaming, TLS/AUTH policy, Result ownership, synchronization, and Close.
- `scripts/lint_platform_policy.sh` verifies this code uses the shared platform
  capability/socket adapters rather than adding raw OS branches.

## Alternatives Considered

- Keep zero class identifiers and validate only payload size: rejected because
  unrelated zero-tagged objects can satisfy a size check.
- Continue using managed one-byte/one-line transport helpers: rejected because
  they amplify allocation and recovery complexity inside protocol parsers.
- Accept permissive HTTP or SMTP lines for compatibility: rejected because
  ambiguous framing can desynchronize security-sensitive state machines.
- Reset SSE state on timeout: rejected because it loses bytes and can publish a
  fragment as a different event on the next call.
- Let Close directly free active TLS/TCP state: rejected because another thread
  can still be decrypting, sending, or parsing through that storage.
- Build the entire SMTP message before sending: rejected because it multiplies
  peak memory and makes large bodies unnecessarily expensive.
- Drop HELO, Boolean sends, LastError, HTML, reconnect, redirects, chunked SSE,
  implicit TLS, STARTTLS, or AUTH LOGIN: rejected because hardening must preserve
  the existing runtime feature surface.
