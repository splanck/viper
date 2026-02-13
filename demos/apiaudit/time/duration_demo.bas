' duration_demo.bas
PRINT "=== Viper.Time.Duration Demo ==="
DIM d AS LONG
d = Viper.Time.Duration.FromSeconds(3661)
PRINT Viper.Time.Duration.TotalSeconds(d)
PRINT Viper.Time.Duration.TotalMinutes(d)
PRINT Viper.Time.Duration.Hours(d)
PRINT Viper.Time.Duration.Minutes(d)
PRINT Viper.Time.Duration.Seconds(d)
PRINT Viper.Time.Duration.ToString(d)
DIM zero AS LONG
zero = Viper.Time.Duration.Zero()
PRINT Viper.Time.Duration.TotalSeconds(zero)
PRINT "done"
END
