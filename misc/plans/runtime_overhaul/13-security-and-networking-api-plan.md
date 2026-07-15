# Security And Networking API Plan

## Goal

Make crypto and networking APIs safe by default while preserving enough power
for real applications, protocol work, compatibility, and local testing.

Security-sensitive APIs should make the safe path obvious and the dangerous path
hard to choose accidentally.

## Crypto Findings

### Legacy hashes are too easy to discover

Current public hash surfaces include MD5, SHA1, HMAC-MD5, HMAC-SHA1, CRC32, and
modern SHA variants in the same broad area.

Decision:

- keep modern hashes in the primary docs.
- move legacy cryptographic hashes to `Viper.Crypto.Legacy.Hash` or mark them
  `legacy` in catalog metadata.
- document CRC32 as checksum/integrity-only, not cryptographic.
- add docs and example audits that prevent MD5/SHA1 from appearing in ordinary
  new-code examples.

### Legacy encryption helpers need stronger naming

AES-CBC convenience helpers are useful for compatibility but should not be the
most visible AES API.

Decision:

- authenticated encryption is the primary path.
- legacy modes live under `Legacy` or include `Legacy` in the name.
- docs explain migration from legacy encrypted data to authenticated formats.

### Decryption failure must not be silent

Current decrypt APIs can return `NULL` for corrupt or authentication-failed
data.

Decision:

- decryption returns `Result<Bytes>` when diagnostics matter.
- a `TryDecrypt` helper may return `Option<Bytes>` only when failure detail is
  intentionally discarded.
- docs must show checking the result before using plaintext.

### Approved-mode toggles need policy

Public mode toggles such as disabling approved mode are policy-changing
operations, not everyday helpers.

Decision:

- classify such APIs as advanced/unsafe.
- require docs to explain process/context scope.
- prefer configuration objects initialized at startup over casual global
  mutation.

## Randomness

### Deterministic versus cryptographic random

`Viper.Math.Random` and `Viper.Crypto.SecureRandom` serve different users.

Decision:

- `Math.Random` docs say deterministic/non-cryptographic.
- `Crypto.Rand` docs own tokens, keys, IVs, nonces, salts, and security
  randomness.
- examples generating secrets must never use `Math.Random`.

### `Random.Chance`

`Chance(probability)` reads as a predicate but returns `i64`.

Decision:

- make `Chance` return `i1`, or
- rename the integer-returning form to a name that makes numeric output clear.

The boolean return is preferred for readability.

## TLS And Certificates

### Verification bypass

APIs such as `SetTlsVerify(false)` are too easy to copy into production.

Decision:

- prefer explicit names such as `AllowInsecureCertificatesForTesting` or an
  options field named `insecure_skip_certificate_verification`.
- classify the bypass as `unsafe` or `testing`.
- require docs to show it only in local-test sections.
- production examples always verify certificates.

### TLS result details

TLS connection failures should provide actionable diagnostics:

- DNS/connect failure.
- timeout.
- certificate validation failure.
- protocol negotiation failure.
- unsupported capability.

Target:

```text
Tls.Connect(options) -> Result<TlsSession>
TlsSession.LastDiagnostics -> optional debug telemetry only
```

## HTTP API Shape

### Convenience helpers

Bare `Http.Get(url) -> str` style APIs are useful for scripts but not enough for
production applications.

Decision:

- keep simple helpers as `convenience` or `scripting` stability tier.
- production docs should prefer request/response APIs.
- convenience helpers should trap or return result consistently when the
  underlying request fails; they should not hide status codes.

### Production response object

Target shape:

```text
HttpClient.Send(request) -> Result<HttpResponse>
HttpResponse.Status -> HttpStatus
HttpResponse.Headers -> Headers
HttpResponse.BodyText -> str
HttpResponse.BodyBytes -> Bytes
HttpResponse.Tls -> Option<TlsInfo>
```

`RestClient.LastStatus`, `LastResponse`, and `LastOk` should become diagnostics
or compatibility accessors after returned response objects are the normal path.

## Socket And Async Networking

Timeout and async APIs should use explicit duration/result contracts:

- `ConnectFor(host, port, timeout)` needs unit metadata or a `Duration`.
- async socket send/receive should return `Future<Result<T>>` or operation
  handles with explicit completion results.
- callbacks must declare invocation thread and lifetime.

## API Naming Decisions

Preferred names:

- `HttpStatus`, not raw status `i64` without a domain.
- `TlsOptions`, not a long positional connect function.
- `AllowInsecureCertificatesForTesting`, not `SetTlsVerify(false)`.
- `Legacy.Hash.MD5`, not primary `Hash.MD5` in modern docs.
- `Random.Chance -> i1`, not integer truthiness.

## Documentation Updates

Docs must:

- separate modern and legacy crypto.
- show result checking for network and crypto failures.
- show certificate verification as the default.
- mark no-verify snippets as local testing only.
- route production HTTP examples through response objects.
- say which random API is safe for secrets.

## Audit Rules

Add checks for:

- MD5/SHA1/CRC32 in ordinary examples outside legacy/checksum sections.
- TLS verification bypass examples outside unsafe/test docs.
- crypto decrypt APIs returning raw `obj`/`NULL` without result metadata.
- HTTP examples ignoring status/error details after result APIs exist.
- `Math.Random` usage in examples that mention token, key, salt, nonce, secret,
  password, or cryptographic security.

## Acceptance Criteria

- A new user sees the safe crypto/networking path first.
- Dangerous operations are discoverable but visibly dangerous.
- Production network code can handle status, headers, TLS, timeouts, and errors
  without side-channel state.
- Security docs and examples do not normalize insecure defaults.
