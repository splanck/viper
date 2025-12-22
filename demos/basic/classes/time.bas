' Viper.Time Demo - Time and Date Utilities
' This demo showcases clock, datetime, and timing utilities

' === Clock ===
PRINT "=== Clock ==="
PRINT "Clock.Ticks (monotonic): "; Viper.Time.Clock.Ticks()
PRINT "Clock.TicksUs (monotonic us): "; Viper.Time.Clock.TicksUs()
PRINT

' === DateTime (Static API) ===
PRINT "=== DateTime ==="
DIM ts AS INTEGER
ts = Viper.DateTime.Now()
PRINT "DateTime.Now() (Unix timestamp): "; ts
PRINT "DateTime.NowMs() (Unix ms): "; Viper.DateTime.NowMs()
PRINT "Year: "; Viper.DateTime.Year(ts)
PRINT "Month: "; Viper.DateTime.Month(ts)
PRINT "Day: "; Viper.DateTime.Day(ts)
PRINT "Hour: "; Viper.DateTime.Hour(ts)
PRINT "Minute: "; Viper.DateTime.Minute(ts)
PRINT "Second: "; Viper.DateTime.Second(ts)
PRINT "DayOfWeek: "; Viper.DateTime.DayOfWeek(ts)
PRINT "ISO Format: "; Viper.DateTime.ToISO(ts)
PRINT

DIM epoch AS INTEGER
epoch = 0
PRINT "Unix Epoch (timestamp=0):"
PRINT "  Year: "; Viper.DateTime.Year(epoch)
PRINT "  Month: "; Viper.DateTime.Month(epoch)
PRINT "  Day: "; Viper.DateTime.Day(epoch)
PRINT

' === Stopwatch ===
PRINT "=== Stopwatch ==="
DIM sw AS Viper.Diagnostics.Stopwatch
sw = Viper.Diagnostics.Stopwatch.New()
sw.Start()
' Do some work
DIM i AS INTEGER
DIM sum AS INTEGER
sum = 0
FOR i = 1 TO 100000
    sum = sum + i
NEXT i
sw.Stop()
PRINT "Summed 1..100000 = "; sum
PRINT "Elapsed ms: "; sw.ElapsedMs
PRINT "Elapsed us: "; sw.ElapsedUs
PRINT

' === Countdown ===
PRINT "=== Countdown ==="
DIM cd AS Viper.Time.Countdown
cd = Viper.Time.Countdown.New(1000)  ' 1000ms countdown
PRINT "Created 1000ms countdown"
PRINT "Expired immediately: "; cd.Expired
PRINT "Remaining: "; cd.Remaining; " ms"

END
