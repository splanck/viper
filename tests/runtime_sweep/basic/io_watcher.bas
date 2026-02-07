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

DIM watcher AS Viper.IO.Watcher
watcher = Viper.IO.Watcher.New(base)
Viper.Core.Diagnostics.AssertEqStr(watcher.Path, base, "watch.path")
Viper.Core.Diagnostics.Assert(watcher.IsWatching = FALSE, "watch.init")

watcher.Start()
Viper.Core.Diagnostics.Assert(watcher.IsWatching, "watch.start")

DIM watchFile AS STRING
watchFile = Viper.IO.Path.Join(base, "event.txt")
Viper.IO.File.WriteAllText(watchFile, "ping")

DIM event1 AS INTEGER
DIM event2 AS INTEGER
event1 = watcher.Poll()
event2 = watcher.PollFor(50)
    Viper.Core.Diagnostics.Assert(event1 >= Viper.IO.Watcher.EVENT_NONE, "watch.poll")
    Viper.Core.Diagnostics.Assert(event2 >= Viper.IO.Watcher.EVENT_NONE, "watch.pollfor")

DIM path1 AS STRING
DIM type1 AS INTEGER
IF event1 <> Viper.IO.Watcher.EVENT_NONE OR event2 <> Viper.IO.Watcher.EVENT_NONE THEN
    path1 = watcher.EventPath()
    type1 = watcher.EventType()
        Viper.Core.Diagnostics.Assert(type1 >= Viper.IO.Watcher.EVENT_NONE, "watch.eventtype")
END IF

watcher.Stop()
Viper.Core.Diagnostics.Assert(watcher.IsWatching = FALSE, "watch.stop")

Viper.IO.File.Delete(watchFile)
Viper.IO.Dir.RemoveAll(base)

PRINT "RESULT: ok"
END
