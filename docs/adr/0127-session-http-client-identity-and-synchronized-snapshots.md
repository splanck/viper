---
status: active
audience: contributors
last-verified: 2026-07-17
---

# ADR 0127: Give Session HTTP Clients Stable Identity and Synchronized Snapshots

## Status

Accepted

## Context

`Zanna.Network.HttpClient` and `Zanna.Network.RestClient` are the two reusable
HTTP session surfaces. Both previously allocated public receiver objects with
class identifier zero. Most methods then cast any non-null managed pointer to a
private payload containing Maps, native cookie links, response pointers, and
mutex-bearing connection pools. A Seq, stale handle, or unrelated zero-tagged
object could therefore reach native synchronization or be finalized as the
wrong payload.

The clients also exposed mutable state with inconsistent synchronization.
RestClient headers and last-response diagnostics were documented as unsafe;
HttpClient locked individual setters but performed managed allocation and
cookie parsing in critical sections without complete recovery ownership.
Concurrent keep-alive enable/resize operations could publish a pool built for a
stale capacity, and resizing a disabled client reattached an unused pool.

Request setup copied some values but did not own the entire redirect or helper
transaction. Traps while applying defaults, building Cookie fields, capturing
Set-Cookie, resolving Location, parsing JSON, or constructing Result values
could leak requests, intermediate responses, Strings, or native cookie staging.
`RestClient.BaseUrl` returned its internal String rather than an independent
reference. `SetTimeout(0)` was accepted as the documented no-deadline value but
was skipped during request construction, leaving the lower-level 30-second
default in effect.

Stable class identifiers and changes to the shared native header-map helper
affect the runtime C boundary. The snapshot, timeout, and pool-publication
rules are cross-layer runtime contracts, so they require an explicit decision.
All language-visible client methods and features remain available.

## Decision

- Reserve `RT_HTTP_CLIENT_CLASS_ID` as `-0x72020D` and
  `RT_RESTCLIENT_CLASS_ID` as `-0x72020E`. Every non-null public receiver must
  validate stable class identity, complete payload size, and initialized
  synchronization state before private access. Historical NULL neutral/no-op
  behavior remains where the public API already promised it.
- Initialize each native mutex before fallible subordinate state is published.
  Install a partial-safe finalizer first, and destroy the mutex only when its
  initialization flag is set. Constructor recovery releases the header Map,
  pool, base URL, and native lock at every allocation boundary.
- Synchronize mutable defaults, cookie jars, redirect/timeout policy, pool
  configuration, and RestClient last-response diagnostics. Request paths retain
  complete header/base/pool snapshots before transport so the client lock is
  not held across network work. Trap recovery must unlock and release every
  partial snapshot before re-raising the original diagnostic.
- Make shared case-insensitive header replacement transactional. The internal
  `rt_http_header_map_set_ci` and `rt_http_header_map_remove_ci` helpers return
  success/failure, contain managed traps, and require a complete key snapshot
  before mutation. Insert the requested spelling before removing aliases; an
  allocation failure preserves the previous complete value.
- Return an independent caller-owned copy from `RestClient.BaseUrl`. Return a
  retained caller reference from `LastResponse`, while the client keeps its own
  synchronized cached reference. Updating or clearing the cache releases the
  previous client-owned response after detachment.
- Execute RestClient request setup, Result methods, authentication staging, and
  JSON helpers as explicit ownership transactions. Result error construction
  uses a fresh recovery frame after the request frame is cleared. JSON helpers
  consume their temporary raw HttpRes reference after body copying/parsing; the
  compatibility last-response cache remains separately retained.
- Execute each HttpClient verb and redirect chain beneath one heap-backed
  transaction. Copy URL and optional body by exact runtime length, track the
  current request/response and Location/next-URL objects, strip sensitive
  defaults on cross-origin hops, and release all intermediate state before
  preserving the original generic or network-categorized trap.
- Stage all response Set-Cookie fields without holding the client mutex. Parse,
  validate, and allocate the complete detached native cookie list first, then
  merge it under one allocation-free critical section. Native OOM leaves the
  pre-response jar unchanged rather than accepting a prefix. `GetCookies`
  copies native bytes while locked and constructs its caller-owned managed Map
  after unlocking with recovery for every partial key/value.
- Build replacement connection pools outside the client lock. Enabling must
  recheck the configured size before publication; resizing publishes a pool
  only if keep-alive is still enabled. Disabling atomically detaches, then
  clears/releases the old pool after unlocking. Superseded and losing-race
  pools are always released.
- Apply every accepted timeout to the lower-level HttpReq, including zero.
  Zero therefore means no address/socket-operation deadline instead of falling
  back to the request constructor's 30-second default.

## Consequences

- Wrong-class and stale managed handles cannot reach client mutexes, cookie
  pointers, header Maps, cached responses, or pools.
- One HttpClient or RestClient can be shared across concurrent callers. Each
  request observes one complete snapshot; final RestClient diagnostics reflect
  whichever completed request publishes last.
- Header/authentication replacement and response-cookie capture preserve prior
  state on allocation failure. Cookie parsing and managed Map construction no
  longer extend the jar critical section.
- Request, redirect, JSON, Result, and diagnostic ownership is balanced on
  success, nested traps, and returning embedder trap hooks. No fallback success
  object is manufactured after failure.
- Keep-alive state and actual pool capacity cannot diverge through an
  enable/resize race, and a disabled client retains no idle pool.
- `BaseUrl`, `LastResponse`, responses, and cookie Maps have explicit
  caller-owned snapshot/reference semantics.
- The two new class identifiers and the success return from the internal
  header-map helpers extend the native runtime contract. Public registry names,
  signatures, redirect/cookie behavior, authentication, JSON helpers, and all
  existing session features are preserved.

## Verification

- `test_rt_http_client` verifies stable identity, forged-receiver rejection,
  constructor and malformed-request allocation sweeps, cookie-Map allocation
  sweeps, case-insensitive header rollback on every allocation boundary, wire
  uniqueness under concurrent replacement, concurrent cookie snapshots, pool
  enable/resize races, and exact managed-object baselines.
- `test_rt_restclient` verifies stable identity, constructor and Result
  allocation sweeps, copied BaseUrl ownership, transactional headers,
  synchronized concurrent replacement, keep-alive reuse, cross-origin
  credential stripping, and raw/JSON/Result behavior.
- `test_rt_network_highlevel` verifies cookie scope and policy, response-cookie
  handling, redirect rules, sensitive-header stripping, informational
  responses, case-insensitive configuration, and keep-alive reuse.
- `test_rt_trap_return_network` verifies both client surfaces raise exactly one
  trap and stop local control flow for forged or invalid String handles when an
  embedder trap hook returns.
- `docs/zannalib/network.md` and both public C headers document stable receiver
  validation, synchronized snapshots, independent return ownership, timeout
  zero, cookie staging, and transactional pool publication.

## Alternatives Considered

- Keep class identifier zero and validate only payload size: rejected because
  unrelated zero-tagged objects can satisfy the same size threshold.
- Hold the session mutex through network transport: rejected because slow or
  blocked I/O would serialize unrelated requests and setters.
- Allocate managed snapshots while locked without recovery: rejected because a
  non-local trap would strand the mutex and partial references.
- Parse and publish each Set-Cookie field independently: rejected because OOM
  would silently expose a response-dependent prefix of the intended jar update.
- Reattach a pool whenever its capacity changes: rejected because disabled
  clients should not retain unused sockets or a hidden pool reference.
- Treat timeout zero as “use default”: rejected because the public contract
  already defines zero as disabling the deadline.
- Remove concurrent use, cookie support, redirect behavior, JSON helpers, or
  compatibility diagnostics: rejected because hardening must preserve the full
  runtime feature surface.
