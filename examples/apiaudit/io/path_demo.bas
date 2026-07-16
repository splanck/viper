' =============================================================================
' API Audit: Viper.IO.Path - Path Manipulation
' =============================================================================
' Tests: Abs, Dir, Ext, Name, Stem, IsAbs, Join, Norm, Sep, WithExt
' =============================================================================

PRINT "=== API Audit: Viper.IO.Path ==="

DIM testPath AS STRING
testPath = "/home/user/documents/file.txt"

' --- Name ---
PRINT "--- Name ---"
PRINT "Name: "; Viper.IO.Path.Name(testPath)
PRINT "Name('test.tar.gz'): "; Viper.IO.Path.Name("test.tar.gz")

' --- Stem ---
PRINT "--- Stem ---"
PRINT "Stem: "; Viper.IO.Path.Stem(testPath)
PRINT "Stem('archive.tar.gz'): "; Viper.IO.Path.Stem("archive.tar.gz")

' --- Dir ---
PRINT "--- Dir ---"
PRINT "Dir: "; Viper.IO.Path.Directory(testPath)
PRINT "Dir('file.txt'): "; Viper.IO.Path.Directory("file.txt")

' --- Ext ---
PRINT "--- Ext ---"
PRINT "Ext: "; Viper.IO.Path.Extension(testPath)
PRINT "Ext('noext'): "; Viper.IO.Path.Extension("noext")

' --- IsAbs ---
PRINT "--- IsAbs ---"
PRINT "IsAbs('/home/user'): "; Viper.IO.Path.IsAbsolute("/home/user")
PRINT "IsAbs('relative'): "; Viper.IO.Path.IsAbsolute("relative/path")

' --- Join ---
PRINT "--- Join ---"
PRINT "Join('/home', 'user'): "; Viper.IO.Path.Join("/home", "user")
PRINT "Join('a', 'b'): "; Viper.IO.Path.Join("a", "b")

' --- Abs ---
PRINT "--- Abs ---"
PRINT "Abs('.'): "; Viper.IO.Path.Absolute(".")

' --- Norm ---
PRINT "--- Norm ---"
PRINT "Norm('/home/user/../user/./docs'): "; Viper.IO.Path.Normalize("/home/user/../user/./docs")
PRINT "Norm('/a/b/c/../../d'): "; Viper.IO.Path.Normalize("/a/b/c/../../d")

' --- Sep ---
PRINT "--- Sep ---"
PRINT "Sep: "; Viper.IO.Path.Separator()

' --- WithExt ---
PRINT "--- WithExt ---"
PRINT "WithExt('file.txt', '.md'): "; Viper.IO.Path.WithExtension("file.txt", ".md")
PRINT "WithExt('noext', '.txt'): "; Viper.IO.Path.WithExtension("noext", ".txt")

PRINT "=== Path Demo Complete ==="
END
