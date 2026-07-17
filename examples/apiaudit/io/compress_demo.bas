' =============================================================================
' API Audit: Zanna.IO.Compress - Compression
' =============================================================================
' Tests: DeflateStr, InflateStr, GzipStr, GunzipStr (roundtrip)
' =============================================================================

PRINT "=== API Audit: Zanna.IO.Compress ==="

DIM testStr AS STRING
testStr = "Hello, Zanna compression! This is a test string for deflate and gzip."

' --- DeflateStr / InflateStr ---
PRINT "--- DeflateStr / InflateStr ---"
PRINT "Original: "; testStr
DIM deflated AS OBJECT
deflated = Zanna.IO.Compress.DeflateStr(testStr)
PRINT "DeflateStr done"
DIM inflated AS STRING
inflated = Zanna.IO.Compress.InflateStr(deflated)
PRINT "InflateStr result: "; inflated

' --- GzipStr / GunzipStr ---
PRINT "--- GzipStr / GunzipStr ---"
DIM gzipped AS OBJECT
gzipped = Zanna.IO.Compress.GzipStr(testStr)
PRINT "GzipStr done"
DIM gunzipped AS STRING
gunzipped = Zanna.IO.Compress.GunzipStr(gzipped)
PRINT "GunzipStr result: "; gunzipped

' --- Longer text roundtrip ---
PRINT "--- Longer text roundtrip ---"
DIM longStr AS STRING
longStr = "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"
DIM longDeflated AS OBJECT
longDeflated = Zanna.IO.Compress.DeflateStr(longStr)
DIM longInflated AS STRING
longInflated = Zanna.IO.Compress.InflateStr(longDeflated)
PRINT "Roundtrip: "; longInflated

PRINT "=== Compress Demo Complete ==="
END
