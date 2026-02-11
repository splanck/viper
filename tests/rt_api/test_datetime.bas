' test_datetime.bas — Viper.Time.DateTime + Duration
DIM t AS INTEGER
LET t = Viper.Time.DateTime.Create(2024, 6, 15, 10, 30, 0)
PRINT Viper.Time.DateTime.Year(t)
PRINT Viper.Time.DateTime.Month(t)
PRINT Viper.Time.DateTime.Day(t)
PRINT Viper.Time.DateTime.Hour(t)
PRINT Viper.Time.DateTime.Minute(t)
PRINT Viper.Time.DateTime.Second(t)
PRINT Viper.Time.DateTime.DayOfWeek(t)

DIM iso AS STRING
LET iso = Viper.Time.DateTime.ToISO(t)
PRINT Viper.String.Has(iso, "2024")

DIM t2 AS INTEGER
LET t2 = Viper.Time.DateTime.AddDays(t, 10)
PRINT Viper.Time.DateTime.Day(t2)

DIM t3 AS INTEGER
LET t3 = Viper.Time.DateTime.AddSeconds(t, 3600)
PRINT Viper.Time.DateTime.Hour(t3)

DIM now AS INTEGER
LET now = Viper.Time.DateTime.Now()
PRINT now > 0

DIM ms AS INTEGER
LET ms = Viper.Time.DateTime.NowMs()
PRINT ms > 0

' Duration
DIM d AS INTEGER
LET d = Viper.Time.Duration.FromSeconds(90)
PRINT Viper.Time.Duration.TotalSeconds(d)
PRINT Viper.Time.Duration.TotalMillis(d)
PRINT Viper.Time.Duration.Minutes(d)
PRINT Viper.Time.Duration.Seconds(d)

DIM d2 AS INTEGER
LET d2 = Viper.Time.Duration.Create(1, 2, 30, 0, 0)
PRINT Viper.Time.Duration.TotalHours(d2)
PRINT Viper.Time.Duration.ToString(d2)

DIM d3 AS INTEGER
LET d3 = Viper.Time.Duration.Add(d, d2)
PRINT Viper.Time.Duration.TotalSeconds(d3)

DIM dz AS INTEGER
LET dz = Viper.Time.Duration.Zero()
PRINT Viper.Time.Duration.TotalSeconds(dz)

PRINT "done"
END
