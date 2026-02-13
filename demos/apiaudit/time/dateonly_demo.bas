' dateonly_demo.bas
PRINT "=== Viper.Time.DateOnly Demo ==="
DIM d AS OBJECT
d = NEW Viper.Time.DateOnly()
PRINT d.Year
PRINT d.Month
PRINT d.Day
PRINT d.ToString()
PRINT d.DayOfWeek
PRINT d.DayOfYear
PRINT "done"
END
