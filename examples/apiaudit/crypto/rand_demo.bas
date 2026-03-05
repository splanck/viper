' =============================================================================
' API Audit: Viper.Crypto.Rand - Cryptographically Secure Random
' =============================================================================
' Tests: Int, Bytes
' =============================================================================

PRINT "=== API Audit: Viper.Crypto.Rand ==="

' --- Int ---
PRINT "--- Int ---"
PRINT "Rand.Int(1,100): "; Viper.Crypto.Rand.Int(1, 100)
PRINT "Rand.Int(1,100): "; Viper.Crypto.Rand.Int(1, 100)
PRINT "Rand.Int(1,100): "; Viper.Crypto.Rand.Int(1, 100)

' --- Bytes ---
PRINT "--- Bytes ---"
DIM b AS OBJECT
b = Viper.Crypto.Rand.Bytes(8)
PRINT "Length: "; Viper.Collections.Bytes.get_Len(b)
PRINT "Hex: "; Viper.Collections.Bytes.ToHex(b)

DIM b2 AS OBJECT
b2 = Viper.Crypto.Rand.Bytes(32)
PRINT "Length: "; Viper.Collections.Bytes.get_Len(b2)
PRINT "Hex: "; Viper.Collections.Bytes.ToHex(b2)

PRINT "=== CryptoRand Demo Complete ==="
END
