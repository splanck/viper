' =============================================================================
' API Audit: Zanna.IO.Watcher (BASIC)
' =============================================================================
' Tests: New, Path, IsWatching, Start, Stop, Poll, PollFor,
'        EventPath, EventType, EventNone, EventCreated, EventModified,
'        EventDeleted, EventRenamed
' =============================================================================

PRINT "=== API Audit: Zanna.IO.Watcher ==="

DIM tmpDir AS STRING = Zanna.IO.TempFile.Dir()

' --- New ---
PRINT "--- New ---"
DIM w AS OBJECT = Zanna.IO.Watcher.New(tmpDir)
PRINT "Created watcher for temp dir"

' --- Path ---
PRINT "--- Path ---"
PRINT w.Path

' --- IsWatching (before start) ---
PRINT "--- IsWatching (before start) ---"
PRINT w.IsWatching

' --- Event constants ---
PRINT "--- Event constants ---"
PRINT "EventNone: "; w.EventNone
PRINT "EventCreated: "; w.EventCreated
PRINT "EventModified: "; w.EventModified
PRINT "EventDeleted: "; w.EventDeleted
PRINT "EventRenamed: "; w.EventRenamed

' --- Start ---
PRINT "--- Start ---"
w.Start()
PRINT "Watcher started"

' --- IsWatching (after start) ---
PRINT "--- IsWatching (after start) ---"
PRINT w.IsWatching

' --- PollFor (no events yet) ---
PRINT "--- PollFor (no events, 10ms timeout) ---"
DIM evtCount AS INTEGER = w.PollFor(10)
PRINT "Events: "; evtCount

' --- Create a file to trigger an event ---
PRINT "--- Trigger file creation ---"
DIM testFile AS STRING = Zanna.IO.Path.Join(tmpDir, "watcher_test_file_bas.txt")
Zanna.IO.File.WriteAllText(testFile, "hello watcher")
PRINT "File created"

' --- PollFor (should detect creation) ---
PRINT "--- PollFor (after file creation, 500ms) ---"
DIM evtCount2 AS INTEGER = w.PollFor(500)
PRINT "Events: "; evtCount2

' --- EventPath ---
PRINT "--- EventPath ---"
PRINT w.EventPath()

' --- EventType ---
PRINT "--- EventType ---"
PRINT w.EventType()

' --- Stop ---
PRINT "--- Stop ---"
w.Stop()
PRINT "Watcher stopped"

' --- IsWatching (after stop) ---
PRINT "--- IsWatching (after stop) ---"
PRINT w.IsWatching

' --- Cleanup ---
Zanna.IO.File.Delete(testFile)

PRINT "=== Watcher Audit Complete ==="
END
