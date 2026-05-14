---
status: active
audience: public
last-verified: 2026-05-14
---

# Cryptography

> Cryptographic hashing, authentication, key derivation, and secure random generation.

**Part of the [Viper Runtime Library](README.md)**

## Contents

- [Viper.Crypto.Aes](#vipercryptoaes)
- [Viper.Crypto.Cipher](#vipercryptocipher)
- [Viper.Crypto.Hash](#vipercryptohash)
- [Viper.Crypto.KeyDerive](#vipercryptokeyderive)
- [Viper.Crypto.Module](#vipercryptomodule)
- [Viper.Crypto.Password](#vipercryptopassword)
- [Viper.Crypto.Rand](#vipercryptorand)
- [Viper.Crypto.Tls](#vipercryptotls)

---

## Viper.Crypto.Aes

AES utilities: authenticated AES-128-GCM/AES-256-GCM for `Bytes` and password-encrypted strings, plus legacy raw AES-CBC with PKCS7 padding.

**Type:** Static utility class

### Methods

| Method                              | Signature                      | Description                                                                   |
|-------------------------------------|--------------------------------|-------------------------------------------------------------------------------|
| `Encrypt(data, key, iv)`            | `Bytes(Bytes, Bytes, Bytes)`   | Encrypt data with AES key and initialization vector                           |
| `Decrypt(data, key, iv)`            | `Bytes(Bytes, Bytes, Bytes)`   | Decrypt data with AES key and initialization vector                           |
| `EncryptAuth(data, key, aad)`       | `Bytes(Bytes, Bytes, Bytes)`   | Encrypt bytes with AES-GCM and authenticated data                             |
| `DecryptAuth(data, key, aad)`       | `Bytes(Bytes, Bytes, Bytes)`   | Decrypt AES-GCM bytes and verify authenticated data                           |
| `EncryptStr(plaintext, password)`   | `Bytes(String, String)`        | Encrypt a string with a password using PBKDF2 + AES-128-GCM                    |
| `DecryptStr(ciphertext, password)`  | `String(Bytes, String)`        | Decrypt ciphertext to a string using the authenticated string format           |

### Notes

- `EncryptAuth`/`DecryptAuth` accept a 16-byte AES-128 key or a 32-byte AES-256 key and bind the `[magic(4)][nonce(12)]` header plus caller-provided AAD into the GCM tag. Malformed frames, wrong AAD, wrong keys, and modified ciphertext return `null`.
- `Encrypt`/`Decrypt` are legacy AES-CBC helpers. CBC ciphertext is not authenticated; prefer `EncryptAuth`, `EncryptStr`, or `Viper.Crypto.Cipher`. Empty plaintext is valid and round-trips through PKCS7 padding. `Decrypt` returns `null` when CBC padding is invalid. CBC helpers trap in approved mode.
- `EncryptStr` rejects empty passwords, derives an AES-128 key from the password using PBKDF2-HMAC-SHA256 with a random salt and a 300,000-iteration default, and authenticates its header as AAD
- `EncryptStr` output format is `[magic(4)][iterations(4)][salt(16)][nonce(12)][ciphertext][tag(16)]`
- `DecryptStr` remains backward-compatible with older `[IV(16)][AES-CBC ciphertext]` payloads
- String plaintexts and passwords use the stored Viper string byte length, so embedded `NUL` bytes are significant
- For higher-level authenticated encryption with automatic key management, use `Viper.Crypto.Cipher` instead
- The in-tree AES block primitive is portable C and not a certified constant-time backend. Use `Cipher` or the authenticated AES-GCM helpers for production data formats.

### Zia Example

```rust
module AesDemo;

bind Viper.Terminal;
bind Viper.Crypto.Aes as Aes;
bind Viper.Crypto.Rand as CRand;
bind Viper.Collections;
bind Viper.Fmt as Fmt;

func start() {
    // Encrypt a string with a password
    var ciphertext = Aes.EncryptStr("Hello, AES!", "my-password");
    Say("Encrypted len: " + Fmt.Int(ciphertext.Length));

    // Decrypt it back
    var plaintext = Aes.DecryptStr(ciphertext, "my-password");
    Say("Decrypted: " + plaintext);

    // Authenticated AES with explicit key and AAD
    var key = CRand.Bytes(16);   // 128-bit AES-GCM key
    var aad = Bytes.FromStr("file:v1");
    var data = Bytes.FromStr("Secret data");
    var enc = Aes.EncryptAuth(data, key, aad);
    var dec = Aes.DecryptAuth(enc, key, aad);
    Say("Round-trip: " + Bytes.ToStr(dec));
}
```

### BASIC Example

```basic
' Encrypt and decrypt a string with a password
DIM ciphertext AS OBJECT = Viper.Crypto.Aes.EncryptStr("Hello, AES!", "my-password")
DIM plaintext AS STRING = Viper.Crypto.Aes.DecryptStr(ciphertext, "my-password")
PRINT "Decrypted: "; plaintext

' Authenticated AES with explicit key and AAD
DIM key AS OBJECT = Viper.Crypto.Rand.Bytes(16)   ' 128-bit AES-GCM key
DIM aad AS OBJECT = Viper.Collections.Bytes.FromStr("file:v1")
DIM data AS OBJECT = Viper.Collections.Bytes.FromStr("Secret data")
DIM enc AS OBJECT = Viper.Crypto.Aes.EncryptAuth(data, key, aad)
DIM dec AS OBJECT = Viper.Crypto.Aes.DecryptAuth(enc, key, aad)
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
| `EncryptAAD(data, password, aad)`| `Bytes(Bytes,String,Bytes)` | Encrypt and bind caller-provided authenticated data   |
| `DecryptAAD(data, password, aad)`| `Bytes(Bytes,String,Bytes)` | Decrypt and verify caller-provided authenticated data |
| `EncryptWithKey(data, key)`      | `Bytes(Bytes, Bytes)`       | Encrypt data with a 32-byte key                       |
| `DecryptWithKey(data, key)`      | `Bytes(Bytes, Bytes)`       | Decrypt key-encrypted data                            |
| `EncryptWithKeyAAD(data,key,aad)`| `Bytes(Bytes,Bytes,Bytes)`  | Encrypt with a key and authenticated data             |
| `DecryptWithKeyAAD(data,key,aad)`| `Bytes(Bytes,Bytes,Bytes)`  | Decrypt with a key and authenticated data             |

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
- **Nonce Size:** 96 bits (12 bytes, randomly generated)
- **Authentication Tag:** 128 bits (16 bytes)
- **Key Derivation:** PBKDF2-HMAC-SHA256 with random 16-byte salt and a 300,000-iteration default
- Header bytes and caller-provided AAD are authenticated by the AEAD tag
- Decryption verifies that the AEAD backend returned exactly the expected plaintext length; malformed or truncated payloads return `null`
- `Decrypt()` remains backward-compatible with older unversioned PBKDF2/HKDF payloads; new payloads use the versioned `VCP2` format
- Approved mode rejects compatibility and legacy ciphertext formats instead of silently decrypting with non-approved algorithms
- Password strings use their stored byte length, so embedded `NUL` bytes are part of the password

### Zia Example

```rust
module CipherDemo;

bind Viper.Terminal;
bind Viper.Crypto.Cipher as Cipher;
bind Viper.Collections;
bind Viper.Fmt as Fmt;

func start() {
    // Encrypt data with a password
    var plaintext = Bytes.FromStr("Secret message");
    var password = "my-secure-password";
    var ciphertext = Cipher.Encrypt(plaintext, password);
    Say("Encrypted len: " + Fmt.Int(ciphertext.Length));

    // Generate a random encryption key
    var key = Cipher.GenerateKey();
    Say("Key len: " + Fmt.Int(key.Length));
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
DIM decrypted AS Viper.Collections.Bytes
decrypted = Viper.Crypto.Cipher.Decrypt(ciphertext, password)
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
DIM dec2 AS Viper.Collections.Bytes
dec2 = Viper.Crypto.Cipher.DecryptWithKey(enc2, key)
PRINT "Key decrypt: "; dec2.ToStr()
```

### Key-Based Encryption Example

```basic
' Generate a random key
DIM key AS OBJECT = Viper.Crypto.Cipher.GenerateKey()

' Encrypt with the key
DIM plaintext AS OBJECT = Viper.Collections.Bytes.FromString("Secret data")
DIM ciphertext AS OBJECT = Viper.Crypto.Cipher.EncryptWithKey(plaintext, key)

' Decrypt with the same key
DIM decrypted AS OBJECT = Viper.Crypto.Cipher.DecryptWithKey(ciphertext, key)
```

### Key Derivation Example

```basic
' Derive a key from password (useful when you need the same key multiple times)
DIM password AS STRING = "user-password"
DIM salt AS OBJECT = Viper.Crypto.Rand.Bytes(16)

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
DIM decrypted AS OBJECT = Viper.Crypto.Cipher.Decrypt(encData, password)
Viper.IO.File.WriteAllBytes("secret.doc", decrypted)
```

### Error Handling

Cipher operations use two failure modes:

- `Decrypt()` returns `NULL` when authentication fails (wrong password or corrupted ciphertext)
- `DecryptWithKey()` returns `NULL` when authentication fails (wrong key or corrupted data)
- `EncryptWithKey()`/`DecryptWithKey()` trap if key is not exactly 32 bytes
- Structural misuse still traps: null ciphertext, empty password, invalid AAD objects, or malformed too-short ciphertext
- Empty plaintext is allowed and produces valid ciphertext

### Security Recommendations

1. **Use strong passwords:** Combine with password requirements in your application
2. **Store keys securely:** Never hardcode keys in source code
3. **Use password-based for user data:** Let the API handle salt generation
4. **Use key-based for application data:** When you manage key storage separately
5. **Don't reuse keys:** Generate new keys or use password-based encryption with automatic salts

### When to Use Cipher vs. Other Crypto

| Use Case                      | Recommended                          |
|-------------------------------|--------------------------------------|
| Encrypt user data             | `Viper.Crypto.Cipher.Encrypt()`      |
| Encrypt with managed keys     | `Viper.Crypto.Cipher.EncryptWithKey()` |
| Password storage              | `Viper.Crypto.Password.Hash()`          |
| Message authentication only   | `Viper.Crypto.Hash.HmacSHA256()`     |
| Data integrity check          | `Viper.Crypto.Hash.SHA256()`         |
| Secure communication          | `Viper.Crypto.Tls`                   |

---

## Viper.Crypto.Hash

Cryptographic hash functions, checksums, and HMAC authentication for strings and binary data.

**Type:** Static utility class

### Hash Methods

| Method               | Signature         | Description                              |
|----------------------|-------------------|------------------------------------------|
| `CRC32(str)`         | `Integer(String)` | Compute CRC32 checksum of a string       |
| `CRC32Bytes(bytes)`  | `Integer(Bytes)`  | Compute CRC32 checksum of a Bytes object |
| `MD5(str)`           | `String(String)`  | Compute MD5 hash of a string             |
| `MD5Bytes(bytes)`    | `String(Bytes)`   | Compute MD5 hash of a Bytes object       |
| `SHA1(str)`          | `String(String)`  | Compute SHA1 hash of a string            |
| `SHA1Bytes(bytes)`   | `String(Bytes)`   | Compute SHA1 hash of a Bytes object      |
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
| `HmacMD5(key, data)`       | `String(String,String)` | HMAC-MD5 with string key and data   |
| `HmacMD5Bytes(key, data)`  | `String(Bytes,Bytes)`   | HMAC-MD5 with Bytes key and data    |
| `HmacSHA1(key, data)`      | `String(String,String)` | HMAC-SHA1 with string key and data  |
| `HmacSHA1Bytes(key, data)` | `String(Bytes,Bytes)`   | HMAC-SHA1 with Bytes key and data   |
| `HmacSHA256(key, data)`    | `String(String,String)` | HMAC-SHA256 with string key and data|
| `HmacSHA256Bytes(key, data)`| `String(Bytes,Bytes)`  | HMAC-SHA256 with Bytes key and data |

### Hash Output Formats

| Algorithm | Output Size   | Format                    |
|-----------|---------------|---------------------------|
| CRC32     | 32 bits       | Integer (0 to 4294967295) |
| MD5       | 128 bits      | 32-character hex string   |
| SHA1      | 160 bits      | 40-character hex string   |
| SHA256    | 256 bits      | 64-character hex string   |
| HMAC-*    | Same as hash  | Same as underlying hash   |
| Fast      | 64 bits       | Integer (i64), process-keyed SipHash |

String hash and HMAC methods use the stored Viper string byte length. Embedded `NUL` bytes are hashed as data, matching the corresponding `Bytes` methods for the same byte sequence. Empty strings are valid inputs; null string references trap instead of being silently hashed as empty strings.

### Fast Hash Methods

The `Fast`, `FastBytes`, and `FastInt` methods use SipHash-2-4 with a per-process CSPRNG seed. These are **non-cryptographic** hashes for hash-table and partitioning use. They are not stable across process launches and are not suitable for signatures, passwords, MACs, or persistent content IDs.

```rust
module FastHashDemo;

bind Viper.Terminal;
bind Viper.Crypto.Hash as Hash;
bind Viper.Fmt as Fmt;

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

### Security Warnings

- **CRC32**: NOT cryptographic. Only for error detection, not security.
- **MD5**: Cryptographically broken. Collisions can be generated in seconds. Do NOT use for security.
- **SHA1**: Cryptographically broken. Chosen-prefix collisions demonstrated. Do NOT use for security.
- **SHA256**: Currently collision/preimage resistant. Do not use plain SHA256 as a password hash or as a MAC; use `Password`, `KeyDerive`, or HMAC as appropriate.
- **ConstantTimeEquals**: Intended for same-length public-format digests and MAC tags. Length mismatch returns false before byte comparison; do not use it to hide secret lengths.

### Zia Example

```rust
module HashDemo;

bind Viper.Terminal;
bind Viper.Crypto.Hash as Hash;

func start() {
    Say("MD5: " + Hash.MD5("hello"));
    Say("SHA1: " + Hash.SHA1("hello"));
    Say("SHA256: " + Hash.SHA256("hello"));
}
```

### BASIC Example

```basic
' Hash strings with common algorithms
PRINT "MD5: "; Viper.Crypto.Hash.MD5("hello")
PRINT "SHA1: "; Viper.Crypto.Hash.SHA1("hello")
PRINT "SHA256: "; Viper.Crypto.Hash.SHA256("hello")
```

### HMAC Example

```basic
' HMAC for message authentication
DIM secretKey AS STRING = "my-secret-key"
DIM message AS STRING = "Important message to authenticate"

' Compute HMAC-SHA256
DIM mac AS STRING = Viper.Crypto.Hash.HmacSHA256(secretKey, message)
PRINT "HMAC: "; mac

' Verify message authenticity
DIM receivedMac AS STRING = "..." ' Received with message
DIM computedMac AS STRING = Viper.Crypto.Hash.HmacSHA256(secretKey, message)
IF Viper.Crypto.Hash.ConstantTimeEquals(receivedMac, computedMac) THEN
    PRINT "Message is authentic"
ELSE
    PRINT "WARNING: Message was tampered with!"
END IF

' HMAC with binary data
DIM keyBytes AS OBJECT = Viper.Crypto.Rand.Bytes(32)
DIM dataBytes AS OBJECT = Viper.IO.File.ReadAllBytes("data.bin")
DIM binaryMac AS STRING = Viper.Crypto.Hash.HmacSHA256Bytes(keyBytes, dataBytes)
```

### HMAC Algorithm

HMAC (Hash-based Message Authentication Code) provides message authentication using a secret key:

```text
HMAC(K, m) = H((K' xor opad) || H((K' xor ipad) || m))
```

Where:
- K' = K if len(K) <= block_size, else K' = H(K)
- ipad = 0x36 repeated block_size times
- opad = 0x5c repeated block_size times
- block_size = 64 bytes for MD5, SHA1, SHA256

The runtime streams HMAC input through the underlying hash context instead of buffering the whole message.

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
| `salt`       | Bytes   | Unique random salt (non-empty; 16 bytes recommended) |
| `iterations` | Integer | PBKDF2 iteration count (100,000 to 10,000,000; recommend 300,000+ for encryption keys) |
| `n`, `r`, `p` | Integer | scrypt cost parameters; `n` must be a supported power of two |
| `keyLen`     | Integer | Desired key length in bytes (1-1024)           |

### Traps

- `iterations < 100000` or `iterations > 10000000`: Traps instead of silently changing the requested work factor
- null `password`: Traps instead of deriving the empty-password key. A real empty string is allowed when the application explicitly wants that input.
- empty `salt`: Traps with "salt must not be empty"
- `keyLen < 1 or keyLen > 1024`: Traps with "key_len must be between 1 and 1024"
- unsupported scrypt memory/cost parameters: Traps before allocating memory
- scrypt APIs trap in approved mode; use PBKDF2 APIs there

### Zia Example

```rust
module KeyDeriveDemo;

bind Viper.Terminal;
bind Viper.Crypto.KeyDerive as KD;
bind Viper.Crypto.Rand as CRand;

func start() {
    // Generate a random salt
    var salt = CRand.Bytes(16);

    // Derive a key using PBKDF2-SHA256
    var keyHex = KD.Pbkdf2SHA256Str("password123", salt, 300000, 32);
    var scryptHex = KD.ScryptSHA256Str("password123", salt, 16384, 8, 1, 32);
    Say("Derived key: " + keyHex);
    Say("scrypt key: " + scryptHex);
}
```

### BASIC Example

```basic
' Derive a key from a password
DIM password AS STRING = "user-password"
DIM salt AS OBJECT = Viper.Crypto.Rand.Bytes(16)  ' Random 16-byte salt
DIM iterations AS INTEGER = 300000  ' High iteration count for security

' Derive a 32-byte key
DIM key AS OBJECT = Viper.Crypto.KeyDerive.Pbkdf2SHA256(password, salt, iterations, 32)

' Or get it as a hex string
DIM keyHex AS STRING = Viper.Crypto.KeyDerive.Pbkdf2SHA256Str(password, salt, iterations, 32)
PRINT "Derived key: "; keyHex
```

### Password Storage Example

```basic
' Storing a password hash
FUNCTION HashPassword(password AS STRING) AS STRING
    ' Generate random salt
    DIM salt AS OBJECT = Viper.Crypto.Rand.Bytes(16)

    ' Derive key with high iteration count
    DIM hash AS STRING = Viper.Crypto.KeyDerive.Pbkdf2SHA256Str(password, salt, 300000, 32)

    ' Convert salt to hex for storage
    DIM saltHex AS STRING = Viper.Codec.HexEncode(salt)

    ' Store iterations:salt:hash
    RETURN "300000:" & saltHex & ":" & hash
END FUNCTION

FUNCTION VerifyPassword(password AS STRING, stored AS STRING) AS BOOLEAN
    ' Parse stored format: iterations:salt:hash
    DIM parts() AS STRING = SPLIT(stored, ":")
    DIM iterations AS INTEGER = VAL(parts(0))
    DIM salt AS OBJECT = Viper.Codec.HexDecode(parts(1))
    DIM storedHash AS STRING = parts(2)

    ' Recompute hash with same parameters
    DIM computedHash AS STRING = Viper.Crypto.KeyDerive.Pbkdf2SHA256Str(password, salt, iterations, 32)

    RETURN Viper.Crypto.Hash.ConstantTimeEquals(computedHash, storedHash)
END FUNCTION
```

### Security Recommendations

1. **Use `Password` for password storage**: it includes a self-describing format and migration checks
2. **Use unique salts**: Generate a new random salt for each password
3. **Store salt with hash**: You need the salt to verify passwords
4. **Use sufficient key length**: 32 bytes (256 bits) is standard

---

## Viper.Crypto.Module

Validation-readiness controls for the zero-dependency in-tree crypto module.

**Type:** Static utility class

### Methods

| Method                 | Signature  | Description                                             |
|------------------------|------------|---------------------------------------------------------|
| `EnableApprovedMode()` | `Boolean()`| Run module self-tests, instantiate the DRBG, and enable approved-mode policy gates |
| `DisableApprovedMode()`| `Void()`   | Return to compatibility mode                            |
| `IsApprovedMode()`     | `Boolean()`| Return whether approved-mode policy gates are active    |
| `Status()`             | `String()` | Return module state text such as `ready` or a self-test failure |

### Approved-Mode Behavior

- Runs startup self-tests for SHA-2, HMAC/HKDF-SHA256, AES-128-GCM, AES-256-GCM, and an HMAC-DRBG known-answer path before enabling approved mode
- Routes `Viper.Crypto.Rand` and internal nonce/key generation through the module HMAC-DRBG once approved mode is enabled
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
IF Viper.Crypto.Module.EnableApprovedMode() THEN
    PRINT "Crypto module status: "; Viper.Crypto.Module.Status()
END IF
```

---

## Viper.Crypto.Rand

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
| Linux    | getrandom(2), then /dev/urandom fallback |
| macOS    | arc4random_buf                   |
| Windows  | BCryptGenRandom                  |

### Zia Example

```rust
module CryptoRandDemo;

bind Viper.Terminal;
bind Viper.Crypto.Rand as CRand;
bind Viper.Collections;
bind Viper.Fmt as Fmt;

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
DIM key AS OBJECT = Viper.Crypto.Rand.Bytes(32)   ' 256-bit key
DIM iv AS OBJECT = Viper.Crypto.Rand.Bytes(16)    ' 128-bit IV
DIM salt AS OBJECT = Viper.Crypto.Rand.Bytes(16)  ' Salt for PBKDF2

' Generate random integers
DIM dice AS INTEGER = Viper.Crypto.Rand.Int(1, 6)       ' Roll a die: 1-6
DIM card AS INTEGER = Viper.Crypto.Rand.Int(0, 51)      ' Pick a card: 0-51
DIM token AS INTEGER = Viper.Crypto.Rand.Int(100000, 999999)  ' 6-digit code
```

### Security Token Example

```basic
' Generate a secure random token
FUNCTION GenerateToken(length AS INTEGER) AS STRING
    DIM bytes AS OBJECT = Viper.Crypto.Rand.Bytes(length)
    RETURN Viper.Codec.HexEncode(bytes)
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
    FOR i AS INTEGER = n - 1 TO 1 STEP -1
        DIM j AS INTEGER = Viper.Crypto.Rand.Int(0, i)
        ' Swap arr(i) and arr(j)
        DIM temp AS INTEGER = arr(i)
        arr(i) = arr(j)
        arr(j) = temp
    NEXT i
END SUB
```

### Use Cases

- **Key generation**: Generate encryption keys, IVs, nonces
- **Salt generation**: Create unique salts for password hashing
- **Token generation**: Create session tokens, API keys
- **Secure selection**: Pick random elements securely
- **Cryptographic protocols**: Implement secure authentication flows

### Security Guarantees

- Uses operating system's cryptographic random number generator (CSPRNG)
- Suitable for all cryptographic purposes
- Unpredictable output even with partial state disclosure
- Thread-safe on all platforms

---

## Viper.Crypto.Tls

TLS (Transport Layer Security) client for encrypted TCP connections. Uses TLS 1.3 with modern cipher suites.

**Type:** Instance class

**Constructors:**

- `Viper.Crypto.Tls.Connect(host, port)` - Connect with TLS to host:port
- `Viper.Crypto.Tls.ConnectFor(host, port, timeoutMs)` - Connect with timeout
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
| `SendStr(text)` | Integer | Send string as UTF-8, return bytes sent    |

### Receive Methods

| Method             | Returns | Description                                |
|--------------------|---------|--------------------------------------------|
| `Recv(maxBytes)`   | Bytes   | Receive up to maxBytes (may return fewer)  |
| `RecvStr(maxBytes)`| String  | Receive up to maxBytes as UTF-8 string     |

### Control Methods

| Method    | Returns | Description                                       |
|-----------|---------|---------------------------------------------------|
| `Close()` | void    | Close the TLS connection                          |
| `Error()` | String  | Get last error message (empty if no error)        |

### TLS Implementation

The TLS implementation uses:

- **Protocol:** TLS 1.3 (RFC 8446)
- **Key Exchange:** X25519 (Curve25519 ECDH)
- **Cipher:** ChaCha20-Poly1305 AEAD
- **Hash:** SHA-256
- **Certificate Verification:** Enabled by default against the runtime trust source. Windows uses CryptoAPI; macOS and Linux use the built-in PEM-bundle verifier with standard system trust bundles.
- **Trust and ALPN controls:** `ConnectOptions` can pin validation to a PEM bundle, advertise comma-separated ALPN preferences such as `"h2,http/1.1"`, and read the negotiated protocol from `NegotiatedAlpn`.
- **Certificate Signature Support:** In-tree verification of ECDSA P-256, RSA PKCS#1 v1.5, and RSA-PSS certificate signatures. The TLS client advertises only signature algorithms it can verify; ECDSA P-384 is not advertised until P-384 CertificateVerify support is implemented.
- **Leaf-certificate policy:** Built-in verification enforces TLS server-auth EKU and requires the `digitalSignature` KeyUsage bit when KeyUsage is present on the server certificate
- **DER strictness:** Certificate signature algorithms, ECDSA signatures, RSA public keys, and PSS parameters are parsed as strict DER with exact length consumption and canonical INTEGERs
- **Hostname / SNI behavior:** DNS hostnames are sent in SNI; IP literals are verified against IP SANs but are not sent in SNI. SubjectAltName suppresses CommonName fallback even when the SAN contains no DNS names, and broad public-suffix wildcards are rejected.
- **SubjectAltName matching:** Hostname verification scans all DNS SAN entries. The public C extraction helper still writes only up to its caller-provided output capacity.
- **Certificate chain behavior:** Built-in verification rejects malformed certificate-list tails and chains with more than 16 intermediates instead of silently ignoring excess entries.
- **Handshake strictness:** Unexpected handshake messages and trailing certificate-message bytes fail the handshake instead of being skipped.
- **Key-share validation:** X25519 all-zero shared secrets are rejected during the handshake
- **String handling:** `SendStr` sends the full stored string byte length, including embedded `NUL` bytes
- **Connection state:** `IsOpen` is true only while the TLS session is in the connected state
- **Approved mode:** Current public TLS is compatibility-mode only because the wire handshake is still X25519/SHA-256 based. Approved mode fails closed for `Viper.Crypto.Tls` until the P-256/P-384 ECDHE TLS profile is wired into ClientHello, ServerHello, key schedule, and interop tests. The native P-256 ECDH primitive exists for that work.

### Zia Example

> Tls is accessible via fully-qualified calls: `Viper.Crypto.Tls.Connect(...)`, `Viper.Crypto.Tls.Send(...)`, `Viper.Crypto.Tls.Recv(...)`.

### BASIC Example

```basic
' Connect to HTTPS server
DIM conn AS OBJECT = Viper.Crypto.Tls.Connect("example.com", 443)

IF conn.IsOpen THEN
    PRINT "Connected to "; conn.Host; ":"; conn.Port

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
ELSE
    PRINT "TLS Error: "; conn.Error()
END IF
```

### Timeout Example

```basic
' Connect with 5 second timeout
DIM conn AS OBJECT = Viper.Crypto.Tls.ConnectFor("slow-server.com", 443, 5000)

IF conn.IsOpen THEN
    ' Connection succeeded within timeout
    conn.SendStr("Hello, TLS!")
    DIM response AS STRING = conn.RecvStr(1024)
    conn.Close()
ELSE
    PRINT "Connection timed out or failed: "; conn.Error()
END IF
```

### Binary Data Example

```basic
' Send and receive binary data over TLS
DIM conn AS OBJECT = Viper.Crypto.Tls.Connect("api.example.com", 8443)

IF conn.IsOpen THEN
    ' Send binary packet
    DIM packet AS OBJECT = Viper.Collections.Bytes.FromHex("010203040506")
    conn.Send(packet)

    ' Receive binary response
    DIM response AS OBJECT = conn.Recv(1024)
    PRINT "Received "; response.Length; " bytes"
    PRINT "Hex: "; response.ToHex()

    conn.Close()
END IF
```

### Error Handling

TLS wrapper methods use return values for routine failures:

- `Connect()` / `ConnectFor()` return `NULL` on invalid input, connection failure, timeout, or TLS handshake failure
- Certificate / hostname / handshake diagnostics are preserved in `conn.Error()` when a connection object exists
- `ConnectFor()` rejects timeout values too large to fit the runtime socket timeout
- Host strings containing embedded `NUL` bytes are rejected instead of being truncated
- `Send()` / `SendStr()` return a negative value if the connection is closed or invalid
- `Recv()` returns `NULL` on receive errors; `RecvStr()` returns an empty string on receive errors
- `RecvLine()` returns an empty string if the connection closes or errors before a newline, so truncated protocol lines are not reported as complete lines

Use `Error()` to get descriptive error messages for debugging.

### Security Notes

- **Certificate verification:** Server certificates are validated against system trust store
- **Hostname verification:** Server certificate must match the requested hostname
- **Leaf certificate purpose:** The server certificate must be valid for TLS server authentication and, when KeyUsage is present, include `digitalSignature`
- **No self-signed certificates:** Self-signed or untrusted certificates will fail
- **Forward secrecy:** X25519 key exchange provides perfect forward secrecy
- **AEAD encryption:** ChaCha20-Poly1305 provides authenticated encryption

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
| `HashScrypt(password)`    | `String(String)`           | Same as `Hash`                                                   |
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

This format stores everything needed for verification: the algorithm identifier, iteration count, salt, and derived key.

### Notes

- Uses scrypt-SHA256 as the default password hashing KDF
- Approved mode changes `Hash` to PBKDF2-HMAC-SHA256 and disables `HashScrypt` / `HashScryptParams`
- `HashScryptParams` rejects parameters weaker than the default password policy (`N=16384`, `r=8`, `p=1`) and rejects unsupported memory costs before derivation
- `HashIters` is retained for PBKDF2 compatibility and rejects requests below 100,000
- Hashing a null password traps; `Verify(NULL, hash)` returns `false`
- A random 16-byte salt is generated automatically for each hash
- The salt and cost parameters are embedded in the output string, so no separate storage is needed
- `Verify` parses the stored hash string to extract parameters before re-deriving
- `Verify` accepts current `SCRYPT$...` hashes and legacy `PBKDF2$...` hashes
- `NeedsRehash` is policy-aware: compatibility mode recommends upgrading PBKDF2 to scrypt, while approved mode fully validates PBKDF2 salt/hash fields and accepts only well-formed hashes at or above the default iteration count
- `Verify` returns `false` for malformed, null, or non-canonical stored hashes instead of trapping
- Stored password hashes require strict Base64 salt/hash fields with the expected decoded lengths
- Embedded `NUL` bytes in passwords are significant
- Use `HashIters` to increase iterations beyond the default when you have the latency budget

### Zia Example

```rust
module PasswordDemo;

bind Viper.Terminal;
bind Viper.Crypto.Password as Password;
bind Viper.Fmt as Fmt;

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
PRINT "Needs rehash: "; Viper.Crypto.Password.NeedsRehash(strongHash)    ' Output: 1 for PBKDF2
```

### Security Recommendations

1. **Use `Hash` for production:** scrypt is the current runtime baseline for password storage
2. **Rehash over time:** call `NeedsRehash` after login and replace old hashes with `Hash`
3. **Never store plaintext:** Always store the hash string, never the original password
4. **Timing-safe comparison:** `Verify` uses constant-time comparison to prevent timing attacks

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
