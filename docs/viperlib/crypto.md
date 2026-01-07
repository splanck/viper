# Cryptography

> Cryptographic hashing, authentication, key derivation, and secure random generation.

**Part of the [Viper Runtime Library](README.md)**

## Contents

- [Viper.Crypto.Hash](#vipercryptohash)
- [Viper.Crypto.KeyDerive](#vipercryptokeyderive)
- [Viper.Crypto.Rand](#vipercryptorand)

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

### Security Warnings

- **CRC32**: NOT cryptographic. Only for error detection, not security.
- **MD5**: Cryptographically broken. Collisions can be generated in seconds. Do NOT use for security.
- **SHA1**: Cryptographically broken. Chosen-prefix collisions demonstrated. Do NOT use for security.
- **SHA256**: Currently secure. Recommended for all security applications.

### Hash Example

```basic
' Compute checksums and hashes
DIM data AS STRING = "Hello, World!"

' CRC32 checksum (returns integer)
DIM crc AS INTEGER = Viper.Crypto.Hash.CRC32(data)
PRINT crc  ' Output: some integer value

' MD5 hash (32 hex characters)
DIM md5 AS STRING = Viper.Crypto.Hash.MD5(data)
PRINT md5  ' Output: "65a8e27d8879283831b664bd8b7f0ad4"

' SHA1 hash (40 hex characters)
DIM sha1 AS STRING = Viper.Crypto.Hash.SHA1(data)
PRINT sha1  ' Output: "0a0a9f2a6772942557ab5355d76af442f8f65e01"

' SHA256 hash (64 hex characters)
DIM sha256 AS STRING = Viper.Crypto.Hash.SHA256(data)
PRINT sha256  ' Output: "dffd6021bb2bd5b0af676290809ec3a53191dd81c7f70a4b28688a362182986f"

' Hash binary data using Bytes variants
DIM bytes AS OBJECT = NEW Viper.Collections.Bytes()
bytes.WriteString("Hello")
DIM hash AS STRING = Viper.Crypto.Hash.SHA256Bytes(bytes)
PRINT hash
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

### Example

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

### Example

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

## See Also

- [Collections](collections.md) - `Bytes` for binary data handling
- [Text Processing](text.md) - `Codec` for Base64/Hex encoding of hashes and keys
- [Network](network.md) - Secure communication with HMAC message authentication
