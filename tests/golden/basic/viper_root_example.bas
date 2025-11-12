REM ============================================================================
REM viper_root_example.bas
REM Purpose: Illustrative example showing hypothetical Viper.* namespace usage.
REM Track A: Demonstrates syntax; "Viper" root is reserved for future built-ins
REM Track B: (Future) Will provide actual Viper.System.*, Viper.IO.*, etc.
REM
REM NOTE: This program demonstrates the INTENDED syntax for Track B.
REM       Attempting to declare "NAMESPACE Viper" produces E_NS_009 error.
REM       Built-in Viper namespaces will be provided by the runtime in Track B.
REM ============================================================================

REM In Track B (future), users would write:
REM   USING Viper.System.Text
REM   USING Viper.IO.FileSystem
REM And then use StringBuilder, File, etc. without qualification.

REM For Track A, we demonstrate the reserved namespace error is correctly caught.
REM The following would be illegal and produce E_NS_009:
REM   NAMESPACE Viper.MyLibrary

NAMESPACE MyApplication
  CLASS Program
    DIM version AS I64
  END CLASS
END NAMESPACE

REM When Track B is implemented, user code will coexist with Viper.* namespaces
REM Example (Track B future):
REM   CLASS MyHelper : Viper.System.Object
REM   END CLASS

PRINT "Viper namespace example (illustrative for Track B)"
END
