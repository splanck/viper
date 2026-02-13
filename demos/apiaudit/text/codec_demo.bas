' Viper.Text.Codec API Audit - Base64, Hex, and URL Encoding
' Tests all Codec functions

PRINT "=== Viper.Text.Codec API Audit ==="

' --- Base64Enc / Base64Dec ---
PRINT "--- Base64 ---"
DIM b64 AS STRING
b64 = Viper.Text.Codec.Base64Enc("Hello, World!")
PRINT "Encoded: "; b64
PRINT "Decoded: "; Viper.Text.Codec.Base64Dec(b64)

DIM b64_2 AS STRING
b64_2 = Viper.Text.Codec.Base64Enc("Viper Language")
PRINT "Encoded: "; b64_2
PRINT "Decoded: "; Viper.Text.Codec.Base64Dec(b64_2)

' Empty string
DIM b64_empty AS STRING
b64_empty = Viper.Text.Codec.Base64Enc("")
PRINT "Empty encoded: "; b64_empty
PRINT "Empty decoded: "; Viper.Text.Codec.Base64Dec(b64_empty)

' --- HexEnc / HexDec ---
PRINT "--- Hex ---"
DIM hex AS STRING
hex = Viper.Text.Codec.HexEnc("Hi!")
PRINT "Hex encoded: "; hex
PRINT "Hex decoded: "; Viper.Text.Codec.HexDec(hex)

DIM hex2 AS STRING
hex2 = Viper.Text.Codec.HexEnc("ABC")
PRINT "Hex encoded: "; hex2
PRINT "Hex decoded: "; Viper.Text.Codec.HexDec(hex2)

' --- UrlEncode / UrlDecode ---
PRINT "--- URL Encoding ---"
DIM url AS STRING
url = Viper.Text.Codec.UrlEncode("hello world&foo=bar")
PRINT "URL encoded: "; url
PRINT "URL decoded: "; Viper.Text.Codec.UrlDecode(url)

DIM url2 AS STRING
url2 = Viper.Text.Codec.UrlEncode("https://example.com/path?q=test value")
PRINT "URL encoded: "; url2
PRINT "URL decoded: "; Viper.Text.Codec.UrlDecode(url2)

' Special characters
DIM url3 AS STRING
url3 = Viper.Text.Codec.UrlEncode("a+b=c&d=e f")
PRINT "URL encoded: "; url3
PRINT "URL decoded: "; Viper.Text.Codec.UrlDecode(url3)

PRINT "=== Codec Demo Complete ==="
END
