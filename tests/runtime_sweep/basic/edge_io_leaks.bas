' Resource leak testing for IO operations
' Opens/closes many handles to detect leaks

DIM i AS INTEGER
DIM testFile AS STRING
testFile = "/tmp/viper_leak_test.txt"

PRINT "=== IO Resource Leak Test ==="

' === File handle leak test ===
PRINT "Testing file handle leaks..."
CONST FILE_ITERS AS INTEGER = 1000

' Create test file
Viper.IO.File.WriteAllText(testFile, "test content for leak checking")

FOR i = 1 TO FILE_ITERS
    DIM content AS STRING
    content = Viper.IO.File.ReadAllText(testFile)
    IF i MOD 200 = 0 THEN
        PRINT "  Read iteration "; i
    END IF
NEXT i
PRINT "Completed "; FILE_ITERS; " file reads"

' Write many times
FOR i = 1 TO FILE_ITERS
    Viper.IO.File.WriteAllText(testFile, "iteration " + Viper.Fmt.Int(i))
    IF i MOD 200 = 0 THEN
        PRINT "  Write iteration "; i
    END IF
NEXT i
PRINT "Completed "; FILE_ITERS; " file writes"

Viper.IO.File.Delete(testFile)
PRINT "Cleaned up test file"
PRINT ""

' === StringBuilder leak test ===
PRINT "Testing StringBuilder resource usage..."
CONST SB_ITERS AS INTEGER = 100

FOR i = 1 TO SB_ITERS
    DIM sb AS Viper.Text.StringBuilder
    sb = Viper.Text.StringBuilder.New()
    sb.Append(Viper.String.Repeat("x", 10000))
    sb.Clear()
    sb.Append("short")
    DIM s AS STRING
    s = sb.ToString()
NEXT i
PRINT "Completed "; SB_ITERS; " StringBuilder cycles"
PRINT ""

' === Seq/List/Map creation leak test ===
PRINT "Testing collection allocation..."
CONST COLL_ITERS AS INTEGER = 500

FOR i = 1 TO COLL_ITERS
    DIM seq AS Viper.Collections.Seq
    seq = Viper.Collections.Seq.New()
    seq.Push("item1")
    seq.Push("item2")
    seq.Push("item3")
    seq.Clear()

    DIM map AS Viper.Collections.Map
    map = Viper.Collections.Map.New()
    map.Set("key1", "value1")
    map.Set("key2", "value2")
    map.Clear()

    IF i MOD 100 = 0 THEN
        PRINT "  Collection iteration "; i
    END IF
NEXT i
PRINT "Completed "; COLL_ITERS; " collection cycles"
PRINT ""

' === BinFile read/write leak test ===
PRINT "Testing BinFile handles..."
DIM binFile AS STRING
binFile = "/tmp/viper_bin_leak.bin"
CONST BIN_ITERS AS INTEGER = 200

FOR i = 1 TO BIN_ITERS
    DIM bf AS Viper.IO.BinFile
    bf = Viper.IO.BinFile.Open(binFile, "w")
    bf.WriteByte(65)
    bf.WriteByte(66)
    bf.Close()

    bf = Viper.IO.BinFile.Open(binFile, "r")
    DIM b1 AS INTEGER
    DIM b2 AS INTEGER
    b1 = bf.ReadByte()
    b2 = bf.ReadByte()
    bf.Close()

    IF i MOD 50 = 0 THEN
        PRINT "  BinFile iteration "; i
    END IF
NEXT i
Viper.IO.File.Delete(binFile)
PRINT "Completed "; BIN_ITERS; " BinFile cycles"
PRINT ""

' === LineReader/LineWriter leak test ===
PRINT "Testing LineReader/LineWriter handles..."
DIM lineFile AS STRING
lineFile = "/tmp/viper_line_leak.txt"
CONST LINE_ITERS AS INTEGER = 200

FOR i = 1 TO LINE_ITERS
    DIM lw AS Viper.IO.LineWriter
    lw = Viper.IO.LineWriter.Open(lineFile)
    lw.WriteLn("Line 1")
    lw.WriteLn("Line 2")
    lw.Close()

    DIM lr AS Viper.IO.LineReader
    lr = Viper.IO.LineReader.Open(lineFile)
    DIM line1 AS STRING
    DIM line2 AS STRING
    line1 = lr.Read()
    line2 = lr.Read()
    lr.Close()

    IF i MOD 50 = 0 THEN
        PRINT "  LineReader/Writer iteration "; i
    END IF
NEXT i
Viper.IO.File.Delete(lineFile)
PRINT "Completed "; LINE_ITERS; " LineReader/Writer cycles"
PRINT ""

PRINT "=== IO Resource Leak Test Complete ==="
PRINT "If no crashes or memory errors occurred, resource management is likely correct."
END
