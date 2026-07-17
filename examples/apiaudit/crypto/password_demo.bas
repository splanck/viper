' =============================================================================
' API Audit: Zanna.Crypto.Password - Password Hashing
' =============================================================================
' Tests: Hash, HashIters, Verify
' =============================================================================

PRINT "=== API Audit: Zanna.Crypto.Password ==="

' --- Hash ---
PRINT "--- Hash ---"
DIM h AS STRING
h = Zanna.Crypto.Password.Hash("mypassword")
PRINT "Hash: "; h

' --- Verify (correct) ---
PRINT "--- Verify (correct) ---"
PRINT "Verify correct: "; Zanna.Crypto.Password.Verify("mypassword", h)

' --- Verify (wrong) ---
PRINT "--- Verify (wrong) ---"
PRINT "Verify wrong: "; Zanna.Crypto.Password.Verify("wrongpassword", h)

' --- HashIters ---
PRINT "--- HashIters ---"
DIM h2 AS STRING
h2 = Zanna.Crypto.Password.HashIters("test", 100000)
PRINT "HashIters: "; h2

' Verify the iterated hash
PRINT "Verify iterated: "; Zanna.Crypto.Password.Verify("test", h2)

PRINT "=== Password Demo Complete ==="
END
