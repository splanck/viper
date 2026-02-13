' daterange_demo.bas
PRINT "=== Viper.Time.DateRange Demo ==="
DIM s AS LONG
s = Viper.Time.DateTime.Create(2025, 1, 1, 0, 0, 0)
DIM e AS LONG
e = Viper.Time.DateTime.Create(2025, 1, 31, 23, 59, 59)
DIM r AS OBJECT
r = NEW Viper.Time.DateRange(s, e)
PRINT r.Start
PRINT r.Days()
DIM mid AS LONG
mid = Viper.Time.DateTime.Create(2025, 1, 15, 12, 0, 0)
PRINT r.Contains(mid)
PRINT r.ToString()
PRINT "done"
END
