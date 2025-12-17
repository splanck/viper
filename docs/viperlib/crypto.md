# Cryptography

> Cryptographic hashing and checksums.

**Part of the [Viper Runtime Library](README.md)**

## Contents

- [Viper.Crypto.Hash](#vipercryptohash)

---

## Viper.Crypto.Hash

Cryptographic hash functions and checksums for strings and binary data.

**Type:** Static utility class

### Methods

| Method | Signature | Description |
|--------|-----------|-------------|
| `CRC32(str)` | `Integer(String)` | Compute CRC32 checksum of a string |
| `CRC32Bytes(bytes)` | `Integer(Bytes)` | Compute CRC32 checksum of a Bytes object |
| `MD5(str)` | `String(String)` | Compute MD5 hash of a string |
| `MD5Bytes(bytes)` | `String(Bytes)` | Compute MD5 hash of a Bytes object |
| `SHA1(str)` | `String(String)` | Compute SHA1 hash of a string |
| `SHA1Bytes(bytes)` | `String(Bytes)` | Compute SHA1 hash of a Bytes object |
| `SHA256(str)` | `String(String)` | Compute SHA256 hash of a string |
| `SHA256Bytes(bytes)` | `String(Bytes)` | Compute SHA256 hash of a Bytes object |

### Notes

- All hash outputs (MD5, SHA1, SHA256) are lowercase hex strings
- CRC32 returns an integer (the raw checksum value)
- MD5 returns a 32-character hex string
- SHA1 returns a 40-character hex string
- SHA256 returns a 64-character hex string
- **Security Warning:** MD5 and SHA1 are cryptographically broken and should NOT be used for security-sensitive applications. Use SHA256 or better for security purposes. These are provided for checksums, fingerprinting, and legacy compatibility.

### Example

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

