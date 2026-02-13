' =============================================================================
' API Audit: Viper.IO.Watcher (BASIC)
' =============================================================================
' Tests: New, Path, IsWatching, Start, Stop, Poll, PollFor,
'        EventPath, EventType, EVENT_NONE, EVENT_CREATED, EVENT_MODIFIED,
'        EVENT_DELETED, EVENT_RENAMED
' =============================================================================

PRINT "=== API Audit: Viper.IO.Watcher ==="

DIM tmpDir AS STRING = Viper.IO.TempFile.Dir()

' --- New ---
PRINT "--- New ---"
DIM w AS OBJECT = Viper.IO.Watcher.New(tmpDir)
PRINT "Created watcher for temp dir"

' --- Path ---
PRINT "--- Path ---"
PRINT w.Path

' --- IsWatching (before start) ---
PRINT "--- IsWatching (before start) ---"
PRINT w.IsWatching

' --- Event constants ---
PRINT "--- Event constants ---"
PRINT "EVENT_NONE: "; w.EVENT_NONE
PRINT "EVENT_CREATED: "; w.EVENT_CREATED
PRINT "EVENT_MODIFIED: "; w.EVENT_MODIFIED
PRINT "EVENT_DELETED: "; w.EVENT_DELETED
PRINT "EVENT_RENAMED: "; w.EVENT_RENAMED

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
DIM testFile AS STRING = Viper.IO.Path.Join(tmpDir, "watcher_test_file_bas.txt")
Viper.IO.File.WriteAllText(testFile, "hello watcher")
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
Viper.IO.File.Delete(testFile)

PRINT "=== Watcher Audit Complete ==="
END
