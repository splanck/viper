---
status: active
audience: public
last-verified: 2026-07-15
---

# Cryptography

> Cryptographic hashing, authentication, key derivation, and secure random generation.

**Part of the [Viper Runtime Library](README.md)**

## Contents

- [Viper.Crypto.Aes](#vipercryptoaes)
- [Viper.Crypto.Cipher](#vipercryptocipher)
- [Viper.Crypto.Hash](#vipercryptohash)
- [Viper.Crypto.Legacy.Aes](#vipercryptolegacyaes)
- [Viper.Crypto.Legacy.Hash](#vipercryptolegacyhash)
- [Viper.Crypto.KeyDerive](#vipercryptokeyderive)
- [Viper.Crypto.Compliance](#vipercryptomodule)
- [Viper.Crypto.Password](#vipercryptopassword)
- [Viper.Crypto.SecureRandom](#vipercryptorand)
- [Viper.Crypto.Tls](#vipercryptotls)

---

## Viper.Crypto.Aes

AES utilities: authenticated AES-128-GCM/AES-256-GCM for `Bytes` and password-encrypted strings, plus legacy raw AES-CBC with PKCS7 padding.

**Type:** Static utility class

### Methods

| Method                              | Signature                      | Description                                                                   |
|-------------------------------------|--------------------------------|-------------------------------------------------------------------------------|
| `EncryptAuth(data, key, aad)`       | `Bytes(Bytes, Bytes, Bytes)`   | Encrypt bytes with AES-GCM and authenticated data                             |
| `DecryptAuth(data, key, aad)`       | `Bytes(Bytes, Bytes, Bytes)`   | Decrypt AES-GCM bytes and verify authenticated data                           |
| `DecryptAuthResult(data, key, aad)` | `Result(Bytes)`                | Decrypt AES-GCM bytes with diagnostic failure                                 |
| `TryDecryptAuth(data, key, aad)`    | `Option(Bytes)`                | Decrypt AES-GCM bytes when failure detail is not needed                       |
| `EncryptStr(plaintext, password)`   | `Bytes(String, String)`        | Encrypt a string with a password using PBKDF2 + AES-128-GCM                    |
| `DecryptStr(ciphertext, password)`  | `String(Bytes, String)`        | Decrypt ciphertext to a string using the authenticated string format           |
| `DecryptStrResult(ciphertext, password)` | `Result(String)`          | Decrypt a password-encrypted string with diagnostic failure                    |
| `TryDecryptStr(ciphertext, password)` | `Option(String)`             | Decrypt a password-encrypted string when failure detail is not needed          |

### Notes

- `EncryptAuth`/`DecryptAuth` accept a 16-byte AES-128 key or a 32-byte AES-256 key and bind the `[magic(4)][nonce(12)]` header plus caller-provided AAD into the GCM tag. New robust code should prefer `DecryptAuthResult` or `TryDecryptAuth` so wrong keys, wrong AAD, malformed frames, and modified ciphertext are explicit values.
- `Encrypt`/`Decrypt` remain as AES-CBC compatibility helpers and are also available as `Viper.Crypto.Legacy.Aes.EncryptCBC` and `DecryptCBC`. CBC ciphertext is not authenticated; prefer `EncryptAuth`, `EncryptStr`, or `Viper.Crypto.Cipher`.
- `EncryptStr` rejects empty passwords, derives an AES-128 key from the password using PBKDF2-HMAC-SHA256 with a random salt and a 300,000-iteration default, and authenticates its header as AAD
- `EncryptStr` output format is `[magic(4)][iterations(4)][salt(16)][nonce(12)][ciphertext][tag(16)]`
- `DecryptStr` can read older `[IV(16)][AES-CBC ciphertext]` payloads, but the legacy/current
  dispatch uses the first four random IV bytes as a format discriminator. A legacy IV equal to
  `VAG1` is misclassified; this rare framing defect is tracked in
  [VDOC-173](../documentation-review-findings.md#vdoc-173--random-legacy-ciphertext-prefixes-can-collide-with-current-format-magic).
- The legacy CBC string KDF uses only the first 256 password bytes. Current `VAG1` encryption uses
  the full password; migrate successfully decrypted legacy content immediately. See
  [VDOC-178](../documentation-review-findings.md#vdoc-178--legacy-aes-string-decryption-truncates-passwords-at-256-bytes).
- String plaintexts and passwords use the stored Viper string byte length, so embedded `NUL` bytes are significant
- CBC helpers are disabled in approved mode
- For higher-level authenticated encryption with automatic key management, use `Viper.Crypto.Cipher` instead
- The in-tree AES block primitive is portable C and not a certified constant-time backend. Use `Cipher` or the authenticated AES-GCM helpers for production data formats.

### Zia Example

```rust
module AesDemo;

bind Viper.Terminal;
bind Viper.Crypto.Aes as Aes;
bind Viper.Crypto.SecureRandom as CRand;
bind Viper.Collections;
bind Viper.Text.Fmt as Fmt;

func start() {
    // Encrypt a string with a password
    var ciphertext = Aes.EncryptStr("Hello, AES!", "my-password");
    Say("Encrypted len: " + Fmt.Int(Viper.Collections.Bytes.get_Length(ciphertext)));

    // Decrypt it back with explicit failure handling
    var textResult = Aes.DecryptStrResult(ciphertext, "my-password");
    var plaintext = textResult.UnwrapStr();
    Say("Decrypted: " + plaintext);

    // Authenticated AES with explicit key and AAD
    var key = CRand.Bytes(16);   // 128-bit AES-GCM key
    var aad = Bytes.FromStr("file:v1");
    var data = Bytes.FromStr("Secret data");
    var enc = Aes.EncryptAuth(data, key, aad);
    var dec = Aes.DecryptAuthResult(enc, key, aad).Unwrap();
    Say("Round-trip: " + Bytes.ToStr(dec));
}
```

### BASIC Example

```basic
' Encrypt and decrypt a string with a password
DIM ciphertext AS OBJECT = Viper.Crypto.Aes.EncryptStr("Hello, AES!", "my-password")
DIM textResult AS OBJECT = Viper.Crypto.Aes.DecryptStrResult(ciphertext, "my-password")
DIM plaintext AS STRING = textResult.UnwrapStr()
PRINT "Decrypted: "; plaintext

' Authenticated AES with explicit key and AAD
DIM key AS OBJECT = Viper.Crypto.SecureRandom.Bytes(16)   ' 128-bit AES-GCM key
DIM aad AS OBJECT = Viper.Collections.Bytes.FromStr("file:v1")
DIM data AS OBJECT = Viper.Collections.Bytes.FromStr("Secret data")
DIM enc AS OBJECT = Viper.Crypto.Aes.EncryptAuth(data, key, aad)
DIM aesResult AS OBJECT = Viper.Crypto.Aes.DecryptAuthResult(enc, key, aad)
DIM dec AS OBJECT = aesResult.Unwrap()
PRINT "Round-trip: "; Viper.Collections.Bytes.ToStr(dec)
```

---

## Viper.Crypto.Cipher

High-level symmetric encryption with automatic key derivation. Compatibility mode uses ChaCha20-Poly1305 AEAD; approved mode uses AES-256-GCM.

**Type:** Static utility class

### Encryption Methods

| Method                           | Signature                   | Description                                           |
|----------------------------------|-----------------------------|-------------------------------------------------------|
| `Encrypt(data, password)`        | `Bytes(Bytes, String)`      | Encrypt data with password (automatic salt/nonce)     |
| `Decrypt(data, password)`        | `Bytes(Bytes, String)`      | Decrypt password-encrypted data                       |
| `DecryptResult(data, password)`  | `Result(Bytes)`             | Decrypt password-encrypted data with diagnostic failure |
| `TryDecrypt(data, password)`     | `Option(Bytes)`             | Decrypt password-encrypted data without diagnostics   |
| `EncryptAAD(data, password, aad)`| `Bytes(Bytes,String,Bytes)` | Encrypt and bind caller-provided authenticated data   |
| `DecryptAAD(data, password, aad)`| `Bytes(Bytes,String,Bytes)` | Decrypt and verify caller-provided authenticated data |
| `DecryptAADResult(data, password, aad)` | `Result(Bytes)`       | Decrypt AAD-bound data with diagnostic failure        |
| `TryDecryptAAD(data, password, aad)` | `Option(Bytes)`          | Decrypt AAD-bound data without diagnostics            |
| `EncryptWithKey(data, key)`      | `Bytes(Bytes, Bytes)`       | Encrypt data with a 32-byte key                       |
| `DecryptWithKey(data, key)`      | `Bytes(Bytes, Bytes)`       | Decrypt key-encrypted data                            |
| `DecryptWithKeyResult(data, key)`| `Result(Bytes)`             | Decrypt key-encrypted data with diagnostic failure    |
| `TryDecryptWithKey(data, key)`   | `Option(Bytes)`             | Decrypt key-encrypted data without diagnostics        |
| `EncryptWithKeyAAD(data,key,aad)`| `Bytes(Bytes,Bytes,Bytes)`  | Encrypt with a key and authenticated data             |
| `DecryptWithKeyAAD(data,key,aad)`| `Bytes(Bytes,Bytes,Bytes)`  | Decrypt with a key and authenticated data             |
| `DecryptWithKeyAADResult(data,key,aad)` | `Result(Bytes)`      | Decrypt key/AAD data with diagnostic failure          |
| `TryDecryptWithKeyAAD(data,key,aad)` | `Option(Bytes)`         | Decrypt key/AAD data without diagnostics              |

### Key Management Methods

| Method                           | Signature                   | Description                                           |
|----------------------------------|-----------------------------|-------------------------------------------------------|
| `GenerateKey()`                  | `Bytes()`                   | Generate a random 32-byte encryption key              |
| `DeriveKey(password, salt)`      | `Bytes(String, Bytes)`      | Derive 32-byte key from password using PBKDF2         |

### Ciphertext Format

Password-based encryption produces ciphertext in this format:

```text
[magic "VCP2"(4)][iterations(4)][salt(16)][nonce(12)][ciphertext][tag(16)]
```

Approved-mode password encryption produces:

```text
[magic "VCA1"(4)][iterations(4)][salt(16)][nonce(12)][ciphertext][tag(16)]
```

Key-based encryption produces:

```text
[magic "VCK2"(4)][nonce(12)][ciphertext][tag(16)]
```

Approved-mode key encryption produces:

```text
[magic "VKA1"(4)][nonce(12)][ciphertext][tag(16)]
```

### Security Properties

- **Algorithm:** ChaCha20-Poly1305 AEAD in compatibility mode; AES-256-GCM in approved mode
- **Key Size:** 256 bits (32 bytes)
- **Nonce Size:** 96 bits (12 bytes). Cipher currently uses a 32-bit CSPRNG prefix followed by a
  64-bit process-local counter, not 96 independently random bits.
- **Authentication Tag:** 128 bits (16 bytes)
- **Key Derivation:** PBKDF2-HMAC-SHA256 with random 16-byte salt and a 300,000-iteration default
- Header bytes and caller-provided AAD are authenticated by the AEAD tag
- Decryption verifies that the AEAD backend returned exactly the expected plaintext length. New robust code should prefer `DecryptResult`/`DecryptAADResult`/`DecryptWithKeyResult`/`DecryptWithKeyAADResult`, or the `TryDecrypt*` forms when diagnostics are intentionally discarded.
- `Decrypt()` can read older unversioned PBKDF2/HKDF payloads; new payloads use `VCP2`. Because a
  legacy random salt/nonce can equal a current four-byte magic prefix, this compatibility is not
  absolute; see
  [VDOC-173](../documentation-review-findings.md#vdoc-173--random-legacy-ciphertext-prefixes-can-collide-with-current-format-magic).
- Approved mode rejects compatibility and legacy ciphertext formats instead of silently decrypting with non-approved algorithms
- Password strings use their stored byte length, so embedded `NUL` bytes are part of the password
- Password mode derives a fresh key from an independently random 16-byte salt for each message.
  Raw-key mode instead depends on nonce uniqueness under the caller-managed key. Its current
  32-bit cross-process prefix is unsafe for a persistent key reused across enough restarts; see
  [VDOC-172](../documentation-review-findings.md#vdoc-172--ciphers-raw-key-nonce-construction-has-only-a-32-bit-cross-process-margin).
- Several byte-returning Cipher/Aes registry signatures are unqualified `obj`. In Zia, use an
  explicit `Viper.Collections.Bytes` receiver for properties of an inferred result, as the examples
  below do. This typing defect is tracked in
  [VDOC-020](../documentation-review-findings.md#vdoc-020--untyped-concrete-object-results-break-member-typing-or-infer-the-declaring-class).

### Zia Example

```rust
module CipherDemo;

bind Viper.Terminal;
bind Viper.Crypto.Cipher as Cipher;
bind Viper.Collections;
bind Viper.Text.Fmt as Fmt;

func start() {
    // Encrypt data with a password
    var plaintext = Bytes.FromStr("Secret message");
    var password = "my-secure-password";
    var ciphertext = Cipher.Encrypt(plaintext, password);
    Say("Encrypted len: " + Fmt.Int(Viper.Collections.Bytes.get_Length(ciphertext)));

    var decryptResult = Cipher.DecryptResult(ciphertext, password);
    var decrypted = decryptResult.Unwrap();
    Say("Decrypted: " + Bytes.ToStr(decrypted));

    // Generate a random encryption key
    var key = Cipher.GenerateKey();
    Say("Key len: " + Fmt.Int(Viper.Collections.Bytes.get_Length(key)));
}
```

### BASIC Example

```basic
' Encrypt data with a password
DIM plaintext AS Viper.Collections.Bytes
plaintext = Viper.Collections.Bytes.FromStr("Secret message")
DIM password AS STRING = "my-secure-password"
DIM ciphertext AS Viper.Collections.Bytes
ciphertext = Viper.Crypto.Cipher.Encrypt(plaintext, password)
PRINT "Encrypted len: "; ciphertext.Length

' Decrypt and verify round-trip
DIM decryptResult AS OBJECT
decryptResult = Viper.Crypto.Cipher.DecryptResult(ciphertext, password)
DIM decrypted AS Viper.Collections.Bytes
decrypted = decryptResult.Unwrap()
PRINT "Decrypted: "; decrypted.ToStr()

' Generate a random encryption key
DIM key AS Viper.Collections.Bytes
key = Viper.Crypto.Cipher.GenerateKey()
PRINT "Key len: "; key.Length

' Key-based encrypt/decrypt
DIM plain2 AS Viper.Collections.Bytes
plain2 = Viper.Collections.Bytes.FromStr("Key-based test")
DIM enc2 AS Viper.Collections.Bytes
enc2 = Viper.Crypto.Cipher.EncryptWithKey(plain2, key)
DIM keyResult AS OBJECT
keyResult = Viper.Crypto.Cipher.DecryptWithKeyResult(enc2, key)
DIM dec2 AS Viper.Collections.Bytes
dec2 = keyResult.Unwrap()
PRINT "Key decrypt: "; dec2.ToStr()
```

### Key-Based Encryption Example

```basic
' Generate a random key
DIM key AS OBJECT = Viper.Crypto.Cipher.GenerateKey()

' Encrypt with the key
DIM plaintext AS OBJECT = Viper.Collections.Bytes.FromStr("Secret data")
DIM ciphertext AS OBJECT = Viper.Crypto.Cipher.EncryptWithKey(plaintext, key)

' Decrypt with the same key
DIM decryptResult AS OBJECT = Viper.Crypto.Cipher.DecryptWithKeyResult(ciphertext, key)
DIM decrypted AS OBJECT = decryptResult.Unwrap()
```

### Key Derivation Example

```basic
' Derive a key from password (useful when you need the same key multiple times)
DIM password AS STRING = "user-password"
DIM salt AS OBJECT = Viper.Crypto.SecureRandom.Bytes(16)
DIM data AS OBJECT = Viper.Collections.Bytes.FromStr("Secret data")

' Derive key
DIM key AS OBJECT = Viper.Crypto.Cipher.DeriveKey(password, salt)

' Use key for encryption
DIM ciphertext AS OBJECT = Viper.Crypto.Cipher.EncryptWithKey(data, key)

' Store salt alongside ciphertext for later decryption
```

### File Encryption Example

```basic
' Encrypt a file
DIM fileData AS OBJECT = Viper.IO.File.ReadAllBytes("secret.doc")
DIM password AS STRING = "file-password"

DIM encrypted AS OBJECT = Viper.Crypto.Cipher.Encrypt(fileData, password)
Viper.IO.File.WriteAllBytes("secret.doc.enc", encrypted)

' Decrypt a file
DIM encData AS OBJECT = Viper.IO.File.ReadAllBytes("secret.doc.enc")
DIM decryptResult AS OBJECT = Viper.Crypto.Cipher.DecryptResult(encData, password)
DIM decrypted AS OBJECT = decryptResult.Unwrap()
Viper.IO.File.WriteAllBytes("secret.doc", decrypted)
```

### Error Handling

Cipher operations expose both compatibility and production failure shapes:

- `DecryptResult*` returns `Ok(Bytes)` on success and `Err(str)` for authentication failure, malformed ciphertext, empty passwords, invalid key size, approved-mode rejection, or other runtime traps.
- `TryDecrypt*` returns `Some(Bytes)` on success and `None` for any failure when details are not needed.
- Compatibility `Decrypt*` methods remain callable and may return `NULL` or trap depending on the failure.
- Empty plaintext is allowed and produces valid ciphertext

### Security Recommendations

1. **Use strong passwords:** Combine with password requirements in your application
2. **Store keys securely:** Never hardcode keys in source code
3. **Use password-based for user data:** Let the API handle salt generation
4. **Use key-based for application data:** When you manage key storage separately
5. **Manage raw-key nonce scope:** An AEAD key may protect multiple messages only while every nonce
   under that key is unique. Cipher's current process-local nonce state is not a durable uniqueness
   guarantee across restarts; rotate persistent raw keys or use password mode while VDOC-172 is open.

### When to Use Cipher vs. Other Crypto

| Use Case                      | Recommended                          |
|-------------------------------|--------------------------------------|
| Encrypt user data             | `Viper.Crypto.Cipher.Encrypt()`      |
| Encrypt with managed keys     | `Viper.Crypto.Cipher.EncryptWithKey()` |
| Password storage              | `Viper.Crypto.Password.Hash()`          |
| Message authentication only   | `Viper.Crypto.Hash.HmacSha256()`     |
| Data integrity check          | `Viper.Crypto.Hash.Sha256()`         |
| Secure communication          | `Viper.Crypto.Tls`                   |

---

## Viper.Crypto.Hash

Modern hash and HMAC helpers for strings and binary data. Security-sensitive code should use SHA-256, HMAC-SHA256, `Password`, `KeyDerive`, or `Cipher` depending on the job. MD5, SHA-1, HMAC-MD5, HMAC-SHA1, and CRC32 remain available through `Viper.Crypto.Legacy.Hash`.

**Type:** Static utility class

### Hash Methods

| Method               | Signature         | Description                              |
|----------------------|-------------------|------------------------------------------|
| `SHA256(str)`        | `String(String)`  | Compute SHA256 hash of a string          |
| `SHA256Bytes(bytes)` | `String(Bytes)`   | Compute SHA256 hash of a Bytes object    |
| `Fast(str)`          | `Integer(String)` | Compute keyed SipHash-2-4 of a string    |
| `FastBytes(data)`    | `Integer(Bytes)`  | Compute keyed SipHash-2-4 of Bytes       |
| `FastInt(value)`     | `Integer(Integer)`| Compute keyed SipHash-2-4 of an integer  |
| `ConstantTimeEquals(a, b)` | `Boolean(String,String)` | Timing-safe equality for digests/MACs |
| `ConstantTimeEqualsBytes(a, b)` | `Boolean(Bytes,Bytes)` | Timing-safe equality for binary tags |

### HMAC Methods

| Method                     | Signature               | Description                         |
|----------------------------|-------------------------|-------------------------------------|
| `HmacSHA256(key, data)`    | `String(String,String)` | HMAC-SHA256 with string key and data|
| `HmacSHA256Bytes(key, data)`| `String(Bytes,Bytes)`  | HMAC-SHA256 with Bytes key and data |

### Hash Output Formats

| Algorithm | Output Size   | Format                    |
|-----------|---------------|---------------------------|
| SHA256    | 256 bits      | 64-character hex string   |
| HMAC-SHA256 | 256 bits    | 64-character hex string   |
| Fast      | 64 bits       | Integer (i64), process-keyed SipHash |

String hash and HMAC methods use the stored Viper string byte length. Embedded `NUL` bytes are
hashed as data, matching the corresponding `Bytes` methods for the same byte sequence. Empty
strings are valid inputs; null string references trap instead of being silently hashed as empty
strings. For compatibility, the `Bytes` hash/HMAC/equality forms treat a null Bytes reference as an
empty byte sequence.

### Fast Hash Methods

The `Fast`, `FastBytes`, and `FastInt` methods use SipHash-2-4 with a per-process CSPRNG seed. These
are **non-cryptographic** hashes for hash-table and partitioning use. They are not stable across
process launches and are not suitable for signatures, passwords, MACs, or persistent content IDs.
Approved mode disables them. A returning entropy-failure trap hook and native-MSVC concurrent first
use can currently break the seed guarantee; see
[VDOC-176](../documentation-review-findings.md#vdoc-176--siphash-seed-initialization-can-publish-predictable-or-racily-accessed-state).

```rust
module FastHashDemo;

bind Viper.Terminal;
bind Viper.Crypto.Hash as Hash;
bind Viper.Text.Fmt as Fmt;

func start() {
    Say("Hash: " + Fmt.Int(Hash.Fast("hello")));
    Say("Int hash: " + Fmt.Int(Hash.FastInt(42)));
}
```

```basic
' Fast non-cryptographic hashing
DIM h AS INTEGER = Viper.Crypto.Hash.Fast("hello")
PRINT "String hash:"; h

' Hash binary data
DIM data AS OBJECT = Viper.Collections.Bytes.FromStr("binary data")
DIM h2 AS INTEGER = Viper.Crypto.Hash.FastBytes(data)
PRINT "Bytes hash:"; h2

' Hash an integer
DIM h3 AS INTEGER = Viper.Crypto.Hash.FastInt(42)
PRINT "Int hash:"; h3
```

### Security Notes

- **SHA256**: Currently collision/preimage resistant. Do not use plain SHA256 as a password hash or as a MAC; use `Password`, `KeyDerive`, or HMAC as appropriate.
- **ConstantTimeEquals**: Intended for same-length public-format digests and MAC tags. Length mismatch returns false before byte comparison; do not use it to hide secret lengths.
- **Legacy algorithms**: CRC32, MD5, SHA1, HMAC-MD5, and HMAC-SHA1 are compatibility/checksum tools only. Use `Viper.Crypto.Legacy.Hash` when you must read or produce those formats.
- **Approved mode**: SHA-256 and HMAC-SHA256 remain available; legacy algorithms and `Fast*` are disabled.

### Zia Example

```rust
module HashDemo;

bind Viper.Terminal;
bind Viper.Crypto.Hash as Hash;

func start() {
    Say("SHA256: " + Hash.Sha256("hello"));
    Say("HMAC-SHA256: " + Hash.HmacSha256("key", "hello"));
}
```

### BASIC Example

```basic
PRINT "SHA256: "; Viper.Crypto.Hash.Sha256("hello")
PRINT "HMAC-SHA256: "; Viper.Crypto.Hash.HmacSha256("key", "hello")
```

### HMAC Example

```basic
' HMAC for message authentication
DIM secretKey AS STRING = "my-secret-key"
DIM message AS STRING = "Important message to authenticate"

' Compute HMAC-SHA256
DIM mac AS STRING = Viper.Crypto.Hash.HmacSha256(secretKey, message)
PRINT "HMAC: "; mac

' Verify message authenticity
DIM receivedMac AS STRING = "..." ' Received with message
DIM computedMac AS STRING = Viper.Crypto.Hash.HmacSha256(secretKey, message)
IF Viper.Crypto.Hash.ConstantTimeEquals(receivedMac, computedMac) THEN
    PRINT "Message is authentic"
ELSE
    PRINT "WARNING: Message was tampered with!"
END IF

' HMAC with binary data
DIM keyBytes AS OBJECT = Viper.Crypto.SecureRandom.Bytes(32)
DIM dataBytes AS OBJECT = Viper.IO.File.ReadAllBytes("data.bin")
DIM binaryMac AS STRING = Viper.Crypto.Hash.HmacSha256Bytes(keyBytes, dataBytes)
```

### HMAC Algorithm

HMAC (Hash-based Message Authentication Code) provides message authentication using a secret key:

```text
HMAC(K, m) = H((K' xor opad) || H((K' xor ipad) || m))
```

Where:

- K' = K zero-padded to `block_size` when `len(K) <= block_size`; otherwise H(K) zero-padded to
  `block_size`
- ipad = 0x36 repeated block_size times
- opad = 0x5c repeated block_size times
- block_size = 64 bytes for SHA256 and the legacy MD5/SHA1 HMAC variants

The runtime streams HMAC input through the underlying hash context instead of buffering the whole message.

---

## Viper.Crypto.Legacy.Hash

Compatibility hashes, checksums, and legacy HMACs. These names are intentionally separate from the modern `Viper.Crypto.Hash` examples.

| Method | Signature | Use |
|--------|-----------|-----|
| `CRC32(str)` / `CRC32Bytes(bytes)` | `Integer(...)` | File/archive checksum and accidental-corruption detection only |
| `MD5(str)` / `MD5Bytes(bytes)` | `String(...)` | Legacy digest formats only |
| `SHA1(str)` / `SHA1Bytes(bytes)` | `String(...)` | Legacy digest formats only |
| `HmacMD5(...)` / `HmacMD5Bytes(...)` | `String(...)` | Legacy protocol compatibility only |
| `HmacSHA1(...)` / `HmacSHA1Bytes(...)` | `String(...)` | Legacy protocol compatibility only |

`Viper.Crypto.Hash` still accepts the old names for compatibility, but new docs and examples use `Legacy.Hash` when an old algorithm is deliberate.

---

## Viper.Crypto.Legacy.Aes

AES-CBC compatibility helpers. CBC mode is not authenticated and must not be used for new encrypted data formats unless another authentication layer is supplied.

| Method | Signature | Description |
|--------|-----------|-------------|
| `EncryptCBC(data, key, iv)` | `Bytes(Bytes,Bytes,Bytes)` | Encrypt with AES-CBC and PKCS7 padding |
| `DecryptCBC(data, key, iv)` | `Bytes(Bytes,Bytes,Bytes)` | Compatibility raw decrypt; may return `NULL` or trap |
| `DecryptCBCResult(data, key, iv)` | `Result(Bytes)` | AES-CBC decrypt with diagnostic failure |
| `TryDecryptCBC(data, key, iv)` | `Option(Bytes)` | AES-CBC decrypt without diagnostics |

The old `Viper.Crypto.Aes.Encrypt`, `Decrypt`, `DecryptResult`, and `TryDecrypt` names remain as compatibility aliases.

Keys must be 16 or 32 bytes and IVs must be 16 bytes. These helpers are disabled in approved mode.
They provide confidentiality only: callers must supply a fresh unpredictable IV and a separate
encrypt-then-MAC construction if an old external format requires CBC. Prefer the authenticated APIs
for all new formats.

---

## Viper.Crypto.KeyDerive

Key derivation functions for deriving cryptographic keys from passwords.

**Type:** Static utility class

### Methods

| Method                                      | Signature                            | Description                        |
|---------------------------------------------|--------------------------------------|------------------------------------|
| `Pbkdf2SHA256(password, salt, iterations, keyLen)` | `Bytes(String,Bytes,Integer,Integer)` | Derive key using PBKDF2-HMAC-SHA256 |
| `Pbkdf2SHA256Str(password, salt, iterations, keyLen)` | `String(String,Bytes,Integer,Integer)` | Same but returns hex string |
| `ScryptSHA256(password, salt, n, r, p, keyLen)` | `Bytes(String,Bytes,Integer,Integer,Integer,Integer)` | Derive key using memory-hard scrypt |
| `ScryptSHA256Str(password, salt, n, r, p, keyLen)` | `String(String,Bytes,Integer,Integer,Integer,Integer)` | Same but returns hex string |

### Parameters

| Parameter    | Type    | Description                                    |
|--------------|---------|------------------------------------------------|
| `password`   | String  | The password to derive from                    |
| `salt`       | Bytes   | Non-empty salt; generate it independently for each derived-key context (16 bytes is the runtime convention) |
| `iterations` | Integer | PBKDF2 iteration count from 100,000 through 10,000,000; Cipher currently uses 300,000 |
| `n`, `r`, `p` | Integer | scrypt costs: `n` is a power of two from 2 through 2^20; `r` and `p` are 1-32, subject to the memory cap |
| `keyLen`     | Integer | Desired key length in bytes (1-1024)           |

### Traps

- `iterations < 100000` or `iterations > 10000000`: Traps instead of silently changing the requested work factor
- null `password`: Traps instead of deriving the empty-password key. A real empty string is allowed when the application explicitly wants that input.
- empty `salt`: Traps with "salt must not be empty"
- `keyLen < 1 or keyLen > 1024`: Traps with "key_len must be between 1 and 1024"
- unsupported scrypt memory/cost parameters: Traps before allocating memory. The runtime caps
  `n * 128 * r` at 64 MiB in addition to the numeric bounds above.
- scrypt APIs trap in approved mode; use PBKDF2 APIs there
- Passwords use their stored string byte length, including embedded `NUL` bytes; a real empty string
  is allowed

### Zia Example

```rust
module KeyDeriveDemo;

bind Viper.Terminal;
bind Viper.Crypto.KeyDerive as KD;
bind Viper.Crypto.SecureRandom as CRand;

func start() {
    // Generate a random salt
    var salt = CRand.Bytes(16);

    // Derive a key using PBKDF2-SHA256
    var keyHex = KD.Pbkdf2Sha256Encoded("password123", salt, 300000, 32);
    var scryptHex = KD.ScryptEncoded("password123", salt, 16384, 8, 1, 32);
    Say("Derived key: " + keyHex);
    Say("scrypt key: " + scryptHex);
}
```

### BASIC Example

```basic
' Derive a key from a password
DIM password AS STRING = "user-password"
DIM salt AS OBJECT = Viper.Crypto.SecureRandom.Bytes(16)  ' Random 16-byte salt
DIM iterations AS INTEGER = 300000  ' High iteration count for security

' Derive a 32-byte key
DIM key AS OBJECT = Viper.Crypto.KeyDerive.Pbkdf2Sha256(password, salt, iterations, 32)

' Or get it as a hex string
DIM keyHex AS STRING = Viper.Crypto.KeyDerive.Pbkdf2Sha256Encoded(password, salt, iterations, 32)
PRINT "Derived key: "; keyHex
```

### Password Storage Example

```basic
FUNCTION HashPassword(password AS STRING) AS STRING
    ' Password.Hash generates a salt and returns a self-describing record.
    RETURN Viper.Crypto.Password.Hash(password)
END FUNCTION

FUNCTION VerifyPassword(password AS STRING, stored AS STRING) AS BOOLEAN
    RETURN Viper.Crypto.Password.Verify(password, stored)
END FUNCTION
```

### Security Recommendations

1. **Use `Password` for password storage**: it includes a self-describing format and migration checks
2. **Use independent salts**: Generate a fresh random salt for each password/context; uniqueness is
   probabilistic, not guaranteed
3. **Store salt with hash**: You need the salt to verify passwords
4. **Use sufficient key length**: 32 bytes (256 bits) is standard

---

## Viper.Crypto.Compliance

Validation-readiness controls for the zero-dependency in-tree crypto module.

**Type:** Static utility class

### Methods

| Method                 | Signature  | Description                                             |
|------------------------|------------|---------------------------------------------------------|
| `EnableApprovedModeForProcess()` | `Boolean()` | Run module self-tests, instantiate the DRBG, and enable process-wide approved-mode policy gates |
| `DisableApprovedModeForProcess()`| `Void()`    | Return the process to compatibility mode                |
| `IsApprovedModeForProcess()`     | `Boolean()` | Return whether process-wide approved-mode policy gates are active |
| `Status()`             | `String()` | Return module state text such as `ready` or a self-test failure |

`EnableApprovedMode`, `DisableApprovedMode`, and `IsApprovedMode` remain
available for compatibility. New code should use the `ForProcess` names so the
global policy scope is visible in source and generated API docs.

### Approved-Mode Behavior

- Mode and error state are process-global and serialized; the `ForProcess` names make that scope
  explicit
- Runs self-tests for SHA-2, HMAC/HKDF-SHA256, AES-128-GCM, AES-256-GCM, and an HMAC-DRBG known-answer path before enabling approved mode
- Routes `Viper.Crypto.SecureRandom` and internal nonce/key generation through the module HMAC-DRBG once approved mode is enabled
- Serializes module state and DRBG access, chunks oversized random requests to the DRBG request limit, and reseeds the DRBG from OS entropy on the configured reseed interval
- Self-test or DRBG initialization failure pins the module in an error state. The error state fails closed for service checks, and disabling approved mode does not re-enable compatibility algorithms after such a failure.
- Keeps compatibility-mode algorithms available when approved mode is disabled
- Disables non-approved public services in approved mode: MD5, SHA-1, HMAC-MD5, HMAC-SHA1, CRC32, fast hash, scrypt, ChaCha20-Poly1305 formats, legacy Cipher formats, AES-CBC helpers, and current X25519-only TLS
- Uses AES-256-GCM for `Viper.Crypto.Cipher` in approved mode
- Uses PBKDF2-HMAC-SHA256 for `Viper.Crypto.Password.Hash` in approved mode

### Validation Claim

Approved mode is a validation-readiness policy mode, not a CMVP certificate. Viper can only claim FIPS validation after an accredited lab completes algorithm validation, module testing, and CMVP approval for a frozen module boundary and operational environment.

### BASIC Example

```basic
IF Viper.Crypto.Compliance.EnableApprovedModeForProcess() THEN
    PRINT "Crypto module status: "; Viper.Crypto.Compliance.Status()
END IF
```

---

## Viper.Crypto.SecureRandom

Cryptographically secure random number generation.

**Type:** Static utility class

### Methods

| Method          | Signature              | Description                               |
|-----------------|------------------------|-------------------------------------------|
| `Bytes(count)`  | `Bytes(Integer)`       | Generate count random bytes               |
| `Int(min, max)` | `Integer(Integer,Integer)` | Generate random integer in [min, max] |

### Parameters

| Method  | Parameter | Constraints                |
|---------|-----------|----------------------------|
| `Bytes` | `count`   | Must be >= 0               |
| `Int`   | `min`     | Must be <= max             |
| `Int`   | `max`     | Must be >= min             |

### Traps

- `Bytes(0)` returns an empty `Bytes` object
- `Bytes(count)` with count < 0: Traps with "count must not be negative"
- `Int(min, max)` with min > max: Traps with "min must not be greater than max"
- `Int(min, max)` supports ranges spanning the full signed 64-bit domain

### Platform Implementation

Compatibility mode reads directly from the platform CSPRNG. Approved mode seeds and serves output from the in-tree HMAC-DRBG after module self-tests pass.

| Platform | Source                          |
|----------|----------------------------------|
| Linux    | `getrandom(2)`; `/dev/urandom` fallback only when the syscall is unavailable (`ENOSYS`) |
| macOS    | arc4random_buf                   |
| Windows  | BCryptGenRandom                  |
| Other Unix / ViperDOS | `/dev/urandom`       |

`Int` uses rejection sampling, is inclusive at both ends, and supports the full signed 64-bit
domain without range overflow.

### Zia Example

```rust
module CryptoRandDemo;

bind Viper.Terminal;
bind Viper.Crypto.SecureRandom as CRand;
bind Viper.Collections;
bind Viper.Text.Fmt as Fmt;

func start() {
    // Generate cryptographically secure random bytes
    var bytes = CRand.Bytes(16);
    var hex = Bytes.ToHex(bytes);
    Say("Hex: " + hex);

    // Generate a random integer in range
    var n = CRand.Int(1, 100);
    Say("Random 1-100: " + Fmt.Int(n));
}
```

### BASIC Example

```basic
' Generate random bytes
DIM key AS OBJECT = Viper.Crypto.SecureRandom.Bytes(32)   ' 256-bit key
DIM iv AS OBJECT = Viper.Crypto.SecureRandom.Bytes(16)    ' 128-bit IV
DIM salt AS OBJECT = Viper.Crypto.SecureRandom.Bytes(16)  ' Salt for PBKDF2

' Generate random integers
DIM dice AS INTEGER = Viper.Crypto.SecureRandom.Int(1, 6)       ' Roll a die: 1-6
DIM card AS INTEGER = Viper.Crypto.SecureRandom.Int(0, 51)      ' Pick a card: 0-51
DIM token AS INTEGER = Viper.Crypto.SecureRandom.Int(100000, 999999)  ' 6-digit code
```

### Security Token Example

```basic
' Generate a secure random token
FUNCTION GenerateToken(length AS INTEGER) AS STRING
    DIM bytes AS OBJECT = Viper.Crypto.SecureRandom.Bytes(length)
    RETURN Viper.Collections.Bytes.ToHex(bytes)
END FUNCTION

' Generate a 64-character token (32 random bytes)
DIM apiToken AS STRING = GenerateToken(32)
PRINT "API Token: "; apiToken
```

### Secure Shuffle Example

```basic
' Fisher-Yates shuffle using secure random
SUB SecureShuffle(arr() AS INTEGER)
    DIM n AS INTEGER = UBOUND(arr)
    FOR i = n TO 1 STEP -1
        DIM j AS INTEGER = Viper.Crypto.SecureRandom.Int(0, i)
        ' Swap arr(i) and arr(j)
        DIM temp AS INTEGER = arr(i)
        arr(i) = arr(j)
        arr(j) = temp
    NEXT i
END SUB
```

### Use Cases

- **Key generation**: Generate encryption keys, IVs, nonces
- **Salt generation**: Create independently random salts for password hashing
- **Token generation**: Create session tokens, API keys
- **Secure selection**: Pick random elements securely
- **Cryptographic protocols**: Implement secure authentication flows

### Security Guarantees

- Compatibility mode reads from the operating system CSPRNG; approved mode uses the locked in-tree HMAC-DRBG seeded
  and periodically reseeded from that CSPRNG.
- Output is intended for keys, salts, nonces, tokens, and secure selection; callers must still follow each protocol's
  size, uniqueness, and encoding requirements.
- Approved-mode DRBG access and the direct platform RNG calls are serialized/thread-safe. The
  cached `/dev/urandom` fallback currently has an unsynchronized first-use descriptor read, so a
  blanket concurrency guarantee is not yet valid on that path; see
  [VDOC-175](../documentation-review-findings.md#vdoc-175--the-unix-random-fallback-has-an-unsynchronized-first-use-descriptor-read).

---

## Viper.Crypto.Tls

TLS (Transport Layer Security) client for encrypted TCP connections. Uses TLS 1.3 with modern cipher suites.

**Type:** Instance class

**Constructors:**

- `Viper.Crypto.Tls.ConnectResult(host, port)` - Connect with TLS and return `Ok(Tls)` or `Err(message)`
- `Viper.Crypto.Tls.ConnectForResult(host, port, timeoutMs)` - Connect with a per-address/per-I/O timeout and return `Ok(Tls)` or `Err(message)`
- `Viper.Crypto.Tls.ConnectOptionsResult(host, port, caFile, alpn, verifyCert, timeoutMs)` - Connect with explicit trust bundle, ALPN preferences, verification policy, and timeout as a `Result`
- `Viper.Crypto.Tls.Connect(host, port)` - Connect with TLS to host:port
- `Viper.Crypto.Tls.ConnectFor(host, port, timeoutMs)` - Connect with a per-address/per-I/O timeout
- `Viper.Crypto.Tls.ConnectOptions(host, port, caFile, alpn, verifyCert, timeoutMs)` - Connect with explicit trust bundle, ALPN preferences, verification policy, and timeout. Pass `""` for default CA bundle or no ALPN.

### Properties

| Property | Type    | Description                                |
|----------|---------|--------------------------------------------|
| `Host`   | String  | Remote host name (read-only)               |
| `Port`   | Integer | Remote port number (read-only)             |
| `NegotiatedAlpn` | String | Negotiated ALPN protocol, or empty string |
| `IsOpen` | Boolean | True if connection is open (read-only)     |

### Send Methods

| Method          | Returns | Description                                |
|-----------------|---------|--------------------------------------------|
| `Send(data)`    | Integer | Send Bytes, return number of bytes sent    |
| `SendStr(text)` | Integer | Send the string's stored bytes, return bytes sent |

### Receive Methods

| Method             | Returns | Description                                |
|--------------------|---------|--------------------------------------------|
| `Recv(maxBytes)`   | Bytes   | Receive up to min(maxBytes, 16,384); may return fewer |
| `RecvStr(maxBytes)`| String  | Receive up to min(maxBytes, 16,384) as a length-aware string; no UTF-8 validation |
| `RecvLine()`       | String  | Read through LF, strip a preceding CR; empty on truncation/error or an over-64-KiB line |

### Control Methods

| Method    | Returns | Description                                       |
|-----------|---------|---------------------------------------------------|
| `Close()` | void    | Send `close_notify`, briefly drain the peer response, and close the socket |
| `Error()` | String  | Compatibility diagnostic for an existing TLS handle |

### TLS Implementation

The TLS implementation uses:

- **Protocol:** TLS 1.3 (RFC 8446)
- **Key Exchange:** X25519 (Curve25519 ECDH)
- **Cipher suites:** AES-128-GCM-SHA256 (advertised first) and ChaCha20-Poly1305-SHA256
- **Hash:** SHA-256
- **Certificate Verification:** Enabled by default against the runtime trust source. Windows uses CryptoAPI; macOS and Linux use the built-in PEM-bundle verifier with standard system trust bundles.
- **Trust and ALPN controls:** `ConnectOptions` can pin validation to a PEM bundle, advertise comma-separated ALPN preferences such as `"h2,http/1.1"`, and read the negotiated protocol from `NegotiatedAlpn`.
- **Certificate Signature Support:** In-tree verification of ECDSA P-256, RSA PKCS#1 v1.5, and RSA-PSS certificate signatures. The TLS client advertises only signature algorithms it can verify; ECDSA P-384 is not advertised until P-384 CertificateVerify support is implemented.
- **Leaf-certificate policy:** Built-in verification enforces TLS server-auth EKU and requires the `digitalSignature` KeyUsage bit when KeyUsage is present on the server certificate
- **DER strictness:** Certificate signature algorithms, ECDSA signatures, RSA public keys, and PSS parameters are parsed as strict DER with exact length consumption and canonical INTEGERs
- **Hostname / SNI behavior:** DNS hostnames are sent in SNI; IP literals are verified against IP SANs but are not sent in SNI. SubjectAltName suppresses CommonName fallback even when the SAN contains no DNS names, and broad public-suffix wildcards are rejected.
- **SubjectAltName matching:** Hostname verification scans all DNS SAN entries. The public C extraction helper still writes only up to its caller-provided output capacity.
- **Certificate chain behavior:** The parser rejects malformed/trailing certificate-list data and
  certificate lists above 1 MiB. It does not impose a 16-intermediate count limit.
- **Handshake strictness:** Unexpected handshake messages and trailing certificate-message bytes fail the handshake instead of being skipped.
- **Key-share validation:** X25519 all-zero shared secrets are rejected during the handshake
- **String handling:** `SendStr` sends the full stored string byte length, including embedded `NUL` bytes
- **Connection state:** `IsOpen` is true only while the TLS session is in the connected state
- **Verification switch:** `verifyCert=false` skips trust-chain and hostname policy for local
  testing, but the TLS 1.3 CertificateVerify proof of private-key possession is still checked
- **Timeout scope:** the configured value is reused for each resolved-address attempt and individual
  socket read/write; it is not an overall connection or handshake deadline. Nonpositive values use
  the 30-second default. See
  [VDOC-179](../documentation-review-findings.md#vdoc-179--tls-connectfor-applies-its-timeout-repeatedly-instead-of-as-a-deadline).
- **Concurrency:** TLS sessions contain mutable record buffers, keys, and sequence counters without
  an internal lock. Serialize all operations on one connection.
- **Approved mode:** Current public TLS is compatibility-mode only because the wire handshake is still X25519/SHA-256 based. Approved mode fails closed for `Viper.Crypto.Tls` until the P-256/P-384 ECDHE TLS profile is wired into ClientHello, ServerHello, key schedule, and interop tests. The native P-256 ECDH primitive exists for that work.

### Zia Example

> Tls is accessible via fully-qualified calls: `Viper.Crypto.Tls.ConnectResult(...)`, `Viper.Crypto.Tls.Send(...)`, `Viper.Crypto.Tls.Recv(...)`.

### BASIC Example

```basic
' Connect to HTTPS server
DIM tlsResult AS OBJECT = Viper.Crypto.Tls.ConnectResult("example.com", 443)

IF tlsResult.IsErr THEN
    PRINT "TLS Error: "; tlsResult.UnwrapErrStr()
    END
END IF

DIM conn AS Viper.Crypto.Tls = tlsResult.Unwrap()
IF conn.IsOpen THEN
    ' Materialize string-valued properties before PRINT. Directly printing a
    ' runtime string property currently triggers VDOC-180 in the BASIC lowerer.
    DIM connectedHost AS STRING = conn.Host
    DIM connectedPort AS INTEGER = conn.Port
    PRINT "Connected to "; connectedHost; ":"; connectedPort

    ' Send HTTP request over TLS
    DIM request AS STRING = "GET / HTTP/1.1" + CHR(13) + CHR(10) + _
                            "Host: example.com" + CHR(13) + CHR(10) + _
                            "Connection: close" + CHR(13) + CHR(10) + _
                            CHR(13) + CHR(10)
    conn.SendStr(request)

    ' Receive response
    DIM response AS STRING = conn.RecvStr(4096)
    PRINT response

    conn.Close()
END IF
```

### Timeout Example

```basic
' Connect with 5 second timeout
DIM connResult AS OBJECT = Viper.Crypto.Tls.ConnectForResult("slow-server.com", 443, 5000)

IF connResult.IsOk THEN
    DIM conn AS Viper.Crypto.Tls = connResult.Unwrap()
    ' Each address attempt and later TLS I/O uses this timeout independently.
    conn.SendStr("Hello, TLS!")
    DIM response AS STRING = conn.RecvStr(1024)
    conn.Close()
ELSE
    PRINT "Connection timed out or failed: "; connResult.UnwrapErrStr()
END IF
```

### Binary Data Example

```basic
' Send and receive binary data over TLS
DIM connResult AS OBJECT = Viper.Crypto.Tls.ConnectResult("api.example.com", 8443)

IF connResult.IsOk THEN
    DIM conn AS Viper.Crypto.Tls = connResult.Unwrap()
    ' Send binary packet
    DIM packet AS OBJECT = Viper.Collections.Bytes.FromHex("010203040506")
    conn.Send(packet)

    ' Receive binary response
    DIM response AS Viper.Collections.Bytes = conn.Recv(1024)
    PRINT "Received "; response.Length; " bytes"
    PRINT "Hex: "; response.ToHex()

    conn.Close()
END IF
```

### Error Handling

TLS wrapper methods use return values for routine failures:

- Prefer `ConnectResult()` / `ConnectForResult()` / `ConnectOptionsResult()` for production code; invalid input, connection failure, timeout, certificate verification failure, and TLS handshake failure return `Err(message)`
- `Connect()` / `ConnectFor()` / `ConnectOptions()` remain available for compatibility and return `NULL` on setup failure
- `Error()` remains available as a compatibility diagnostic for an existing connection object
- `ConnectFor()` rejects timeout values too large to fit the runtime socket timeout
- A timeout at or below zero selects the 30-second default. Positive timeouts apply independently
  to each address attempt and subsequent read/write operation, not to the whole call.
- Host strings containing embedded `NUL` bytes are rejected instead of being truncated
- `Send()` / `SendStr()` return a negative value if the connection is closed or invalid
- `Recv()` returns an empty `Bytes` on clean EOF and `NULL` on receive errors. `RecvStr()` returns an
  empty string for EOF, errors, and a valid empty request, so those cases are not distinguishable by
  the returned text alone.
- `RecvLine()` returns an empty string if the connection closes or errors before a newline, so truncated protocol lines are not reported as complete lines

Use a `Connect*Result` error for setup failures. After a handle exists, `Error()` exposes the
session's current diagnostic; after `Close()` it returns `"connection closed"`.

### Security Notes

- **Certificate verification:** Server certificates are validated against the configured trust
  source by default. Windows uses CryptoAPI; macOS/Linux use the in-tree verifier and a PEM bundle.
  The in-tree path builds only from certificates supplied by the peer and does not fetch missing
  intermediates through AIA.
- **Hostname verification:** Server certificate must match the requested hostname
- **Leaf certificate purpose:** The server certificate must be valid for TLS server authentication and, when KeyUsage is present, include `digitalSignature`
- **Trust policy:** Untrusted certificates fail. A self-signed certificate can be accepted only when
  it is deliberately installed in/selectable through the configured trust source and satisfies the
  remaining name/purpose checks.
- **Revocation:** The runtime does not perform OCSP/CRL checks. On the in-tree verifier,
  `VIPER_TLS_REQUIRE_REVOCATION` makes verification fail closed because mandatory revocation is not
  implemented.
- **Forward secrecy:** The ephemeral X25519 key exchange provides forward secrecy
- **AEAD encryption:** AES-128-GCM or ChaCha20-Poly1305 provides authenticated encryption

### Use Cases

- **HTTPS clients:** Connect to secure web services
- **Secure APIs:** Communicate with REST/gRPC services over TLS
- **Database connections:** Secure database communication
- **Email protocols:** IMAPS, SMTPS, POP3S
- **Custom protocols:** Add TLS security to any TCP protocol

### Tls vs Http

| Use Case                    | Recommended Class  |
|-----------------------------|--------------------|
| Simple HTTPS requests       | `Viper.Network.Http` |
| Custom HTTP headers/options | `Viper.Network.HttpReq` |
| WebSocket over TLS          | `Viper.Network.WebSocket` (wss://) |
| Raw TLS socket              | `Viper.Crypto.Tls` |
| Custom TLS protocols        | `Viper.Crypto.Tls` |

---

## Viper.Crypto.Password

High-level password hashing and verification using memory-hard scrypt by default. Approved mode uses PBKDF2-HMAC-SHA256 because scrypt is not an approved-mode service.

**Type:** Static utility class

### Methods

| Method                    | Signature                  | Description                                                      |
|---------------------------|----------------------------|------------------------------------------------------------------|
| `Hash(password)`          | `String(String)`           | Hash a password with default scrypt parameters, or PBKDF2 in approved mode |
| `HashScrypt(password)`    | `String(String)`           | Force default scrypt in compatibility mode; traps in approved mode |
| `HashScryptParams(password, n, r, p)` | `String(String,Integer,Integer,Integer)` | Hash with explicit scrypt parameters at or above the password policy minimum |
| `HashIters(password, n)`  | `String(String, Integer)`  | Legacy PBKDF2 hash with a custom iteration count and random salt |
| `Verify(password, hash)`  | `Boolean(String, String)`  | Verify a password against a previously generated hash            |
| `NeedsRehash(hash)`       | `Boolean(String)`          | Report whether a stored hash should be upgraded                  |

### Output Format

`Hash` and `HashScrypt` return a self-describing scrypt string in compatibility mode:

```text
SCRYPT$<log2N>$<r>$<p>$<base64-salt>$<base64-hash>
```

`HashIters` returns the PBKDF2 format. `Hash` also returns this format in approved mode:

```text
PBKDF2$<iterations>$<base64-salt>$<base64-hash>
```

Each format stores everything needed for verification: the algorithm identifier, cost parameters,
salt, and derived key.

### Notes

- Uses scrypt-SHA256 as the default password hashing KDF in compatibility mode
- Approved mode changes `Hash` to PBKDF2-HMAC-SHA256 and disables `HashScrypt` / `HashScryptParams`
- `HashScryptParams` rejects parameters weaker than the default password policy (`N=16384`, `r=8`, `p=1`) and rejects unsupported memory costs before derivation
- `HashIters` is retained for PBKDF2 compatibility and rejects requests below 100,000
- Hashing a null password traps; `Verify(NULL, hash)` returns `false`. A real empty password is
  accepted, and embedded `NUL` bytes are significant.
- An independently random 16-byte salt is generated automatically for each hash
- The salt and cost parameters are embedded in the output string, so no separate storage is needed
- `Verify` parses the stored hash string to extract parameters before re-deriving
- `Verify` accepts both `SCRYPT$...` and `PBKDF2$...` records in compatibility mode. Approved mode
  rejects scrypt records and verifies only PBKDF2.
- `NeedsRehash` fully validates the stored numeric/Base64 fields. Approved mode accepts well-formed
  PBKDF2 hashes at or above 300,000 iterations. Compatibility mode recommends upgrading every
  PBKDF2 record and currently considers only the exact default scrypt tuple (`N=16384`, `r=8`,
  `p=1`) current; even a stronger custom tuple is marked stale. This policy bug is tracked in
  [VDOC-174](../documentation-review-findings.md#vdoc-174--passwordneedsrehash-marks-stronger-custom-scrypt-hashes-as-stale).
- `Verify` returns `false` for malformed, null, or non-canonical stored hashes instead of trapping
- Stored password hashes require strict Base64 salt/hash fields with the expected decoded lengths
- Use `HashIters` to increase iterations beyond the default when you have the latency budget

### Zia Example

```rust
module PasswordDemo;

bind Viper.Terminal;
bind Viper.Crypto.Password as Password;
bind Viper.Text.Fmt as Fmt;

func start() {
    // Hash a password
    var hash = Password.Hash("secret123");
    Say("Hash: " + hash);  // SCRYPT$14$8$1$...

    // Verify correct password
    Say("Verify correct: " + Fmt.Bool(Password.Verify("secret123", hash)));  // true

    // Verify wrong password
    Say("Verify wrong: " + Fmt.Bool(Password.Verify("wrong", hash)));        // false
}
```

### BASIC Example

```basic
' Hash a password
DIM hash AS STRING = Viper.Crypto.Password.Hash("secret123")
PRINT "Hash: "; hash  ' Output: SCRYPT$14$8$1$...

' Verify correct password
PRINT "Correct: "; Viper.Crypto.Password.Verify("secret123", hash)  ' Output: 1

' Verify wrong password
PRINT "Wrong: "; Viper.Crypto.Password.Verify("wrong", hash)        ' Output: 0

' Hash with custom iteration count
DIM strongHash AS STRING = Viper.Crypto.Password.HashIters("secret123", 500000)
PRINT "Strong hash: "; strongHash
PRINT "Verify: "; Viper.Crypto.Password.Verify("secret123", strongHash)  ' Output: 1
PRINT "Needs rehash: "; Viper.Crypto.Password.NeedsRehash(strongHash)    ' 1 in compatibility mode
```

### Security Recommendations

1. **Use `Hash` for the active module policy:** compatibility mode uses the runtime's scrypt
   baseline; approved mode uses PBKDF2-HMAC-SHA256
2. **Rehash over time:** call `NeedsRehash` after login and replace old hashes with `Hash`
3. **Never store plaintext:** Always store the hash string, never the original password
4. **Fixed-time final comparison:** `Verify` compares the final 32-byte derived values without an
   early byte-mismatch exit. Format parsing, algorithm selection, validation, and KDF work are not
   constant-time, so callers should still return one uniform authentication failure externally.

### Password vs KeyDerive

| Use Case                    | Recommended                |
|-----------------------------|----------------------------|
| Store user passwords        | `Viper.Crypto.Password`   |
| Derive encryption keys      | `Viper.Crypto.KeyDerive`  |
| Simple hash-and-verify      | `Viper.Crypto.Password`   |
| Custom salt management      | `Viper.Crypto.KeyDerive`  |

---

## See Also

- [Collections](collections/README.md) - `Bytes` for binary data handling
- [Text Processing](text/README.md) - `Codec` for Base64/Hex encoding of hashes and keys
- [Network](network.md) - `Tcp` for unencrypted connections, `Http` for HTTPS, `WebSocket` for WSS
