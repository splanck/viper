' EXPECT_OUT: RESULT: ok
' COVER: Viper.IO.Watcher.new
' COVER: Viper.IO.Watcher.EVENT_CREATED
' COVER: Viper.IO.Watcher.EVENT_DELETED
' COVER: Viper.IO.Watcher.EVENT_MODIFIED
' COVER: Viper.IO.Watcher.EVENT_NONE
' COVER: Viper.IO.Watcher.EVENT_RENAMED
' COVER: Viper.IO.Watcher.IsWatching
' COVER: Viper.IO.Watcher.Path
' COVER: Viper.IO.Watcher.EventPath
' COVER: Viper.IO.Watcher.EventType
' COVER: Viper.IO.Watcher.Poll
' COVER: Viper.IO.Watcher.PollFor
' COVER: Viper.IO.Watcher.Start
' COVER: Viper.IO.Watcher.Stop

DIM cwd AS STRING
cwd = Viper.IO.Dir.Current()
DIM base AS STRING
base = Viper.IO.Path.Join(cwd, "tests/runtime_sweep/tmp_watch")
IF Viper.IO.Dir.Exists(base) THEN
    Viper.IO.Dir.RemoveAll(base)
END IF
Viper.IO.Dir.MakeAll(base)

DIM watcher AS OBJECT
watcher = Viper.IO.Watcher.New(base)
Viper.Diagnostics.AssertEqStr(watcher.Path, base, "watch.path")
Viper.Diagnostics.Assert(watcher.IsWatching = 0, "watch.init")

watcher.Start()
Viper.Diagnostics.Assert(watcher.IsWatching, "watch.start")

DIM event1 AS INTEGER
DIM event2 AS INTEGER
event1 = watcher.Poll()
event2 = watcher.PollFor(10)
Viper.Diagnostics.Assert(event1 >= watcher.EVENT_NONE, "watch.poll")
Viper.Diagnostics.Assert(event2 >= watcher.EVENT_NONE, "watch.pollfor")

DIM path1 AS STRING
DIM type1 AS INTEGER
path1 = watcher.EventPath()
type1 = watcher.EventType()
Viper.Diagnostics.Assert(type1 >= watcher.EVENT_NONE, "watch.eventtype")

watcher.Stop()
Viper.Diagnostics.Assert(watcher.IsWatching = 0, "watch.stop")

Viper.IO.Dir.RemoveAll(base)

PRINT "RESULT: ok"
END
