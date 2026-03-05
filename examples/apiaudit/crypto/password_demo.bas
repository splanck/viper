' =============================================================================
' API Audit: Viper.Crypto.Password - Password Hashing
' =============================================================================
' Tests: Hash, HashIters, Verify
' =============================================================================

PRINT "=== API Audit: Viper.Crypto.Password ==="

' --- Hash ---
PRINT "--- Hash ---"
DIM h AS STRING
h = Viper.Crypto.Password.Hash("mypassword")
PRINT "Hash: "; h

' --- Verify (correct) ---
PRINT "--- Verify (correct) ---"
PRINT "Verify correct: "; Viper.Crypto.Password.Verify("mypassword", h)

' --- Verify (wrong) ---
PRINT "--- Verify (wrong) ---"
PRINT "Verify wrong: "; Viper.Crypto.Password.Verify("wrongpassword", h)

' --- HashIters ---
PRINT "--- HashIters ---"
DIM h2 AS STRING
h2 = Viper.Crypto.Password.HashIters("test", 1000)
PRINT "HashIters: "; h2

' Verify the iterated hash
PRINT "Verify iterated: "; Viper.Crypto.Password.Verify("test", h2)

PRINT "=== Password Demo Complete ==="
END
