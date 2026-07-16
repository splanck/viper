' Edge case testing for Network operations

DIM result AS STRING
DIM flag AS INTEGER

PRINT "=== Network Edge Case Tests ==="

' === URL Validation ===
PRINT "=== URL Validation ==="

' Valid URLs
flag = Viper.Network.Url.IsValid("https://example.com")
PRINT "IsValid('https://example.com'): "; flag

flag = Viper.Network.Url.IsValid("http://localhost:8080/path")
PRINT "IsValid('http://localhost:8080/path'): "; flag

flag = Viper.Network.Url.IsValid("https://user:pass@host.com/path?query=1#frag")
PRINT "IsValid('https://user:pass@host.com/path?query=1#frag'): "; flag

' Invalid URLs
flag = Viper.Network.Url.IsValid("")
PRINT "IsValid(''): "; flag

flag = Viper.Network.Url.IsValid("not a url")
PRINT "IsValid('not a url'): "; flag

flag = Viper.Network.Url.IsValid("http://")
PRINT "IsValid('http://'): "; flag

flag = Viper.Network.Url.IsValid("://missing-scheme.com")
PRINT "IsValid('://missing-scheme.com'): "; flag

flag = Viper.Network.Url.IsValid("ftp://files.example.com")
PRINT "IsValid('ftp://files.example.com'): "; flag
PRINT ""

' === URL Parsing ===
PRINT "=== URL Parsing ==="

DIM testUrl AS STRING
testUrl = "https://user:pass@example.com:8080/path/to/resource?query=value&foo=bar#section"

DIM u AS Viper.Network.Url
u = Viper.Network.Url.Parse(testUrl)

PRINT "Parsed: "; testUrl
DIM uScheme AS STRING
uScheme = u.Scheme
DIM uHost AS STRING
uHost = u.Host
PRINT "Scheme: "; uScheme
PRINT "Host: "; uHost
PRINT "Port: "; u.Port
DIM uPath AS STRING
uPath = u.Path
PRINT "Path: "; uPath
DIM uQuery AS STRING
uQuery = u.Query
PRINT "Query: "; uQuery
DIM uFragment AS STRING
uFragment = u.Fragment
PRINT "Fragment: "; uFragment
DIM uUser AS STRING
uUser = u.User
PRINT "User: "; uUser
DIM uPass AS STRING
uPass = u.Pass
PRINT "Pass: "; uPass
DIM uFull AS STRING
uFull = u.Full
PRINT "Full: "; uFull
PRINT ""

' Minimal URL
testUrl = "http://x"
u = Viper.Network.Url.Parse(testUrl)
PRINT "Parsing minimal URL: "; testUrl
DIM uHost2 AS STRING
uHost2 = u.Host
PRINT "Host: '"; uHost2; "'"
PRINT "Port: "; u.Port; " (0 = default)"
PRINT ""

' === URL Encoding/Decoding ===
PRINT "=== URL Encoding/Decoding ==="

result = Viper.Text.Codec.UrlEncode("hello world")
PRINT "Encode('hello world'): "; result

result = Viper.Text.Codec.UrlEncode("a=1&b=2")
PRINT "Encode('a=1&b=2'): "; result

result = Viper.Text.Codec.UrlEncode("")
PRINT "Encode(''): '"; result; "'"

result = Viper.Text.Codec.UrlEncode("already%20encoded")
PRINT "Encode('already%20encoded'): "; result

result = Viper.Text.Codec.UrlDecode("hello%20world")
PRINT "Decode('hello%20world'): "; result

result = Viper.Text.Codec.UrlDecode("")
PRINT "Decode(''): '"; result; "'"

result = Viper.Text.Codec.UrlDecode("%")
PRINT "Decode('%') [incomplete escape]: '"; result; "'"

result = Viper.Text.Codec.UrlDecode("%ZZ")
PRINT "Decode('%ZZ') [invalid hex]: '"; result; "'"

' Unicode in URLs
result = Viper.Text.Codec.UrlEncode("日本語")
PRINT "Encode('日本語'): "; result

result = Viper.Text.Codec.UrlDecode(Viper.Text.Codec.UrlEncode("日本語"))
PRINT "Round-trip '日本語': "; result
PRINT ""

' === Base64 ===
PRINT "=== Base64 Encoding ==="

result = Viper.Text.Codec.Base64Encode("Hello, World!")
PRINT "Base64Enc('Hello, World!'): "; result

result = Viper.Text.Codec.Base64Decode(result)
PRINT "Base64Dec back: "; result

result = Viper.Text.Codec.Base64Encode("")
PRINT "Base64Enc(''): '"; result; "'"

result = Viper.Text.Codec.Base64Decode("")
PRINT "Base64Dec(''): '"; result; "'"

' Invalid base64 - this traps with DomainError, so we skip it
' result = Viper.Text.Codec.Base64Decode("!!invalid!!")
' PRINT "Base64Dec('!!invalid!!'): '"; result; "'"
PRINT "(Skipping invalid base64 - traps instead of returning empty)"
PRINT ""

' === Hex Encoding ===
PRINT "=== Hex Encoding ==="
result = Viper.Text.Codec.HexEncode("ABC")
PRINT "HexEnc('ABC'): "; result

result = Viper.Text.Codec.HexDecode("414243")
PRINT "HexDec('414243'): "; result

' Invalid hex - traps with DomainError
' result = Viper.Text.Codec.HexDecode("ZZ")
' PRINT "HexDec('ZZ') [invalid]: '"; result; "'"
PRINT "(Skipping invalid hex - traps instead of returning empty)"
PRINT ""

PRINT "=== Network Edge Case Tests Complete ==="
END
