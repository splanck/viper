' =============================================================================
' API Audit: Zanna.IO.File - File Operations
' =============================================================================
' Tests: Exists, WriteAllText, ReadAllText, AppendLine, ReadAllLines, Size,
'        Delete, Copy, Move, Touch, Modified
' =============================================================================

PRINT "=== API Audit: Zanna.IO.File ==="

DIM tmpDir AS STRING
tmpDir = Zanna.IO.TempFile.Dir()
DIM testFile AS STRING
testFile = Zanna.IO.Path.Join(tmpDir, "zanna_file_audit_bas.txt")
DIM copyFile AS STRING
copyFile = Zanna.IO.Path.Join(tmpDir, "zanna_file_audit_bas_copy.txt")
DIM moveFile AS STRING
moveFile = Zanna.IO.Path.Join(tmpDir, "zanna_file_audit_bas_moved.txt")

' --- WriteAllText ---
PRINT "--- WriteAllText ---"
Zanna.IO.File.WriteAllText(testFile, "Hello from Zanna BASIC!")
PRINT "WriteAllText done"

' --- Exists ---
PRINT "--- Exists ---"
PRINT "Exists(testFile): "; Zanna.IO.File.Exists(testFile)
PRINT "Exists('/nonexistent'): "; Zanna.IO.File.Exists("/nonexistent_file_xyz")

' --- ReadAllText ---
PRINT "--- ReadAllText ---"
PRINT "ReadAllText: "; Zanna.IO.File.ReadAllText(testFile)

' --- Size ---
PRINT "--- Size ---"
PRINT "Size: "; Zanna.IO.File.SizeBytes(testFile)

' --- AppendLine ---
PRINT "--- AppendLine ---"
Zanna.IO.File.AppendLine(testFile, "Line 2")
Zanna.IO.File.AppendLine(testFile, "Line 3")
PRINT "AppendLine done"
PRINT "ReadAllText after append: "; Zanna.IO.File.ReadAllText(testFile)

' --- ReadAllLines ---
PRINT "--- ReadAllLines ---"
DIM lines AS OBJECT
lines = Zanna.IO.File.ReadAllLines(testFile)
PRINT "ReadAllLines returned (object)"

' --- Modified ---
PRINT "--- Modified ---"
PRINT "Modified (timestamp): "; Zanna.IO.File.Modified(testFile)

' --- Copy ---
PRINT "--- Copy ---"
Zanna.IO.File.Copy(testFile, copyFile)
PRINT "Copy done"
PRINT "Copy exists: "; Zanna.IO.File.Exists(copyFile)

' --- Move ---
PRINT "--- Move ---"
Zanna.IO.File.Move(copyFile, moveFile)
PRINT "Move done"
PRINT "Original exists: "; Zanna.IO.File.Exists(copyFile)
PRINT "Moved exists: "; Zanna.IO.File.Exists(moveFile)

' --- Touch ---
PRINT "--- Touch ---"
DIM touchFile AS STRING
touchFile = Zanna.IO.Path.Join(tmpDir, "zanna_touch_audit_bas.txt")
Zanna.IO.File.Touch(touchFile)
PRINT "Touch done"
PRINT "Touched file exists: "; Zanna.IO.File.Exists(touchFile)

' --- Delete (cleanup) ---
PRINT "--- Delete ---"
Zanna.IO.File.Delete(testFile)
Zanna.IO.File.Delete(moveFile)
Zanna.IO.File.Delete(touchFile)
PRINT "Delete done"
PRINT "testFile exists: "; Zanna.IO.File.Exists(testFile)

PRINT "=== File Demo Complete ==="
END
