' =============================================================================
' API Audit: Zanna.Crypto.SecureRandom - Cryptographically Secure Random
' =============================================================================
' Tests: Int, Bytes
' =============================================================================

PRINT "=== API Audit: Zanna.Crypto.SecureRandom ==="

' --- Int ---
PRINT "--- Int ---"
PRINT "Rand.Int(1,100): "; Zanna.Crypto.SecureRandom.Int(1, 100)
PRINT "Rand.Int(1,100): "; Zanna.Crypto.SecureRandom.Int(1, 100)
PRINT "Rand.Int(1,100): "; Zanna.Crypto.SecureRandom.Int(1, 100)

' --- Bytes ---
PRINT "--- Bytes ---"
DIM b AS OBJECT
b = Zanna.Crypto.SecureRandom.Bytes(8)
PRINT "Length: "; Zanna.Collections.Bytes.get_Length(b)
PRINT "Hex: "; Zanna.Collections.Bytes.ToHex(b)

DIM b2 AS OBJECT
b2 = Zanna.Crypto.SecureRandom.Bytes(32)
PRINT "Length: "; Zanna.Collections.Bytes.get_Length(b2)
PRINT "Hex: "; Zanna.Collections.Bytes.ToHex(b2)

PRINT "=== CryptoRand Demo Complete ==="
END
