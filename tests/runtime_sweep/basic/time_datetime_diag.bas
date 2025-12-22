' EXPECT_OUT: RESULT: ok
' COVER: Viper.DateTime.AddDays
' COVER: Viper.DateTime.AddSeconds
' COVER: Viper.DateTime.Create
' COVER: Viper.DateTime.Day
' COVER: Viper.DateTime.DayOfWeek
' COVER: Viper.DateTime.Diff
' COVER: Viper.DateTime.Format
' COVER: Viper.DateTime.Hour
' COVER: Viper.DateTime.Minute
' COVER: Viper.DateTime.Month
' COVER: Viper.DateTime.Now
' COVER: Viper.DateTime.NowMs
' COVER: Viper.DateTime.Second
' COVER: Viper.DateTime.ToISO
' COVER: Viper.DateTime.Year
' COVER: Viper.Diagnostics.Stopwatch.New
' COVER: Viper.Diagnostics.Stopwatch.ElapsedMs
' COVER: Viper.Diagnostics.Stopwatch.ElapsedNs
' COVER: Viper.Diagnostics.Stopwatch.ElapsedUs
' COVER: Viper.Diagnostics.Stopwatch.IsRunning
' COVER: Viper.Diagnostics.Stopwatch.Reset
' COVER: Viper.Diagnostics.Stopwatch.Restart
' COVER: Viper.Diagnostics.Stopwatch.Start
' COVER: Viper.Diagnostics.Stopwatch.Stop
' COVER: Viper.Time.Clock.Sleep
' COVER: Viper.Time.Clock.Ticks
' COVER: Viper.Time.Clock.TicksUs
' COVER: Viper.Time.Countdown.New
' COVER: Viper.Time.Countdown.Elapsed
' COVER: Viper.Time.Countdown.Expired
' COVER: Viper.Time.Countdown.Interval
' COVER: Viper.Time.Countdown.IsRunning
' COVER: Viper.Time.Countdown.Remaining
' COVER: Viper.Time.Countdown.Reset
' COVER: Viper.Time.Countdown.Start
' COVER: Viper.Time.Countdown.Stop
' COVER: Viper.Time.Countdown.Wait

DIM now AS INTEGER
now = Viper.DateTime.Now()
Viper.Diagnostics.Assert(now > 0, "dt.now")
DIM nowMs AS INTEGER
nowMs = Viper.DateTime.NowMs()
Viper.Diagnostics.Assert(nowMs > 0, "dt.nowms")

DIM ts AS INTEGER
ts = Viper.DateTime.Create(2000, 1, 2, 3, 4, 5)
Viper.Diagnostics.AssertEq(Viper.DateTime.Year(ts), 2000, "dt.year")
Viper.Diagnostics.AssertEq(Viper.DateTime.Month(ts), 1, "dt.month")
Viper.Diagnostics.AssertEq(Viper.DateTime.Day(ts), 2, "dt.day")
Viper.Diagnostics.AssertEq(Viper.DateTime.Hour(ts), 3, "dt.hour")
Viper.Diagnostics.AssertEq(Viper.DateTime.Minute(ts), 4, "dt.minute")
Viper.Diagnostics.AssertEq(Viper.DateTime.Second(ts), 5, "dt.second")
Viper.Diagnostics.AssertEq(Viper.DateTime.DayOfWeek(ts), 0, "dt.dow")
Viper.Diagnostics.AssertEqStr(Viper.DateTime.Format(ts, "%Y-%m-%d"), "2000-01-02", "dt.format")
Viper.Diagnostics.AssertEqStr(Viper.DateTime.ToISO(ts), "2000-01-02T03:04:05", "dt.iso")

DIM ts2 AS INTEGER
ts2 = Viper.DateTime.AddSeconds(ts, 60)
Viper.Diagnostics.AssertEq(Viper.DateTime.Minute(ts2), 5, "dt.addsec")
DIM ts3 AS INTEGER
ts3 = Viper.DateTime.AddDays(ts, 1)
Viper.Diagnostics.AssertEq(Viper.DateTime.Day(ts3), 3, "dt.adddays")
Viper.Diagnostics.AssertEq(Viper.DateTime.Diff(ts3, ts), 86400, "dt.diff")

Viper.Time.Clock.Sleep(1)
DIM t1 AS INTEGER
DIM t2 AS INTEGER
t1 = Viper.Time.Clock.Ticks()
t2 = Viper.Time.Clock.TicksUs()
Viper.Diagnostics.Assert(t1 >= 0, "clock.ticks")
Viper.Diagnostics.Assert(t2 >= 0, "clock.ticksus")

DIM cd AS OBJECT
cd = Viper.Time.Countdown.New(10)
Viper.Diagnostics.AssertEq(cd.Interval, 10, "cd.interval")
cd.Start()
Viper.Diagnostics.Assert(cd.IsRunning, "cd.running")
cd.Wait()
Viper.Diagnostics.Assert(cd.Expired, "cd.expired")
cd.Reset()
Viper.Diagnostics.Assert(cd.Elapsed = 0, "cd.reset")
cd.Start()
cd.Stop()
Viper.Diagnostics.Assert(cd.IsRunning = 0, "cd.stop")
Viper.Diagnostics.Assert(cd.Remaining >= 0, "cd.remaining")

DIM sw AS OBJECT
sw = Viper.Diagnostics.Stopwatch.New()
Viper.Diagnostics.Assert(sw.IsRunning = 0, "sw.new")
sw.Start()
Viper.Diagnostics.Assert(sw.IsRunning, "sw.start")
Viper.Time.Clock.Sleep(1)
sw.Stop()
Viper.Diagnostics.Assert(sw.IsRunning = 0, "sw.stop")
Viper.Diagnostics.Assert(sw.ElapsedMs >= 0, "sw.elapsedms")
Viper.Diagnostics.Assert(sw.ElapsedUs >= 0, "sw.elapsedus")
Viper.Diagnostics.Assert(sw.ElapsedNs >= 0, "sw.elapsedns")
sw.Reset()
Viper.Diagnostics.Assert(sw.ElapsedMs = 0, "sw.reset")
sw.Restart()
Viper.Diagnostics.Assert(sw.IsRunning, "sw.restart")
sw.Stop()

PRINT "RESULT: ok"
END
