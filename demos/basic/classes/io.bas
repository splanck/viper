' Viper.IO Demo - File and Path Utilities
' This demo showcases file operations, path manipulation, and directory access

' === Path Operations ===
PRINT "=== Path Operations ==="
DIM testPath AS STRING
testPath = "/Users/demo/documents/file.txt"
PRINT "Path: "; testPath
PRINT "Name: "; Viper.IO.Path.Name(testPath)
PRINT "Stem: "; Viper.IO.Path.Stem(testPath)
PRINT "Dir: "; Viper.IO.Path.Dir(testPath)
PRINT "Ext: "; Viper.IO.Path.Ext(testPath)
PRINT "IsAbs: "; Viper.IO.Path.IsAbs(testPath)
PRINT "Join('a', 'b'): "; Viper.IO.Path.Join("a", "b")
PRINT "Sep: "; Viper.IO.Path.Sep()
PRINT

' === File Existence ===
PRINT "=== File Existence ==="
PRINT "File.Exists('/tmp'): "; Viper.IO.File.Exists("/tmp")
PRINT "File.Exists('/nonexistent'): "; Viper.IO.File.Exists("/nonexistent")
PRINT "Dir.Exists('/tmp'): "; Viper.IO.Dir.Exists("/tmp")
PRINT

' === File Read/Write ===
PRINT "=== File Read/Write ==="
DIM testFile AS STRING
testFile = "/tmp/viper_demo_test.txt"
Viper.IO.File.WriteAllText(testFile, "Hello from Viper!")
PRINT "Wrote to: "; testFile
DIM content AS STRING
content = Viper.IO.File.ReadAllText(testFile)
PRINT "Read back: "; content
PRINT "Size: "; Viper.IO.File.Size(testFile); " bytes"
Viper.IO.File.Delete(testFile)
PRINT "Deleted test file"
PRINT

' === Directory Operations ===
PRINT "=== Directory Operations ==="
DIM tempDir AS STRING
tempDir = "/tmp/viper_demo_dir"
Viper.IO.Dir.Make(tempDir)
PRINT "Created: "; tempDir
PRINT "Exists: "; Viper.IO.Dir.Exists(tempDir)
Viper.IO.Dir.Remove(tempDir)
PRINT "Removed: "; tempDir
PRINT

' === Current Directory ===
PRINT "=== Current Directory ==="
PRINT "Dir.Current: "; Viper.IO.Dir.Current()

END
