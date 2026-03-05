' =============================================================================
' API Audit: Viper.Time.RelativeTime - Human-Readable Time Formatting
' =============================================================================
' Tests: Format, FormatFrom, FormatDuration, FormatShort
' =============================================================================

PRINT "=== API Audit: Viper.Time.RelativeTime ==="

DIM now AS INTEGER
now = Viper.Time.DateTime.Now()

' --- Format ---
PRINT "--- Format ---"
PRINT "Format(now): "; Viper.Time.RelativeTime.Format(now)

DIM hourAgo AS INTEGER
hourAgo = Viper.Time.DateTime.AddSeconds(now, -3600)
PRINT "Format(1h ago): "; Viper.Time.RelativeTime.Format(hourAgo)

DIM dayAgo AS INTEGER
dayAgo = Viper.Time.DateTime.AddDays(now, -1)
PRINT "Format(1d ago): "; Viper.Time.RelativeTime.Format(dayAgo)

DIM weekAgo AS INTEGER
weekAgo = Viper.Time.DateTime.AddDays(now, -7)
PRINT "Format(7d ago): "; Viper.Time.RelativeTime.Format(weekAgo)

' --- FormatFrom ---
PRINT "--- FormatFrom ---"
DIM ts1 AS INTEGER
DIM ts2 AS INTEGER
ts1 = Viper.Time.DateTime.Create(2024, 1, 1, 0, 0, 0)
ts2 = Viper.Time.DateTime.Create(2024, 1, 2, 0, 0, 0)
PRINT "FormatFrom(Jan1, Jan2): "; Viper.Time.RelativeTime.FormatFrom(ts1, ts2)

' --- FormatDuration ---
PRINT "--- FormatDuration ---"
PRINT "FormatDuration(5s): "; Viper.Time.RelativeTime.FormatDuration(Viper.Time.Duration.FromSeconds(5))
PRINT "FormatDuration(90s): "; Viper.Time.RelativeTime.FormatDuration(Viper.Time.Duration.FromSeconds(90))
PRINT "FormatDuration(2h): "; Viper.Time.RelativeTime.FormatDuration(Viper.Time.Duration.FromHours(2))

' --- FormatShort ---
PRINT "--- FormatShort ---"
PRINT "FormatShort(now): "; Viper.Time.RelativeTime.FormatShort(now)
PRINT "FormatShort(1h ago): "; Viper.Time.RelativeTime.FormatShort(hourAgo)
PRINT "FormatShort(1d ago): "; Viper.Time.RelativeTime.FormatShort(dayAgo)

PRINT "=== RelativeTime Demo Complete ==="
END
