' =============================================================================
' API Audit: Viper.IO.Glob - File Pattern Matching
' =============================================================================
' Tests: Match, Files
' =============================================================================

PRINT "=== API Audit: Viper.IO.Glob ==="

' --- Match ---
PRINT "--- Match ---"
PRINT "Match('*.txt', 'file.txt'): "; Viper.IO.Glob.Match("*.txt", "file.txt")
PRINT "Match('*.txt', 'file.bas'): "; Viper.IO.Glob.Match("*.txt", "file.bas")
PRINT "Match('*.zia', 'demo.zia'): "; Viper.IO.Glob.Match("*.zia", "demo.zia")
PRINT "Match('test*', 'test_file'): "; Viper.IO.Glob.Match("test*", "test_file")
PRINT "Match('test*', 'other_file'): "; Viper.IO.Glob.Match("test*", "other_file")
PRINT "Match('?at', 'cat'): "; Viper.IO.Glob.Match("?at", "cat")
PRINT "Match('?at', 'boat'): "; Viper.IO.Glob.Match("?at", "boat")

' --- Files ---
PRINT "--- Files ---"
DIM cwd AS STRING
cwd = Viper.IO.Dir.Current()
PRINT "Searching in: "; cwd
DIM files AS OBJECT
files = Viper.IO.Glob.Files(cwd, "*.bas")
PRINT "Files('*.bas') returned"

PRINT "=== Glob Demo Complete ==="
END
