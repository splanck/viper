' clock_demo.bas
PRINT "=== Viper.Time.Clock Demo ==="
DIM t1 AS LONG
t1 = Viper.Time.Clock.Ticks()
DIM t2 AS LONG
t2 = Viper.Time.Clock.Ticks()
PRINT (t2 >= t1)
DIM us AS LONG
us = Viper.Time.Clock.TicksUs()
PRINT (us > 0)
PRINT "done"
END
