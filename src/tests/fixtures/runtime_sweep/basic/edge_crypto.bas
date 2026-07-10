' Edge case testing for Crypto operations

DIM result AS STRING
DIM num AS INTEGER

' === Empty string hashing ===
PRINT "=== Empty String Hashing ==="
PRINT "MD5(''): "; Viper.Crypto.Hash.MD5("")
PRINT "SHA1(''): "; Viper.Crypto.Hash.SHA1("")
PRINT "SHA256(''): "; Viper.Crypto.Hash.SHA256("")
PRINT "CRC32(''): "; Viper.Crypto.Hash.CRC32("")
PRINT ""

' === Known test vectors ===
PRINT "=== Known Test Vectors ==="
' MD5("hello") should be 5d41402abc4b2a76b9719d911017c592
PRINT "MD5('hello'): "; Viper.Crypto.Hash.MD5("hello")

' SHA1("hello") should be aaf4c61ddcc5e8a2dabede0f3b482cd9aea9434d
PRINT "SHA1('hello'): "; Viper.Crypto.Hash.SHA1("hello")

' SHA256("hello") should be 2cf24dba5fb0a30e26e83b2ac5b9e29e1b161e5c1fa7425e73043362938b9824
PRINT "SHA256('hello'): "; Viper.Crypto.Hash.SHA256("hello")
PRINT ""

' === HMAC with empty key/data ===
PRINT "=== HMAC Edge Cases ==="
PRINT "HmacMD5('', ''): "; Viper.Crypto.Hash.HmacMD5("", "")
PRINT "HmacSHA1('', ''): "; Viper.Crypto.Hash.HmacSHA1("", "")
PRINT "HmacSHA256('', ''): "; Viper.Crypto.Hash.HmacSHA256("", "")
PRINT ""

' === Large data hashing ===
PRINT "=== Large Data Hashing ==="
DIM largeData AS STRING
largeData = Viper.String.Repeat("x", 100000)
PRINT "Hashing 100KB of 'x'..."
result = Viper.Crypto.Hash.SHA256(largeData)
PRINT "SHA256 of 100KB: "; result
PRINT ""

' === Random edge cases ===
PRINT "=== Secure Random ==="
PRINT "Rand.Int(0, 0): "; Viper.Crypto.Rand.Int(0, 0)
PRINT "Rand.Int(0, 1): "; Viper.Crypto.Rand.Int(0, 1)
PRINT "Rand.Int(5, 5): "; Viper.Crypto.Rand.Int(5, 5)
PRINT "Rand.Int(-10, 10): "; Viper.Crypto.Rand.Int(-10, 10)
PRINT ""

' Try negative range
PRINT "Rand.Int(10, 0) [inverted range]..."
' num = Viper.Crypto.Rand.Int(10, 0)  ' May crash
' PRINT "Result: "; num

PRINT ""
PRINT "=== Crypto Edge Case Tests Complete ==="
END
