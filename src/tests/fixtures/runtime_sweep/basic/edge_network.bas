' Edge case testing for Network operations

DIM result AS STRING
DIM flag AS INTEGER

PRINT "=== Network Edge Case Tests ==="

' === URL Validation ===
PRINT "=== URL Validation ==="

' Valid URLs
flag = Zanna.Network.Url.IsValid("https://example.com")
PRINT "IsValid('https://example.com'): "; flag

flag = Zanna.Network.Url.IsValid("http://localhost:8080/path")
PRINT "IsValid('http://localhost:8080/path'): "; flag

flag = Zanna.Network.Url.IsValid("https://user:pass@host.com/path?query=1#frag")
PRINT "IsValid('https://user:pass@host.com/path?query=1#frag'): "; flag

' Invalid URLs
flag = Zanna.Network.Url.IsValid("")
PRINT "IsValid(''): "; flag

flag = Zanna.Network.Url.IsValid("not a url")
PRINT "IsValid('not a url'): "; flag

flag = Zanna.Network.Url.IsValid("http://")
PRINT "IsValid('http://'): "; flag

flag = Zanna.Network.Url.IsValid("://missing-scheme.com")
PRINT "IsValid('://missing-scheme.com'): "; flag

flag = Zanna.Network.Url.IsValid("ftp://files.example.com")
PRINT "IsValid('ftp://files.example.com'): "; flag
PRINT ""

' === URL Parsing ===
PRINT "=== URL Parsing ==="

DIM testUrl AS STRING
testUrl = "https://user:pass@example.com:8080/path/to/resource?query=value&foo=bar#section"

DIM u AS Zanna.Network.Url
u = Zanna.Network.Url.Parse(testUrl)

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
u = Zanna.Network.Url.Parse(testUrl)
PRINT "Parsing minimal URL: "; testUrl
DIM uHost2 AS STRING
uHost2 = u.Host
PRINT "Host: '"; uHost2; "'"
PRINT "Port: "; u.Port; " (0 = default)"
PRINT ""

' === URL Encoding/Decoding ===
PRINT "=== URL Encoding/Decoding ==="

result = Zanna.Text.Codec.UrlEncode("hello world")
PRINT "Encode('hello world'): "; result

result = Zanna.Text.Codec.UrlEncode("a=1&b=2")
PRINT "Encode('a=1&b=2'): "; result

result = Zanna.Text.Codec.UrlEncode("")
PRINT "Encode(''): '"; result; "'"

result = Zanna.Text.Codec.UrlEncode("already%20encoded")
PRINT "Encode('already%20encoded'): "; result

result = Zanna.Text.Codec.UrlDecode("hello%20world")
PRINT "Decode('hello%20world'): "; result

result = Zanna.Text.Codec.UrlDecode("")
PRINT "Decode(''): '"; result; "'"

result = Zanna.Text.Codec.UrlDecode("%")
PRINT "Decode('%') [incomplete escape]: '"; result; "'"

result = Zanna.Text.Codec.UrlDecode("%ZZ")
PRINT "Decode('%ZZ') [invalid hex]: '"; result; "'"

' Unicode in URLs
result = Zanna.Text.Codec.UrlEncode("日本語")
PRINT "Encode('日本語'): "; result

result = Zanna.Text.Codec.UrlDecode(Zanna.Text.Codec.UrlEncode("日本語"))
PRINT "Round-trip '日本語': "; result
PRINT ""

' === Base64 ===
PRINT "=== Base64 Encoding ==="

result = Zanna.Text.Codec.Base64Encode("Hello, World!")
PRINT "Base64Enc('Hello, World!'): "; result

result = Zanna.Text.Codec.Base64Decode(result)
PRINT "Base64Dec back: "; result

result = Zanna.Text.Codec.Base64Encode("")
PRINT "Base64Enc(''): '"; result; "'"

result = Zanna.Text.Codec.Base64Decode("")
PRINT "Base64Dec(''): '"; result; "'"

' Invalid base64 - this traps with DomainError, so we skip it
' result = Zanna.Text.Codec.Base64Decode("!!invalid!!")
' PRINT "Base64Dec('!!invalid!!'): '"; result; "'"
PRINT "(Skipping invalid base64 - traps instead of returning empty)"
PRINT ""

' === Hex Encoding ===
PRINT "=== Hex Encoding ==="
result = Zanna.Text.Codec.HexEncode("ABC")
PRINT "HexEnc('ABC'): "; result

result = Zanna.Text.Codec.HexDecode("414243")
PRINT "HexDec('414243'): "; result

' Invalid hex - traps with DomainError
' result = Zanna.Text.Codec.HexDecode("ZZ")
' PRINT "HexDec('ZZ') [invalid]: '"; result; "'"
PRINT "(Skipping invalid hex - traps instead of returning empty)"
PRINT ""

PRINT "=== Network Edge Case Tests Complete ==="
END
