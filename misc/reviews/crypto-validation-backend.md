---
status: active
audience: maintainers
last-verified: 2026-05-08
---

# Zero-Dependency Crypto Validation Backend

This document tracks the implementation path for a validation-ready Zanna crypto backend. Zanna remains a zero-dependency project: no OpenSSL, BoringSSL, libsodium, platform TLS library, or platform crypto provider is used as the module backend.

## Scope

The in-repo work creates a validation-ready module boundary and approved-mode policy. It does not itself create a FIPS 140-3 certificate. A FIPS claim requires ACVP algorithm evidence, CSTL testing, CMVP submission, and an approved certificate for a frozen module boundary and operational environment.

## Implemented In This Phase

1. Native module API in `rt_crypto_module.*` with explicit compatibility and approved modes.
2. Module state machine with `UNINITIALIZED`, `SELF_TESTING`, `READY`, and `ERROR` states.
3. Startup self-tests for SHA-2, HMAC/HKDF-SHA256, AES-128-GCM, AES-256-GCM, and HMAC-DRBG determinism.
4. HMAC-DRBG-backed random generation in approved mode, seeded from OS entropy.
5. AES-256-GCM primitive and high-level Cipher approved-mode formats.
6. HMAC-SHA384, HMAC-SHA512, HKDF-SHA384, and TLS-label HKDF-SHA384 primitives.
7. P-256 ECDH primitive for the future approved TLS key-share profile.
8. Public `Zanna.Crypto.Module` controls for enabling approved mode and reporting status.
9. Approved-mode policy gates for non-approved services: MD5, SHA-1, HMAC-MD5, HMAC-SHA1, CRC32, fast hash, scrypt, AES-CBC, ChaCha20-Poly1305 Cipher formats, legacy Cipher formats, and current TLS.
10. Approved-mode `Zanna.Crypto.Cipher` encryption uses AES-256-GCM.
11. Approved-mode `Zanna.Crypto.Password.Hash` uses PBKDF2-HMAC-SHA256.
12. Runtime tests cover AES-256-GCM vectors, HMAC-SHA384/SHA512 vectors, P-256 ECDH agreement, approved-mode Cipher formats, approved-mode Password behavior, and module policy checks.

## Remaining Engineering Before Lab Submission

1. Replace the P-256 scalar multiplication path with a constant-time implementation suitable for ECDH private scalars.
2. Add P-384 ECDH/ECDSA if the target validation profile requires it.
3. Wire TLS 1.3 approved mode to advertise and negotiate P-256/P-384 key shares and `TLS_AES_128_GCM_SHA256` / `TLS_AES_256_GCM_SHA384`.
4. Add AES-GCM IV generation and per-key invocation accounting in the module boundary.
5. Add DRBG reseed interval policy, prediction-resistance policy if required, and concurrency protection around global DRBG state.
6. Add full ACVP adapter output/input support for AES-GCM, SHA-2, HMAC, HKDF, PBKDF2, DRBG, ECDSA, RSA, and ECDH/KAS.
7. Add negative CTests that execute trap paths in subprocesses for all approved-mode disallowed APIs.
8. Add module integrity/self-check strategy appropriate to the final software module boundary.
9. Write the security policy, finite state model, roles/services table, SSP table, zeroization table, operational environment list, build procedure, and lifecycle assurance evidence.
10. Freeze compiler, flags, source set, and operational environments before CSTL pre-review.

## Non-Approved Services

These remain available in compatibility mode only:

- ChaCha20-Poly1305
- X25519
- scrypt
- MD5 and HMAC-MD5
- SHA-1 and HMAC-SHA1
- CRC32
- fast hash
- raw AES-CBC helper APIs
- current public TLS handshake

## Validation Handoff

The six-phase validation program is split between repository work and external certification work:

1. Module boundary and approved-mode policy: implemented in repo.
2. Approved primitive coverage and self-tests: implemented for the initial primitive set, with constant-time and ACVP hardening still tracked.
3. Public API wiring and compatibility preservation: implemented for Hash, KeyDerive, Password, Rand, Aes, Cipher, and TLS fail-closed behavior.
4. Test and documentation coverage: implemented for focused runtime tests and public docs.
5. CSTL pre-review and ACVP execution: external handoff, not locally completable.
6. CMVP submission and certificate: external handoff, not locally completable.
