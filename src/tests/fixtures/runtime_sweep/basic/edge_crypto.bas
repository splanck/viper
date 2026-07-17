' Edge case testing for Crypto operations

DIM result AS STRING
DIM num AS INTEGER

' === Empty string hashing ===
PRINT "=== Empty String Hashing ==="
PRINT "MD5(''): "; Zanna.Crypto.Legacy.Hash.Md5("")
PRINT "SHA1(''): "; Zanna.Crypto.Legacy.Hash.Sha1("")
PRINT "SHA256(''): "; Zanna.Crypto.Hash.Sha256("")
PRINT "CRC32(''): "; Zanna.Crypto.Legacy.Hash.Crc32("")
PRINT ""

' === Known test vectors ===
PRINT "=== Known Test Vectors ==="
' MD5("hello") should be 5d41402abc4b2a76b9719d911017c592
PRINT "MD5('hello'): "; Zanna.Crypto.Legacy.Hash.Md5("hello")

' SHA1("hello") should be aaf4c61ddcc5e8a2dabede0f3b482cd9aea9434d
PRINT "SHA1('hello'): "; Zanna.Crypto.Legacy.Hash.Sha1("hello")

' SHA256("hello") should be 2cf24dba5fb0a30e26e83b2ac5b9e29e1b161e5c1fa7425e73043362938b9824
PRINT "SHA256('hello'): "; Zanna.Crypto.Hash.Sha256("hello")
PRINT ""

' === HMAC with empty key/data ===
PRINT "=== HMAC Edge Cases ==="
PRINT "HmacMD5('', ''): "; Zanna.Crypto.Legacy.Hash.HmacMd5("", "")
PRINT "HmacSHA1('', ''): "; Zanna.Crypto.Legacy.Hash.HmacSha1("", "")
PRINT "HmacSHA256('', ''): "; Zanna.Crypto.Hash.HmacSha256("", "")
PRINT ""

' === Large data hashing ===
PRINT "=== Large Data Hashing ==="
DIM largeData AS STRING
largeData = Zanna.String.Repeat("x", 100000)
PRINT "Hashing 100KB of 'x'..."
result = Zanna.Crypto.Hash.Sha256(largeData)
PRINT "SHA256 of 100KB: "; result
PRINT ""

' === Random edge cases ===
PRINT "=== Secure Random ==="
PRINT "Rand.Int(0, 0): "; Zanna.Crypto.SecureRandom.Int(0, 0)
PRINT "Rand.Int(0, 1): "; Zanna.Crypto.SecureRandom.Int(0, 1)
PRINT "Rand.Int(5, 5): "; Zanna.Crypto.SecureRandom.Int(5, 5)
PRINT "Rand.Int(-10, 10): "; Zanna.Crypto.SecureRandom.Int(-10, 10)
PRINT ""

' Try negative range
PRINT "Rand.Int(10, 0) [inverted range]..."
' num = Zanna.Crypto.SecureRandom.Int(10, 0)  ' May crash
' PRINT "Result: "; num

PRINT ""
PRINT "=== Crypto Edge Case Tests Complete ==="
END
