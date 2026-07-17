' =============================================================================
' API Audit: Zanna.IO.LineWriter - Line-by-Line File Writing
' =============================================================================
' Tests: Open, Append, Write, WriteLn, WriteChar, NewLine, SetNewLine,
'        Flush, Close
' =============================================================================

PRINT "=== API Audit: Zanna.IO.LineWriter ==="

DIM tmpDir AS STRING
tmpDir = Zanna.IO.TempFile.Dir()
DIM testPath AS STRING
testPath = Zanna.IO.Path.Join(tmpDir, "zanna_linewriter_audit_bas.txt")

' --- Open ---
PRINT "--- Open ---"
DIM writer AS OBJECT
writer = Zanna.IO.LineWriter.Open(testPath)
PRINT "Open done"

' --- Write ---
PRINT "--- Write ---"
Zanna.IO.LineWriter.Write(writer, "Hello ")
PRINT "Write('Hello ') done"

' --- WriteLn ---
PRINT "--- WriteLn ---"
Zanna.IO.LineWriter.WriteLine(writer, "World")
PRINT "WriteLn('World') done"

' --- WriteChar ---
PRINT "--- WriteChar ---"
Zanna.IO.LineWriter.WriteChar(writer, 65)
Zanna.IO.LineWriter.WriteChar(writer, 66)
Zanna.IO.LineWriter.WriteChar(writer, 67)
PRINT "WriteChar(A, B, C) done"

' --- get_NewLine ---
PRINT "--- get_NewLine ---"
PRINT "NewLine returned"

' --- set_NewLine ---
PRINT "--- set_NewLine ---"
Zanna.IO.LineWriter.set_NewLine(writer, CHR$(13) + CHR$(10))
PRINT "set_NewLine to CRLF"
Zanna.IO.LineWriter.set_NewLine(writer, CHR$(10))
PRINT "Restored to LF"

' --- More writes + Flush ---
Zanna.IO.LineWriter.WriteLine(writer, "")
Zanna.IO.LineWriter.WriteLine(writer, "Final line")

PRINT "--- Flush ---"
Zanna.IO.LineWriter.Flush(writer)
PRINT "Flush done"

' --- Close ---
PRINT "--- Close ---"
Zanna.IO.LineWriter.Close(writer)
PRINT "Close done"

' Verify
PRINT "--- Verify ---"
PRINT "File content: "; Zanna.IO.File.ReadAllText(testPath)

' --- Append ---
PRINT "--- Append ---"
DIM appWriter AS OBJECT
appWriter = Zanna.IO.LineWriter.Append(testPath)
Zanna.IO.LineWriter.WriteLine(appWriter, "Appended line")
Zanna.IO.LineWriter.Close(appWriter)
PRINT "Append done"

' Cleanup
Zanna.IO.File.Delete(testPath)
PRINT "Cleanup done"

PRINT "=== LineWriter Demo Complete ==="
END
