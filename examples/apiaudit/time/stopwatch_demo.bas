' =============================================================================
' API Audit: Zanna.Time.Stopwatch - High-Precision Timing
' =============================================================================
' Tests: New, StartNew, Start, Stop, Reset, Restart,
'        ElapsedMs, ElapsedUs, ElapsedNs, IsRunning
' =============================================================================

PRINT "=== API Audit: Zanna.Time.Stopwatch ==="

' --- New ---
PRINT "--- New ---"
DIM sw AS OBJECT
sw = Zanna.Time.Stopwatch.New()
PRINT "Created stopwatch"

' --- IsRunning (before start) ---
PRINT "--- IsRunning (before start) ---"
PRINT "IsRunning: "; Zanna.Time.Stopwatch.get_IsRunning(sw)

' --- ElapsedMs (before start) ---
PRINT "--- ElapsedMs (before start) ---"
PRINT "ElapsedMs: "; Zanna.Time.Stopwatch.get_ElapsedMs(sw)

' --- Start ---
PRINT "--- Start ---"
Zanna.Time.Stopwatch.Start(sw)
PRINT "Started"
PRINT "IsRunning: "; Zanna.Time.Stopwatch.get_IsRunning(sw)

' Do some work
DIM i AS INTEGER
DIM sum AS INTEGER
sum = 0
FOR i = 1 TO 100000
    sum = sum + i
NEXT i

' --- Stop ---
PRINT "--- Stop ---"
Zanna.Time.Stopwatch.Stop(sw)
PRINT "Stopped"
PRINT "IsRunning: "; Zanna.Time.Stopwatch.get_IsRunning(sw)

' --- ElapsedMs ---
PRINT "--- ElapsedMs ---"
PRINT "ElapsedMs: "; Zanna.Time.Stopwatch.get_ElapsedMs(sw)

' --- ElapsedUs ---
PRINT "--- ElapsedUs ---"
PRINT "ElapsedUs: "; Zanna.Time.Stopwatch.get_ElapsedUs(sw)

' --- ElapsedNs ---
PRINT "--- ElapsedNs ---"
PRINT "ElapsedNs: "; Zanna.Time.Stopwatch.get_ElapsedNs(sw)

' --- Reset ---
PRINT "--- Reset ---"
Zanna.Time.Stopwatch.Reset(sw)
PRINT "Reset"
PRINT "ElapsedMs after reset: "; Zanna.Time.Stopwatch.get_ElapsedMs(sw)
PRINT "IsRunning after reset: "; Zanna.Time.Stopwatch.get_IsRunning(sw)

' --- Restart ---
PRINT "--- Restart ---"
Zanna.Time.Stopwatch.Start(sw)
DIM j AS INTEGER
FOR j = 1 TO 50000
    sum = sum + j
NEXT j
Zanna.Time.Stopwatch.Restart(sw)
PRINT "Restarted"
PRINT "IsRunning after restart: "; Zanna.Time.Stopwatch.get_IsRunning(sw)
Zanna.Time.Stopwatch.Stop(sw)
PRINT "ElapsedMs after restart+stop: "; Zanna.Time.Stopwatch.get_ElapsedMs(sw)

' --- StartNew ---
PRINT "--- StartNew ---"
DIM sw2 AS OBJECT
sw2 = Zanna.Time.Stopwatch.StartNew()
PRINT "StartNew created (running): "; Zanna.Time.Stopwatch.get_IsRunning(sw2)
DIM k AS INTEGER
FOR k = 1 TO 50000
    sum = sum + k
NEXT k
Zanna.Time.Stopwatch.Stop(sw2)
PRINT "ElapsedMs: "; Zanna.Time.Stopwatch.get_ElapsedMs(sw2)
PRINT "ElapsedUs: "; Zanna.Time.Stopwatch.get_ElapsedUs(sw2)

PRINT "=== Stopwatch Demo Complete ==="
END
