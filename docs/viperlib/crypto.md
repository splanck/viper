# Cryptography

> Cryptographic hashing, authentication, key derivation, and secure random generation.

**Part of the [Viper Runtime Library](README.md)**

## Contents

- [Viper.Crypto.Cipher](#vipercryptocipher)
- [Viper.Crypto.Hash](#vipercryptohash)
- [Viper.Crypto.KeyDerive](#vipercryptokeyderive)
- [Viper.Crypto.Password](#vipercryptopassword)
- [Viper.Crypto.Rand](#vipercryptorand)
- [Viper.Crypto.Tls](#vipercryptotls)

---

## Viper.Crypto.Cipher

High-level symmetric encryption using ChaCha20-Poly1305 AEAD with automatic key derivation.

**Type:** Static utility class

### Encryption Methods

| Method                           | Signature                   | Description                                           |
|----------------------------------|-----------------------------|-------------------------------------------------------|
| `Encrypt(data, password)`        | `Bytes(Bytes, String)`      | Encrypt data with password (automatic salt/nonce)     |
| `Decrypt(data, password)`        | `Bytes(Bytes, String)`      | Decrypt password-encrypted data                       |
| `EncryptWithKey(data, key)`      | `Bytes(Bytes, Bytes)`       | Encrypt data with a 32-byte key                       |
| `DecryptWithKey(data, key)`      | `Bytes(Bytes, Bytes)`       | Decrypt key-encrypted data                            |

### Key Management Methods

| Method                           | Signature                   | Description                                           |
|----------------------------------|-----------------------------|-------------------------------------------------------|
| `GenerateKey()`                  | `Bytes()`                   | Generate a random 32-byte encryption key              |
| `DeriveKey(password, salt)`      | `Bytes(String, Bytes)`      | Derive 32-byte key from password using HKDF           |

### Ciphertext Format

Password-based encryption produces ciphertext in this format:

```
[salt(16 bytes)][nonce(12 bytes)][ciphertext][tag(16 bytes)]
```

Key-based encryption produces:

```
[nonce(12 bytes)][ciphertext][tag(16 bytes)]
```

### Security Properties

- **Algorithm:** ChaCha20-Poly1305 AEAD (RFC 8439)
- **Key Size:** 256 bits (32 bytes)
- **Nonce Size:** 96 bits (12 bytes, randomly generated)
- **Authentication Tag:** 128 bits (16 bytes)
- **Key Derivation:** HKDF-SHA256 with random 16-byte salt

### Zia Example

```zia
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
    Say("Encrypted len: " + Fmt.Int(ciphertext.Len));

    // Generate a random encryption key
    var key = Cipher.GenerateKey();
    Say("Key len: " + Fmt.Int(key.Len));
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
PRINT "Encrypted len: "; ciphertext.Len

' Decrypt and verify round-trip
DIM decrypted AS Viper.Collections.Bytes
decrypted = Viper.Crypto.Cipher.Decrypt(ciphertext, password)
PRINT "Decrypted: "; decrypted.ToStr()

' Generate a random encryption key
DIM key AS Viper.Collections.Bytes
key = Viper.Crypto.Cipher.GenerateKey()
PRINT "Key len: "; key.Len

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

Cipher operations trap on errors:

- `Decrypt()` traps if authentication fails (wrong password or corrupted data)
- `DecryptWithKey()` traps if authentication fails (wrong key or corrupted data)
- `EncryptWithKey()`/`DecryptWithKey()` trap if key is not exactly 32 bytes
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
| Password storage              | `Viper.Crypto.KeyDerive.Pbkdf2SHA256()` |
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
| `Fast(str)`          | `Integer(String)` | Compute FNV-1a hash of a string          |
| `FastBytes(data)`    | `Integer(Bytes)`  | Compute FNV-1a hash of a Bytes object    |
| `FastInt(value)`     | `Integer(Integer)`| Compute FNV-1a hash of an integer        |

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
| FNV-1a    | 64 bits       | Integer (i64)             |

### Fast Hash Methods

The `Fast`, `FastBytes`, and `FastInt` methods use the FNV-1a (Fowler-Noll-Vo) hash algorithm. These are **non-cryptographic** hashes designed for speed in hash tables, checksums, and data partitioning. They are NOT suitable for security purposes.

```zia
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
- **SHA256**: Currently secure. Recommended for all security applications.

### Zia Example

```zia
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
IF receivedMac = computedMac THEN
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

```
HMAC(K, m) = H((K' xor opad) || H((K' xor ipad) || m))
```

Where:
- K' = K if len(K) <= block_size, else K' = H(K)
- ipad = 0x36 repeated block_size times
- opad = 0x5c repeated block_size times
- block_size = 64 bytes for MD5, SHA1, SHA256

---

## Viper.Crypto.KeyDerive

Key derivation functions for deriving cryptographic keys from passwords.

**Type:** Static utility class

### Methods

| Method                                      | Signature                            | Description                        |
|---------------------------------------------|--------------------------------------|------------------------------------|
| `Pbkdf2SHA256(password, salt, iterations, keyLen)` | `Bytes(String,Bytes,Integer,Integer)` | Derive key using PBKDF2-HMAC-SHA256 |
| `Pbkdf2SHA256Str(password, salt, iterations, keyLen)` | `String(String,Bytes,Integer,Integer)` | Same but returns hex string |

### Parameters

| Parameter    | Type    | Description                                    |
|--------------|---------|------------------------------------------------|
| `password`   | String  | The password to derive from                    |
| `salt`       | Bytes   | Unique random salt (at least 16 bytes recommended) |
| `iterations` | Integer | Number of iterations (minimum 1000, recommend 100000+) |
| `keyLen`     | Integer | Desired key length in bytes (1-1024)           |

### Traps

- `iterations < 1000`: Traps with "iterations must be at least 1000"
- `keyLen < 1 or keyLen > 1024`: Traps with "key_len must be between 1 and 1024"

### Zia Example

```zia
module KeyDeriveDemo;

bind Viper.Terminal;
bind Viper.Crypto.KeyDerive as KD;
bind Viper.Crypto.Rand as CRand;

func start() {
    // Generate a random salt
    var salt = CRand.Bytes(16);

    // Derive a key using PBKDF2-SHA256
    var keyHex = KD.Pbkdf2SHA256Str("password123", salt, 1000, 32);
    Say("Derived key: " + keyHex);
}
```

### BASIC Example

```basic
' Derive a key from a password
DIM password AS STRING = "user-password"
DIM salt AS OBJECT = Viper.Crypto.Rand.Bytes(16)  ' Random 16-byte salt
DIM iterations AS INTEGER = 100000  ' High iteration count for security

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
    DIM hash AS STRING = Viper.Crypto.KeyDerive.Pbkdf2SHA256Str(password, salt, 100000, 32)

    ' Convert salt to hex for storage
    DIM saltHex AS STRING = Viper.Codec.HexEncode(salt)

    ' Store iterations:salt:hash
    RETURN "100000:" & saltHex & ":" & hash
END FUNCTION

FUNCTION VerifyPassword(password AS STRING, stored AS STRING) AS BOOLEAN
    ' Parse stored format: iterations:salt:hash
    DIM parts() AS STRING = SPLIT(stored, ":")
    DIM iterations AS INTEGER = VAL(parts(0))
    DIM salt AS OBJECT = Viper.Codec.HexDecode(parts(1))
    DIM storedHash AS STRING = parts(2)

    ' Recompute hash with same parameters
    DIM computedHash AS STRING = Viper.Crypto.KeyDerive.Pbkdf2SHA256Str(password, salt, iterations, 32)

    RETURN computedHash = storedHash
END FUNCTION
```

### Security Recommendations

1. **Use high iteration counts**: At least 100,000 for password storage (2024)
2. **Use unique salts**: Generate a new random salt for each password
3. **Store salt with hash**: You need the salt to verify passwords
4. **Use sufficient key length**: 32 bytes (256 bits) is standard

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
| `Bytes` | `count`   | Must be >= 1               |
| `Int`   | `min`     | Must be <= max             |
| `Int`   | `max`     | Must be >= min             |

### Traps

- `Bytes(count)` with count < 1: Traps with "count must be at least 1"
- `Int(min, max)` with min > max: Traps with "min must not be greater than max"

### Platform Implementation

| Platform | Source                          |
|----------|----------------------------------|
| Linux    | /dev/urandom                     |
| macOS    | /dev/urandom                     |
| Windows  | BCryptGenRandom                  |

### Zia Example

```zia
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

### Properties

| Property | Type    | Description                                |
|----------|---------|--------------------------------------------|
| `Host`   | String  | Remote host name (read-only)               |
| `Port`   | Integer | Remote port number (read-only)             |
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
- **Certificate Verification:** Enabled by default

### Zia Example

> Tls is accessible via fully-qualified calls: `Viper.Crypto.Tls.Connect(...)`, `Viper.Crypto.Tls.Send(...)`, `Viper.Crypto.Tls.Receive(...)`.

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
    PRINT "Received "; response.Len; " bytes"
    PRINT "Hex: "; response.ToHex()

    conn.Close()
END IF
```

### Error Handling

TLS operations trap on errors:

- `Connect()` traps on connection refused, host not found, or TLS handshake failure
- `ConnectFor()` traps on timeout
- `Send()` traps if connection is closed
- `Recv()` traps on receive errors
- Certificate validation failures cause `Connect()` to trap

Use `Error()` to get descriptive error messages for debugging.

### Security Notes

- **Certificate verification:** Server certificates are validated against system trust store
- **Hostname verification:** Server certificate must match the requested hostname
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

High-level password hashing and verification using PBKDF2. Provides a simple API for securely storing and checking passwords without manual salt management.

**Type:** Static utility class

### Methods

| Method                    | Signature                  | Description                                                      |
|---------------------------|----------------------------|------------------------------------------------------------------|
| `Hash(password)`          | `String(String)`           | Hash a password with 100,000 iterations and random salt          |
| `HashIters(password, n)`  | `String(String, Integer)`  | Hash a password with a custom iteration count and random salt    |
| `Verify(password, hash)`  | `Boolean(String, String)`  | Verify a password against a previously generated hash            |

### Output Format

`Hash` and `HashIters` return a self-describing string in the format:

```
PBKDF2$<iterations>$<base64-salt>$<base64-hash>
```

This format stores everything needed for verification: the algorithm identifier, iteration count, salt, and derived key.

### Notes

- Uses PBKDF2-HMAC-SHA256 as the underlying key derivation function
- Default iteration count is 100,000 (suitable for most applications)
- A random 16-byte salt is generated automatically for each hash
- The salt and iteration count are embedded in the output string, so no separate storage is needed
- `Verify` parses the stored hash string to extract parameters before re-deriving
- Use `HashIters` to increase iterations for higher security or decrease for testing

### Zia Example

```zia
module PasswordDemo;

bind Viper.Terminal;
bind Viper.Crypto.Password as Password;
bind Viper.Fmt as Fmt;

func start() {
    // Hash a password
    var hash = Password.Hash("secret123");
    Say("Hash: " + hash);  // PBKDF2$100000$...

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
PRINT "Hash: "; hash  ' Output: PBKDF2$100000$...

' Verify correct password
PRINT "Correct: "; Viper.Crypto.Password.Verify("secret123", hash)  ' Output: 1

' Verify wrong password
PRINT "Wrong: "; Viper.Crypto.Password.Verify("wrong", hash)        ' Output: 0

' Hash with custom iteration count
DIM strongHash AS STRING = Viper.Crypto.Password.HashIters("secret123", 200000)
PRINT "Strong hash: "; strongHash
PRINT "Verify: "; Viper.Crypto.Password.Verify("secret123", strongHash)  ' Output: 1
```

### Security Recommendations

1. **Use default iterations for production:** 100,000 iterations provides good security for most use cases
2. **Increase iterations over time:** As hardware gets faster, increase the iteration count
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

- [Collections](collections.md) - `Bytes` for binary data handling
- [Text Processing](text.md) - `Codec` for Base64/Hex encoding of hashes and keys
- [Network](network.md) - `Tcp` for unencrypted connections, `Http` for HTTPS, `WebSocket` for WSS
