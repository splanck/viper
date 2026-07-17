' Edge case testing for IO operations

DIM result AS STRING
DIM num AS INTEGER
DIM flag AS INTEGER

' === Non-existent file operations ===
PRINT "=== Non-existent File Tests ==="

flag = Zanna.IO.File.Exists("/nonexistent/path/file.txt")
PRINT "Exists('/nonexistent/path/file.txt'): "; flag

num = Zanna.IO.File.SizeBytes("/nonexistent/file.txt")
PRINT "Size('/nonexistent/file.txt'): "; num

flag = Zanna.IO.Dir.Exists("/nonexistent/dir")
PRINT "Dir.Exists('/nonexistent/dir'): "; flag

' Read non-existent file
PRINT "ReadAllText('/nonexistent/file.txt')..."
' result = Zanna.IO.File.ReadAllText("/nonexistent/file.txt")  ' May trap
PRINT ""

' === Empty file handling ===
PRINT "=== Empty File Tests ==="

DIM emptyFile AS STRING
emptyFile = "/tmp/zanna_test_empty.txt"
Zanna.IO.File.WriteAllText(emptyFile, "")
PRINT "Created empty file"
PRINT "Size: "; Zanna.IO.File.SizeBytes(emptyFile)
result = Zanna.IO.File.ReadAllText(emptyFile)
PRINT "ReadAllText length: "; Zanna.String.get_Length(result)
Zanna.IO.File.Delete(emptyFile)
PRINT ""

' === Path edge cases ===
PRINT "=== Path Edge Cases ==="

result = Zanna.IO.Path.Name("")
PRINT "Path.Name(''): '"; result; "'"

result = Zanna.IO.Path.Directory("")
PRINT "Path.Directory(''): '"; result; "'"

result = Zanna.IO.Path.Extension("")
PRINT "Path.Extension(''): '"; result; "'"

result = Zanna.IO.Path.Join("", "")
PRINT "Path.Join('', ''): '"; result; "'"

result = Zanna.IO.Path.Join("/", "")
PRINT "Path.Join('/', ''): '"; result; "'"

result = Zanna.IO.Path.Join("", "file.txt")
PRINT "Path.Join('', 'file.txt'): '"; result; "'"

flag = Zanna.IO.Path.IsAbsolute("")
PRINT "Path.IsAbsolute(''): "; flag

flag = Zanna.IO.Path.IsAbsolute("relative/path")
PRINT "Path.IsAbsolute('relative/path'): "; flag

flag = Zanna.IO.Path.IsAbsolute("/absolute/path")
PRINT "Path.IsAbsolute('/absolute/path'): "; flag
PRINT ""

' === Special filenames ===
PRINT "=== Special Filenames ==="

DIM specialFile AS STRING
specialFile = "/tmp/zanna test file with spaces.txt"
Zanna.IO.File.WriteAllText(specialFile, "test")
PRINT "Created file with spaces: "; Zanna.IO.File.Exists(specialFile)
Zanna.IO.File.Delete(specialFile)

' File with unicode
specialFile = "/tmp/zanna_файл_日本語.txt"
Zanna.IO.File.WriteAllText(specialFile, "unicode content")
PRINT "Created file with unicode: "; Zanna.IO.File.Exists(specialFile)
result = Zanna.IO.File.ReadAllText(specialFile)
PRINT "Content: "; result
Zanna.IO.File.Delete(specialFile)
PRINT ""

' === Large file test ===
PRINT "=== Large File Test ==="
DIM largeFile AS STRING
largeFile = "/tmp/zanna_large_test.txt"
DIM largeContent AS STRING
largeContent = Zanna.String.Repeat("x", 100000)
Zanna.IO.File.WriteAllText(largeFile, largeContent)
PRINT "Wrote 100KB file"
PRINT "Size: "; Zanna.IO.File.SizeBytes(largeFile)
result = Zanna.IO.File.ReadAllText(largeFile)
PRINT "Read back length: "; Zanna.String.get_Length(result)
Zanna.IO.File.Delete(largeFile)
PRINT ""

' === Append operations ===
PRINT "=== Append Operations ==="
DIM appendFile AS STRING
appendFile = "/tmp/zanna_append_test.txt"
Zanna.IO.File.WriteAllText(appendFile, "Line1")
Zanna.IO.File.AppendLine(appendFile, "Line2")
Zanna.IO.File.Append(appendFile, "Line3")
result = Zanna.IO.File.ReadAllText(appendFile)
PRINT "After appends: "; result
Zanna.IO.File.Delete(appendFile)
PRINT ""

PRINT "=== IO Edge Case Tests Complete ==="
END
