' relativetime_demo.bas
PRINT "=== Viper.Time.RelativeTime Demo ==="
DIM now AS LONG
now = Viper.Time.DateTime.Now()
DIM past AS LONG
past = Viper.Time.DateTime.AddSeconds(now, -3600)
PRINT Viper.Time.RelativeTime.Format(past)
DIM dur AS LONG
dur = Viper.Time.Duration.FromMinutes(45)
PRINT Viper.Time.RelativeTime.FormatDuration(dur)
PRINT "done"
END
