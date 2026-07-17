' Zanna.Text.Codec API Audit - Base64, Hex, and URL Encoding
' Tests all Codec functions

PRINT "=== Zanna.Text.Codec API Audit ==="

' --- Base64Enc / Base64Dec ---
PRINT "--- Base64 ---"
DIM b64 AS STRING
b64 = Zanna.Text.Codec.Base64Encode("Hello, World!")
PRINT "Encoded: "; b64
PRINT "Decoded: "; Zanna.Text.Codec.Base64Decode(b64)

DIM b64_2 AS STRING
b64_2 = Zanna.Text.Codec.Base64Encode("Zanna Language")
PRINT "Encoded: "; b64_2
PRINT "Decoded: "; Zanna.Text.Codec.Base64Decode(b64_2)

' Empty string
DIM b64_empty AS STRING
b64_empty = Zanna.Text.Codec.Base64Encode("")
PRINT "Empty encoded: "; b64_empty
PRINT "Empty decoded: "; Zanna.Text.Codec.Base64Decode(b64_empty)

' --- HexEnc / HexDec ---
PRINT "--- Hex ---"
DIM hex AS STRING
hex = Zanna.Text.Codec.HexEncode("Hi!")
PRINT "Hex encoded: "; hex
PRINT "Hex decoded: "; Zanna.Text.Codec.HexDecode(hex)

DIM hex2 AS STRING
hex2 = Zanna.Text.Codec.HexEncode("ABC")
PRINT "Hex encoded: "; hex2
PRINT "Hex decoded: "; Zanna.Text.Codec.HexDecode(hex2)

' --- UrlEncode / UrlDecode ---
PRINT "--- URL Encoding ---"
DIM url AS STRING
url = Zanna.Text.Codec.UrlEncode("hello world&foo=bar")
PRINT "URL encoded: "; url
PRINT "URL decoded: "; Zanna.Text.Codec.UrlDecode(url)

DIM url2 AS STRING
url2 = Zanna.Text.Codec.UrlEncode("https://example.com/path?q=test value")
PRINT "URL encoded: "; url2
PRINT "URL decoded: "; Zanna.Text.Codec.UrlDecode(url2)

' Special characters
DIM url3 AS STRING
url3 = Zanna.Text.Codec.UrlEncode("a+b=c&d=e f")
PRINT "URL encoded: "; url3
PRINT "URL decoded: "; Zanna.Text.Codec.UrlDecode(url3)

PRINT "=== Codec Demo Complete ==="
END
