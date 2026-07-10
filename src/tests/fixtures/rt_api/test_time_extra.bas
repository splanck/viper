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

DIM d AS OBJECT
PRINT "relative: "; Viper.Time.RelativeTime.FormatDuration(65000)
d = Viper.Time.DateOnly.Create(2026, 4, 6)
PRINT "date year: "; d.Year
PRINT "date month: "; d.Month
PRINT "date day: "; d.Day

PRINT "done"
END
