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
PRINT "Dir: "; Viper.IO.Path.Dir(testPath)
PRINT "Dir('file.txt'): "; Viper.IO.Path.Dir("file.txt")

' --- Ext ---
PRINT "--- Ext ---"
PRINT "Ext: "; Viper.IO.Path.Ext(testPath)
PRINT "Ext('noext'): "; Viper.IO.Path.Ext("noext")

' --- IsAbs ---
PRINT "--- IsAbs ---"
PRINT "IsAbs('/home/user'): "; Viper.IO.Path.IsAbs("/home/user")
PRINT "IsAbs('relative'): "; Viper.IO.Path.IsAbs("relative/path")

' --- Join ---
PRINT "--- Join ---"
PRINT "Join('/home', 'user'): "; Viper.IO.Path.Join("/home", "user")
PRINT "Join('a', 'b'): "; Viper.IO.Path.Join("a", "b")

' --- Abs ---
PRINT "--- Abs ---"
PRINT "Abs('.'): "; Viper.IO.Path.Abs(".")

' --- Norm ---
PRINT "--- Norm ---"
PRINT "Norm('/home/user/../user/./docs'): "; Viper.IO.Path.Norm("/home/user/../user/./docs")
PRINT "Norm('/a/b/c/../../d'): "; Viper.IO.Path.Norm("/a/b/c/../../d")

' --- Sep ---
PRINT "--- Sep ---"
PRINT "Sep: "; Viper.IO.Path.Sep()

' --- WithExt ---
PRINT "--- WithExt ---"
PRINT "WithExt('file.txt', '.md'): "; Viper.IO.Path.WithExt("file.txt", ".md")
PRINT "WithExt('noext', '.txt'): "; Viper.IO.Path.WithExt("noext", ".txt")

PRINT "=== Path Demo Complete ==="
END
