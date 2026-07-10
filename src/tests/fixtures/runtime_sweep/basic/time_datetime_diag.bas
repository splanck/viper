' EXPECT_OUT: RESULT: ok
' COVER: Viper.Time.DateTime.AddDays
' COVER: Viper.Time.DateTime.AddSeconds
' COVER: Viper.Time.DateTime.Create
' COVER: Viper.Time.DateTime.Day
' COVER: Viper.Time.DateTime.DayOfWeek
' COVER: Viper.Time.DateTime.Diff
' COVER: Viper.Time.DateTime.Format
' COVER: Viper.Time.DateTime.Hour
' COVER: Viper.Time.DateTime.Minute
' COVER: Viper.Time.DateTime.Month
' COVER: Viper.Time.DateTime.Now
' COVER: Viper.Time.DateTime.NowMs
' COVER: Viper.Time.DateTime.Second
' COVER: Viper.Time.DateTime.ToISO
' COVER: Viper.Time.DateTime.Year
' COVER: Viper.Time.Stopwatch.New
' COVER: Viper.Time.Stopwatch.ElapsedMs
' COVER: Viper.Time.Stopwatch.ElapsedNs
' COVER: Viper.Time.Stopwatch.ElapsedUs
' COVER: Viper.Time.Stopwatch.IsRunning
' COVER: Viper.Time.Stopwatch.Reset
' COVER: Viper.Time.Stopwatch.Restart
' COVER: Viper.Time.Stopwatch.Start
' COVER: Viper.Time.Stopwatch.Stop
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
now = Viper.Time.DateTime.Now()
Viper.Core.Diagnostics.Assert(now > 0, "dt.now")
DIM nowMs AS INTEGER
nowMs = Viper.Time.DateTime.NowMs()
Viper.Core.Diagnostics.Assert(nowMs > 0, "dt.nowms")

DIM ts AS INTEGER
ts = Viper.Time.DateTime.Create(2000, 1, 2, 3, 4, 5)
Viper.Core.Diagnostics.AssertEq(Viper.Time.DateTime.Year(ts), 2000, "dt.year")
Viper.Core.Diagnostics.AssertEq(Viper.Time.DateTime.Month(ts), 1, "dt.month")
Viper.Core.Diagnostics.AssertEq(Viper.Time.DateTime.Day(ts), 2, "dt.day")
Viper.Core.Diagnostics.AssertEq(Viper.Time.DateTime.Hour(ts), 3, "dt.hour")
Viper.Core.Diagnostics.AssertEq(Viper.Time.DateTime.Minute(ts), 4, "dt.minute")
Viper.Core.Diagnostics.AssertEq(Viper.Time.DateTime.Second(ts), 5, "dt.second")
Viper.Core.Diagnostics.AssertEq(Viper.Time.DateTime.DayOfWeek(ts), 0, "dt.dow")
Viper.Core.Diagnostics.AssertEqStr(Viper.Time.DateTime.Format(ts, "%Y-%m-%d"), "2000-01-02", "dt.format")
DIM iso AS STRING
iso = Viper.Time.DateTime.ToISO(ts)
Viper.Core.Diagnostics.AssertEq(iso.Length, 20, "dt.iso.len")
Viper.Core.Diagnostics.AssertEqStr(Viper.String.MidLen(iso, 11, 1), "T", "dt.iso.t")
Viper.Core.Diagnostics.AssertEqStr(Viper.String.MidLen(iso, 20, 1), "Z", "dt.iso.z")

DIM ts2 AS INTEGER
ts2 = Viper.Time.DateTime.AddSeconds(ts, 60)
Viper.Core.Diagnostics.AssertEq(Viper.Time.DateTime.Minute(ts2), 5, "dt.addsec")
DIM ts3 AS INTEGER
ts3 = Viper.Time.DateTime.AddDays(ts, 1)
Viper.Core.Diagnostics.AssertEq(Viper.Time.DateTime.Day(ts3), 3, "dt.adddays")
Viper.Core.Diagnostics.AssertEq(Viper.Time.DateTime.Diff(ts3, ts), 86400, "dt.diff")

Viper.Time.Clock.Sleep(1)
DIM t1 AS INTEGER
DIM t2 AS INTEGER
t1 = Viper.Time.Clock.Ticks()
t2 = Viper.Time.Clock.TicksUs()
Viper.Core.Diagnostics.Assert(t1 >= 0, "clock.ticks")
Viper.Core.Diagnostics.Assert(t2 >= 0, "clock.ticksus")

DIM cd AS OBJECT
cd = Viper.Time.Countdown.New(10)
Viper.Core.Diagnostics.AssertEq(cd.Interval, 10, "cd.interval")
cd.Start()
Viper.Core.Diagnostics.Assert(cd.IsRunning, "cd.running")
cd.Wait()
Viper.Core.Diagnostics.Assert(cd.Expired, "cd.expired")
cd.Reset()
Viper.Core.Diagnostics.Assert(cd.Elapsed = 0, "cd.reset")
cd.Start()
cd.Stop()
Viper.Core.Diagnostics.Assert(cd.IsRunning = 0, "cd.stop")
Viper.Core.Diagnostics.Assert(cd.Remaining >= 0, "cd.remaining")

DIM sw AS OBJECT
sw = Viper.Time.Stopwatch.New()
Viper.Core.Diagnostics.Assert(sw.IsRunning = 0, "sw.new")
sw.Start()
Viper.Core.Diagnostics.Assert(sw.IsRunning, "sw.start")
Viper.Time.Clock.Sleep(1)
sw.Stop()
Viper.Core.Diagnostics.Assert(sw.IsRunning = 0, "sw.stop")
Viper.Core.Diagnostics.Assert(sw.ElapsedMs >= 0, "sw.elapsedms")
Viper.Core.Diagnostics.Assert(sw.ElapsedUs >= 0, "sw.elapsedus")
Viper.Core.Diagnostics.Assert(sw.ElapsedNs >= 0, "sw.elapsedns")
sw.Reset()
Viper.Core.Diagnostics.Assert(sw.ElapsedMs = 0, "sw.reset")
sw.Restart()
Viper.Core.Diagnostics.Assert(sw.IsRunning, "sw.restart")
sw.Stop()

PRINT "RESULT: ok"
END
