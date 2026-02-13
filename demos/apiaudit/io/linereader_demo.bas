' =============================================================================
' API Audit: Viper.IO.LineReader - Line-by-Line File Reading
' =============================================================================
' Tests: Open, Read, ReadAll, Eof, Close
' =============================================================================

PRINT "=== API Audit: Viper.IO.LineReader ==="

' Create temp file
DIM tmpDir AS STRING
tmpDir = Viper.IO.TempFile.Dir()
DIM testPath AS STRING
testPath = Viper.IO.Path.Join(tmpDir, "viper_linereader_audit_bas.txt")
Viper.IO.File.WriteAllText(testPath, "Line one" + CHR$(10) + "Line two" + CHR$(10) + "Line three" + CHR$(10))
PRINT "Created test file with 3 lines"

' --- Open ---
PRINT "--- Open ---"
DIM reader AS OBJECT
reader = Viper.IO.LineReader.Open(testPath)
PRINT "Open done"

' --- Eof (initial) ---
PRINT "--- Eof (initial) ---"
PRINT "Eof: "; Viper.IO.LineReader.get_Eof(reader)

' --- Read ---
PRINT "--- Read ---"
PRINT "Line 1: "; Viper.IO.LineReader.Read(reader)
PRINT "Line 2: "; Viper.IO.LineReader.Read(reader)
PRINT "Line 3: "; Viper.IO.LineReader.Read(reader)

' --- Eof (at end) ---
PRINT "--- Eof (at end) ---"
PRINT "Eof: "; Viper.IO.LineReader.get_Eof(reader)

' --- Close ---
PRINT "--- Close ---"
Viper.IO.LineReader.Close(reader)
PRINT "Close done"

' --- ReadAll ---
PRINT "--- ReadAll ---"
DIM reader2 AS OBJECT
reader2 = Viper.IO.LineReader.Open(testPath)
PRINT "ReadAll: "; Viper.IO.LineReader.ReadAll(reader2)
Viper.IO.LineReader.Close(reader2)

' Cleanup
Viper.IO.File.Delete(testPath)
PRINT "Cleanup done"

PRINT "=== LineReader Demo Complete ==="
END
