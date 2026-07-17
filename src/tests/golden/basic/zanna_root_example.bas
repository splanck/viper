REM ============================================================================
REM zanna_root_example.bas
REM Purpose: Illustrative example showing hypothetical Zanna.* namespace usage.
REM Track A: Demonstrates syntax; "Zanna" root is reserved for built-ins
REM Canonical runtime classes are provided under Zanna.* (String, Object, IO.File,
REM Text.StringBuilder, Collections.List). System.* names are kept as aliases.
REM ============================================================================

REM Example: Using canonical runtime classes
DIM sb AS Zanna.Text.StringBuilder
sb = NEW Zanna.Text.StringBuilder()
sb = Zanna.Text.StringBuilder.Append(sb, "hello")
sb = Zanna.Text.StringBuilder.Append(sb, " ")
sb = Zanna.Text.StringBuilder.Append(sb, "world")
PRINT sb.ToString()

Zanna.IO.File.WriteAllText("zanna_root_example.tmp", sb.ToString())
PRINT Zanna.IO.File.Exists("zanna_root_example.tmp")

REM For Track A, we demonstrate the reserved namespace error is correctly caught.
REM The following would be illegal and produce E_NS_009:
REM   NAMESPACE Zanna.MyLibrary

NAMESPACE MyApplication
  CLASS Program
    DIM version AS I64
  END CLASS
END NAMESPACE

PRINT "Zanna namespace example (canonical Zanna.* classes)"
END
