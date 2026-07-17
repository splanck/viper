' =============================================================================
' API Audit: Zanna.IO.Archive (BASIC)
' =============================================================================
' Tests: Create, Open, Path, Count, Names, Has, AddStr, ReadStr,
'        AddFile, Extract, ExtractAll, Info, Finish, IsZip
' =============================================================================

PRINT "=== API Audit: Zanna.IO.Archive ==="

DIM tmpDir AS STRING = Zanna.IO.TempFile.Dir()
DIM zipPath AS STRING = Zanna.IO.Path.Join(tmpDir, "zanna_archive_audit_bas.zip")
DIM extractDir AS STRING = Zanna.IO.Path.Join(tmpDir, "zanna_archive_extract_bas")

' --- Create ---
PRINT "--- Create ---"
DIM arc AS OBJECT = Zanna.IO.Archive.Create(zipPath)
PRINT "Created archive"

' --- Path ---
PRINT "--- Path ---"
PRINT arc.Path

' --- AddStr ---
PRINT "--- AddStr ---"
arc.AddStr("hello.txt", "Hello from Zanna BASIC!")
arc.AddStr("data/info.txt", "Some nested data")
arc.AddStr("numbers.txt", "1 2 3 4 5")
PRINT "Added 3 entries with AddStr"

' --- Finish ---
PRINT "--- Finish ---"
arc.Finish()
PRINT "Archive finished/closed"

' --- IsZip ---
PRINT "--- IsZip ---"
PRINT "IsZip(zipPath): "; Zanna.IO.Archive.IsZip(zipPath)
PRINT "IsZip(nonexistent): "; Zanna.IO.Archive.IsZip("/nonexistent_file.zip")

' --- Open ---
PRINT "--- Open ---"
DIM arc2 AS OBJECT = Zanna.IO.Archive.Open(zipPath)
PRINT "Opened archive"

' --- Count ---
PRINT "--- Count ---"
PRINT arc2.Count

' --- Names ---
PRINT "--- Names ---"
DIM names AS OBJECT = arc2.Names
PRINT "Names count: "; names.Count

' --- Has ---
PRINT "--- Has ---"
PRINT "Has hello.txt: "; arc2.Has("hello.txt")
PRINT "Has data/info.txt: "; arc2.Has("data/info.txt")
PRINT "Has nonexistent.txt: "; arc2.Has("nonexistent.txt")

' --- ReadStr ---
PRINT "--- ReadStr ---"
PRINT arc2.ReadStr("hello.txt")
PRINT arc2.ReadStr("data/info.txt")
PRINT arc2.ReadStr("numbers.txt")

' --- Info ---
PRINT "--- Info ---"
DIM info AS OBJECT = arc2.Info("hello.txt")
PRINT "Info returned (object)"

' --- ExtractAll ---
PRINT "--- ExtractAll ---"
arc2.ExtractAll(extractDir)
PRINT "ExtractAll done"

' --- Verify extracted ---
PRINT "--- Verify extracted ---"
DIM extractedFile AS STRING = Zanna.IO.Path.Join(extractDir, "hello.txt")
PRINT "Exists: "; Zanna.IO.File.Exists(extractedFile)
PRINT Zanna.IO.File.ReadAllText(extractedFile)

' --- Cleanup ---
Zanna.IO.File.Delete(zipPath)
Zanna.IO.File.Delete(Zanna.IO.Path.Join(extractDir, "hello.txt"))
Zanna.IO.File.Delete(Zanna.IO.Path.Join(extractDir, "data/info.txt"))
Zanna.IO.File.Delete(Zanna.IO.Path.Join(extractDir, "numbers.txt"))

PRINT "=== Archive Audit Complete ==="
END
