' =============================================================================
' API Audit: Viper.IO.File - File Operations
' =============================================================================
' Tests: Exists, WriteAllText, ReadAllText, AppendLine, ReadAllLines, Size,
'        Delete, Copy, Move, Touch, Modified
' =============================================================================

PRINT "=== API Audit: Viper.IO.File ==="

DIM tmpDir AS STRING
tmpDir = Viper.IO.TempFile.Dir()
DIM testFile AS STRING
testFile = Viper.IO.Path.Join(tmpDir, "viper_file_audit_bas.txt")
DIM copyFile AS STRING
copyFile = Viper.IO.Path.Join(tmpDir, "viper_file_audit_bas_copy.txt")
DIM moveFile AS STRING
moveFile = Viper.IO.Path.Join(tmpDir, "viper_file_audit_bas_moved.txt")

' --- WriteAllText ---
PRINT "--- WriteAllText ---"
Viper.IO.File.WriteAllText(testFile, "Hello from Viper BASIC!")
PRINT "WriteAllText done"

' --- Exists ---
PRINT "--- Exists ---"
PRINT "Exists(testFile): "; Viper.IO.File.Exists(testFile)
PRINT "Exists('/nonexistent'): "; Viper.IO.File.Exists("/nonexistent_file_xyz")

' --- ReadAllText ---
PRINT "--- ReadAllText ---"
PRINT "ReadAllText: "; Viper.IO.File.ReadAllText(testFile)

' --- Size ---
PRINT "--- Size ---"
PRINT "Size: "; Viper.IO.File.Size(testFile)

' --- AppendLine ---
PRINT "--- AppendLine ---"
Viper.IO.File.AppendLine(testFile, "Line 2")
Viper.IO.File.AppendLine(testFile, "Line 3")
PRINT "AppendLine done"
PRINT "ReadAllText after append: "; Viper.IO.File.ReadAllText(testFile)

' --- ReadAllLines ---
PRINT "--- ReadAllLines ---"
DIM lines AS OBJECT
lines = Viper.IO.File.ReadAllLines(testFile)
PRINT "ReadAllLines returned (object)"

' --- Modified ---
PRINT "--- Modified ---"
PRINT "Modified (timestamp): "; Viper.IO.File.Modified(testFile)

' --- Copy ---
PRINT "--- Copy ---"
Viper.IO.File.Copy(testFile, copyFile)
PRINT "Copy done"
PRINT "Copy exists: "; Viper.IO.File.Exists(copyFile)

' --- Move ---
PRINT "--- Move ---"
Viper.IO.File.Move(copyFile, moveFile)
PRINT "Move done"
PRINT "Original exists: "; Viper.IO.File.Exists(copyFile)
PRINT "Moved exists: "; Viper.IO.File.Exists(moveFile)

' --- Touch ---
PRINT "--- Touch ---"
DIM touchFile AS STRING
touchFile = Viper.IO.Path.Join(tmpDir, "viper_touch_audit_bas.txt")
Viper.IO.File.Touch(touchFile)
PRINT "Touch done"
PRINT "Touched file exists: "; Viper.IO.File.Exists(touchFile)

' --- Delete (cleanup) ---
PRINT "--- Delete ---"
Viper.IO.File.Delete(testFile)
Viper.IO.File.Delete(moveFile)
Viper.IO.File.Delete(touchFile)
PRINT "Delete done"
PRINT "testFile exists: "; Viper.IO.File.Exists(testFile)

PRINT "=== File Demo Complete ==="
END
