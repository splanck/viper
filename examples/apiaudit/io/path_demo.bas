' =============================================================================
' API Audit: Zanna.IO.Path - Path Manipulation
' =============================================================================
' Tests: Abs, Dir, Ext, Name, Stem, IsAbs, Join, Norm, Sep, WithExt
' =============================================================================

PRINT "=== API Audit: Zanna.IO.Path ==="

DIM testPath AS STRING
testPath = "/home/user/documents/file.txt"

' --- Name ---
PRINT "--- Name ---"
PRINT "Name: "; Zanna.IO.Path.Name(testPath)
PRINT "Name('test.tar.gz'): "; Zanna.IO.Path.Name("test.tar.gz")

' --- Stem ---
PRINT "--- Stem ---"
PRINT "Stem: "; Zanna.IO.Path.Stem(testPath)
PRINT "Stem('archive.tar.gz'): "; Zanna.IO.Path.Stem("archive.tar.gz")

' --- Dir ---
PRINT "--- Dir ---"
PRINT "Dir: "; Zanna.IO.Path.Directory(testPath)
PRINT "Dir('file.txt'): "; Zanna.IO.Path.Directory("file.txt")

' --- Ext ---
PRINT "--- Ext ---"
PRINT "Ext: "; Zanna.IO.Path.Extension(testPath)
PRINT "Ext('noext'): "; Zanna.IO.Path.Extension("noext")

' --- IsAbs ---
PRINT "--- IsAbs ---"
PRINT "IsAbs('/home/user'): "; Zanna.IO.Path.IsAbsolute("/home/user")
PRINT "IsAbs('relative'): "; Zanna.IO.Path.IsAbsolute("relative/path")

' --- Join ---
PRINT "--- Join ---"
PRINT "Join('/home', 'user'): "; Zanna.IO.Path.Join("/home", "user")
PRINT "Join('a', 'b'): "; Zanna.IO.Path.Join("a", "b")

' --- Abs ---
PRINT "--- Abs ---"
PRINT "Abs('.'): "; Zanna.IO.Path.Absolute(".")

' --- Norm ---
PRINT "--- Norm ---"
PRINT "Norm('/home/user/../user/./docs'): "; Zanna.IO.Path.Normalize("/home/user/../user/./docs")
PRINT "Norm('/a/b/c/../../d'): "; Zanna.IO.Path.Normalize("/a/b/c/../../d")

' --- Sep ---
PRINT "--- Sep ---"
PRINT "Sep: "; Zanna.IO.Path.Separator()

' --- WithExt ---
PRINT "--- WithExt ---"
PRINT "WithExt('file.txt', '.md'): "; Zanna.IO.Path.WithExtension("file.txt", ".md")
PRINT "WithExt('noext', '.txt'): "; Zanna.IO.Path.WithExtension("noext", ".txt")

PRINT "=== Path Demo Complete ==="
END
