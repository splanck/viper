' countdown_demo.bas
PRINT "=== Viper.Time.Countdown Demo ==="
DIM cd AS OBJECT
cd = NEW Viper.Time.Countdown(1000)
PRINT cd.Interval
PRINT cd.IsRunning
PRINT cd.Expired
PRINT "done"
END
