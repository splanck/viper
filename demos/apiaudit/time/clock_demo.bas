' =============================================================================
' API Audit: Viper.Time.Clock - System Clock Utilities
' =============================================================================
' Tests: Ticks, TicksUs, Sleep
' =============================================================================

PRINT "=== API Audit: Viper.Time.Clock ==="

' --- Ticks ---
PRINT "--- Ticks ---"
DIM t1 AS INTEGER
t1 = Viper.Time.Clock.Ticks()
PRINT "Ticks (monotonic ms): "; t1

DIM t2 AS INTEGER
t2 = Viper.Time.Clock.Ticks()
PRINT "Ticks again: "; t2

' --- TicksUs ---
PRINT "--- TicksUs ---"
DIM u1 AS INTEGER
u1 = Viper.Time.Clock.TicksUs()
PRINT "TicksUs (monotonic us): "; u1

DIM u2 AS INTEGER
u2 = Viper.Time.Clock.TicksUs()
PRINT "TicksUs again: "; u2

' --- Sleep ---
PRINT "--- Sleep ---"
PRINT "Sleeping 10ms..."
DIM before AS INTEGER
before = Viper.Time.Clock.Ticks()
Viper.Time.Clock.Sleep(10)
DIM aft AS INTEGER
aft = Viper.Time.Clock.Ticks()
PRINT "Ticks before: "; before
PRINT "Ticks after: "; aft
PRINT "Elapsed (should be >= 10): "; aft - before

PRINT "=== Clock Demo Complete ==="
END
