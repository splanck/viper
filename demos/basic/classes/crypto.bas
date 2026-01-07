' Viper.Crypto Demo - Cryptographic Utilities
' This demo showcases hashing, random generation, and key derivation

' === Secure Random ===
PRINT "=== Crypto Secure Random ==="
PRINT "Rand.Int(0, 100): "; Viper.Crypto.Rand.Int(0, 100)
PRINT "Rand.Int(0, 100): "; Viper.Crypto.Rand.Int(0, 100)
PRINT "Rand.Int(1000, 9999): "; Viper.Crypto.Rand.Int(1000, 9999)
PRINT

' === Hash Functions ===
PRINT "=== Hash Functions ==="
DIM data AS STRING
data = "Hello, World!"
PRINT "Data: "; data
PRINT "CRC32: "; Viper.Crypto.Hash.CRC32(data)
PRINT "MD5: "; Viper.Crypto.Hash.MD5(data)
PRINT "SHA1: "; Viper.Crypto.Hash.SHA1(data)
PRINT "SHA256: "; Viper.Crypto.Hash.SHA256(data)
PRINT

' === HMAC Functions ===
PRINT "=== HMAC Functions ==="
DIM key AS STRING
key = "secret-key"
PRINT "Key: "; key
PRINT "HmacMD5: "; Viper.Crypto.Hash.HmacMD5(key, data)
PRINT "HmacSHA1: "; Viper.Crypto.Hash.HmacSHA1(key, data)
PRINT "HmacSHA256: "; Viper.Crypto.Hash.HmacSHA256(key, data)

END
