' =============================================================================
' API Audit: Viper.IO.Compress - Compression
' =============================================================================
' Tests: DeflateStr, InflateStr, GzipStr, GunzipStr (roundtrip)
' =============================================================================

PRINT "=== API Audit: Viper.IO.Compress ==="

DIM testStr AS STRING
testStr = "Hello, Viper compression! This is a test string for deflate and gzip."

' --- DeflateStr / InflateStr ---
PRINT "--- DeflateStr / InflateStr ---"
PRINT "Original: "; testStr
DIM deflated AS OBJECT
deflated = Viper.IO.Compress.DeflateStr(testStr)
PRINT "DeflateStr done"
DIM inflated AS STRING
inflated = Viper.IO.Compress.InflateStr(deflated)
PRINT "InflateStr result: "; inflated

' --- GzipStr / GunzipStr ---
PRINT "--- GzipStr / GunzipStr ---"
DIM gzipped AS OBJECT
gzipped = Viper.IO.Compress.GzipStr(testStr)
PRINT "GzipStr done"
DIM gunzipped AS STRING
gunzipped = Viper.IO.Compress.GunzipStr(gzipped)
PRINT "GunzipStr result: "; gunzipped

' --- Longer text roundtrip ---
PRINT "--- Longer text roundtrip ---"
DIM longStr AS STRING
longStr = "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"
DIM longDeflated AS OBJECT
longDeflated = Viper.IO.Compress.DeflateStr(longStr)
DIM longInflated AS STRING
longInflated = Viper.IO.Compress.InflateStr(longDeflated)
PRINT "Roundtrip: "; longInflated

PRINT "=== Compress Demo Complete ==="
END
