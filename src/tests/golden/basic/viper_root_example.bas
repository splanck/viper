REM ============================================================================
REM viper_root_example.bas
REM Purpose: Illustrative example showing hypothetical Viper.* namespace usage.
REM Track A: Demonstrates syntax; "Viper" root is reserved for built-ins
REM Canonical runtime classes are provided under Viper.* (String, Object, IO.File,
REM Text.StringBuilder, Collections.List). System.* names are kept as aliases.
REM ============================================================================

REM Example: Using canonical runtime classes
DIM sb AS Viper.Text.StringBuilder
sb = NEW Viper.Text.StringBuilder()
sb = Viper.Text.StringBuilder.Append(sb, "hello")
sb = Viper.Text.StringBuilder.Append(sb, " ")
sb = Viper.Text.StringBuilder.Append(sb, "world")
PRINT sb.ToString()

Viper.IO.File.WriteAllText("viper_root_example.tmp", sb.ToString())
PRINT Viper.IO.File.Exists("viper_root_example.tmp")

REM For Track A, we demonstrate the reserved namespace error is correctly caught.
REM The following would be illegal and produce E_NS_009:
REM   NAMESPACE Viper.MyLibrary

NAMESPACE MyApplication
  CLASS Program
    DIM version AS I64
  END CLASS
END NAMESPACE

PRINT "Viper namespace example (canonical Viper.* classes)"
END
