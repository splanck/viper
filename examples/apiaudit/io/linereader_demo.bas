' =============================================================================
' API Audit: Zanna.IO.LineReader - Line-by-Line File Reading
' =============================================================================
' Tests: Open, Read, ReadAll, Eof, Close
' =============================================================================

PRINT "=== API Audit: Zanna.IO.LineReader ==="

' Create temp file
DIM tmpDir AS STRING
tmpDir = Zanna.IO.TempFile.Dir()
DIM testPath AS STRING
testPath = Zanna.IO.Path.Join(tmpDir, "zanna_linereader_audit_bas.txt")
Zanna.IO.File.WriteAllText(testPath, "Line one" + CHR$(10) + "Line two" + CHR$(10) + "Line three" + CHR$(10))
PRINT "Created test file with 3 lines"

' --- Open ---
PRINT "--- Open ---"
DIM reader AS OBJECT
reader = Zanna.IO.LineReader.Open(testPath)
PRINT "Open done"

' --- Eof (initial) ---
PRINT "--- Eof (initial) ---"
PRINT "Eof: "; Zanna.IO.LineReader.get_Eof(reader)

' --- Read ---
PRINT "--- Read ---"
PRINT "Line 1: "; Zanna.IO.LineReader.Read(reader)
PRINT "Line 2: "; Zanna.IO.LineReader.Read(reader)
PRINT "Line 3: "; Zanna.IO.LineReader.Read(reader)

' --- Eof (at end) ---
PRINT "--- Eof (at end) ---"
PRINT "Eof: "; Zanna.IO.LineReader.get_Eof(reader)

' --- Close ---
PRINT "--- Close ---"
Zanna.IO.LineReader.Close(reader)
PRINT "Close done"

' --- ReadAll ---
PRINT "--- ReadAll ---"
DIM reader2 AS OBJECT
reader2 = Zanna.IO.LineReader.Open(testPath)
PRINT "ReadAll: "; Zanna.IO.LineReader.ReadAll(reader2)
Zanna.IO.LineReader.Close(reader2)

' Cleanup
Zanna.IO.File.Delete(testPath)
PRINT "Cleanup done"

PRINT "=== LineReader Demo Complete ==="
END
