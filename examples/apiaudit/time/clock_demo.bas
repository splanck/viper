' =============================================================================
' API Audit: Zanna.Time.Clock - System Clock Utilities
' =============================================================================
' Tests: Ticks, TicksUs, Sleep
' =============================================================================

PRINT "=== API Audit: Zanna.Time.Clock ==="

' --- Ticks ---
PRINT "--- Ticks ---"
DIM t1 AS INTEGER
t1 = Zanna.Time.Clock.NowMs()
PRINT "Ticks (monotonic ms): "; t1

DIM t2 AS INTEGER
t2 = Zanna.Time.Clock.NowMs()
PRINT "Ticks again: "; t2

' --- TicksUs ---
PRINT "--- TicksUs ---"
DIM u1 AS INTEGER
u1 = Zanna.Time.Clock.NowMicros()
PRINT "TicksUs (monotonic us): "; u1

DIM u2 AS INTEGER
u2 = Zanna.Time.Clock.NowMicros()
PRINT "TicksUs again: "; u2

' --- Sleep ---
PRINT "--- Sleep ---"
PRINT "Sleeping 10ms..."
DIM before AS INTEGER
before = Zanna.Time.Clock.NowMs()
Zanna.Time.Clock.Sleep(10)
DIM aft AS INTEGER
aft = Zanna.Time.Clock.NowMs()
PRINT "Ticks before: "; before
PRINT "Ticks after: "; aft
PRINT "Elapsed (should be >= 10): "; aft - before

PRINT "=== Clock Demo Complete ==="
END
