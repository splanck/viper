' EXPECT_OUT: RESULT: ok
' COVER: Zanna.Time.DateTime.AddDays
' COVER: Zanna.Time.DateTime.AddSeconds
' COVER: Zanna.Time.DateTime.FromParts
' COVER: Zanna.Time.DateTime.Day
' COVER: Zanna.Time.DateTime.DayOfWeek
' COVER: Zanna.Time.DateTime.Diff
' COVER: Zanna.Time.DateTime.Format
' COVER: Zanna.Time.DateTime.Hour
' COVER: Zanna.Time.DateTime.Minute
' COVER: Zanna.Time.DateTime.Month
' COVER: Zanna.Time.DateTime.Now
' COVER: Zanna.Time.DateTime.NowMs
' COVER: Zanna.Time.DateTime.Second
' COVER: Zanna.Time.DateTime.ToIso8601
' COVER: Zanna.Time.DateTime.Year
' COVER: Zanna.Time.Stopwatch.New
' COVER: Zanna.Time.Stopwatch.ElapsedMs
' COVER: Zanna.Time.Stopwatch.ElapsedNs
' COVER: Zanna.Time.Stopwatch.ElapsedUs
' COVER: Zanna.Time.Stopwatch.IsRunning
' COVER: Zanna.Time.Stopwatch.Reset
' COVER: Zanna.Time.Stopwatch.Restart
' COVER: Zanna.Time.Stopwatch.Start
' COVER: Zanna.Time.Stopwatch.Stop
' COVER: Zanna.Time.Clock.Sleep
' COVER: Zanna.Time.Clock.NowMs
' COVER: Zanna.Time.Clock.NowMicros
' COVER: Zanna.Time.Countdown.New
' COVER: Zanna.Time.Countdown.Elapsed
' COVER: Zanna.Time.Countdown.IsExpired
' COVER: Zanna.Time.Countdown.Interval
' COVER: Zanna.Time.Countdown.IsRunning
' COVER: Zanna.Time.Countdown.Remaining
' COVER: Zanna.Time.Countdown.Reset
' COVER: Zanna.Time.Countdown.Start
' COVER: Zanna.Time.Countdown.Stop
' COVER: Zanna.Time.Countdown.Wait

DIM now AS INTEGER
now = Zanna.Time.DateTime.Now()
Zanna.Core.Diagnostics.Assert(now > 0, "dt.now")
DIM nowMs AS INTEGER
nowMs = Zanna.Time.DateTime.NowMs()
Zanna.Core.Diagnostics.Assert(nowMs > 0, "dt.nowms")

DIM ts AS INTEGER
ts = Zanna.Time.DateTime.FromParts(2000, 1, 2, 3, 4, 5)
Zanna.Core.Diagnostics.AssertEq(Zanna.Time.DateTime.Year(ts), 2000, "dt.year")
Zanna.Core.Diagnostics.AssertEq(Zanna.Time.DateTime.Month(ts), 1, "dt.month")
Zanna.Core.Diagnostics.AssertEq(Zanna.Time.DateTime.Day(ts), 2, "dt.day")
Zanna.Core.Diagnostics.AssertEq(Zanna.Time.DateTime.Hour(ts), 3, "dt.hour")
Zanna.Core.Diagnostics.AssertEq(Zanna.Time.DateTime.Minute(ts), 4, "dt.minute")
Zanna.Core.Diagnostics.AssertEq(Zanna.Time.DateTime.Second(ts), 5, "dt.second")
Zanna.Core.Diagnostics.AssertEq(Zanna.Time.DateTime.DayOfWeek(ts), 0, "dt.dow")
Zanna.Core.Diagnostics.AssertEqStr(Zanna.Time.DateTime.Format(ts, "%Y-%m-%d"), "2000-01-02", "dt.format")
DIM iso AS STRING
iso = Zanna.Time.DateTime.ToIso8601(ts)
Zanna.Core.Diagnostics.AssertEq(iso.Length, 20, "dt.iso.len")
Zanna.Core.Diagnostics.AssertEqStr(Zanna.String.MidLen(iso, 11, 1), "T", "dt.iso.t")
Zanna.Core.Diagnostics.AssertEqStr(Zanna.String.MidLen(iso, 20, 1), "Z", "dt.iso.z")

DIM ts2 AS INTEGER
ts2 = Zanna.Time.DateTime.AddSeconds(ts, 60)
Zanna.Core.Diagnostics.AssertEq(Zanna.Time.DateTime.Minute(ts2), 5, "dt.addsec")
DIM ts3 AS INTEGER
ts3 = Zanna.Time.DateTime.AddDays(ts, 1)
Zanna.Core.Diagnostics.AssertEq(Zanna.Time.DateTime.Day(ts3), 3, "dt.adddays")
Zanna.Core.Diagnostics.AssertEq(Zanna.Time.DateTime.Diff(ts3, ts), 86400, "dt.diff")

Zanna.Time.Clock.Sleep(1)
DIM t1 AS INTEGER
DIM t2 AS INTEGER
t1 = Zanna.Time.Clock.NowMs()
t2 = Zanna.Time.Clock.NowMicros()
Zanna.Core.Diagnostics.Assert(t1 >= 0, "clock.ticks")
Zanna.Core.Diagnostics.Assert(t2 >= 0, "clock.ticksus")

DIM cd AS OBJECT
cd = Zanna.Time.Countdown.New(10)
Zanna.Core.Diagnostics.AssertEq(cd.Interval, 10, "cd.interval")
cd.Start()
Zanna.Core.Diagnostics.Assert(cd.IsRunning, "cd.running")
cd.Wait()
Zanna.Core.Diagnostics.Assert(cd.IsExpired, "cd.expired")
cd.Reset()
Zanna.Core.Diagnostics.Assert(cd.Elapsed = 0, "cd.reset")
cd.Start()
cd.Stop()
Zanna.Core.Diagnostics.Assert(cd.IsRunning = FALSE, "cd.stop")
Zanna.Core.Diagnostics.Assert(cd.Remaining >= 0, "cd.remaining")

DIM sw AS OBJECT
sw = Zanna.Time.Stopwatch.New()
Zanna.Core.Diagnostics.Assert(sw.IsRunning = FALSE, "sw.new")
sw.Start()
Zanna.Core.Diagnostics.Assert(sw.IsRunning, "sw.start")
Zanna.Time.Clock.Sleep(1)
sw.Stop()
Zanna.Core.Diagnostics.Assert(sw.IsRunning = FALSE, "sw.stop")
Zanna.Core.Diagnostics.Assert(sw.ElapsedMs >= 0, "sw.elapsedms")
Zanna.Core.Diagnostics.Assert(sw.ElapsedUs >= 0, "sw.elapsedus")
Zanna.Core.Diagnostics.Assert(sw.ElapsedNs >= 0, "sw.elapsedns")
sw.Reset()
Zanna.Core.Diagnostics.Assert(sw.ElapsedMs = 0, "sw.reset")
sw.Restart()
Zanna.Core.Diagnostics.Assert(sw.IsRunning, "sw.restart")
sw.Stop()

PRINT "RESULT: ok"
END
