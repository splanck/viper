' =============================================================================
' API Audit: Viper.Time.Stopwatch - High-Precision Timing
' =============================================================================
' Tests: New, StartNew, Start, Stop, Reset, Restart,
'        ElapsedMs, ElapsedUs, ElapsedNs, IsRunning
' =============================================================================

PRINT "=== API Audit: Viper.Time.Stopwatch ==="

' --- New ---
PRINT "--- New ---"
DIM sw AS OBJECT
sw = Viper.Time.Stopwatch.New()
PRINT "Created stopwatch"

' --- IsRunning (before start) ---
PRINT "--- IsRunning (before start) ---"
PRINT "IsRunning: "; Viper.Time.Stopwatch.get_IsRunning(sw)

' --- ElapsedMs (before start) ---
PRINT "--- ElapsedMs (before start) ---"
PRINT "ElapsedMs: "; Viper.Time.Stopwatch.get_ElapsedMs(sw)

' --- Start ---
PRINT "--- Start ---"
Viper.Time.Stopwatch.Start(sw)
PRINT "Started"
PRINT "IsRunning: "; Viper.Time.Stopwatch.get_IsRunning(sw)

' Do some work
DIM i AS INTEGER
DIM sum AS INTEGER
sum = 0
FOR i = 1 TO 100000
    sum = sum + i
NEXT i

' --- Stop ---
PRINT "--- Stop ---"
Viper.Time.Stopwatch.Stop(sw)
PRINT "Stopped"
PRINT "IsRunning: "; Viper.Time.Stopwatch.get_IsRunning(sw)

' --- ElapsedMs ---
PRINT "--- ElapsedMs ---"
PRINT "ElapsedMs: "; Viper.Time.Stopwatch.get_ElapsedMs(sw)

' --- ElapsedUs ---
PRINT "--- ElapsedUs ---"
PRINT "ElapsedUs: "; Viper.Time.Stopwatch.get_ElapsedUs(sw)

' --- ElapsedNs ---
PRINT "--- ElapsedNs ---"
PRINT "ElapsedNs: "; Viper.Time.Stopwatch.get_ElapsedNs(sw)

' --- Reset ---
PRINT "--- Reset ---"
Viper.Time.Stopwatch.Reset(sw)
PRINT "Reset"
PRINT "ElapsedMs after reset: "; Viper.Time.Stopwatch.get_ElapsedMs(sw)
PRINT "IsRunning after reset: "; Viper.Time.Stopwatch.get_IsRunning(sw)

' --- Restart ---
PRINT "--- Restart ---"
Viper.Time.Stopwatch.Start(sw)
DIM j AS INTEGER
FOR j = 1 TO 50000
    sum = sum + j
NEXT j
Viper.Time.Stopwatch.Restart(sw)
PRINT "Restarted"
PRINT "IsRunning after restart: "; Viper.Time.Stopwatch.get_IsRunning(sw)
Viper.Time.Stopwatch.Stop(sw)
PRINT "ElapsedMs after restart+stop: "; Viper.Time.Stopwatch.get_ElapsedMs(sw)

' --- StartNew ---
PRINT "--- StartNew ---"
DIM sw2 AS OBJECT
sw2 = Viper.Time.Stopwatch.StartNew()
PRINT "StartNew created (running): "; Viper.Time.Stopwatch.get_IsRunning(sw2)
DIM k AS INTEGER
FOR k = 1 TO 50000
    sum = sum + k
NEXT k
Viper.Time.Stopwatch.Stop(sw2)
PRINT "ElapsedMs: "; Viper.Time.Stopwatch.get_ElapsedMs(sw2)
PRINT "ElapsedUs: "; Viper.Time.Stopwatch.get_ElapsedUs(sw2)

PRINT "=== Stopwatch Demo Complete ==="
END
