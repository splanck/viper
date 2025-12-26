' Edge case testing for IO operations

DIM result AS STRING
DIM num AS INTEGER
DIM flag AS INTEGER

' === Non-existent file operations ===
PRINT "=== Non-existent File Tests ==="

flag = Viper.IO.File.Exists("/nonexistent/path/file.txt")
PRINT "Exists('/nonexistent/path/file.txt'): "; flag

num = Viper.IO.File.Size("/nonexistent/file.txt")
PRINT "Size('/nonexistent/file.txt'): "; num

flag = Viper.IO.Dir.Exists("/nonexistent/dir")
PRINT "Dir.Exists('/nonexistent/dir'): "; flag

' Read non-existent file
PRINT "ReadAllText('/nonexistent/file.txt')..."
' result = Viper.IO.File.ReadAllText("/nonexistent/file.txt")  ' May trap
PRINT ""

' === Empty file handling ===
PRINT "=== Empty File Tests ==="

DIM emptyFile AS STRING
emptyFile = "/tmp/viper_test_empty.txt"
Viper.IO.File.WriteAllText(emptyFile, "")
PRINT "Created empty file"
PRINT "Size: "; Viper.IO.File.Size(emptyFile)
result = Viper.IO.File.ReadAllText(emptyFile)
PRINT "ReadAllText length: "; Viper.String.Length(result)
Viper.IO.File.Delete(emptyFile)
PRINT ""

' === Path edge cases ===
PRINT "=== Path Edge Cases ==="

result = Viper.IO.Path.Name("")
PRINT "Path.Name(''): '"; result; "'"

result = Viper.IO.Path.Dir("")
PRINT "Path.Dir(''): '"; result; "'"

result = Viper.IO.Path.Ext("")
PRINT "Path.Ext(''): '"; result; "'"

result = Viper.IO.Path.Join("", "")
PRINT "Path.Join('', ''): '"; result; "'"

result = Viper.IO.Path.Join("/", "")
PRINT "Path.Join('/', ''): '"; result; "'"

result = Viper.IO.Path.Join("", "file.txt")
PRINT "Path.Join('', 'file.txt'): '"; result; "'"

flag = Viper.IO.Path.IsAbs("")
PRINT "Path.IsAbs(''): "; flag

flag = Viper.IO.Path.IsAbs("relative/path")
PRINT "Path.IsAbs('relative/path'): "; flag

flag = Viper.IO.Path.IsAbs("/absolute/path")
PRINT "Path.IsAbs('/absolute/path'): "; flag
PRINT ""

' === Special filenames ===
PRINT "=== Special Filenames ==="

DIM specialFile AS STRING
specialFile = "/tmp/viper test file with spaces.txt"
Viper.IO.File.WriteAllText(specialFile, "test")
PRINT "Created file with spaces: "; Viper.IO.File.Exists(specialFile)
Viper.IO.File.Delete(specialFile)

' File with unicode
specialFile = "/tmp/viper_файл_日本語.txt"
Viper.IO.File.WriteAllText(specialFile, "unicode content")
PRINT "Created file with unicode: "; Viper.IO.File.Exists(specialFile)
result = Viper.IO.File.ReadAllText(specialFile)
PRINT "Content: "; result
Viper.IO.File.Delete(specialFile)
PRINT ""

' === Large file test ===
PRINT "=== Large File Test ==="
DIM largeFile AS STRING
largeFile = "/tmp/viper_large_test.txt"
DIM largeContent AS STRING
largeContent = Viper.String.Repeat("x", 100000)
Viper.IO.File.WriteAllText(largeFile, largeContent)
PRINT "Wrote 100KB file"
PRINT "Size: "; Viper.IO.File.Size(largeFile)
result = Viper.IO.File.ReadAllText(largeFile)
PRINT "Read back length: "; Viper.String.Length(result)
Viper.IO.File.Delete(largeFile)
PRINT ""

' === Append operations ===
PRINT "=== Append Operations ==="
DIM appendFile AS STRING
appendFile = "/tmp/viper_append_test.txt"
Viper.IO.File.WriteAllText(appendFile, "Line1")
Viper.IO.File.AppendLine(appendFile, "Line2")
Viper.IO.File.Append(appendFile, "Line3")
result = Viper.IO.File.ReadAllText(appendFile)
PRINT "After appends: "; result
Viper.IO.File.Delete(appendFile)
PRINT ""

PRINT "=== IO Edge Case Tests Complete ==="
END
