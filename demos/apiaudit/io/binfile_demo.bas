' =============================================================================
' API Audit: Viper.IO.BinFile - Binary File I/O
' =============================================================================
' Tests: Open, Close, ReadByte, WriteByte, Read, Write, Seek, Pos, Size, Eof, Flush
' =============================================================================

PRINT "=== API Audit: Viper.IO.BinFile ==="

DIM tmpDir AS STRING
tmpDir = Viper.IO.TempFile.Dir()
DIM testPath AS STRING
testPath = Viper.IO.Path.Join(tmpDir, "viper_binfile_audit_bas.bin")

' --- Open (write) + WriteByte ---
PRINT "--- Open (write) + WriteByte ---"
DIM wf AS OBJECT
wf = Viper.IO.BinFile.Open(testPath, "w")
Viper.IO.BinFile.WriteByte(wf, 65)
Viper.IO.BinFile.WriteByte(wf, 66)
Viper.IO.BinFile.WriteByte(wf, 67)
PRINT "Wrote 3 bytes: A B C"

' --- Pos ---
PRINT "--- Pos ---"
PRINT "Pos after write: "; Viper.IO.BinFile.get_Pos(wf)

' --- Flush ---
PRINT "--- Flush ---"
Viper.IO.BinFile.Flush(wf)
PRINT "Flush done"

' --- Close ---
PRINT "--- Close ---"
Viper.IO.BinFile.Close(wf)
PRINT "Close done"

' --- Open (read) ---
PRINT "--- Open (read) ---"
DIM rf AS OBJECT
rf = Viper.IO.BinFile.Open(testPath, "r")

' --- Size ---
PRINT "--- Size ---"
PRINT "Size: "; Viper.IO.BinFile.get_Size(rf)

' --- ReadByte ---
PRINT "--- ReadByte ---"
PRINT "ReadByte 1: "; Viper.IO.BinFile.ReadByte(rf)
PRINT "ReadByte 2: "; Viper.IO.BinFile.ReadByte(rf)
PRINT "ReadByte 3: "; Viper.IO.BinFile.ReadByte(rf)

' --- Seek ---
PRINT "--- Seek ---"
Viper.IO.BinFile.Seek(rf, 0, 0)
PRINT "Seek to 0, ReadByte: "; Viper.IO.BinFile.ReadByte(rf)

' --- Eof ---
PRINT "--- Eof ---"
Viper.IO.BinFile.Seek(rf, 0, 2)
PRINT "Eof at end: "; Viper.IO.BinFile.get_Eof(rf)

Viper.IO.BinFile.Close(rf)

' Cleanup
Viper.IO.File.Delete(testPath)
PRINT "Cleanup done"

PRINT "=== BinFile Demo Complete ==="
END
