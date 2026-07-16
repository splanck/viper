---
status: active
audience: contributors
last-verified: 2026-07-02
---

# ADR-0041: Crypto Result APIs and Legacy Namespaces

## Status

Accepted

## Context

The public crypto runtime mixed modern authenticated primitives with older
compatibility algorithms in the same visible classes. `Viper.Crypto.Hash`
advertised SHA-256 next to MD5, SHA-1, and CRC32, which made legacy algorithms
look equally appropriate for new security-sensitive code. `Viper.Crypto.Aes`
also exposed unauthenticated AES-CBC decrypt helpers under short modern names,
while AES-GCM decrypt helpers and `Viper.Crypto.Cipher` decryption primarily
used null returns or traps for routine failure.

Production applications need decryption failures to be ordinary values:
authentication failure, malformed ciphertext, wrong AAD, invalid key length,
empty password, approved-mode rejection, and runtime traps should be expressible
without relying on nullable objects. Existing programs still need the old names
to remain callable.

## Decision

Add explicit failure-shape decrypt APIs:

- `Viper.Crypto.Cipher.DecryptResult`, `DecryptAADResult`,
  `DecryptWithKeyResult`, and `DecryptWithKeyAADResult` return
  `Viper.Result`.
- `Viper.Crypto.Cipher.TryDecrypt`, `TryDecryptAAD`, `TryDecryptWithKey`, and
  `TryDecryptWithKeyAAD` return `Viper.Option`.
- `Viper.Crypto.Aes.DecryptAuthResult`, `TryDecryptAuth`,
  `DecryptStrResult`, and `TryDecryptStr` provide the same shape for AES-GCM
  and password-encrypted strings.
- AES-CBC compatibility decrypt names remain callable and gain
  `DecryptCBCResult` and `TryDecryptCBC` through `Viper.Crypto.Legacy.Aes`.

Move compatibility-only algorithms into explicit legacy namespaces:

- `Viper.Crypto.Legacy.Hash` owns CRC32, MD5, SHA-1, HMAC-MD5, and HMAC-SHA1
  helpers for old wire formats, checksums, archives, and protocol
  compatibility.
- `Viper.Crypto.Legacy.Aes` owns AES-CBC helpers.

The old `Viper.Crypto.Hash` legacy algorithm names and old AES-CBC names remain
available for compatibility, but runtime API contract metadata marks them as
legacy and provides migration targets. New docs and examples should use
`Viper.Crypto.Hash` only for SHA-256, HMAC-SHA256, `Fast`, and
`ConstantTimeEquals`, and should use Result/Option decrypt APIs for robust
error handling.

## Consequences

- New crypto code has obvious modern APIs for authenticated encryption and
  explicit failure handling.
- Legacy algorithms remain available without being presented as first-choice
  cryptographic building blocks.
- API dumps can distinguish stable modern crypto, legacy compatibility names,
  and migration targets for audits and generated documentation.
- Documentation, examples, demos, and educational material need to use
  `Legacy.Hash`/`Legacy.Aes` only when old formats are the point of the code.
