' datetime_demo.bas
PRINT "=== Viper.Time.DateTime Demo ==="
DIM now AS LONG
now = Viper.Time.DateTime.Now()
PRINT Viper.Time.DateTime.Year(now)
PRINT Viper.Time.DateTime.Month(now)
PRINT Viper.Time.DateTime.Day(now)
DIM ts AS LONG
ts = Viper.Time.DateTime.Create(2025, 6, 15, 10, 30, 0)
PRINT Viper.Time.DateTime.ToISO(ts)
PRINT Viper.Time.DateTime.Format(ts, "YYYY-MM-DD")
PRINT "done"
END
