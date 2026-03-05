' =============================================================================
' API Audit: Viper.Math.Bits - Bitwise Operations (BASIC)
' =============================================================================
' Tests: And, Or, Xor, Not, Shl, Shr, Ushr, Set, Clear, Get, Toggle,
'        Flip, Count, LeadZ, TrailZ, Rotl, Rotr, Swap
' =============================================================================

PRINT "=== API Audit: Viper.Math.Bits ==="

' --- And ---
PRINT "--- And ---"
PRINT "Bits.And(12, 10): "; Viper.Math.Bits.And(12, 10)
PRINT "Bits.And(255, 15): "; Viper.Math.Bits.And(255, 15)
PRINT "Bits.And(0, 0): "; Viper.Math.Bits.And(0, 0)

' --- Or ---
PRINT "--- Or ---"
PRINT "Bits.Or(12, 10): "; Viper.Math.Bits.Or(12, 10)
PRINT "Bits.Or(0, 0): "; Viper.Math.Bits.Or(0, 0)
PRINT "Bits.Or(240, 15): "; Viper.Math.Bits.Or(240, 15)

' --- Xor ---
PRINT "--- Xor ---"
PRINT "Bits.Xor(12, 10): "; Viper.Math.Bits.Xor(12, 10)
PRINT "Bits.Xor(255, 255): "; Viper.Math.Bits.Xor(255, 255)
PRINT "Bits.Xor(0, 0): "; Viper.Math.Bits.Xor(0, 0)

' --- Not ---
PRINT "--- Not ---"
PRINT "Bits.Not(0): "; Viper.Math.Bits.Not(0)
PRINT "Bits.Not(1): "; Viper.Math.Bits.Not(1)
PRINT "Bits.Not(255): "; Viper.Math.Bits.Not(255)

' --- Shl ---
PRINT "--- Shl ---"
PRINT "Bits.Shl(1, 0): "; Viper.Math.Bits.Shl(1, 0)
PRINT "Bits.Shl(1, 4): "; Viper.Math.Bits.Shl(1, 4)
PRINT "Bits.Shl(1, 8): "; Viper.Math.Bits.Shl(1, 8)
PRINT "Bits.Shl(3, 2): "; Viper.Math.Bits.Shl(3, 2)

' --- Shr ---
PRINT "--- Shr ---"
PRINT "Bits.Shr(16, 2): "; Viper.Math.Bits.Shr(16, 2)
PRINT "Bits.Shr(256, 4): "; Viper.Math.Bits.Shr(256, 4)
PRINT "Bits.Shr(1, 0): "; Viper.Math.Bits.Shr(1, 0)

' --- Ushr ---
PRINT "--- Ushr ---"
PRINT "Bits.Ushr(16, 2): "; Viper.Math.Bits.Ushr(16, 2)
PRINT "Bits.Ushr(-1, 60): "; Viper.Math.Bits.Ushr(-1, 60)

' --- Set ---
PRINT "--- Set ---"
PRINT "Bits.Set(0, 0): "; Viper.Math.Bits.Set(0, 0)
PRINT "Bits.Set(0, 3): "; Viper.Math.Bits.Set(0, 3)
PRINT "Bits.Set(1, 1): "; Viper.Math.Bits.Set(1, 1)

' --- Clear ---
PRINT "--- Clear ---"
PRINT "Bits.Clear(255, 0): "; Viper.Math.Bits.Clear(255, 0)
PRINT "Bits.Clear(255, 7): "; Viper.Math.Bits.Clear(255, 7)
PRINT "Bits.Clear(8, 3): "; Viper.Math.Bits.Clear(8, 3)

' --- Get ---
PRINT "--- Get ---"
PRINT "Bits.Get(5, 0): "; Viper.Math.Bits.Get(5, 0)
PRINT "Bits.Get(5, 1): "; Viper.Math.Bits.Get(5, 1)
PRINT "Bits.Get(5, 2): "; Viper.Math.Bits.Get(5, 2)

' --- Toggle ---
PRINT "--- Toggle ---"
PRINT "Bits.Toggle(0, 0): "; Viper.Math.Bits.Toggle(0, 0)
PRINT "Bits.Toggle(1, 0): "; Viper.Math.Bits.Toggle(1, 0)
PRINT "Bits.Toggle(5, 1): "; Viper.Math.Bits.Toggle(5, 1)

' --- Flip ---
PRINT "--- Flip ---"
PRINT "Bits.Flip(0): "; Viper.Math.Bits.Flip(0)
PRINT "Bits.Flip(1): "; Viper.Math.Bits.Flip(1)
PRINT "Bits.Flip(255): "; Viper.Math.Bits.Flip(255)

' --- Count ---
PRINT "--- Count ---"
PRINT "Bits.Count(0): "; Viper.Math.Bits.Count(0)
PRINT "Bits.Count(1): "; Viper.Math.Bits.Count(1)
PRINT "Bits.Count(7): "; Viper.Math.Bits.Count(7)
PRINT "Bits.Count(255): "; Viper.Math.Bits.Count(255)

' --- LeadZ ---
PRINT "--- LeadZ ---"
PRINT "Bits.LeadZ(1): "; Viper.Math.Bits.LeadZ(1)
PRINT "Bits.LeadZ(256): "; Viper.Math.Bits.LeadZ(256)

' --- TrailZ ---
PRINT "--- TrailZ ---"
PRINT "Bits.TrailZ(1): "; Viper.Math.Bits.TrailZ(1)
PRINT "Bits.TrailZ(8): "; Viper.Math.Bits.TrailZ(8)
PRINT "Bits.TrailZ(16): "; Viper.Math.Bits.TrailZ(16)

' --- Rotl ---
PRINT "--- Rotl ---"
PRINT "Bits.Rotl(1, 1): "; Viper.Math.Bits.Rotl(1, 1)
PRINT "Bits.Rotl(1, 4): "; Viper.Math.Bits.Rotl(1, 4)

' --- Rotr ---
PRINT "--- Rotr ---"
PRINT "Bits.Rotr(2, 1): "; Viper.Math.Bits.Rotr(2, 1)
PRINT "Bits.Rotr(16, 4): "; Viper.Math.Bits.Rotr(16, 4)

' --- Swap ---
PRINT "--- Swap ---"
PRINT "Bits.Swap(1): "; Viper.Math.Bits.Swap(1)
PRINT "Bits.Swap(256): "; Viper.Math.Bits.Swap(256)

PRINT "=== Bits Audit Complete ==="
END
