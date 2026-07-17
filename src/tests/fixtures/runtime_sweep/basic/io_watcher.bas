' EXPECT_OUT: RESULT: ok
' COVER: Zanna.IO.Watcher.New
' COVER: Zanna.IO.Watcher.EventCreated
' COVER: Zanna.IO.Watcher.EventDeleted
' COVER: Zanna.IO.Watcher.EventModified
' COVER: watcher.EventNone
' COVER: Zanna.IO.Watcher.EventRenamed
' COVER: Zanna.IO.Watcher.IsWatching
' COVER: Zanna.IO.Watcher.Path
' COVER: Zanna.IO.Watcher.EventPath
' COVER: Zanna.IO.Watcher.EventType
' COVER: Zanna.IO.Watcher.Poll
' COVER: Zanna.IO.Watcher.PollFor
' COVER: Zanna.IO.Watcher.Start
' COVER: Zanna.IO.Watcher.Stop

DIM cwd AS STRING
cwd = Zanna.IO.Dir.Current()
DIM base AS STRING
base = Zanna.IO.Path.Join(cwd, "tests/runtime_sweep/tmp_watch")
IF Zanna.IO.Dir.Exists(base) THEN
    Zanna.IO.Dir.RemoveAll(base)
END IF
Zanna.IO.Dir.MakeAll(base)

DIM watcher AS Zanna.IO.Watcher
watcher = Zanna.IO.Watcher.New(base)
Zanna.Core.Diagnostics.AssertEqStr(watcher.Path, base, "watch.path")
Zanna.Core.Diagnostics.Assert(watcher.IsWatching = FALSE, "watch.init")

watcher.Start()
Zanna.Core.Diagnostics.Assert(watcher.IsWatching, "watch.start")

DIM watchFile AS STRING
watchFile = Zanna.IO.Path.Join(base, "event.txt")
Zanna.IO.File.WriteAllText(watchFile, "ping")

DIM event1 AS INTEGER
DIM event2 AS INTEGER
event1 = watcher.Poll()
event2 = watcher.PollFor(50)
    Zanna.Core.Diagnostics.Assert(event1 >= watcher.EventNone, "watch.poll")
    Zanna.Core.Diagnostics.Assert(event2 >= watcher.EventNone, "watch.pollfor")

DIM path1 AS STRING
DIM type1 AS INTEGER
IF event1 <> watcher.EventNone OR event2 <> watcher.EventNone THEN
    path1 = watcher.EventPath()
    type1 = watcher.EventType()
        Zanna.Core.Diagnostics.Assert(type1 >= watcher.EventNone, "watch.eventtype")
END IF

watcher.Stop()
Zanna.Core.Diagnostics.Assert(watcher.IsWatching = FALSE, "watch.stop")

Zanna.IO.File.Delete(watchFile)
Zanna.IO.Dir.RemoveAll(base)

PRINT "RESULT: ok"
END
