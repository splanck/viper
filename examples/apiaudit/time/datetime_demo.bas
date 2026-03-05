' =============================================================================
' API Audit: Viper.Time.DateTime - Date/Time Operations
' =============================================================================
' Tests: Now, NowMs, Create, Year, Month, Day, Hour, Minute, Second,
'        DayOfWeek, AddDays, AddSeconds, Diff, Format, ToISO
' =============================================================================

PRINT "=== API Audit: Viper.Time.DateTime ==="

' --- Now ---
PRINT "--- Now ---"
DIM now AS INTEGER
now = Viper.Time.DateTime.Now()
PRINT "Now: "; now

' --- NowMs ---
PRINT "--- NowMs ---"
PRINT "NowMs: "; Viper.Time.DateTime.NowMs()

' --- Date components ---
PRINT "--- Date components ---"
PRINT "Year: "; Viper.Time.DateTime.Year(now)
PRINT "Month: "; Viper.Time.DateTime.Month(now)
PRINT "Day: "; Viper.Time.DateTime.Day(now)
PRINT "Hour: "; Viper.Time.DateTime.Hour(now)
PRINT "Minute: "; Viper.Time.DateTime.Minute(now)
PRINT "Second: "; Viper.Time.DateTime.Second(now)

' --- DayOfWeek ---
PRINT "--- DayOfWeek ---"
PRINT "DayOfWeek: "; Viper.Time.DateTime.DayOfWeek(now)

' --- Create ---
PRINT "--- Create ---"
DIM ts AS INTEGER
ts = Viper.Time.DateTime.Create(2024, 6, 15, 12, 30, 0)
PRINT "Create(2024,6,15,12,30,0): "; ts
PRINT "Year: "; Viper.Time.DateTime.Year(ts)
PRINT "Month: "; Viper.Time.DateTime.Month(ts)
PRINT "Day: "; Viper.Time.DateTime.Day(ts)

' --- AddDays ---
PRINT "--- AddDays ---"
DIM tomorrow AS INTEGER
tomorrow = Viper.Time.DateTime.AddDays(ts, 1)
PRINT "AddDays(1) Day: "; Viper.Time.DateTime.Day(tomorrow)

' --- AddSeconds ---
PRINT "--- AddSeconds ---"
DIM plusHour AS INTEGER
plusHour = Viper.Time.DateTime.AddSeconds(ts, 3600)
PRINT "AddSeconds(3600) Hour: "; Viper.Time.DateTime.Hour(plusHour)

' --- Diff ---
PRINT "--- Diff ---"
PRINT "Diff(tomorrow, ts): "; Viper.Time.DateTime.Diff(tomorrow, ts)

' --- Format ---
PRINT "--- Format ---"
PRINT "Format: "; Viper.Time.DateTime.Format(ts, "%Y-%m-%d %H:%M:%S")

' --- ToISO ---
PRINT "--- ToISO ---"
PRINT "ToISO: "; Viper.Time.DateTime.ToISO(ts)
PRINT "ToISO(0): "; Viper.Time.DateTime.ToISO(0)

PRINT "=== DateTime Demo Complete ==="
END
