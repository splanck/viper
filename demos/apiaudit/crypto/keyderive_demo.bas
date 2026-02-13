' =============================================================================
' API Audit: Viper.Crypto.KeyDerive (BASIC)
' =============================================================================
' Tests: Pbkdf2SHA256, Pbkdf2SHA256Str
' =============================================================================

PRINT "=== API Audit: Viper.Crypto.KeyDerive ==="

' Create a salt
DIM salt AS OBJECT = Viper.Crypto.Rand.Bytes(16)
PRINT "Generated 16-byte salt"
PRINT "Salt length: "; Viper.Collections.Bytes.get_Len(salt)

' --- Pbkdf2SHA256 (returns bytes) ---
PRINT "--- Pbkdf2SHA256 ---"
DIM derived AS OBJECT = Viper.Crypto.KeyDerive.Pbkdf2SHA256("mypassword", salt, 1000, 32)
PRINT "Derived key length: "; Viper.Collections.Bytes.get_Len(derived)

' --- Pbkdf2SHA256Str (returns hex string) ---
PRINT "--- Pbkdf2SHA256Str ---"
DIM derivedStr AS STRING = Viper.Crypto.KeyDerive.Pbkdf2SHA256Str("mypassword", salt, 1000, 32)
PRINT "Derived key (hex): "; derivedStr

' --- Same password + salt => same result ---
PRINT "--- Determinism check ---"
DIM derivedStr2 AS STRING = Viper.Crypto.KeyDerive.Pbkdf2SHA256Str("mypassword", salt, 1000, 32)
PRINT "Same inputs produce same output: "; derivedStr = derivedStr2

' --- Different password => different result ---
PRINT "--- Different password ---"
DIM derivedStr3 AS STRING = Viper.Crypto.KeyDerive.Pbkdf2SHA256Str("otherpassword", salt, 1000, 32)
PRINT "Different password: "; derivedStr3
PRINT "Different from first: "; derivedStr <> derivedStr3

' --- Different salt => different result ---
PRINT "--- Different salt ---"
DIM salt2 AS OBJECT = Viper.Crypto.Rand.Bytes(16)
DIM derivedStr4 AS STRING = Viper.Crypto.KeyDerive.Pbkdf2SHA256Str("mypassword", salt2, 1000, 32)
PRINT "Different salt: "; derivedStr4
PRINT "Different from first: "; derivedStr <> derivedStr4

' --- Different key length ---
PRINT "--- Different key length (64 bytes) ---"
DIM derived64 AS OBJECT = Viper.Crypto.KeyDerive.Pbkdf2SHA256("mypassword", salt, 1000, 64)
PRINT "Key length: "; Viper.Collections.Bytes.get_Len(derived64)

' --- Different iterations ---
PRINT "--- Different iterations (2000) ---"
DIM derivedStr5 AS STRING = Viper.Crypto.KeyDerive.Pbkdf2SHA256Str("mypassword", salt, 2000, 32)
PRINT "2000 iterations: "; derivedStr5
PRINT "Different from 1000 iterations: "; derivedStr <> derivedStr5

PRINT "=== KeyDerive Audit Complete ==="
END
