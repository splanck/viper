' =============================================================================
' API Audit: Viper.System.Exec - Process Execution
' =============================================================================
' Tests: Run, Capture, Shell, ShellCapture, ShellResult
' NOTE: Use safe commands only
' =============================================================================

PRINT "=== API Audit: Viper.System.Exec ==="

' --- Run ---
PRINT "--- Run ---"
DIM rc AS INTEGER
rc = Viper.System.Exec.Run("echo hello")
PRINT "Run('echo hello') exit code: "; rc

' --- Capture ---
PRINT "--- Capture ---"
DIM captured AS STRING
captured = Viper.System.Exec.Capture("echo hello")
PRINT "Capture('echo hello'): "; captured

' --- Shell ---
PRINT "--- Shell ---"
DIM sc AS INTEGER
sc = Viper.System.Exec.Shell("echo shell_test")
PRINT "Shell('echo shell_test') exit code: "; sc

' --- ShellCapture ---
PRINT "--- ShellCapture ---"
DIM sout AS STRING
sout = Viper.System.Exec.ShellCapture("echo captured_output")
PRINT "ShellCapture('echo captured_output'): "; sout

' --- ShellResult ---
PRINT "--- ShellResult ---"
DIM shellResult AS OBJECT
shellResult = Viper.System.Exec.ShellResult("echo result_output")
PRINT "ShellResult Output: "; Viper.System.CommandResult.get_Output(shellResult)
PRINT "ShellResult ExitCode: "; Viper.System.CommandResult.get_ExitCode(shellResult)
PRINT "ShellResult Succeeded: "; Viper.System.CommandResult.get_Succeeded(shellResult)

' Test with shell features
PRINT "--- ShellCapture with pipe ---"
DIM piped AS STRING
piped = Viper.System.Exec.ShellCapture("echo abc | tr a-z A-Z")
PRINT "ShellCapture('echo abc | tr a-z A-Z'): "; piped

PRINT "=== Exec Demo Complete ==="
END
