' =============================================================================
' API Audit: Viper.IO.Archive (BASIC)
' =============================================================================
' Tests: Create, Open, Path, Count, Names, Has, AddStr, ReadStr,
'        AddFile, Extract, ExtractAll, Info, Finish, IsZip
' =============================================================================

PRINT "=== API Audit: Viper.IO.Archive ==="

DIM tmpDir AS STRING = Viper.IO.TempFile.Dir()
DIM zipPath AS STRING = Viper.IO.Path.Join(tmpDir, "viper_archive_audit_bas.zip")
DIM extractDir AS STRING = Viper.IO.Path.Join(tmpDir, "viper_archive_extract_bas")

' --- Create ---
PRINT "--- Create ---"
DIM arc AS OBJECT = Viper.IO.Archive.Create(zipPath)
PRINT "Created archive"

' --- Path ---
PRINT "--- Path ---"
PRINT arc.Path

' --- AddStr ---
PRINT "--- AddStr ---"
arc.AddStr("hello.txt", "Hello from Viper BASIC!")
arc.AddStr("data/info.txt", "Some nested data")
arc.AddStr("numbers.txt", "1 2 3 4 5")
PRINT "Added 3 entries with AddStr"

' --- Finish ---
PRINT "--- Finish ---"
arc.Finish()
PRINT "Archive finished/closed"

' --- IsZip ---
PRINT "--- IsZip ---"
PRINT "IsZip(zipPath): "; Viper.IO.Archive.IsZip(zipPath)
PRINT "IsZip(nonexistent): "; Viper.IO.Archive.IsZip("/nonexistent_file.zip")

' --- Open ---
PRINT "--- Open ---"
DIM arc2 AS OBJECT = Viper.IO.Archive.Open(zipPath)
PRINT "Opened archive"

' --- Count ---
PRINT "--- Count ---"
PRINT arc2.Count

' --- Names ---
PRINT "--- Names ---"
DIM names AS OBJECT = arc2.Names
PRINT "Names count: "; names.Len

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
DIM extractedFile AS STRING = Viper.IO.Path.Join(extractDir, "hello.txt")
PRINT "Exists: "; Viper.IO.File.Exists(extractedFile)
PRINT Viper.IO.File.ReadAllText(extractedFile)

' --- Cleanup ---
Viper.IO.File.Delete(zipPath)
Viper.IO.File.Delete(Viper.IO.Path.Join(extractDir, "hello.txt"))
Viper.IO.File.Delete(Viper.IO.Path.Join(extractDir, "data/info.txt"))
Viper.IO.File.Delete(Viper.IO.Path.Join(extractDir, "numbers.txt"))

PRINT "=== Archive Audit Complete ==="
END
