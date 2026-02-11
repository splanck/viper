' test_time_extra.bas — Stopwatch, Clock, Countdown, DateOnly, DateRange, RelativeTime
DIM sw AS Viper.Time.Stopwatch
sw = Viper.Time.Stopwatch.New()
PRINT "sw running: "; sw.IsRunning
sw.Start()
PRINT "sw running after start: "; sw.IsRunning
sw.Stop()
PRINT "sw running after stop: "; sw.IsRunning
PRINT "sw elapsed ms >= 0: "; (sw.ElapsedMs >= 0)
sw.Reset()
PRINT "sw elapsed after reset: "; sw.ElapsedMs

PRINT "clock ticks > 0: "; (Viper.Time.Clock.Ticks() > 0)
PRINT "clock ticksus > 0: "; (Viper.Time.Clock.TicksUs() > 0)

DIM cd AS Viper.Time.Countdown
cd = Viper.Time.Countdown.New(5000)
PRINT "cd interval: "; cd.Interval
PRINT "cd expired: "; cd.Expired
PRINT "cd running: "; cd.IsRunning

' NOTE: DateOnly, DateRange, RelativeTime are not recognized by BASIC frontend (BUG-009)
' Viper.Time.DateOnly.Create — unknown procedure
' Viper.Time.RelativeTime.FormatDuration — unknown procedure

PRINT "done"
END
