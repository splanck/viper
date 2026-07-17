' =============================================================================
' API Audit: Zanna.IO.Glob - File Pattern Matching
' =============================================================================
' Tests: Match, Files
' =============================================================================

PRINT "=== API Audit: Zanna.IO.Glob ==="

' --- Match ---
PRINT "--- Match ---"
PRINT "Match('*.txt', 'file.txt'): "; Zanna.IO.Glob.Match("*.txt", "file.txt")
PRINT "Match('*.txt', 'file.bas'): "; Zanna.IO.Glob.Match("*.txt", "file.bas")
PRINT "Match('*.zia', 'demo.zia'): "; Zanna.IO.Glob.Match("*.zia", "demo.zia")
PRINT "Match('test*', 'test_file'): "; Zanna.IO.Glob.Match("test*", "test_file")
PRINT "Match('test*', 'other_file'): "; Zanna.IO.Glob.Match("test*", "other_file")
PRINT "Match('?at', 'cat'): "; Zanna.IO.Glob.Match("?at", "cat")
PRINT "Match('?at', 'boat'): "; Zanna.IO.Glob.Match("?at", "boat")

' --- Files ---
PRINT "--- Files ---"
DIM cwd AS STRING
cwd = Zanna.IO.Dir.Current()
PRINT "Searching in: "; cwd
DIM files AS OBJECT
files = Zanna.IO.Glob.Files(cwd, "*.bas")
PRINT "Files('*.bas') returned"

PRINT "=== Glob Demo Complete ==="
END
