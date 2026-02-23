# Encoding & Identity
> Codec (Base64, Hex, URL encoding), Uuid

**Part of [Viper Runtime Library](../README.md) › [Text Processing](README.md)**

---

## Viper.Text.Codec

String-based encoding and decoding utilities for Base64, Hex, and URL encoding.

**Type:** Static utility class

### Methods

| Method           | Signature        | Description                              |
|------------------|------------------|------------------------------------------|
| `Base64Enc(str)` | `String(String)` | Base64-encode a string's bytes           |
| `Base64Dec(str)` | `String(String)` | Decode a Base64 string to original bytes |
| `HexEnc(str)`    | `String(String)` | Hex-encode a string's bytes (lowercase)  |
| `HexDec(str)`    | `String(String)` | Decode a hex string to original bytes    |
| `UrlEncode(str)` | `String(String)` | URL-encode a string (percent-encoding)   |
| `UrlDecode(str)` | `String(String)` | URL-decode a string                      |

### Notes

- All methods operate on strings (C strings without embedded null bytes)
- For binary data with null bytes, use `Bytes.ToBase64`/`Bytes.FromBase64` or `Bytes.ToHex`/`Bytes.FromHex`
- **URL Encoding:**
    - Unreserved characters (A-Z, a-z, 0-9, `-`, `_`, `.`, `~`) pass through unchanged
    - All other characters are encoded as `%XX` (lowercase hex)
    - Decoding treats `+` as space (form encoding convention)
- **Base64:** RFC 4648 standard alphabet with `=` padding
- **Hex:** Lowercase hex encoding (e.g., "Hello" → "48656c6c6f")
- Invalid input to `Base64Dec` or `HexDec` will trap

### Zia Example

```rust
module CodecDemo;

bind Viper.Terminal;
bind Viper.Text.Codec as Codec;

func start() {
    Say("Base64: " + Codec.Base64Enc("Hello"));        // SGVsbG8=
    Say("Decoded: " + Codec.Base64Dec("SGVsbG8="));     // Hello
    Say("Hex: " + Codec.HexEnc("Hello"));               // 48656c6c6f
    Say("HexDec: " + Codec.HexDec("48656c6c6f"));       // Hello
    Say("UrlEnc: " + Codec.UrlEncode("hello world"));   // hello%20world
    Say("UrlDec: " + Codec.UrlDecode("hello%20world")); // hello world
}
```

### BASIC Example

```basic
' URL encoding for query parameters
DIM original AS STRING = "key=value&name=John Doe"
DIM encoded AS STRING = Viper.Text.Codec.UrlEncode(original)
PRINT encoded  ' Output: "key%3dvalue%26name%3dJohn%20Doe"

DIM decoded AS STRING = Viper.Text.Codec.UrlDecode(encoded)
PRINT decoded = original  ' Output: 1 (true)

' Base64 encoding for data transmission
DIM data AS STRING = "Hello, World!"
DIM b64 AS STRING = Viper.Text.Codec.Base64Enc(data)
PRINT b64  ' Output: "SGVsbG8sIFdvcmxkIQ=="

DIM restored AS STRING = Viper.Text.Codec.Base64Dec(b64)
PRINT restored  ' Output: "Hello, World!"

' Hex encoding for display
DIM hex AS STRING = Viper.Text.Codec.HexEnc("ABC")
PRINT hex  ' Output: "414243"

DIM unhex AS STRING = Viper.Text.Codec.HexDec(hex)
PRINT unhex  ' Output: "ABC"
```

---

## Viper.Text.Uuid

UUID version 4 (random) generation and manipulation per RFC 4122.

**Type:** Static utility class

### Properties

| Property | Type   | Description                                                 |
|----------|--------|-------------------------------------------------------------|
| `Empty`  | String | Returns the nil UUID "00000000-0000-0000-0000-000000000000" |

### Methods

| Method             | Signature         | Description                            |
|--------------------|-------------------|----------------------------------------|
| `New()`            | `String()`        | Generate a new random UUID v4          |
| `IsValid(guid)`    | `Boolean(String)` | Check if string is a valid UUID format |
| `ToBytes(guid)`    | `Bytes(String)`   | Convert UUID string to 16-byte array   |
| `FromBytes(bytes)` | `String(Bytes)`   | Convert 16-byte array to UUID string   |

### Notes

- Generated UUIDs follow UUID version 4 format (random)
- Format: `xxxxxxxx-xxxx-4xxx-yxxx-xxxxxxxxxxxx` where:
    - `4` indicates version 4 (random UUID)
    - `y` is one of `8`, `9`, `a`, or `b` (variant indicator)
- All hex characters are lowercase
- Uses cryptographically secure random source where available (/dev/urandom on Unix, CryptGenRandom on Windows)
- `ToBytes()` traps if the UUID format is invalid
- `FromBytes()` traps if the Bytes object is not exactly 16 bytes

### Zia Example

```rust
module UuidDemo;

bind Viper.Terminal;
bind Viper.Text.Uuid as Uuid;
bind Viper.Fmt as Fmt;

func start() {
    var id = Uuid.New();
    Say("UUID: " + id);                                    // e.g. a1b2c3d4-...
    Say("Valid: " + Fmt.Bool(Uuid.IsValid(id)));            // true
    Say("Invalid: " + Fmt.Bool(Uuid.IsValid("not-uuid"))); // false
}
```

### BASIC Example

```basic
' Generate a new UUID
DIM id AS STRING = Viper.Text.Uuid.New()
PRINT id  ' Example: "550e8400-e29b-41d4-a716-446655440000"

' Check if a string is a valid UUID
DIM valid AS INTEGER = Viper.Text.Uuid.IsValid(id)
PRINT valid  ' Output: 1 (true)

DIM invalid AS INTEGER = Viper.Text.Uuid.IsValid("not-a-uuid")
PRINT invalid  ' Output: 0 (false)

' Get the empty/nil UUID
DIM empty AS STRING = Viper.Text.Uuid.Empty
PRINT empty  ' Output: "00000000-0000-0000-0000-000000000000"

' Convert to/from bytes for storage or transmission
DIM bytes AS OBJECT = Viper.Text.Uuid.ToBytes(id)
DIM restored AS STRING = Viper.Text.Uuid.FromBytes(bytes)
PRINT restored = id  ' Output: 1 (true)
```

---


## See Also

- [Data Formats](formats.md)
- [Pattern Matching](patterns.md)
- [Formatting & Generation](formatting.md)
- [Text Processing Overview](README.md)
- [Viper Runtime Library](../README.md)
