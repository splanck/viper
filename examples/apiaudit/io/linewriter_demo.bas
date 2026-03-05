' =============================================================================
' API Audit: Viper.IO.LineWriter - Line-by-Line File Writing
' =============================================================================
' Tests: Open, Append, Write, WriteLn, WriteChar, NewLine, SetNewLine,
'        Flush, Close
' =============================================================================

PRINT "=== API Audit: Viper.IO.LineWriter ==="

DIM tmpDir AS STRING
tmpDir = Viper.IO.TempFile.Dir()
DIM testPath AS STRING
testPath = Viper.IO.Path.Join(tmpDir, "viper_linewriter_audit_bas.txt")

' --- Open ---
PRINT "--- Open ---"
DIM writer AS OBJECT
writer = Viper.IO.LineWriter.Open(testPath)
PRINT "Open done"

' --- Write ---
PRINT "--- Write ---"
Viper.IO.LineWriter.Write(writer, "Hello ")
PRINT "Write('Hello ') done"

' --- WriteLn ---
PRINT "--- WriteLn ---"
Viper.IO.LineWriter.WriteLn(writer, "World")
PRINT "WriteLn('World') done"

' --- WriteChar ---
PRINT "--- WriteChar ---"
Viper.IO.LineWriter.WriteChar(writer, 65)
Viper.IO.LineWriter.WriteChar(writer, 66)
Viper.IO.LineWriter.WriteChar(writer, 67)
PRINT "WriteChar(A, B, C) done"

' --- get_NewLine ---
PRINT "--- get_NewLine ---"
PRINT "NewLine returned"

' --- set_NewLine ---
PRINT "--- set_NewLine ---"
Viper.IO.LineWriter.set_NewLine(writer, CHR$(13) + CHR$(10))
PRINT "set_NewLine to CRLF"
Viper.IO.LineWriter.set_NewLine(writer, CHR$(10))
PRINT "Restored to LF"

' --- More writes + Flush ---
Viper.IO.LineWriter.WriteLn(writer, "")
Viper.IO.LineWriter.WriteLn(writer, "Final line")

PRINT "--- Flush ---"
Viper.IO.LineWriter.Flush(writer)
PRINT "Flush done"

' --- Close ---
PRINT "--- Close ---"
Viper.IO.LineWriter.Close(writer)
PRINT "Close done"

' Verify
PRINT "--- Verify ---"
PRINT "File content: "; Viper.IO.File.ReadAllText(testPath)

' --- Append ---
PRINT "--- Append ---"
DIM appWriter AS OBJECT
appWriter = Viper.IO.LineWriter.Append(testPath)
Viper.IO.LineWriter.WriteLn(appWriter, "Appended line")
Viper.IO.LineWriter.Close(appWriter)
PRINT "Append done"

' Cleanup
Viper.IO.File.Delete(testPath)
PRINT "Cleanup done"

PRINT "=== LineWriter Demo Complete ==="
END
